#include "net.hpp"
#include "wire.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace flyback::net;
using Clock = std::chrono::steady_clock;
void want(bool v, const char *m) {
    if (!v)
        throw std::runtime_error(m);
}
static void vectors() {
    std::ifstream input(FBAR_VECTOR_FILE);
    auto all = Json::parse(input);
    auto unhex = [](const std::string &h) {
        wire::Bytes bytes;
        want(h.size() % 2 == 0, "invalid vector hex");
        for (size_t i = 0; i < h.size(); i += 2)
            bytes.push_back(static_cast<std::uint8_t>(std::stoul(h.substr(i, 2), nullptr, 16)));
        return bytes;
    };
    for (const auto &f : all.at("frames")) {
        auto b = unhex(f.at("hex"));
        bool reliable = f.at("reliable");
        want(wire::encode(f.at("json"), reliable) == b,
             "canonical encoded frame diverged from published vector");
        want(wire::decode(b.data(), b.size(), reliable) == f.at("json"),
             "canonical decoded frame diverged from published vector");
    }
    const auto &d = all.at("delta");
    auto p = unhex(d.at("patch_hex"));
    want(wire::delta(d.at("base"), d.at("next")) == p && wire::apply(d.at("base"), p) == d.at("next"),
         "delta vector mismatch");
}
void weak_link_test();
int main(int argc, char **argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "--weak") {
            weak_link_test();
            return 0;
        }
        vectors();
        auto dir = std::filesystem::temp_directory_path() /
                   ("flyback-quic-" + std::to_string(Clock::now().time_since_epoch().count()));
        Endpoint s, c;
        s.listen("127.0.0.1", 0, dir);
        auto p = c.connect("127.0.0.1", s.port());
        Peer remote = 0;
        bool challenge = false, up = false, got = false, shot = false, bulk = false;
        std::string pin = s.fingerprint();
        int attempts = 0;
        auto until = Clock::now() + std::chrono::seconds(9);
        std::string report(35000, 'x');
        auto input = Json{{"op", "input"}, {"mx", 200}, {"seq", 3}};
        auto state = Json{
            {"type", "snapshot"},
            {"tick", 7},
            {"players", Json::array({Json{{"x", -50}, {"y", 100}}, Json{{"x", 202}, {"y", -12}}})}};
        auto manifest = Json{{"type", "manifest"}, {"map", "双湖"}, {"terrain", report}};
        while (Clock::now() < until && !(got && shot && bulk)) {
            while (auto e = c.poll()) {
                if (e->kind == EventKind::trust_required) {
                    want(!challenge && e->data["fingerprint"] == pin, "bad trust prompt");
                    want(!c.send(p, {{"op", "join"}, {"password", "never-sent"}}, true),
                         "sent before acceptance");
                    challenge = true;
                    c.trust(p, true);
                }
                if (e->kind == EventKind::connected) {
                    up = true;
                    want(c.send(p, {{"op", "list"}}, true), "list send");
                }
                if (e->kind == EventKind::message) {
                    if (e->data == state)
                        shot = true;
                    if (e->data == manifest)
                        bulk = true;
                }
                if (e->kind == EventKind::disconnected)
                    throw std::runtime_error("unexpected disconnect client: " + e->data.dump());
            }
            while (auto e = s.poll()) {
                if (e->kind == EventKind::connected)
                    remote = e->peer;
                if (e->kind == EventKind::message) {
                    if (e->data == input) {
                        want(!e->reliable, "input was stream");
                        got = true;
                    }
                    if (e->data == Json{{"op", "list"}})
                        want(s.send(e->peer, manifest, true), "manifest send");
                }
                if (e->kind == EventKind::disconnected)
                    throw std::runtime_error("unexpected disconnect server: " + e->data.dump());
            }
            if (remote && up && ++attempts % 10 == 0) {
                c.send(p, input, false);
                s.send(remote, state, false);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        want(challenge && up && got && shot && bulk,
             "QUIC connect/stream/datagram delivery incomplete");
        auto stats = c.stats(p);
        std::cout << "TLS1.3/FBAR IPv4 ephemeral UDP " << s.port()
                  << " authenticated, bidirectional datagrams and control/bulk intact: " << stats.dump()
                  << std::endl;
        Json view = state;
        view["room_id"] = "twin";
        view["round_epoch"] = 1;
        view["timeline_epoch"] = 0;
        view["pov"] = -1;
        view["events"] = Json::array();
        for (auto &v : view["players"]) {
            v["alive"] = true;
            v["vx"] = 0;
            v["vy"] = 44;
            v["st"] = 1;
            v["face"] = 15;
        }
        view["darts"] = view["players"];
        auto observed = [&](Json msg) {
            auto end = Clock::now() + std::chrono::seconds(1);
            bool received = false;
            while (Clock::now() < end && !received) {
                want(s.send(remote, msg, false), "spectator send failed");
                while (s.poll()) {
                }
                auto window = Clock::now() + std::chrono::milliseconds(8);
                while (Clock::now() < window && !received) {
                    while (auto e = c.poll())
                        if (e->kind == EventKind::message && e->data == msg)
                            received = true;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            want(received, "spectator delta not reconstructed");
        };
        observed(view);
        for (int i = 8; i < 12; i++) {
            view["tick"] = i;
            view["players"][0]["x"] = i * 10;
            observed(view);
        }
        want(s.stats(remote).at("spectator_deltas") > 0, "spectator did not use ACK-based delta");
        auto bases = s.stats(remote).at("spectator_bases").get<unsigned>();
        view["pov"] = 0;
        view["players"][1] = Json{{"vis", false}, {"alive", true}};
        view["tick"] = 12;
        observed(view);
        want(s.stats(remote).at("spectator_bases") > bases, "view switch did not reset reference");
        std::cout
            << "Spectator: ACK-based delta delivered and new POV received its own restricted baseline"
            << std::endl;
        auto changed = pin;
        changed[0] = pin[0] == 'F' ? 'A' : 'F';
        Peer bad = c.connect("127.0.0.1", s.port(), changed),
             good = c.connect("127.0.0.1", s.port(), pin);
        bool blocked = false, repinned = false;
        until = Clock::now() + std::chrono::seconds(8);
        while (Clock::now() < until && !(blocked && repinned)) {
            while (auto e = c.poll()) {
                if (e->peer == bad) {
                    want(e->kind != EventKind::connected && e->kind != EventKind::trust_required,
                         "pin mismatch not enforced");
                    if (e->kind == EventKind::disconnected) {
                        blocked = e->data.value("reason", "") == "certificate_pin_changed";
                    }
                }
                if (e->peer == good && e->kind == EventKind::connected)
                    repinned = true;
            }
            while (s.poll()) {
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        want(blocked && repinned, "pin confirmation or mismatch block failed");
        c.disconnect(good);
        c.disconnect(p);
        std::cout << "remembered key accepted; changed key rejected before login" << std::endl;
        auto b = wire::encode(manifest, true);
        want(b[8] == 1 && wire::decode(b.data(), b.size(), true) == manifest, "bulk zstd");
        auto pw = wire::encode(Json{{"op", "join"}, {"password", std::string(600, 'a')}}, true);
        want(pw[8] == 0, "secret compressed");
        auto msg = wire::encode(input, false);
        msg[6] = 1;
        bool rejected = false;
        try {
            wire::decode(msg.data(), msg.size(), false);
        } catch (...) {
            rejected = true;
        }
        want(rejected, "profile mismatch accepted");
        // IPv6, real large one-way stream (incompressible text), trust refusal and quiet teardown.
        {
            Endpoint v6, client;
            v6.listen("::1", 0, dir);
            auto id = client.connect("::1", v6.port(), pin);
            bool receive = false, connect = false;
            Peer serverPeer = 0;
            std::string noise;
            std::uint32_t rnd = 113;
            for (int i = 0; i < 90000; i++) {
                rnd ^= rnd << 13;
                rnd ^= rnd >> 17;
                rnd ^= rnd << 5;
                noise += char(33 + rnd % 90);
            }
            Json clip = {{"type", "replay_clip"}, {"payload", noise}};
            auto end = Clock::now() + std::chrono::seconds(7);
            while (Clock::now() < end && !receive) {
                while (auto e = v6.poll()) {
                    if (e->kind == EventKind::connected)
                        serverPeer = e->peer;
                    if (e->kind == EventKind::message && e->data.value("op", "") == "list") {
                        want(v6.send(e->peer, clip, true), "bulk replay send");
                    }
                }
                while (auto e = client.poll()) {
                    if (e->kind == EventKind::connected) {
                        connect = true;
                        want(client.send(id, {{"op", "list"}}, true), "IPv6 list");
                    }
                    if (e->kind == EventKind::message && e->data == clip)
                        receive = true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            want(connect && serverPeer && receive, "IPv6/large independent stream");
        }
        {
            Endpoint decline;
            auto declined = decline.connect("127.0.0.1", s.port());
            bool rejected = false;
            auto end = Clock::now() + std::chrono::seconds(5);
            while (Clock::now() < end && !rejected) {
                while (auto e = decline.poll()) {
                    if (e->kind == EventKind::trust_required)
                        decline.trust(declined, false);
                    if (e->kind == EventKind::connected)
                        throw std::runtime_error("declined certificate connected");
                    if (e->kind == EventKind::disconnected)
                        rejected = true;
                }
                while (s.poll()) {
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            want(rejected, "declined certificate must stop");
        }
        std::cout << "IPv6 accepted pinned key; 90kB replay on independent reliable stream; first-use "
                     "decline blocked"
                  << std::endl;
        if (argc > 1 && std::string(argv[1]) == "--lan") {
            Endpoint lan, browse;
            lan.listen("*", 0, dir);
            lan.advertise("飞返");
            browse.discover();
            bool found = false;
            auto end = Clock::now() + std::chrono::seconds(4);
            while (Clock::now() < end && !found) {
                while (lan.poll()) {
                }
                while (auto e = browse.poll()) {
                    if (e->kind == EventKind::discovered && e->data["port"] == lan.port()) {
                        found = true;
                        std::cout << "mDNS: " << e->data.dump() << std::endl;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            want(found, "LAN discovery failed; check multicast/firewall/interface");
        }
        std::filesystem::remove(dir / "server.key");
        std::filesystem::remove(dir / "server.crt");
        std::filesystem::remove(dir);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
