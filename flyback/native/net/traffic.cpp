#include "net.hpp"
#include "wire.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>
// Replay exactly one archived FFA4 through the real wire encoder and loopback
// sockets. Player traffic uses fully visible states, not seat-specific fog.
using namespace flyback::net;
using Clock = std::chrono::steady_clock;
static std::vector<Json> replay(const char *path) {
    std::ifstream f(path);
    if (!f)
        throw std::runtime_error("could not open replay");
    std::vector<Json> states;
    std::string line;
    Json events = Json::array();
    while (std::getline(f, line)) {
        std::istringstream is(line);
        std::string k;
        int tick;
        is >> k >> tick;
        if (k == "E") {
            std::string t;
            int a, b, c, d;
            if (!(is >> t >> a >> b >> c >> d))
                throw std::runtime_error("invalid event");
            events.push_back(Json{{"tick", tick},
                                  {"type", t},
                                  {"ordinal", events.size()},
                                  {"a", a},
                                  {"b", b},
                                  {"c", c},
                                  {"d", d}});
        }
        if (k != "S")
            continue;
        std::vector<std::vector<int>> groups;
        std::string col;
        std::getline(is, col, '|');
        while (std::getline(is, col, '|')) {
            std::istringstream vs(col);
            std::vector<int> g;
            int x;
            while (vs >> x)
                g.push_back(x);
            groups.push_back(std::move(g));
        }
        if (groups.size() != 9)
            throw std::runtime_error("provide an upstream FFA4 replay");
        auto ps = Json::array(), ds = Json::array();
        for (int i = 0; i < 4; i++) {
            auto &p = groups[i], &d = groups[i + 4];
            if (p.size() != 18 || d.size() != 7)
                throw std::runtime_error("invalid replay column count");
            Json sh = {{"seat", i}, {"alive", p[0] != 0}, {"vis", true}, {"kills", p[12]}};
            int at = 1;
            for (auto key : {"x", "y", "vx", "vy", "face", "st", "timer", "ax", "ay", "cd", "expose"})
                sh[key] = p[at++];
            ps.push_back(std::move(sh));
            Json disc = {{"seat", i}, {"st", d[0]}, {"vis", true}};
            at = 1;
            for (auto key : {"x", "y", "vx", "vy", "left", "age"})
                disc[key] = d[at++];
            ds.push_back(std::move(disc));
        }
        Json j = {{"type", "snapshot"},
                  {"room_id", "fixture-g23457"},
                  {"round_epoch", 1},
                  {"timeline_epoch", 0},
                  {"pov", -1},
                  {"tick", tick},
                  {"finished", false},
                  {"fallen", groups[8].at(0)},
                  {"warn", groups[8].at(1)},
                  {"players", ps},
                  {"darts", ds},
                  {"events", events}};
        states.push_back(std::move(j));
        events = Json::array();
    }
    if (states.size() < 20)
        throw std::runtime_error("short fixture");
    states.back()["finished"] = true;
    return states;
}
int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "usage: flyback-net-traffic boomerang/replays/g23457.txt\n";
        return 2;
    }
    try {
        auto frames = replay(argv[1]);
        auto dir = std::filesystem::temp_directory_path() /
                   ("flyback-traffic-" + std::to_string(Clock::now().time_since_epoch().count()));
        Endpoint server, player, watch;
        server.listen("127.0.0.1", 0, dir);
        player.connect("127.0.0.1", server.port(), server.fingerprint());
        watch.connect("127.0.0.1", server.port(), server.fingerprint());
        Peer pp = 0, wp = 0;
        std::size_t pr = 0, wr = 0;
        auto pump = [&]() {
            while (auto e = player.poll()) {
                if (e->kind == EventKind::connected)
                    player.send(e->peer, {{"op", "identify"}, {"name", "player"}});
                if (e->kind == EventKind::message)
                    pr++;
            }
            while (auto e = watch.poll()) {
                if (e->kind == EventKind::connected)
                    watch.send(e->peer, {{"op", "identify"}, {"name", "watch"}});
                if (e->kind == EventKind::message)
                    wr++;
            }
            while (auto e = server.poll()) {
                if (e->kind == EventKind::message) {
                    if (e->data.value("name", "") == "player")
                        pp = e->peer;
                    else
                        wp = e->peer;
                }
            }
        };
        auto end = Clock::now() + std::chrono::seconds(4);
        while ((!pp || !wp) && Clock::now() < end) {
            pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!pp || !wp)
            throw std::runtime_error("QUIC handshake timeout");
        auto a = server.stats(pp).at("bytes_sent").get<std::uint64_t>(),
             b = server.stats(wp).at("bytes_sent").get<std::uint64_t>();
        double app = 0;
        int max = 0;
        end = Clock::now();
        Json two = Json::array();
        for (auto &frame : frames) {
            auto p = frame;
            p["pov"] = 0;
            p["last_applied_input_seq"] = p["tick"];
            auto bytes = wire::encode(p, false);
            if (wire::decode(bytes.data(), bytes.size(), false) != p)
                throw std::runtime_error("codec mismatch");
            app += bytes.size();
            max = std::max<int>(max, bytes.size());
            if (!server.send(pp, p, false))
                throw std::runtime_error("player send over negotiated MTU");
            for (auto &e : frame["events"])
                two.push_back(e);
            if (frame["tick"].get<int>() % 2 == 0) {
                auto w = frame;
                w["events"] = two;
                two = Json::array();
                if (!server.send(wp, w, false))
                    throw std::runtime_error("spectator send failed");
            }
            end += std::chrono::milliseconds(50);
            do {
                pump();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while (Clock::now() < end);
        }
        auto p = server.stats(pp), w = server.stats(wp);
        double sec = frames.size() / 20.;
        std::cout << Json{{"states", frames.size()},
                          {"seconds_at_20tps", sec},
                          {"player_application_B_mean", app / frames.size()},
                          {"player_application_B_max", max},
                          {"player_udp_payload_kbit_s",
                           (p.at("bytes_sent").get<std::uint64_t>() - a) * 8 / sec / 1000},
                          {"watch_udp_payload_kbit_s",
                           (w.at("bytes_sent").get<std::uint64_t>() - b) * 8 / sec / 1000},
                          {"player_received", pr},
                          {"watch_received", wr},
                          {"watch_bases", w["spectator_bases"]},
                          {"watch_deltas", w["spectator_deltas"]}}
                         .dump(2)
                  << std::endl;
        std::filesystem::remove(dir / "server.crt");
        std::filesystem::remove(dir / "server.key");
        std::filesystem::remove(dir);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
