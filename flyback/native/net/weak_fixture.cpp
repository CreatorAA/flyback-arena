// Integration fixture: two kernel UDP sockets, fixed to 127.0.0.1, one specific
// client and an in-process Flyback server. No machine-wide firewall/netem changes.
#include "net.hpp"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using Fd = SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
using Fd = int;
#define INVALID_SOCKET (-1)
#endif
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <random>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>
namespace {
using namespace flyback::net;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
void ok(bool b, const char *what) {
    if (!b)
        throw std::runtime_error(what);
}
sockaddr_in lo(std::uint16_t p) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(p);
    return a;
}
void tick_wait() {
#ifdef _WIN32
    struct Timer {
        HANDLE h = CreateWaitableTimerExW(nullptr, nullptr, 0x2, TIMER_ALL_ACCESS);
        ~Timer() {
            if (h)
                CloseHandle(h);
        }
    };
    thread_local Timer timer;
    ok(timer.h != nullptr, "high resolution fixture clock unavailable");
    LARGE_INTEGER due{};
    due.QuadPart = -10000;
    ok(SetWaitableTimer(timer.h, &due, 0, nullptr, nullptr, FALSE) != 0, "fixture clock arm");
    WaitForSingleObject(timer.h, INFINITE);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
}
struct Socket {
    Fd fd = INVALID_SOCKET;
    sockaddr_in local{};
    Socket() {
        fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        ok(fd != INVALID_SOCKET, "fixture socket");
        auto a = lo(0);
        ok(bind(fd, (sockaddr *)&a, sizeof(a)) == 0, "fixture bind");
        socklen_t n = sizeof(local);
        ok(getsockname(fd, (sockaddr *)&local, &n) == 0, "fixture endpoint");
#ifdef _WIN32
        u_long nb = 1;
        ok(ioctlsocket(fd, FIONBIO, &nb) == 0, "fixture fionbio");
#else
        ok(fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK) == 0, "fixture nonblock");
#endif
    }
    ~Socket() {
#ifdef _WIN32
        if (fd != INVALID_SOCKET)
            closesocket(fd);
#else
        if (fd != INVALID_SOCKET)
            close(fd);
#endif
    }
};
struct Hop {
    Clock::time_point due;
    bool forward;
    std::vector<char> data;
};
class SimulatedLink {
    Socket front, back;
    sockaddr_in dst, client{};
    std::mt19937 rng{91202};
    std::vector<Hop> queue;

