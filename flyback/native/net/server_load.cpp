// Local-only authoritative server fixture. Requires the opt-in test server name
// and a pinned real server certificate. One independent QUIC connection per seat.
#include "net.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace {
using namespace flyback::net;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
void want(bool ok, const std::string &why) {
    if (!ok)
        throw std::runtime_error(why);
}
void nap() {
#ifdef _WIN32
    struct Wait {
        HANDLE h = CreateWaitableTimerExW(nullptr, nullptr, 2, TIMER_ALL_ACCESS);
        ~Wait() {
            if (h)
                CloseHandle(h);
        }
    };
    thread_local Wait w;
    if (w.h) {
        LARGE_INTEGER t{};
        t.QuadPart = -10000;
        SetWaitableTimer(w.h, &t, 0, nullptr, nullptr, FALSE);
        WaitForSingleObject(w.h, INFINITE);
        return;
    }
#endif
    std::this_thread::sleep_for(1ms);
}
struct Seat {
    Peer peer = 0;
    bool hello = false;
    std::string room, state;
    int pos = -1, rev = 0, round = 0, line = 0, tick = -1, frames = 0, ready_count = 0,
        viewer_count = 0;
};
class Clients {
    Endpoint net;
    std::map<Peer, int> ids;
    std::uint16_t port;
    std::string pin;
    Clock::time_point next_input = Clock::now();
    bool playing = false;
    int seq = 0;
    size_t dir_count = 0;

