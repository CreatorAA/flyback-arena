#include "util.hpp"
#include <array>
#include <cstdlib>
#include <fstream>
#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>
#include <sstream>
#include <thread>
#include <toml.hpp>
#include <vector>
#ifdef _WIN32
#include <aclapi.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
#ifdef FLYBACK_ARGON2_LIB
extern "C" int argon2id_hash_raw(uint32_t, uint32_t, uint32_t, const void *, size_t, const void *, size_t,
                                 void *, size_t);
#endif
namespace fbar {
void wait_active(std::chrono::microseconds duration) {
    if (duration <= 0us)
        return;
#ifdef _WIN32
    struct Timer {
        HANDLE handle = CreateWaitableTimerExW(nullptr, nullptr, 0x2, TIMER_ALL_ACCESS);
        ~Timer() {
            if (handle)
                CloseHandle(handle);
        }
    };
    static thread_local Timer t;
    if (t.handle) {
        LARGE_INTEGER due;
        due.QuadPart = -10 * duration.count();
        if (SetWaitableTimer(t.handle, &due, 0, nullptr, nullptr, FALSE)) {
            WaitForSingleObject(t.handle, INFINITE);
            return;
        }
    }
#endif
    std::this_thread::sleep_for(duration);
}
static std::string hex(const unsigned char *p, size_t n) {
    static const char *d = "0123456789abcdef";
    std::string s;
    s.reserve(2 * n);
    for (size_t i = 0; i < n; i++) {
        s += d[p[i] >> 4];
        s += d[p[i] & 15];
    }
    return s;
}
std::string random_token(size_t n) {
    std::vector<unsigned char> v(n);
    if (RAND_bytes(v.data(), (int)n) != 1)
        throw std::runtime_error("随机源不可用");
    return hex(v.data(), n);
}
static std::string derive(const std::string &p, const std::string &s) {
    if (p.size() > 256 || s.size() != 32)
        throw Error("bad_password", "密码最长 256 字节");
    std::array<unsigned char, 32> b{};
#ifdef FLYBACK_ARGON2_LIB
    if (argon2id_hash_raw(3, 19456, 1, p.data(), p.size(), s.data(), s.size(), b.data(), b.size()))
        throw std::runtime_error("Argon2id 运行失败");
#else
    auto *k = EVP_KDF_fetch(nullptr, "ARGON2ID", nullptr);
    auto *c = k ? EVP_KDF_CTX_new(k) : nullptr;
    EVP_KDF_free(k);
    if (!c)
        throw std::runtime_error("需要带 Argon2id 的 OpenSSL 3.2+ 或 libargon2");
    uint32_t iter = 3, mem = 19456, lanes = 1;
    OSSL_PARAM params[] = {OSSL_PARAM_construct_octet_string("pass", (void *)p.data(), p.size()),
                           OSSL_PARAM_construct_octet_string("salt", (void *)s.data(), s.size()),
                           OSSL_PARAM_construct_uint32("iter", &iter),
                           OSSL_PARAM_construct_uint32("memcost", &mem),
                           OSSL_PARAM_construct_uint32("lanes", &lanes),
                           OSSL_PARAM_construct_end()};
    int ok = EVP_KDF_derive(c, b.data(), b.size(), params);
    EVP_KDF_CTX_free(c);
    if (!ok)
        throw std::runtime_error("Argon2id 运行失败");
#endif
    return hex(b.data(), b.size());
}
std::string password_hash(const std::string &p) {
    auto s = random_token();
    return "argon2id-v1$" + s + "$" + derive(p, s);
}
bool password_ok(const std::string &p, const std::string &v) {
    if (v.size() != 109 || v.substr(0, 12) != "argon2id-v1$" || v[44] != '$')
        return false;
    auto h = derive(p, v.substr(12, 32));
    return CRYPTO_memcmp(h.data(), v.data() + 45, 64) == 0;
}
std::string load_text(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    if (!f)
        throw std::runtime_error("无法读取 " + p.u8string());
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}
void atomic_file(const fs::path &p, const std::string &s, bool secret) {
    if (!p.parent_path().empty())
        fs::create_directories(p.parent_path());
    auto q = p;
    q += fs::u8path(".tmp-" + random_token(5));
    {
        std::ofstream f(q, std::ios::binary);
        if (!f)
            throw std::runtime_error("无法写入 " + q.u8string());
#ifdef _WIN32
        if (secret) {
            HANDLE token = nullptr;
            if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
                throw std::runtime_error("无法获取私有文件所有者");
            DWORD n = 0;
            GetTokenInformation(token, TokenUser, nullptr, 0, &n);
            std::vector<unsigned char> b(n);
            bool ok = GetTokenInformation(token, TokenUser, b.data(), n, &n);
            CloseHandle(token);
            if (!ok)
                throw std::runtime_error("无法读取所有者 SID");
            auto sid = reinterpret_cast<TOKEN_USER *>(b.data())->User.Sid;
            EXPLICIT_ACCESSW acl{};
            acl.grfAccessPermissions = GENERIC_ALL;
            acl.grfAccessMode = SET_ACCESS;
            acl.grfInheritance = NO_INHERITANCE;
            acl.Trustee.TrusteeForm = TRUSTEE_IS_SID;
            acl.Trustee.ptstrName = (wchar_t *)sid;
            PACL list = nullptr;
            auto rc = SetEntriesInAclW(1, &acl, nullptr, &list);
            if (rc == ERROR_SUCCESS) {
                rc = SetNamedSecurityInfoW(const_cast<wchar_t *>(q.c_str()), SE_FILE_OBJECT,
                                           DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                           nullptr, nullptr, list, nullptr);
                LocalFree(list);
            }
            if (rc != ERROR_SUCCESS)
                throw std::runtime_error("无法设置私有文件 ACL");
        }
#else
        if (secret && chmod(q.c_str(), 0600) != 0)
            throw std::runtime_error("无法设置私有权限");
#endif
        f << s;
        f.close();
        if (!f)
            throw std::runtime_error("写入失败 " + q.u8string());
    }
#ifdef _WIN32
    if (!MoveFileExW(q.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fs::remove(q);
        throw std::runtime_error("无法替换文件 " + p.u8string());
    }
#else
    fs::rename(q, p);
#endif
}
fs::path home() {
#ifdef _WIN32
    if (auto *s = _wgetenv(L"FLYBACK_HOME"))
        return fs::absolute(s);
    std::wstring b(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, b.data(), static_cast<DWORD>(b.size()));
    b.resize(n);
    return fs::path(b).parent_path();
#else
    if (auto *s = std::getenv("FLYBACK_HOME"))
        return fs::absolute(fs::u8path(s));
    return fs::canonical("/proc/self/exe").parent_path();
#endif
}
fs::path profile() {
#ifdef _WIN32
    if (auto *s = _wgetenv(L"FLYBACK_PROFILE"))
        return fs::absolute(s);
    const wchar_t *root = _wgetenv(L"LOCALAPPDATA");
    return (root ? fs::path(root) : home()) / "FlybackArena";
#else
    if (auto *s = std::getenv("FLYBACK_PROFILE"))
        return fs::absolute(fs::u8path(s));
    if (auto *s = std::getenv("XDG_DATA_HOME"))
        return fs::u8path(s) / "flyback-arena";
    if (auto *s = std::getenv("HOME"))
        return fs::u8path(s) / ".local/share/flyback-arena";
    return home() / "profile";
#endif
}
std::pair<std::string, unsigned short> endpoint(const std::string &s, unsigned short port) {
    if (s.empty())
        throw Error("bad_address", "地址不能为空");
    std::string host = s, p;
    bool has_port = false;
    if (s[0] == '[') {
        auto i = s.find(']');
        if (i == s.npos)
            throw Error("bad_address", "IPv6 地址缺少 ]");
        host = s.substr(1, i - 1);
        if (i + 1 < s.size()) {
            if (s[i + 1] != ':')
                throw Error("bad_address", "IPv6 地址格式错误");
            p = s.substr(i + 2);
            has_port = true;
        }
    } else if (std::count(s.begin(), s.end(), ':') == 1) {
        auto i = s.find(':');
        host = s.substr(0, i);
        p = s.substr(i + 1);
        has_port = true;
    }
    if (host.empty())
        throw Error("bad_address", "地址缺少主机名");
    if (has_port) {
        if (p.empty() || p.size() > 5 || p.find_first_not_of("0123456789") != p.npos || std::stoi(p) > 65535)
            throw Error("bad_port", "端口须为 0–65535");
        port = (unsigned short)std::stoi(p);
    }
    return {host, port};
}
Config Config::read(const fs::path &file, const fs::path &assets) {
    Config c;
    c.file = fs::absolute(file);
    c.assets = assets;
    c.web_root = home() / "admin-web";
    auto t = toml::parse(load_text(file));
    auto assign = [&](const char *path, auto &v) {
        auto n = t.at_path(path);
        if (!n)
            return;
        using T = std::decay_t<decltype(v)>;
        auto x = n.template value<T>();
        if (!x)
            throw std::runtime_error(std::string("类型错误：") + path);
        v = *x;
    };
    assign("server.name", c.name);
    assign("server.listen", c.listen);
    assign("server.advertise", c.advertise);
    assign("server.allow_room_creation", c.creation);
    assign("server.max_rooms", c.max_rooms);
    assign("server.max_active_matches", c.max_active);
    assign("server.max_spectators_per_room", c.spectator_limit);
    assign("server.idle_room_ttl_seconds", c.ttl);
    assign("server.egress_budget_kbps", c.egress_budget);
    assign("match.spectator_snapshot_hz", c.spectator_hz);
    std::string rules = "classic20";
    int hz = 20;
    assign("match.ruleset", rules);
    assign("match.player_snapshot_hz", hz);
    if (rules != "classic20" || hz != 20)
        throw std::runtime_error("原版需要 classic20 和 20 Hz 玩家快照");
    std::string sd = "./data";
    assign("storage.data_dir", sd);
    c.storage = fs::absolute(c.file.parent_path() / fs::u8path(sd));
    assign("admin.web.enabled", c.web_enabled);
    assign("admin.web.listen", c.web_listen);
    assign("admin.web.public_origin", c.origin);
    assign("admin.web.tls_mode", c.tls_mode);
    assign("admin.web.certificate", c.web_cert);
    assign("admin.web.private_key", c.web_key);
    assign("replays.enabled", c.replays);
    assign("replays.retain_days", c.retain_days);
    assign("replays.max_disk_mib", c.replay_mib);
    assign("logs.rotate_mib", c.log_mib);
    assign("logs.keep_files", c.log_keep);
    if (c.max_rooms < 1 || c.max_active < 1 || c.max_active > c.max_rooms || c.egress_budget < 0 ||
        c.spectator_limit < -1 || c.ttl < 0 || c.spectator_hz < 1 || c.spectator_hz > 20 ||
        c.retain_days < 0 || c.replay_mib < 1 || c.log_mib < 1 || c.log_keep < 1)
        throw std::runtime_error("容量、快照频率或保留期超出范围");
    if (c.tls_mode != "https" && c.tls_mode != "reverse_proxy")
        throw std::runtime_error("admin.web.tls_mode 应为 https 或 reverse_proxy");
    endpoint(c.listen, 27860);
    endpoint(c.web_listen, 27861);
    return c;
}
void Config::persist_limits() const {
    auto raw = load_text(file);
    auto t = toml::parse(raw);
    auto *sec = t["server"].as_table();
    if (!sec)
        throw std::runtime_error("缺少 [server]");
    sec->insert_or_assign("max_rooms", max_rooms);
    sec->insert_or_assign("max_active_matches", max_active);
    sec->insert_or_assign("max_spectators_per_room", spectator_limit);
    sec->insert_or_assign("allow_room_creation", creation);
    sec->insert_or_assign("idle_room_ttl_seconds", ttl);
    sec->insert_or_assign("egress_budget_kbps", egress_budget);
    if (auto *match = t["match"].as_table())
        match->insert_or_assign("spectator_snapshot_hz", spectator_hz);
    std::ostringstream s;
    s << t;
    atomic_file(file.string() + ".bak", raw, true);
    atomic_file(file, s.str(), true);
}
} // namespace fbar