  public:
    unsigned dropped = 0, outage = 0;
    bool blackout = false;
    explicit SimulatedLink(std::uint16_t p) : dst(lo(p)) {}
    std::uint16_t port() const {
        return ntohs(front.local.sin_port);
    }
    void pump() {
        auto now = Clock::now();
        for (bool f : {true, false}) {
            for (int i = 0; i < 64; ++i) {
                std::array<char, 65536> data;
                sockaddr_in peer{};
                socklen_t n = sizeof(peer);
                auto len = recvfrom(f ? front.fd : back.fd, data.data(), int(data.size()), 0,
                                    (sockaddr *)&peer, &n);
                if (len < 0)
                    break;
                if (peer.sin_addr.s_addr != htonl(INADDR_LOOPBACK))
                    continue;
                if (f) {
                    if (!client.sin_port)
                        client = peer;
                    if (peer.sin_port != client.sin_port)
                        continue;
                } else if (peer.sin_port != dst.sin_port)
                    continue;
                if (blackout) {
                    ++outage;
                    continue;
                }
                if (std::uniform_int_distribution<int>(1, 100)(rng) <= 2) {
                    ++dropped;
                    continue;
                }
                queue.push_back(
                    {now + std::chrono::milliseconds(std::uniform_int_distribution<int>(25, 55)(rng)),
                     f,
                     {data.data(), data.data() + len}});
            }
        }
        for (auto i = queue.begin(); i != queue.end();) {
            if (i->due > now) {
                ++i;
                continue;
            }
            auto &target = i->forward ? dst : client;
            sendto(i->forward ? back.fd : front.fd, i->data.data(), int(i->data.size()), 0,
                   (sockaddr *)&target, sizeof(target));
            i = queue.erase(i);
        }
    }
};
} // namespace
void weak_link_test() {
    using namespace flyback::net;
    using namespace std::chrono_literals;
    auto d = std::filesystem::temp_directory_path() /
             ("flyback-weak-" + std::to_string(Clock::now().time_since_epoch().count()));
    Endpoint server, client;
    server.listen("127.0.0.1", 0, d);
    SimulatedLink path(server.port());
    auto c = client.connect("127.0.0.1", path.port(), server.fingerprint());
    Peer s = 0;
    bool open = false, got_result = false;
    std::set<int> inputs, snapshots;
    std::vector<int> controls;
    std::vector<double> latency;
    std::array<Clock::time_point, 100> when{};
    auto pump = [&]() {
        path.pump();
        while (auto e = server.poll()) {
            if (e->kind == EventKind::disconnected)
                throw std::runtime_error("weak link lost server connection");
            if (e->kind == EventKind::connected)
                s = e->peer;
            if (e->kind == EventKind::message) {
                auto op = e->data.value("op", "");
                int i = e->data.value("seq", 0);
                if (op == "ready")
                    controls.push_back(i);
                if (op == "input") {
                    inputs.insert(i);
                    if (i >= 0 && i < 100)
                        latency.push_back(
                            std::chrono::duration<double, std::milli>(Clock::now() - when[i]).count());
                }
            }
        }
        while (auto e = client.poll()) {
            if (e->kind == EventKind::connected)
                open = true;
            if (e->kind == EventKind::disconnected)
                throw std::runtime_error("weak link lost client connection");
            if (e->kind == EventKind::message) {
                if (e->data.value("type", "") == "result")
                    got_result = true;
                else
                    snapshots.insert(e->data.at("tick").get<int>());
            }
        }
    };
    auto end = Clock::now() + 5s;
    while (Clock::now() < end && (!open || !s)) {
        pump();
        tick_wait();
    }
    ok(open && s, "weak link handshake");
    auto first = Clock::now();
    for (int i = 0; i < 100; ++i) {
        path.blackout = (i >= 35 && i < 55);
        when[i] = Clock::now();
        client.send(c, {{"op", "input"}, {"seq", i}, {"mx", 1000}}, false);
        server.send(s, {{"type", "snapshot"}, {"tick", i}}, false);
        if (i % 10 == 0)
            ok(client.send(c, {{"op", "ready"}, {"seq", i}}, true), "weak link control backpressure");
        if (i == 40)
            ok(server.send(s, {{"type", "result"}, {"notice", "reliable during blackout"}}, true),
               "weak link result backpressure");
        auto next = first + (i + 1) * 50ms;
        do {
            pump();
            tick_wait();
        } while (Clock::now() < next);
    }
    end = Clock::now() + 1500ms;
    while (Clock::now() < end) {
        pump();
        tick_wait();
    }
    ok(got_result && controls.size() == 10, "reliable control did not recover");
    for (int i = 0; i < 10; i++)
        ok(controls[i] == i * 10, "reliable control reordered/duplicated");
    ok(path.outage > 0 && path.dropped > 0 && inputs.size() >= 50 && snapshots.size() >= 50 &&
           *snapshots.rbegin() >= 95 && *inputs.rbegin() >= 95,
       "realtime states did not resume");
    std::sort(latency.begin(), latency.end());
    std::cout << Json{{"link", "80ms RTT +/-15ms per direction, 2% random loss, one 1s outage"},
                      {"wire_packets_lost_randomly", path.dropped},
                      {"wire_packets_dropped_in_outage", path.outage},
                      {"inputs_received_of_100", inputs.size()},
                      {"snapshots_received_of_100", snapshots.size()},
                      {"ordered_controls_received_of_10", controls.size()},
                      {"result_during_outage_delivered", got_result},
                      {"input_delivery_p50_ms", latency[latency.size() / 2]},
                      {"input_delivery_p95_ms", latency[(latency.size() * 95 - 1) / 100]}}
                     .dump(2)
              << std::endl;
    std::filesystem::remove(d / "server.key");
    std::filesystem::remove(d / "server.crt");
    std::filesystem::remove(d);
}
