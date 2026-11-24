#include "web.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <httplib.h>
#include <iomanip>
#include <map>
#include <mutex>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif
namespace fbar {
using Req = httplib::Request;
using Res = httplib::Response;
std::string utc_stamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char s[32];
    std::strftime(s, sizeof s, "%FT%TZ", &tm);
    return s;
}
static void reply(Res &r, int code, const J &j) {
    r.status = code;
    r.set_header("Cache-Control", "no-store");
    r.set_content(j.dump(-1, ' ', false, J::error_handler_t::replace), "application/json; charset=utf-8");
}
static void error(Res &r, int code, std::string k, std::string msg) {
    reply(r, code, {{"error", k}, {"message", msg}});
}
static fs::path userfile(const Config &c) { return c.storage / "auth" / "admins.json"; }
static J users(const Config &c) {
    auto p = userfile(c);
#ifdef _WIN32
    // A CRT ifstream does not grant FILE_SHARE_DELETE. Let a simultaneous CLI reset
    // replace the file atomically while this request is reading the previous snapshot.
    struct ReadHandle {
        HANDLE h;
        ~ReadHandle() { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    } file{CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (file.h == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
            return J::object();
        throw std::runtime_error("无法读取管理员资料");
    }
    std::string content;
    char buf[4096];
    DWORD n;
    do {
        if (!ReadFile(file.h, buf, sizeof buf, &n, nullptr))
            throw std::runtime_error("读取管理员资料失败");
        content.append(buf, n);
    } while (n);
    return J::parse(content);
#else
    if (!fs::exists(p))
        return J::object();
    return J::parse(load_text(p));
#endif
}
void Admin::set_user(const Config &c, const std::string &name, const std::string &role,
                     const std::string &p) {
    if (name.empty() || name.size() > 64 || p.size() < 8 || p.size() > 256 ||
        (role != "admin" && role != "operator" && role != "viewer"))
        throw Error("bad_account", "账号最长 64 字节；密码 8–256 字节；角色 admin/operator/viewer");
    auto u = users(c);
    u[name] = {{"hash", password_hash(p)}, {"role", role}};
    atomic_file(userfile(c), u.dump(2), true);
}
struct Admin::Impl {
    Config c;
    J state, accounts;
    struct Session {
        std::string name, role, csrf;
        Time expiry, last;
    };
    std::map<std::string, Session> sessions;
    struct Attempt {
        int n = 0;
        Time wait;
    };
    std::map<std::string, Attempt> attempts;
    std::mutex m;
    std::condition_variable cv;
    std::deque<std::pair<int64_t, std::string>> ring;
    int64_t cursor = 0;
    std::vector<J> queue;
    std::atomic<int> streams{0}, auth_workers{0};
    bool stopping = false;
    std::unique_ptr<httplib::Server> server;
    std::thread thread;
    int port_ = 0;
    Impl(const Config &cfg, J s) : c(cfg), state(std::move(s)), accounts(users(cfg)) {
        if (c.tls_mode == "https") {
            server = std::make_unique<httplib::SSLServer>(
                (c.file.parent_path() / fs::u8path(c.web_cert)).string().c_str(),
                (c.file.parent_path() / fs::u8path(c.web_key)).string().c_str());
        } else
            server = std::make_unique<httplib::Server>();
        if (!server->is_valid())
            throw std::runtime_error("管理 HTTPS 证书或私钥配置无效");
        server->new_task_queue = []() { return new httplib::ThreadPool(16, 64); };
        server->set_payload_max_length(65536);
        server->set_read_timeout(5, 0);
        server->set_write_timeout(3, 0);
        server->set_default_headers(
            {{"X-Content-Type-Options", "nosniff"},
             {"Referrer-Policy", "no-referrer"},
             {"X-Frame-Options", "DENY"},
             {"Content-Security-Policy",
              "default-src 'self'; img-src 'self' data:; style-src 'self' 'unsafe-inline'; script-src "
              "'self'; connect-src 'self'; frame-ancestors 'none'"}});
        if (!server->set_mount_point("/", c.web_root.string()))
            throw std::runtime_error("缺少静态管理台目录：" + c.web_root.u8string());
        routes();
        auto [h, p] = endpoint(c.web_listen, 27861);
        port_ = p ? (server->bind_to_port(h, p) ? p : 0) : server->bind_to_any_port(h);
        if (port_ <= 0)
            throw std::runtime_error("绑定管理 TCP 端口失败：" + c.web_listen);
        thread = std::thread([this] { server->listen_after_bind(); });
    }
    ~Impl() {
        {
            std::lock_guard l(m);
            stopping = true;
        }
        cv.notify_all();
        server->stop();
        if (thread.joinable())
            thread.join();
    }
    std::string token(const Req &q) const {
        auto s = q.get_header_value("Cookie");
        std::istringstream z(s);
        while (std::getline(z, s, ';')) {
            auto i = s.find_first_not_of(" ");
            if (i != s.npos && s.compare(i, 5, "fbar=") == 0)
                return s.substr(i + 5);
        }
        return {};
    }
    // Called under m at authorization boundaries. CLI updates replace the small local file
    // atomically. Read its contents, not just its timestamp: two resets can share an mtime.
    bool refresh_accounts() {
        J next;
        try {
            next = users(c);
            if (!next.is_object())
                throw std::runtime_error("invalid account store");
        } catch (const std::exception &) {
            accounts = J::object();
            sessions.clear();
            cv.notify_all();
            return false;
        }
        if (next == accounts)
            return true;
        for (auto i = sessions.begin(); i != sessions.end();) {
            const auto &name = i->second.name;
            if (!next.contains(name) || !accounts.contains(name) || next.at(name) != accounts.at(name))
                i = sessions.erase(i);
            else
                ++i;
        }
        accounts.swap(next);
        cv.notify_all();
        return true;
    }
    bool accounts_ready(Res &r) {
        if (refresh_accounts())
            return true;
        error(r, 503, "accounts_unavailable", "无法读取管理员资料，请检查专服账号文件");
        return false;
    }
    Session *get(const std::string &key) {
        auto it = sessions.find(key);
        if (it == sessions.end())
            return nullptr;
        auto now = Clock::now();
        if (now >= it->second.expiry || now - it->second.last > 30min) {
            sessions.erase(it);
            return nullptr;
        }
        it->second.last = now;
        return &it->second;
    }
    bool origin(const Req &q) const {
        auto x = q.get_header_value("Origin");
        if (x.empty())
            return true;
        return x == (c.origin.empty() ? std::string(c.tls_mode == "https" ? "https://" : "http://") +
                                            q.get_header_value("Host")
                                      : c.origin);
    }
    bool auth_locked(const Req &q, Res &r, Session &out, bool write) {
        if (!accounts_ready(r))
            return false;
        auto *s = get(token(q));
        if (!s) {
            error(r, 401, "login_required", "请先登录");
            return false;
        }
        if (write && (!origin(q) || q.get_header_value("X-CSRF-Token") != s->csrf)) {
            error(r, 403, "csrf", "来源或 CSRF 校验未通过");
            return false;
        }
        out = *s;
        return true;
    }
    bool auth(const Req &q, Res &r, Session &out, bool write) {
        std::lock_guard l(m);
        return auth_locked(q, r, out, write);
    }
    J session(const Session *s) {
        J v = {{"bootstrap_required", accounts.empty()},
               {"authenticated", s != nullptr},
               {"role", nullptr},
               {"csrf_token", nullptr},
               {"server_name", c.name},
               {"proto_version", "fbar/1.0"}};
        if (s) {
            v["username"] = s->name;
            v["role"] = s->role;
            v["csrf_token"] = s->csrf;
        }
        return v;
    }
    void push(std::string type, J v) {
        v["event_cursor"] = ++cursor;
        state["event_cursor"] = cursor;
        std::string x = "id: " + std::to_string(cursor) + "\nevent: " + type +
                        "\ndata: " + v.dump(-1, ' ', false, J::error_handler_t::replace) + "\n\n";
        ring.emplace_back(cursor, std::move(x));
        while (ring.size() > 128)
            ring.pop_front();
        cv.notify_all();
    }
    int target_revision(const std::string &t) {
        if (t.rfind("room:", 0) == 0)
            for (auto &r : state.at("rooms"))
                if (r.value("room_id", std::string{}) == t.substr(5))
                    return r.value("config_revision", r.value("room_rev", 0));
        return state.value("config_revision", 1);
    }
    void routes() {
        server->set_exception_handler([](const Req &, Res &r, std::exception_ptr ep) {
            try {
                std::rethrow_exception(ep);
            } catch (const J::exception &) {
                error(r, 400, "bad_json", "JSON 或字段类型无效");
            } catch (const std::exception &) {
                error(r, 500, "internal", "管理操作未完成，请查看专服日志");
            }
        });
        server->Get("/api/v1/session", [this](const Req &q, Res &r) {
            std::lock_guard l(m);
            if (!accounts_ready(r))
                return;
            reply(r, 200, session(get(token(q))));
        });
        server->Post("/api/v1/auth/login", [this](const Req &q, Res &r) {
            if (!origin(q)) {
                error(r, 403, "origin", "登录来源不符");
                return;
            }
            auto j = J::parse(q.body);
            auto name = j.value("username", std::string{}), pass = j.value("password", std::string{});
            if (name.size() > 64 || pass.size() > 256) {
                error(r, 400, "bad_input", "输入过长");
                return;
            }
            std::string key = q.remote_addr + "|" + name;
            J acc;
            {
                std::lock_guard l(m);
                if (!accounts_ready(r))
                    return;
                if (accounts.empty()) {
                    error(r, 403, "bootstrap_required", "请在专服本机初始化管理员");
                    return;
                }
                auto now = Clock::now();
                for (auto &k : {key, q.remote_addr}) {
                    auto i = attempts.find(k);
                    if (i != attempts.end() && i->second.wait > now) {
                        reply(r, 429,
                              {{"error", "rate_limited"},
                               {"retry_after_seconds",
                                1 + std::chrono::duration_cast<std::chrono::seconds>(i->second.wait - now)
                                        .count()},
                               {"message", "登录过于频繁"}});
                        return;
                    }
                }
                if (accounts.contains(name))
                    acc = accounts.at(name);
            }
            if (auth_workers.fetch_add(1) >= 3) {
                --auth_workers;
                error(r, 429, "rate_limited", "登录繁忙");
                return;
            }
            bool valid = false;
            try {
                valid = acc.is_object() && password_ok(pass, acc.at("hash"));
            } catch (...) {
                --auth_workers;
                throw;
            }
            --auth_workers;
            std::lock_guard l(m);
            // Verification runs outside m; never issue a session from an obsolete hash or role.
            if (!accounts_ready(r))
                return;
            if (!valid || !accounts.contains(name) || accounts.at(name) != acc) {
                for (auto &k : {key, q.remote_addr}) {
                    if (attempts.size() > 4096)
                        for (auto i = attempts.begin(); i != attempts.end();) {
                            if (i->second.wait < Clock::now() - 10min)
                                i = attempts.erase(i);
                            else
                                ++i;
                        }
                    auto &a = attempts[k];
                    a.n = std::min(10, a.n + 1);
                    a.wait = Clock::now() + std::chrono::seconds(a.n < 3 ? 0 : 1 << std::min(6, a.n - 3));
                }
                error(r, 401, "invalid_credentials", "账号或密码不正确");
                return;
            }
            attempts.erase(key);
            auto key2 = random_token(32);
            auto now = Clock::now();
            for (auto i = sessions.begin(); i != sessions.end();) {
                if (i->second.expiry < now || i->second.last < now - 30min)
                    i = sessions.erase(i);
                else
                    ++i;
            }
            if (sessions.size() >= 512) {
                error(r, 429, "session_limit", "管理会话已满");
                return;
            }
            sessions[key2] = {name, acc.at("role"), random_token(24), now + 8h, now};
            r.set_header("Set-Cookie",
                         "fbar=" + key2 + "; Path=/; HttpOnly; SameSite=Strict" +
                             std::string(c.tls_mode == "https" || c.origin.rfind("https://", 0) == 0
                                             ? "; Secure"
                                             : ""));
            reply(r, 200, session(&sessions[key2]));
        });
        server->Post("/api/v1/auth/logout", [this](const Req &q, Res &r) {
            Session s;
            if (!auth(q, r, s, true))
                return;
            std::lock_guard l(m);
            sessions.erase(token(q));
            cv.notify_all();
            r.set_header("Set-Cookie", "fbar=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
            reply(r, 200, {{"authenticated", false}});
        });
        server->Get("/api/v1/state", [this](const Req &q, Res &r) {
            Session s;
            if (!auth(q, r, s, false))
                return;
            std::lock_guard l(m);
            reply(r, 200, state);
        });
        server->Post("/api/v1/commands", [this](const Req &q, Res &r) {
            std::lock_guard l(m);
            Session s;
            if (!auth_locked(q, r, s, true))
                return;
            auto j = J::parse(q.body);
            auto action = j.at("action").get<std::string>(), target = j.at("target").get<std::string>();
            int expected = j.at("expected_revision");
            static const std::map<std::string, bool> actions = {{"set_capacity", true},
                                                                {"set_drain_mode", true},
                                                                {"set_room_creation", false},
                                                                {"set_room_map", false},
                                                                {"set_room_spectator_policy", false},
                                                                {"kick_member", false},
                                                                {"set_bot_enabled", false},
                                                                {"set_map_enabled", true}};
            auto it = actions.find(action);
            if (it == actions.end()) {
                error(r, 400, "unknown_action", "不支持该命令");
                return;
            }
            if (s.role == "viewer" || (it->second && s.role != "admin")) {
                error(r, 403, "forbidden", "当前账号无此权限");
                return;
            }
            int cur = target_revision(target);
            if (expected != cur) {
                reply(r, 409,
                      {{"error", "revision_conflict"},
                       {"current_revision", cur},
                       {"message", "配置已变化，请重新确认草稿"}});
                return;
            }
            if (queue.size() >= 64) {
                error(r, 429, "busy", "待处理命令过多");
                return;
            }
            auto id = "cmd-" + random_token(8);
            j["command_id"] = id;
            j["actor"] = s.name;
            j["role"] = s.role;
            queue.push_back(j);
            reply(
                r, 202,
                {{"status", "accepted"}, {"command_id", id}, {"expected_revision", cur}, {"target", target}});
        });
        server->Get(R"(/api/v1/replays/([A-Za-z0-9_-]+))", [this](const Req &q, Res &r) {
            Session s;
            if (!auth(q, r, s, false))
                return;
            auto id = q.matches[1].str();
            {
                std::lock_guard l(m);
                auto &replays = state.at("replays");
                if (std::none_of(replays.begin(), replays.end(), [&](auto &v) {
                        return v.value("replay_id", std::string{}) == id && v.value("downloadable", false);
                    })) {
                    error(r, 404, "not_ready", "回放尚不可下载或已过期");
                    return;
                }
            }
            r.set_header("Content-Disposition", "attachment; filename=\"" + id + ".fbar\"");
            r.set_file_content((c.storage / "replays" / (id + ".fbar")).string(), "application/octet-stream");
        });
        server->Get("/api/v1/events", [this](const Req &q, Res &r) {
            Session s;
            if (!auth(q, r, s, false))
                return;
            if (streams.fetch_add(1) >= 8) {
                --streams;
                error(r, 429, "too_many_streams", "管理台订阅已满");
                return;
            }
            int64_t from;
            {
                std::lock_guard l(m);
                from = cursor;
            }
            auto last = q.get_header_value("Last-Event-ID");
            try {
                if (!last.empty())
                    from = std::stoll(last);
                else if (q.has_param("after"))
                    from = std::stoll(q.get_param_value("after"));
            } catch (...) {
                --streams;
                error(r, 400, "bad_cursor", "无效游标");
                return;
            }
            auto id = token(q);
            r.set_header("Cache-Control", "no-store");
            r.set_header("X-Accel-Buffering", "no");
            r.set_chunked_content_provider(
                "text/event-stream; charset=utf-8",
                [this, from, id](size_t, httplib::DataSink &sink) mutable {
                    std::string out;
                    {
                        std::unique_lock l(m);
                        const auto heartbeat = Clock::now() + 6s;
                        for (;;) {
                            if (stopping || !refresh_accounts() || !sessions.count(id))
                                return false;
                            auto now = Clock::now();
                            if (cursor != from || now >= heartbeat)
                                break;
                            cv.wait_until(l, std::min(heartbeat, now + 1s),
                                          [&] { return stopping || cursor != from || !sessions.count(id); });
                        }
                        if (stopping || !get(id))
                            return false;
                        if (from > cursor || (!ring.empty() && from < ring.front().first - 1)) {
                            out = "id: " + std::to_string(cursor) +
                                  "\nevent: resync_required\ndata: {\"reason\":\"cursor_too_old\"}\n\n";
                            from = cursor;
                        } else
                            for (auto &[i, x] : ring)
                                if (i > from) {
                                    out += x;
                                    from = i;
                                    if (out.size() > 64 * 1024)
                                        break;
                                }
                    }
                    if (out.empty())
                        out = ": keepalive\n\n";
                    return sink.write(out.data(), out.size());
                },
                [this](bool) { --streams; });
        });
    }
};
Admin::Admin(const Config &c, J s) : impl(std::make_unique<Impl>(c, std::move(s))) {}
Admin::~Admin() = default;
int Admin::port() const { return impl->port_; }
std::vector<J> Admin::commands() {
    std::lock_guard l(impl->m);
    std::vector<J> q;
    q.swap(impl->queue);
    return q;
}
void Admin::event(std::string k, J j) {
    std::lock_guard l(impl->m);
    impl->push(std::move(k), std::move(j));
}
void Admin::publish(J j) {
    std::lock_guard l(impl->m);
    J diff = J::object();
    for (auto it = j.begin(); it != j.end(); ++it)
        if (it.key() != "event_cursor" &&
            (!impl->state.contains(it.key()) || impl->state[it.key()] != it.value()))
            diff[it.key()] = it.value();
    impl->state = std::move(j);
    impl->state["event_cursor"] = impl->cursor;
    if (!diff.empty())
        impl->push("state.patch", std::move(diff));
}
} // namespace fbar