  public:
    std::vector<Seat> seats;
    Clients(std::uint16_t p, std::string k) : port(p), pin(std::move(k)) {}
    void add() {
        int i = seats.size();
        auto p = net.connect("127.0.0.1", port, pin);
        ids.emplace(p, i);
        Seat s;
        s.peer = p;
        seats.push_back(s);
    }
    void send(int i, Json j, bool r = true) {
        want(net.send(seats.at(i).peer, j, r) || !r, "reliable send was refused");
    }
    void pump() {
        for (int n = 0; n < 4096; n++) {
            auto e = net.poll();
            if (!e)
                break;
            int i = ids.at(e->peer);
            auto &s = seats[i];
            if (e->kind == EventKind::disconnected)
                throw std::runtime_error("test peer " + std::to_string(i) +
                                         " disconnected: " + e->data.dump());
            if (e->kind == EventKind::connected) {
                send(i, {{"op", "identify"}, {"name", "fixture-" + std::to_string(i)}});
                continue;
            }
            if (e->kind != EventKind::message)
                continue;
            const auto &j = e->data;
            auto t = j.value("type", std::string{});
            if (t == "error")
                throw std::runtime_error("room command rejected (" + std::to_string(i) +
                                         "): " + j.dump());
            if (t == "directory") {
                want(j.value("name", "") == "Flyback local capacity fixture",
                     "opt-in private fixture required");
                s.hello = true;
                dir_count = j.at("rooms").size();
            }
            if (t == "left") {
                s.room.clear();
                s.state.clear();
                s.tick = -1;
            }
            if (t == "snapshot") {
                if (s.tick < j.value("tick", -1))
                    s.tick = j.at("tick");
                s.frames++;
            }
            if (t == "room") {
                s.room = j.at("id");
                s.state = j.at("state");
                s.pos = j.at("self_seat");
                s.rev = j.at("room_rev");
                s.round = j.at("round_epoch");
                s.line = j.at("timeline_epoch");
                s.viewer_count = j.at("spectator_count");
                if (i == 0 || i == 4) {
                    s.ready_count = 0;
                    for (const auto &m : j.at("members"))
                        if (m.at("ready") == true && m.at("seat").get<int>() >= 0)
                            s.ready_count++;
                }
            }
        }
        auto now = Clock::now();
        if (playing && now >= next_input) {
            next_input = now + 50ms;
            seq++;
            for (int i = 0; i < 6; i++) {
                auto &s = seats.at(i);
                if (s.state == "playing")
                    send(i,
                         {{"op", "input"},
                          {"seq", seq},
                          {"round_epoch", s.round},
                          {"timeline_epoch", s.line},
                          {"mx", 0},
                          {"my", 0},
                          {"ax", 1000},
                          {"ay", 0},
                          {"act", 0}},
                         false);
            }
        }
    }
    template <class F> void until(F f, const char *why, std::chrono::milliseconds budget = 15s) {
        auto until = Clock::now() + budget;
        do {
            pump();
            if (f())
                return;
            nap();
        } while (Clock::now() < until);
        throw std::runtime_error(why);
    }
    void duration(std::chrono::milliseconds ms) {
        auto t = Clock::now() + ms;
        until([&] { return Clock::now() >= t; }, "duration timer", ms + 1s);
    }
    void prepare(int count) {
        for (int i = 0; i < count; i += 4) {
            int stop = std::min(i + 4, count);
            for (int k = i; k < stop; k++)
                add();
            until(
                [&] {
                    for (int k = i; k < stop; k++)
                        if (!seats[k].hello)
                            return false;
                    return true;
                },
                "identify/list timeout");
        }
    }
    void create(int i, int n, const std::string &name) {
        auto slots = std::vector<std::string>(n, "human");
        send(i, {{"op", "create"},
                 {"request_id", "r-" + name},
                 {"settings",
                  {{"title", name},
                   {"seat_count", n},
                   {"slots", slots},
                   {"maps", Json::array({"twinlakes"})},
                   {"map_policy", "fixed"}}}});
        until([&] { return !seats[i].room.empty(); }, "create timeout");
    }
    void empty_rooms(int n) {
        prepare(1);
        for (int k = 0; k < n; k++) {
            create(0, 2, "empty-" + std::to_string(k));
            send(0, {{"op", "leave"}});
            until([&] { return seats[0].room.empty(); }, "leave timeout");
        }
        send(0, {{"op", "list"}});
        until([&] { return dir_count == std::size_t(n); }, "empty room count mismatch");
        std::cout << Json{{"empty_rooms", n}, {"remaining_members", 0}}.dump() << std::endl;
    }
    void match() {
        prepare(106);
        create(0, 4, "A-four-players-100-viewers");
        create(4, 2, "B-isolation");
        auto A = seats[0].room, B = seats[4].room;
        for (int i = 1; i < 106; i++) {
            if (i == 4)
                continue;
            auto role = i >= 6 ? "spectator" : "player";
            send(i, {{"op", "join"},
                     {"room_id", i == 5 ? B : A},
                     {"role", role},
                     {"request_id", "j-" + std::to_string(i)}});
            until([&] { return !seats[i].room.empty(); }, "join timeout");
        }
        until([&] { return seats[0].viewer_count == 100; }, "server did not count 100 spectators");
        for (int i = 0; i < 6; i++)
            send(i, {{"op", "ready"}, {"ready", true}});
        until([&] { return seats[0].ready_count == 4 && seats[4].ready_count == 2; }, "ready mismatch");
        send(0, {{"op", "start"}, {"room_rev", seats[0].rev}});
        send(4, {{"op", "start"}, {"room_rev", seats[4].rev}});
        playing = true;
        until([&] { return seats[0].tick >= 4 && seats[4].tick >= 4; }, "start/group failed");
        send(4, {{"op", "pause"}, {"yes", true}});
        duration(150ms);
        send(5, {{"op", "vote"}, {"yes", true}});
        until([&] { return seats[4].state == "paused"; }, "isolation pause failed");
        int tickA = seats[0].tick, tickB = seats[4].tick;
        duration(1500ms);
        auto elapsedA = seats[0].tick - tickA, elapsedB = seats[4].tick - tickB;
        want(elapsedA >= 23 && elapsedA <= 25 && elapsedB <= 1, "pause leaked across rooms or changed human match pace");
        send(4, {{"op", "pause"}, {"yes", true}});
        duration(150ms);
        send(5, {{"op", "vote"}, {"yes", true}});
        until([&] { return seats[4].state == "playing"; }, "resume failed");
        until(
            [&] {
                for (int i = 6; i < 106; i++)
                    if (seats[i].frames < 3)
                        return false;
                return true;
            },
            "viewers did not receive delayed view");
        auto snapshot = [&]() {
            double play = 0, watch = 0;
            int vframes = 0;
            for (int i = 0; i < 106; i++) {
                double n = net.stats(seats[i].peer).at("bytes_received").get<double>();
                if (i >= 6) {
                    watch += n;
                    vframes += seats[i].frames;
                } else if (i < 4)
                    play += n;
            }
            return std::make_tuple(play, watch, vframes);
        };
        auto [p0, w0, v0] = snapshot();
        std::vector<int> initial;
        for (auto &s : seats)
            initial.push_back(s.frames);
        int a0 = seats[0].tick, b0 = seats[4].tick;
        std::cout << "steady_window_start" << std::endl;
        auto start = Clock::now();
        duration(10s);
        double real = std::chrono::duration<double>(Clock::now() - start).count();
        auto [p1, w1, v1] = snapshot();
        int a1 = seats[0].tick, b1 = seats[4].tick, minv = 1000, maxv = 0;
        for (int i = 6; i < 106; i++) {
            minv = std::min(minv, seats[i].frames - initial[i]);
            maxv = std::max(maxv, seats[i].frames - initial[i]);
        }
        std::cout << Json{{"authenticated_quic_connections", 106},
                          {"room_A",
                           {{"players", 4},
                            {"spectators", 100},
                            {"ticks", a1 - a0},
                            {"tps", (a1 - a0) / real},
                            {"spectator_frames_total", v1-v0},
                            {"spectator_hz_average", (v1 - v0) / 100.0 / real},
                            {"spectator_frames_min", minv},
                            {"spectator_frames_max", maxv},
                            {"players_udp_payload_kbit_s", (p1 - p0) * 8 / real / 1000},
                            {"spectators_udp_payload_kbit_s", (w1 - w0) * 8 / real / 1000}}},
                          {"room_B", {{"players", 2}, {"ticks", b1 - b0}, {"tps", (b1 - b0) / real}}},
                          {"window_real_seconds", real},
                          {"isolation_1_5s",
                           {{"room_A_ticks", elapsedA}, {"room_B_paused_ticks", elapsedB}}}}
                         .dump()
                  << std::endl;
        const int target = int(std::lround(real * 16)); // human rooms are fixed at 0.8 of classic20
        want(std::abs((a1-a0)-target)<=1 && std::abs((b1-b0)-target)<=1 && minv >= 98,
             "normal room rate or spectator cadence below target");
    }
};
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc != 4) {
            std::cerr << "usage: flyback-net-load {warm|empty|match} LOOPBACK_PORT SPKI\n";
            return 2;
        }
        auto p = std::stoi(argv[2]);
        want(p > 0 && p <= 65535, "port out of range");
        Clients c(p, argv[3]);
        std::string mode = argv[1];
        if (mode == "warm")
            c.prepare(1);
        else if (mode == "empty")
            c.empty_rooms(100);
        else if (mode == "match")
            c.match();
        else
            return 2;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}

