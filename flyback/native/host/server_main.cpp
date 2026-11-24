#include "service.hpp"
#include <atomic>
#include <csignal>
#include <iostream>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#endif
using namespace fbar;
namespace net = flyback::net;
static std::atomic<bool> stop_now{false};
#ifdef _WIN32
static BOOL WINAPI stop_signal(DWORD code) {
    if (code == CTRL_C_EVENT || code == CTRL_CLOSE_EVENT || code == CTRL_BREAK_EVENT ||
        code == CTRL_SHUTDOWN_EVENT) {
        stop_now = true;
        return TRUE;
    }
    return FALSE;
}
#endif
static std::string secret() {
    std::string p;
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    bool tty = GetConsoleMode(h, &mode);
    if (tty)
        SetConsoleMode(h, mode & ~ENABLE_ECHO_INPUT);
    std::getline(std::cin, p);
    if (tty)
        SetConsoleMode(h, mode);
#else
    termios t{};
    bool tty = tcgetattr(STDIN_FILENO, &t) == 0;
    if (tty) {
        auto m = t;
        m.c_lflag &= ~ECHO;
        tcsetattr(STDIN_FILENO, TCSANOW, &m);
    }
    std::getline(std::cin, p);
    if (tty)
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
#endif
    std::cerr << "\n";
    return p;
}
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    auto argument = [&](int i) { return fs::path(argv[i], fs::path::native_format).u8string(); };
#else
int main(int argc, char **argv) {
    auto argument = [&](int i) { return std::string(argv[i]); };
#endif
    try {
        fs::path file = "server.toml", assets = home() / "data";
        std::string user = "admin", role = "admin";
        std::vector<std::string> args;
        for (int i = 1; i < argc; i++) {
            std::string a = argument(i);
            auto val = [&]() -> std::string {
                if (++i >= argc)
                    throw std::runtime_error("缺少 " + a + " 的值");
                return argument(i);
            };
            if (a == "--config")
                file = fs::u8path(val());
            else if (a == "--assets")
                assets = fs::u8path(val());
            else if (a == "--user")
                user = val();
            else if (a == "--role")
                role = val();
            else
                args.push_back(a);
        }
        if (!args.empty() && (args[0] == "--help" || args[0] == "-h")) {
            std::cout
                << "arena-server [--config server.toml] [--assets data]\n  check-config | admin init|reset "
                   "[--user NAME --role admin|operator|viewer]\nConsole: rooms, drain, resume, stop\n";
            return 0;
        }
        if (!fs::exists(file)) {
            auto sample = home() / "server.toml";
            if (!fs::exists(sample))
                sample = home() / "server.example.toml";
            if (!fs::exists(sample))
                throw std::runtime_error("未找到 server.toml；请从发行包复制配置示例");
            atomic_file(file, load_text(sample));
        }
        auto cfg = Config::read(file, assets);
        if (!args.empty() && args[0] == "check-config") {
            std::cout << "OK " << cfg.file.u8string() << "\n";
            return 0;
        }
        if (args.size() == 2 && args[0] == "admin" && (args[1] == "init" || args[1] == "reset")) {
            std::cerr << "账号 " << user << " 的新密码（输入不回显）：";
            auto p = secret();
            std::cerr << "再次输入：";
            if (p != secret())
                throw std::runtime_error("两次输入不一致");
            Admin::set_user(cfg, user, role, p);
            std::cout << "已更新管理员资料，运行中的管理台会自动加载。该账号的旧会话将失效，请用新凭据登录。\n";
            return 0;
        }
        if (!args.empty())
            throw std::runtime_error("未知参数，参见 --help");
        auto [addr, port] = endpoint(cfg.listen, 27860);
        net::Endpoint ep;
        ep.listen(addr, port, cfg.storage / "identity");
        try {
            ep.advertise(cfg.name);
        } catch (const std::exception &e) {
            std::cerr << "自动发现不可用，地址直连仍可用：" << e.what() << "\n";
        }
        std::map<std::string, net::Peer> peers;
        Service svc(cfg, "server", [&](auto &id, J j, bool reliable) {
            auto it = peers.find(id);
            if (it != peers.end() && !ep.send(it->second, j, reliable) && reliable)
                ep.disconnect(it->second);
        });
        svc.network_info(ep.port(), ep.fingerprint());
        svc.start_admin();
        std::cout << J({{"type", "server_started"},
                        {"udp_port", ep.port()},
                        {"listen", addr},
                        {"advertise", cfg.advertise},
                        {"fingerprint", ep.fingerprint()},
                        {"admin_port", svc.web ? svc.web->port() : 0},
                        {"max_rooms", cfg.max_rooms},
                        {"max_active_matches", cfg.max_active},
                        {"max_spectators_per_room", cfg.spectator_limit}})
                         .dump()
                  << std::endl;
        svc.log("server", "专服已启动");
        std::signal(SIGTERM, [](int) { stop_now.store(true); });
        std::signal(SIGINT, [](int) { stop_now.store(true); });
#ifdef _WIN32
        SetConsoleCtrlHandler(stop_signal, TRUE);
#endif
        struct Console {
            std::mutex m;
            std::deque<std::string> lines;
        };
        auto console = std::make_shared<Console>();
#ifdef _WIN32
        std::thread([console] {
            std::string line;
            while (std::getline(std::cin, line)) {
                std::lock_guard l(console->m);
                if (console->lines.size() < 16)
                    console->lines.push_back(line);
            }
        }).detach();
#else
        // A service manager may leave stdin open without ever writing a newline.
        // Keep terminal I/O on the service loop so stop/SIGTERM never has to
        // contend with a detached stdio reader during process destruction.
        pollfd input{STDIN_FILENO, POLLIN, 0};
        std::string partial;
#endif
        Time sample = Clock::now();
        while (!stop_now) {
            for (int k = 0; k < 512; k++) {
                auto e = ep.poll();
                if (!e)
                    break;
                auto p = "p-" + std::to_string(e->peer);
                if (e->kind == net::EventKind::connected) {
                    peers[p] = e->peer;
                    svc.attached(p, e->data.value("host", std::string{}));
                } else if (e->kind == net::EventKind::message) {
                    svc.handle(p, e->data);
                } else if (e->kind == net::EventKind::disconnected) {
                    svc.detached(p);
                    peers.erase(p);
                }
            }
#ifndef _WIN32
            if (input.fd >= 0 && ::poll(&input, 1, 0) > 0) {
                char buf[512];
                auto n = (input.revents & (POLLIN | POLLHUP)) ? ::read(input.fd, buf, sizeof buf) : -1;
                if (n > 0) {
                    for (ssize_t i = 0; i < n; i++) {
                        if (buf[i] == '\n') {
                            if (!partial.empty() && partial.back() == '\r')
                                partial.pop_back();
                            console->lines.push_back(std::move(partial));
                            partial.clear();
                        } else if (partial.size() < 80) {
                            partial += buf[i];
                        }
                    }
                } else if (n == 0 || !(input.revents & (POLLIN | POLLHUP)) ||
                           (errno != EINTR && errno != EAGAIN)) {
                    if (!partial.empty())
                        console->lines.push_back(std::move(partial));
                    input.fd = -1; // EOF disables only the console, not the server.
                }
            }
#endif
            {
                std::lock_guard l(console->m);
                for (auto &s : console->lines) {
                    if (s == "stop")
                        stop_now = true;
                    else if (s == "drain")
                        svc.drain(true);
                    else if (s == "resume")
                        svc.drain(false);
                    else if (s == "rooms")
                        std::cout << svc.directory().dump() << std::endl;
                    else if (s == "status")
                        std::cout << svc.state().dump() << std::endl;
                    else
                        std::cout << "rooms | status | drain | resume | stop\n" << std::flush;
                }
                console->lines.clear();
            }
            if (Clock::now() >= sample) {
                for (auto &[id, p] : peers)
                    svc.net_stats(id, ep.stats(p));
                sample = Clock::now() + 1s;
            }
            svc.pump();
            if (svc.idle())
                std::this_thread::sleep_for(10ms);
            else
                wait_active(1ms);
        }
        svc.shutdown();
        std::cout << "已停止\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "arena-server: " << e.what() << std::endl;
        return 1;
    }
}
