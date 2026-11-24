#include "service.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#include <sys/resource.h>
#endif
namespace fbar {
static J arr() { return J::array(); }
static std::string str(const J &j, const char *k) { return j.value(k, std::string{}); }
static void limited(J &a, J row, int n) {
    a.push_back(std::move(row));
    while ((int)a.size() > n)
        a.erase(a.begin());
}
static double cpu_seconds() {
#ifdef _WIN32
    FILETIME c, e, k, u;
    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u))
        return -1;
    auto val = [](FILETIME f) { return (uint64_t(f.dwHighDateTime) << 32) + f.dwLowDateTime; };
    return (val(k) + val(u)) * 1e-7;
#else
    rusage r{};
    if (getrusage(RUSAGE_SELF, &r) != 0)
        return -1;
    return r.ru_utime.tv_sec + r.ru_stime.tv_sec + (r.ru_utime.tv_usec + r.ru_stime.tv_usec) * 1e-6;
#endif
}
static size_t rss() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX p{};
    return GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&p, sizeof p)
               ? p.WorkingSetSize
               : 0;
#else
    std::ifstream f("/proc/self/statm");
    size_t all = 0, r = 0;
    f >> all >> r;
    return r * sysconf(_SC_PAGESIZE);
#endif
}
Service::Service(Config c, std::string md, Send f)
    : cfg(std::move(c)), mode(std::move(md)), output(std::move(f)),
      roster(flyback::catalog(cfg.assets.u8string())) {
    budget = cfg.egress_budget;
    if (mode == "server") {
        fs::create_directories(cfg.storage);
        policy(false);
    }
    if(cfg.replays){
        auto idx = cfg.storage / "replays" / "index.json";
        if (fs::exists(idx))
            replays = J::parse(load_text(idx));
        prune();
    }
}
void Service::network_info(int p, std::string fp) {
    port_ = p;
    fingerprint_ = std::move(fp);
}
void Service::start_admin() {
    if (cfg.web_enabled){
        web = std::make_unique<Admin>(cfg, state());
        web->publish(state());
    }
}
void Service::policy(bool write) {
    auto path = cfg.storage / "config" / "policy.json";
    if (write) {
        atomic_file(path, J({{"disabled_bots", disabled_bots},
                             {"disabled_maps", disabled_maps},
                             {"config_revision", config_rev},
                             {"egress_budget_kbps", budget},
                             {"spectator_snapshot_hz", cfg.spectator_hz}})
                              .dump(2));
    } else if (fs::exists(path)) {
        auto j = J::parse(load_text(path));
        disabled_bots = j.value("disabled_bots", std::set<std::string>{});
        disabled_maps = j.value("disabled_maps", std::set<std::string>{});
        config_rev = j.value("config_revision", 1);
    }
}
J Service::catalog() const {
    auto c = roster;
    for (auto &b : c["bots"])
        b["enabled"] = !disabled_bots.count(str(b, "id"));
    for (auto &m : c["maps"])
        m["enabled"] = !disabled_maps.count(str(m, "id"));
    return c;
}
void Service::log(std::string source, std::string msg, std::string room) {
    J v = {{"id", ++log_id},   {"stamp", utc_stamp()}, {"level", "info"},
           {"source", source}, {"room_id", room},      {"message", msg}};
    limited(logs, v, 150);
    if (mode != "server")
        return;
    try {
        auto dir = cfg.storage / "logs";
        fs::create_directories(dir);
        auto p = dir / "server.jsonl";
        if (fs::exists(p) && fs::file_size(p) >= size_t(cfg.log_mib) * 1048576) {
            for (int i = cfg.log_keep - 1; i > 0; --i) {
                auto old = i == 1 ? p : dir / ("server." + std::to_string(i - 1) + ".jsonl");
                auto next = dir / ("server." + std::to_string(i) + ".jsonl");
                if (fs::exists(old)) {
                    if (fs::exists(next))
                        fs::remove(next);
                    fs::rename(old, next);
                }
            }
            if (cfg.log_keep == 1)
                std::ofstream(p, std::ios::trunc).close();
        }
        std::ofstream(p, std::ios::app) << v.dump() << "\n";
    } catch (const fs::filesystem_error &) {
    }
}
void Service::fail(const std::string &p, std::string k, std::string m) {
    output(p, {{"type", "error"}, {"code", k}, {"message", m}}, true);
}
void Service::attached(const std::string &p, std::string addr) {
    clients.try_emplace(p).first->second.address = std::move(addr);
}
void Service::detached(const std::string &p) {
    leave(p);
    clients.erase(p);
}
void Service::leave(const std::string &p) {
    auto i = clients.find(p);
    if (i == clients.end() || i->second.room.empty())
        return;
    auto room = i->second.room;
    i->second.room.clear();
    auto r = rooms.find(room);
    if (r != rooms.end()) {
        r->second->leave(p);
        if (mode != "server" && r->second->members.empty())
            rooms.erase(r);
    }
    dir_dirty = true;
}
J Service::directory() const {
    J rows = arr();
    for (auto &[_, r] : rooms)
        rows.push_back(r->summary());
    return {{"type", "directory"},    {"name", cfg.name},
            {"rooms", rows},          {"max_rooms", cfg.max_rooms},
            {"port", port_},          {"fingerprint", fingerprint_},
            {"profile", 0},           {"base_id", "flyback:vanilla"},
            {"ruleset", "classic20"}, {"required_extensions", arr()},
            {"draining", draining}};
}
void Service::complete(const std::string &p, const J &j, const std::string &value) {
    auto cur = clients.find(p);
    if (cur == clients.end())
        return;
    auto &c = cur->second;
    auto op = str(j, "op");
    if (!c.room.empty())
        throw Error("already_in_room", "请先离开当前房间");
    if (op == "create") {
        if (draining || !cfg.creation)
            throw Error("server_draining", "目前不接收新房间");
        if ((int)rooms.size() >= cfg.max_rooms)
            throw Error("room_limit", "服务器房间已满");
        auto id = "rm-" + random_token(5);
        auto r = std::make_unique<Room>(id, mode, output);
        r->port = port_;
        r->advertise = cfg.advertise;
        r->spec_limit = cfg.spectator_limit;
        r->archive_enabled=cfg.replays;
        r->spec_hz = cfg.spectator_hz;
        r->pressure = pressure;
        r->configure(j.at("settings"), catalog(), false);
        r->password = value;
        r->join(p, c.name, false, j.value("field_ready", false));
        rooms.emplace(id, std::move(r));
        c.room = id;
        log("room", "创建房间", id);
    } else {
        auto id = str(j, "room_id");
        auto i = rooms.find(id);
        if (i == rooms.end())
            throw Error("room_closed", "该房间已关闭");
        auto &r = *i->second;
        if (value != "ok")
            throw Error("bad_password", "房间密码错误");
        bool watch = str(j, "role") == "spectator" || r.active();
        r.join(p, c.name, watch, j.value("field_ready", false));
        c.room = id;
        log("room", watch ? "观众加入" : "真人加入", id);
    }
    auto req = str(j, "request_id");
    if (!req.empty()) {
        if (c.requests.size() >= 32)
            c.requests.erase(c.requests.begin());
        c.requests[req] = c.room;
    }
    dir_dirty = true;
}
void Service::handle(const std::string &p, const J &j) {
    try {
        if (!j.is_object() || !j.contains("op"))
            throw Error("invalid", "缺少操作名");
        auto z = clients.find(p);
        if (z == clients.end())
            return;
        auto &c = z->second;
        auto op = str(j, "op");
        auto now = Clock::now();
        if (now - c.rate_since >= 1s) {
            c.rate_since = now;
            c.calls = 0;
        }
        if (++c.calls > 140)
            return;
        if (op == "identify") {
            auto name = j.value("name", std::string("玩家"));
            if (name.empty() || name.size() > 80 ||
                std::any_of(name.begin(), name.end(), [](unsigned char x) { return x < 32 || x == 127; }))
                throw Error("bad_name", "玩家名须为 1–80 字节");
            if (!c.room.empty())
                throw Error("room_locked", "请先离房再改名");
            c.name = name;
            auto cat = catalog();
            cat["type"] = "catalog";
            output(p, cat, true);
            output(p, directory(), true);
            return;
        }
        if (c.name.empty())
            throw Error("identify_first", "请先设置玩家名");
        if (op == "list") {
            output(p, directory(), true);
            return;
        }
        if (op == "leave") {
            leave(p);
            output(p, {{"type", "left"}}, true);
            output(p, directory(), true);
            return;
        }
        if (op == "create" || op == "join") {
            auto validate = [](const J &x) {
                if (x.value("profile", 0) != 0 ||
                    x.value("ruleset", std::string("classic20")) != "classic20" ||
                    !x.value("required_extensions", J::array()).empty())
                    throw Error("missing_extension", "当前服务器仅支持原版 classic20");
            };
            validate(j);
            if (j.contains("settings"))
                validate(j.at("settings"));
            auto req = str(j, "request_id");
            if (!req.empty() && c.requests.count(req) && c.requests[req] == c.room && rooms.count(c.room)) {
                rooms.at(c.room)->broadcast();
                return;
            }
            if (!c.room.empty())
                throw Error("already_in_room", "请先离开当前房间");
            if (std::any_of(pending.begin(), pending.end(), [&](auto &t) { return t.peer == p; }))
                throw Error("auth_busy", "正在验证房间请求");
            if ((op == "create" && mode == "lan" && !rooms.empty()) ||
                (op == "create" && mode != "server" && p != "local"))
                throw Error("create_forbidden", "个人房只由本机房主创建");
            auto clear = j.value("password", std::string{});
            if (clear.size() > 256)
                throw Error("bad_password", "房密最长 256 字节");
            std::string encoded;
            if (op == "join") {
                auto it = rooms.find(str(j, "room_id"));
                if (it == rooms.end())
                    throw Error("room_closed", "该房间已关闭");
                encoded = it->second->password;
                if (encoded.empty()) {
                    complete(p, j, "ok");
                    return;
                }
            } else if (clear.empty()) {
                complete(p, j, "");
                return;
            }
            if (now - c.auth_since > 10s) {
                c.auth_since = now;
                c.attempts = 0;
            }
            auto &origin = auth_attempts[c.address];
            while (!origin.empty() && now - origin.front() > 10s)
                origin.pop_front();
            if (origin.size() >= 40)
                throw Error("rate_limited", "当前地址的房密请求过于频繁");
            origin.push_back(now);
            if (++c.attempts > 6 || pending.size() >= 4)
                throw Error("rate_limited", "房密验证繁忙，请稍后再试");
            auto safe = j;
            safe.erase("password");
            pending.push_back(
                {p, safe, std::async(std::launch::async, [clear = std::move(clear), encoded, op] {
                     return op == "create"                ? password_hash(clear)
                            : password_ok(clear, encoded) ? std::string("ok")
                                                          : std::string("bad");
                 })});
            return;
        }
        if (c.room.empty() || !rooms.count(c.room))
            throw Error("not_member", "请先选择房间");
        auto &r = *rooms.at(c.room);
        auto active =
            std::count_if(rooms.begin(), rooms.end(), [](auto &x) { return x.second->holds_slot(); });
        r.op(p, j, (op == "input" || op == "field_ready" ? J() : catalog()), cfg.assets,
             !draining && (r.holds_slot() || active < cfg.max_active));
        if (op != "input" && op != "watch" && op != "field_ready")
            dir_dirty = true;
    } catch (const Error &e) {
        fail(p, e.code, e.what());
    } catch (const std::exception &e) {
        fail(p, "invalid_request", e.what());
    }
}
void Service::save_error(const std::string& rid,const std::string& why){
    log("replay","无法保存回放："+why,rid);
    if(rooms.count(rid))for(auto&[p,_]:rooms.at(rid)->members)fail(p,"replay_save_failed","本局已结算，但无法保存回放："+why);
}
void Service::archive(J&& record,std::string&& native,const J& meta){
    if(!cfg.replays)return;
    auto rid=str(meta,"id"),id=rid+"-"+std::to_string(meta.value("round_epoch",0));
    if(saves.size()>=16){save_error(rid,"写盘队列已满");return;}
    release_after[id]=Clock::now()+(mode=="offline"?0s:3s);
    saves.push_back({std::move(record),meta,std::move(native)});
}
void Service::save_progress(bool drain){
    do{
        if(saving.valid()){
            if(!drain&&saving.wait_for(0ms)!=std::future_status::ready)return;
            try{
                auto row=saving.get();replays.insert(replays.begin(),row);
                if(mode=="offline" && rooms.count(saving_room))
                    for(auto&[p,_]:rooms.at(saving_room)->members)output(p,{{"type","replay_saved"},{"path",(cfg.storage/"replays"/(str(row,"replay_id")+".fbar")).u8string()}},true);
            }catch(const std::exception&e){save_error(saving_room,e.what());}
        }
        if(saves.empty())return;
        Save work=std::move(saves.front());saves.pop_front();saving_room=str(work.meta,"id");
        saving=std::async(std::launch::async,[job=std::move(work),dir=cfg.storage/"replays",index=replays]()mutable{
            const auto&m=job.meta;auto rid=str(m,"id"),id=rid+"-"+std::to_string(m.value("round_epoch",0));
            auto bytes=job.record.dump();
            atomic_file(dir/(id+".fbar"),bytes);atomic_file(dir/(id+".txt"),job.native);
            J row={{"replay_id",id},{"room_id",rid},{"room_name",m.at("title")},{"round_epoch",m.at("round_epoch")},{"mode",m.at("mode")},{"map_id",m.at("map")},{"ruleset","classic20"},{"ticks",job.record.at("frames").size()-1},{"finished_at",utc_stamp()},{"summary","已结算"},{"size_bytes",bytes.size()+job.native.size()},{"downloadable",true}};
            index.insert(index.begin(),row);atomic_file(dir/"index.json",index.dump());return row;
        });
    }while(drain);
}
void Service::prune() {
    auto dir = cfg.storage / "replays";
    size_t used = 0;
    J keep = arr();
    auto cut = fs::file_time_type::clock::now() - std::chrono::hours(24LL * cfg.retain_days);
    for (auto row : replays) {
        auto id = str(row, "replay_id");
        if (id.empty() || id.find_first_not_of(
                              "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != id.npos)
            continue;
        auto p = dir / (id + ".fbar");
        if (!fs::is_regular_file(p))
            continue;
        auto sz = row.value("size_bytes", size_t(0));
        if (fs::last_write_time(p) < cut || used + sz > size_t(cfg.replay_mib) * 1048576) {
            std::error_code ec;
            fs::remove(p, ec);
            fs::remove(dir / (id + ".txt"), ec);
            continue;
        }
        used += sz;
        keep.push_back(row);
    }
    if (keep != replays) {
        replays = std::move(keep);
        atomic_file(dir / "index.json", replays.dump());
    }
}
void Service::net_stats(const std::string &p, J stats) {
    auto it = clients.find(p);
    if (it == clients.end())
        return;
    auto &c = it->second;
    auto now = Clock::now();
    if (c.transport.is_object() && stats.is_object()) {
        auto t = std::chrono::duration<double>(now - c.sample).count();
        if (t > 0)
            c.kbps = (stats.value("bytes_sent", int64_t(0)) - c.transport.value("bytes_sent", int64_t(0))) *
                     .008 / t;
    }
    c.sample = now;
    c.transport = std::move(stats);
}
J Service::state() const {
    J rs = arr();
    int active = 0;
    double bot_mem = 0, worst_p99 = -1;
    double total = 0;
    auto cat = catalog();
    J bots = arr(), maps = arr();
    for (auto &m : cat["maps"]) {
        auto v = m;
        v["title"] = m["name"];
        v["modes"] = arr();
        for (int n : m["seats"])
            v["modes"].push_back(n == 2 ? "duel" : n == 3 ? "ffa3" : "ffa4");
        v["trio_verified"] = false;
        maps.push_back(v);
    }
    for (auto &b : cat["bots"]) {
        auto v = b;
        v["key"] = b["id"];
        v["label"] = b["name"];
        v["first_step_ms"] = nullptr;
        v["avg_step_ms"] = nullptr;
        v["timeout_count"] = 0;
        v["active_rooms"] = 0;
        v["last_error"] = nullptr;
        v["source"] = b.value("official", true) ? "official" : "community";
        v["binary_ready"] = b.value("available", false);
#ifdef _WIN32
        v["sandbox_status"] = "AppContainer + Job";
#else
        v["sandbox_status"] = "cgroup v2 + Landlock + seccomp";
#endif
        bots.push_back(v);
    }
    for (auto &[_, r] : rooms) {
        auto j = r->admin();
        double sent = 0;
        int64_t queued = 0;
        for (auto &m : j["members"]) {
            auto k = clients.find(str(m, "member_id"));
            if (k != clients.end()) {
                sent += k->second.kbps;
                auto &t = k->second.transport;
                if (t.is_object()) {
                    m["rtt_ms"] = t.value("rtt_us", 0) / 1000.;
                    m["backlog"] = t.value("queued_bytes", 0);
                    queued += t.value("queued_bytes", 0);
                }
            }
        }
        j["egress_kbps"] = sent;
        j["snapshot_backlog"] = queued;
        j["bot_stats"] = r->stats();
        if (j["tick_late_p99_ms"].is_number())
            worst_p99 = std::max(worst_p99, j.at("tick_late_p99_ms").get<double>());
        if (r->active() && r->stats().is_array()) {
            std::set<std::string> counted;
            for (auto &stat : r->stats()) {
                auto id = str(stat, "bot");
                for (auto &b : bots)
                    if (str(b, "key") == id) {
                        if (counted.insert(id).second)
                            b["active_rooms"] = b["active_rooms"].get<int>() + 1;
                        int n = stat.value("samples", 0), prev = b.value("_samples", 0);
                        auto first = stat.value("first_cpu_ms", 0.);
                        b["first_step_ms"] = b["first_step_ms"].is_null()
                                                 ? first
                                                 : std::max(first, b.at("first_step_ms").get<double>());
                        if (n > 0) {
                            double sum =
                                b["avg_step_ms"].is_null() ? 0. : b["avg_step_ms"].get<double>() * prev;
                            b["avg_step_ms"] = (sum + n * stat.value("mean_cpu_ms", 0.)) / (n + prev);
                            b["_samples"] = n + prev;
                        }
                        b["timeout_count"] = b.value("timeout_count", 0) + stat.value("faults", 0);
                        if (!str(stat, "last_error").empty())
                            b["last_error"] = stat["last_error"];
                        break;
                    }
                bot_mem += stat.value("rss_bytes", int64_t(0));
            }
        }
        total += sent;
        active += r->active();
        rs.push_back(j);
    }
    for (auto &b : bots)
        b.erase("_samples");
    J published = arr();
    for (auto &row : replays) {
        auto id = str(row, "replay_id");
        auto it = release_after.find(id);
        if (it == release_after.end() || Clock::now() >= it->second)
            published.push_back(row);
    }
    auto bound=[](const std::string& spec,int port){
        auto host=endpoint(spec,port).first;
        return (host.find(':')==host.npos?host:"["+host+"]")+":"+std::to_string(port);
    };
    J server = {
        {"name", cfg.name},
        {"listen_udp", bound(cfg.listen,port_)},
        {"bound_port", port_},
        {"advertise_udp", cfg.advertise},
        {"admin_listen", web ? J(bound(cfg.web_listen,web->port())):J(nullptr)},
        {"public_origin", cfg.origin},
        {"tls_fingerprint", fingerprint_},
        {"uptime_seconds", std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - started).count()},
        {"draining", draining},
        {"allow_room_creation", cfg.creation},
        {"rss_mib", rss() / 1048576.},
        {"bot_rss_mib", bot_mem / 1048576.},
        {"cpu_percent", cpu_percent >= 0 ? J(cpu_percent) : J(nullptr)},
        {"tick_late_p99_ms", worst_p99 >= 0 ? J(worst_p99) : J(nullptr)},
        {"bandwidth_pressure", pressure},
        {"egress_kbps", total},
        {"egress_budget_kbps", budget}};
#ifdef _WIN32
    server["platform"] = "windows-x64";
#else
    server["platform"] = "linux-x86_64";
#endif
    return {{"revision", config_rev},
            {"config_revision", config_rev},
            {"event_cursor", 0},
            {"server", server},
            {"rooms", rs},
            {"bots", bots},
            {"maps", maps},
            {"logs", logs},
            {"audit", audit},
            {"replays", published},
            {"capacity",
             {{"max_rooms", cfg.max_rooms},
              {"open_rooms", rooms.size()},
              {"max_active_matches", cfg.max_active},
              {"active_matches", active},
              {"max_spectators_per_room", cfg.spectator_limit},
              {"spectator_snapshot_hz", cfg.spectator_hz},
              {"player_snapshot_hz", 20},
              {"idle_room_ttl_seconds", cfg.ttl},
              {"restart_required_fields", {"listen_udp", "admin_listen"}}}}};
}
void Service::command(const J &j) {
    auto action = str(j, "action"), target = str(j, "target"), id = str(j, "command_id");
    bool ok = false;
    std::string note;
    Room *r = nullptr;
    int rev = config_rev;
    try {
        auto payload = j.at("payload");
        if (target.rfind("room:", 0) == 0) {
            auto i = rooms.find(target.substr(5));
            if (i == rooms.end())
                throw Error("room_closed", "房间已关闭");
            r = i->second.get();
            rev = r->policy_rev;
        }
        if (rev != j.value("expected_revision", -1))
            throw Error("revision_conflict", "配置在排队期间已变化");
        if (action == "set_capacity" || action == "set_room_creation") {
            if (action == "set_room_creation" &&
                (!payload.is_object() || payload.size() != 1 || !payload.contains("allow_room_creation")))
                throw Error("bad_payload", "仅可变更建房开关");
            auto next = cfg;
            next.max_rooms = payload.value("max_rooms", cfg.max_rooms);
            next.max_active = payload.value("max_active_matches", cfg.max_active);
            next.spectator_limit = payload.value("max_spectators_per_room", cfg.spectator_limit);
            next.creation = payload.value("allow_room_creation", cfg.creation);
            next.ttl = payload.value("idle_room_ttl_seconds", cfg.ttl);
            next.spectator_hz = payload.value("spectator_snapshot_hz", cfg.spectator_hz);
            auto cap = payload.value("egress_budget_kbps", budget);
            next.egress_budget = cap;
            if (next.max_rooms < 1 || next.max_active < 1 || next.max_active > next.max_rooms ||
                next.spectator_limit < -1 || next.ttl < 0 || next.spectator_hz < 1 ||
                next.spectator_hz > 20 || cap < 0)
                throw Error("bad_capacity", "容量或频率超出范围");
            next.persist_limits();
            for (auto &[_, rm] : rooms) {
                if (rm->spec_limit == cfg.spectator_limit)
                    rm->spec_limit = next.spectator_limit;
                else if (next.spectator_limit >= 0 &&
                         (rm->spec_limit < 0 || rm->spec_limit > next.spectator_limit))
                    rm->spec_limit = next.spectator_limit;
                if (rm->spec_hz == cfg.spectator_hz)
                    rm->spec_hz = next.spectator_hz;
                rm->broadcast();
            }
            cfg = std::move(next);
            budget = cap;
        } else if (action == "set_drain_mode") {
            draining = payload.at("draining");
        } else if (action == "set_room_spectator_policy" && r) {
            auto limit = payload.value("spectator_limit", r->spec_limit),
                 hz = payload.value("spectator_snapshot_hz", r->spec_hz);
            if (limit < -1 || (cfg.spectator_limit >= 0 && (limit < 0 || limit > cfg.spectator_limit)) ||
                hz < 1 || hz > 20)
                throw Error("bad_limit", "房间限额不能放宽服主上限，频率须在 1–20");
            r->spec_limit = limit;
            r->spec_hz = hz;
            r->policy_rev++;
            r->broadcast();
        } else if (action == "set_room_map" && r) {
            r->next_map(payload.at("next_map_id"), catalog());
        } else if (action == "kick_member" && r) {
            auto p = str(payload, "member_id");
            if (!r->members.count(p))
                throw Error("not_member", "成员已离开");
            leave(p);
            output(p, {{"type", "left"}, {"reason", "removed"}}, true);
            r->policy_rev++;
        } else if (action == "set_bot_enabled" || action == "set_map_enabled") {
            bool bot = action == "set_bot_enabled";
            auto prefix = bot ? "bot:" : "map:";
            if (target.rfind(prefix, 0) != 0)
                throw Error("bad_target", "资源类型不符");
            auto key = target.substr(4);
            auto &items = roster[bot ? "bots" : "maps"];
            if (std::none_of(items.begin(), items.end(), [&](auto &i) { return str(i, "id") == key; }))
                throw Error("not_found", "找不到目标");
            auto &set = bot ? disabled_bots : disabled_maps;
            if (payload.at("enabled").get<bool>())
                set.erase(key);
            else
                set.insert(key);
        } else
            throw Error("bad_target", "操作与目标不匹配");
        if (!r) {
            ++config_rev;
            policy(true);
        }
        rev = r ? r->policy_rev : config_rev;
        note = "已应用；现有参赛者和观众保留";
        ok = true;
        dir_dirty = true;
    } catch (const std::exception &e) {
        note = e.what();
    }
    J row = {{"id", ++audit_id},
             {"stamp", utc_stamp()},
             {"actor", str(j, "actor")},
             {"role", str(j, "role")},
             {"action", action},
             {"target", target},
             {"command_id", id},
             {"result", ok ? "applied" : "failed"},
             {"detail", note}};
    limited(audit, row, 160);
    log("audit", row.dump());
    if (web) {
        web->publish(state());
        web->event(ok ? "command.applied" : "command.failed", {{"command_id", id},
                                                               {"action", action},
                                                               {"target", target},
                                                               {"revision", rev},
                                                               {"requires_restart", false},
                                                               {ok ? "summary" : "reason", note}});
    }
}
void Service::pump() {
    auto now = Clock::now();
    if (mode == "server" && (last_resource == Time{} || now - last_resource >= 1s)) {
        auto sample = cpu_seconds();
        if (sample >= 0 && cpu_prior >= 0 && last_resource != Time{})
            cpu_percent =
                100. * (sample - cpu_prior) / std::chrono::duration<double>(now - last_resource).count();
        cpu_prior = sample;
        last_resource = now;
        double sum = 0;
        for (auto &[_, c] : clients)
            sum += c.kbps;
        int previous = pressure;
        if (budget <= 0) {
            pressure = 0;
            over = under = 0;
        } else if (sum > budget) {
            under = 0;
            if (++over >= 2) {
                pressure = std::min(2, pressure + 1);
                over = 0;
            }
        } else if (sum < budget * .78) {
            over = 0;
            if (++under >= 4) {
                pressure = std::max(0, pressure - 1);
                under = 0;
            }
        } else
            over = under = 0;
        if (previous != pressure) {
            for (auto &[_, r] : rooms) {
                r->pressure = pressure;
                r->broadcast();
            }
            dir_dirty = true;
        }
    }
    for (auto i = pending.begin(); i != pending.end();) {
        if (i->task.wait_for(0ms) != std::future_status::ready) {
            ++i;
            continue;
        }
        try {
            complete(i->peer, i->request, i->task.get());
        } catch (const Error &e) {
            fail(i->peer, e.code, e.what());
        } catch (const std::exception &e) {
            fail(i->peer, "auth_error", e.what());
        }
        i = pending.erase(i);
    }
    for (auto i = rooms.begin(); i != rooms.end();) {
        auto &r = *i->second;
        auto prior = r.state;
        r.update(now,[&](J&& rec,std::string&& native,const J& meta){archive(std::move(rec),std::move(native),meta);});
        if (r.series_due(now))
            r.advance_series(now, catalog(), cfg.assets, !draining);
        if (prior != r.state)
            dir_dirty = true;
        if (mode == "server" && r.members.empty() && now - r.empty_since >= std::chrono::seconds(cfg.ttl)) {
            log("room", "回收空房间", r.id);
            i = rooms.erase(i);
            dir_dirty = true;
        } else
            ++i;
    }
    save_progress(false);
    if (dir_dirty && now - last_dir >= 200ms) {
        dir_dirty = false;
        last_dir = now;
        auto d = directory();
        for (auto &[p, c] : clients)
            if (!c.name.empty() && c.room.empty())
                output(p, d, true);
    }
    if (web) {
        for (auto &j : web->commands())
            command(j);
        if (now >= next_admin) {
            auto s = state();
            web->publish(s);
            J rows = arr();
            for (auto &r : s["rooms"])
                rows.push_back({{"room_id", r["room_id"]},
                                {"egress_kbps", r["egress_kbps"]},
                                {"snapshot_backlog", r["snapshot_backlog"]},
                                {"spectator_count", r["spectator_count"]}});
            web->event("telemetry", {{"server", s["server"]}, {"rooms", rows}});
            next_admin = now + 1s;
        }
    }
    if (mode == "server" && now >= next_clean) {
        for (auto i = auth_attempts.begin(); i != auth_attempts.end();) {
            if (i->second.empty() || now - i->second.back() > 10s)
                i = auth_attempts.erase(i);
            else
                ++i;
        }
        if(!saving.valid()&&saves.empty())prune();
        next_clean = now + 5min;
    }
    for (auto i = release_after.begin(); i != release_after.end();)
        if (i->second <= now)
            i = release_after.erase(i);
        else
            ++i;
}
bool Service::idle() const {
    return rooms.empty() ||
           std::all_of(rooms.begin(), rooms.end(), [](auto &v) { return !v.second->active(); });
}
void Service::drain(bool v) {
    draining = v;
    dir_dirty = true;
    log("operator", v ? "服务器进入排空状态" : "服务器恢复接纳");
}
void Service::shutdown() {
    save_progress(true);
    web.reset();
    for (auto &[_, r] : rooms) {
        if (r->active())
            log("room", "关服中断", r->id);
        r->close();
    }
    rooms.clear();
}
} // namespace fbar
