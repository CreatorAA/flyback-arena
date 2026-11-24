#pragma once
#include <chrono>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
namespace fbar {
using J = nlohmann::json;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using Time = Clock::time_point;
using namespace std::chrono_literals;
void wait_active(std::chrono::microseconds d);
std::string random_token(size_t n = 16);
std::string password_hash(const std::string &);
bool password_ok(const std::string &, const std::string &);
void atomic_file(const fs::path &, const std::string &, bool secret = false);
std::string load_text(const fs::path &);
fs::path home();
fs::path profile();
std::pair<std::string, unsigned short> endpoint(const std::string &, unsigned short defport);
struct Config {
    fs::path file, storage, assets, web_root;
    std::string name = "飞返 · 周末服", listen = "0.0.0.0:27860", advertise;
    int max_rooms = 16, max_active = 4, spectator_limit = 32, ttl = 300, spectator_hz = 10, egress_budget = 0;
    bool creation = true, web_enabled = true, replays = true;
    std::string web_listen = "127.0.0.1:27861", origin, tls_mode = "reverse_proxy", web_cert, web_key;
    int retain_days = 14, replay_mib = 4096, log_mib = 16, log_keep = 8;
    static Config read(const fs::path &, const fs::path &);
    void persist_limits() const;
};
struct Error : std::runtime_error {
    std::string code;
    Error(std::string c, std::string m) : std::runtime_error(std::move(m)), code(std::move(c)) {}
};
} // namespace fbar
