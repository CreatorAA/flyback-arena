#include "replay.hpp"
#include "service.hpp"
#include <atomic>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif
using namespace fbar;
namespace net = flyback::net;
namespace {
void out(const J &j) {
    std::cout << j.dump(-1, ' ', false, J::error_handler_t::replace) << '\n' << std::flush;
}
std::string value(const J &j, const char *k) { return j.value(k, std::string{}); }
struct Lines {
    std::mutex m;
    std::deque<std::string> lines;
    std::atomic<bool> eof{false};
    void start() {
        std::thread([this] {
            std::string l;
            while (std::getline(std::cin, l)) {
                if (l.empty())
                    continue;
                {
                    std::lock_guard g(m);
                    if (l.size() <= 131072 && lines.size() < 128)
                        lines.push_back(std::move(l));
                }
            }
            eof = true;
        }).detach();
    }
    std::deque<std::string> take() {
        std::lock_guard g(m);
        std::deque<std::string> a;
        a.swap(lines);
        return a;
    }
};
class Bridge {
    fs::path base = home(), prefs = profile(), saved = prefs / "servers.json";
    J servers = J::array(), pins = J::object(), room = J::object();
    int64_t seq = 0;
    int round = 0, timeline = 0;
    J last_manifest, last_seat;
    std::deque<std::pair<Time, J>> edge_history;
    std::unique_ptr<Service> host;
    std::unique_ptr<net::Endpoint> link;
    net::Peer remote = 0;
    std::string address, name = "玩家";
    bool hosting = false;
    bool field_ready_enabled = false;
    std::map<std::string, net::Peer> peers;
    J playback;
    std::size_t next = 0;
    bool stopped = false;
    double rate = 1;
    Time nextframe{};
    std::string pending_pin;
    void store() { atomic_file(saved, J({{"servers", servers}, {"pins", pins}}).dump(2), true); }
    void show(J j) {
        auto t = value(j, "type");
        if (t == "manifest")
            last_manifest = j;
        if (t == "snapshot" && room.value("self_seat", -1) >= 0 &&
            j.value("pov", -1) == room.value("self_seat", -1))
            last_seat = j;
        if (t == "room") {
            if (round != j.value("round_epoch", 0) || timeline != j.value("timeline_epoch", 0))
                edge_history.clear();
            room = j;
            round = j.value("round_epoch", 0);
            timeline = j.value("timeline_epoch", 0);
        }
        if (t == "catalog") {
            j["servers"] = servers;
            j["local_features"] = {"predict_self", "replay_v1", "import_dart_v2", "field_ready"};
        }
        if (t == "left") {
            room = J::object();
            round = timeline = 0;
        }
        out(j);
    }
    void reset() {
        if (host)
            host->shutdown();
        host.reset();
        link.reset();
        peers.clear();
        remote = 0;
        hosting = false;
        pending_pin.clear();
        room = J::object();
        round = timeline = 0;
        last_manifest = nullptr;
        last_seat = nullptr;
        edge_history.clear();
        playback = nullptr;
    }
    void setup(bool serve, const J &j) {
        reset();
        Config c;
        c.assets = base / "data";
        c.storage = prefs;
        c.name = j.value("server_name", std::string("个人房"));
        c.creation = true;
        c.max_rooms = 1;
        c.max_active = 1;
        c.spectator_limit = -1;
        c.web_enabled = false;
        c.advertise = j.value("advertise", std::string{});
        hosting = serve;
        if (serve) {
            link = std::make_unique<net::Endpoint>();
            int port = j.value("port", 0);
            if (port < 0 || port > 65535)
                throw Error("bad_port", "UDP 端口须为 0–65535");
            link->listen(j.value("bind", std::string("*")), static_cast<unsigned short>(port),
                         prefs / "identity");
        }
        host = std::make_unique<Service>(c, serve ? "lan" : "offline",
                                         [this](const std::string &p, J v, bool reliable) {
                                             if (p == "local")
                                                 show(std::move(v));
                                             else if (peers.count(p) && link) {
                                                 if (!link->send(peers.at(p), v, reliable) && reliable)
                                                     link->disconnect(peers.at(p));
                                             }
                                         });
        if (serve) {
            host->network_info(link->port(), link->fingerprint());
            try {
                link->advertise(j.value("settings", J::object()).value("title", std::string("飞返竞技场")));
            } catch (const std::exception &e) {
                show({{"type", "notice"},
                      {"message", "局域网自动发现不可用，仍可用地址和端口加入。"},
                      {"reason", e.what()}});
            }
        }
        host->attached("local");
        host->handle("local", {{"op", "identify"}, {"name", name}});
        host->handle("local", {{"op", "create"},
                               {"settings", j.value("settings", J::object())},
                               {"field_ready", field_ready_enabled},
                               {"password", j.value("password", std::string{})}});
    }
    void open_replay(const J &j) {
        auto p = fs::u8path(j.at("path").get<std::string>());
        auto r = read_replay_file(p, flyback::catalog((base / "data").u8string()));
        reset();
        playback = std::move(r);
        start_playback();
    }
    void start_playback() {
        next = 0;
        stopped = false;
        rate = 1;
        nextframe = Clock::now();
        show({{"type", "room"},
              {"id", "replay"},
              {"title", "对局回放"},
              {"mode", "replay"},
              {"state", "playing"},
              {"round_epoch", 0},
              {"timeline_epoch", 0},
              {"self_seat", -1},
              {"self_id", "local"},
              {"host_id", "local"},
              {"slots", J::array()},
              {"members", J::array()},
              {"speed", 1}});
        auto m = playback["manifest"];
        m["type"] = "manifest";
        show(m);
    }

  public:
    Bridge() {
        if (fs::exists(saved)) {
            auto j = J::parse(load_text(saved));
            servers = j.value("servers", J::array());
            pins = j.value("pins", J::object());
        }
        catalog();
    }
    void catalog() {
        auto cat = flyback::catalog((base / "data").u8string());
        cat["type"] = "catalog";
        show(cat);
    }
    void command(J j) {
        auto op = value(j, "op");
        if (op == "enable_field_ready") {
            field_ready_enabled = true;
            return;
        }
        if (op == "catalog") {
            catalog();
            return;
        }
        if (op == "local" || op == "host") {
            name = j.value("name", name);
            setup(op == "host", j);
            return;
        }
        if (op == "connect") {
            reset();
            auto addr = j.at("address").get<std::string>();
            auto [h, p] = endpoint(addr, 27860);
            if (!p)
                throw Error("bad_port", "不可连接到 0 号端口");
            address = addr;
            name = j.value("name", name);
            link = std::make_unique<net::Endpoint>();
            auto proposed = j.value("fingerprint", std::string{});
            auto pin = pins.value(addr, std::string{});
            if (!pin.empty() && !proposed.empty() && pin != proposed)
                throw Error("identity_changed", "邀请身份与原有收藏不符，请先核对");
            remote = link->connect(h, p, pin.empty() ? proposed : pin);
            return;
        }
        if (op == "trust") {
            if (!link || !remote || pending_pin.empty())
                throw Error("not_pending", "没有待确认身份");
            bool accept = j.value("accept", false);
            link->trust(remote, accept);
            if (!accept)
                pending_pin.clear();
            return;
        }
        if (op == "save_server" || op == "remove_server") {
            auto a = j.at("address").get<std::string>();
            endpoint(a, 27860);
            servers.erase(std::remove_if(servers.begin(), servers.end(),
                                         [&](auto &v) { return value(v, "address") == a; }),
                          servers.end());
            if (op == "save_server")
                servers.push_back({{"address", a}, {"name", j.value("name", a)}});
            else if (j.value("forget_identity", false))
                pins.erase(a);
            store();
            catalog();
            return;
        }
        if (op == "discover") {
            if (!link)
                link = std::make_unique<net::Endpoint>();
            link->discover();
            return;
        }
        if (op == "replays") {
            auto index = prefs / "replays" / "index.json";
            auto items = fs::exists(index) ? J::parse(load_text(index)) : J::array();
            for (auto &i : items)
                i["path"] = (prefs / "replays" / (value(i, "replay_id") + ".fbar")).u8string();
            show({{"type", "replays"}, {"items", items}});
            return;
        }
        if (op == "replay") {
            open_replay(j);
            return;
        }
        if (!playback.is_null()) {
            if (op == "pause") {
                stopped = !stopped;
                nextframe = Clock::now();
                auto r = room;
                r["state"] = stopped ? "paused" : "playing";
                show(r);
            } else if (op == "speed") {
                auto v = j.value("value", 1.);
                if (v == .5 || v == .8 || v == 1 || v == 2) {
                    rate = v;
                    auto r = room;
                    r["speed"] = rate;
                    show(r);
                }
            } else if (op == "seek") {
                next = std::clamp<size_t>(j.value("tick", 0), 0, playback["frames"].size() - 1);
                nextframe = Clock::now();
            } else if (op == "leave") {
                reset();
                show({{"type", "left"}});
                catalog();
            }
            return;
        }
        if (op == "leave" && host) {
            reset();
            show({{"type", "left"}});
            catalog();
            return;
        }
        if (op == "disconnect") {
            reset();
            show({{"type", "left"}});
            catalog();
            return;
        }
        if (op == "predict") {
            int seat = room.value("self_seat", -1);
            if (seat < 0 || last_seat.is_null() || last_manifest.is_null() ||
                room.value("state", std::string{}) != "playing")
                return;
            auto prediction = flyback::predict_self(last_manifest, last_seat, seat, j.at("inputs"));
            prediction["type"] = "prediction";
            prediction["base_tick"] = last_seat.at("tick");
            prediction["round_epoch"] = round;
            prediction["timeline_epoch"] = timeline;
            show(prediction);
            return;
        }
        if (op == "input") {
            if (room.value("state", std::string{}) != "playing")
                return;
            if (!j.contains("seq"))
                j["seq"] = ++seq;
            j["round_epoch"] = round;
            j["timeline_epoch"] = timeline;
            auto now = Clock::now();
            while (!edge_history.empty() &&
                   (now - edge_history.front().first > 110ms || edge_history.size() > 2))
                edge_history.pop_front();
            if (j.value("act", 0) > 0)
                edge_history.emplace_back(now, j);
            if (!edge_history.empty()) {
                j["edges"] = J::array();
                for (auto &entry : edge_history)
                    j["edges"].push_back(entry.second);
            }
        }
        if ((op == "start" || op == "settings" || op == "again") && !j.contains("room_rev"))
            j["room_rev"] = room.value("room_rev", 0);
        if (op == "field_ready" && !room.value("field_ready_required", false))
            return;
        if (op == "create" || op == "join") {
            if (field_ready_enabled)
                j["field_ready"] = true;
            if (!j.contains("request_id"))
                j["request_id"] = random_token(8);
        }
        if (host)
            host->handle("local", j);
        else if (remote && link) {
            if (!link->send(remote, j, op != "input") && op != "input")
                throw Error("not_connected", "连接尚未就绪");
        } else
            throw Error("not_connected", "请先进入本机比赛或连接服务器");
    }
    bool playing() const { return !playback.is_null() || !room.empty() || (host && !host->idle()) || remote; }
    void pump() {
        if (link)
            for (int i = 0; i < 256; i++) {
                auto e = link->poll();
                if (!e)
                    break;
                auto p = "p-" + std::to_string(e->peer);
                if (e->kind == net::EventKind::discovered) {
                    auto x = e->data;
                    x["type"] = "discovered";
                    show(x);
                    continue;
                }
                if (hosting && host) {
                    if (e->kind == net::EventKind::connected) {
                        peers[p] = e->peer;
                        host->attached(p, value(e->data, "host"));
                    } else if (e->kind == net::EventKind::disconnected) {
                        host->detached(p);
                        peers.erase(p);
                    } else if (e->kind == net::EventKind::message)
                        host->handle(p, e->data);
                } else if (e->peer == remote) {
                    if (e->kind == net::EventKind::connected) {
                        auto fp = value(e->data, "fingerprint");
                        if (!fp.empty()) {
                            pins[address] = fp;
                            store();
                        }
                        pending_pin.clear();
                        link->send(remote, {{"op", "identify"}, {"name", name}}, true);
                        show({{"type", "connected"}, {"address", address}, {"fingerprint", fp}});
                    } else if (e->kind == net::EventKind::trust_required) {
                        pending_pin = value(e->data, "fingerprint");
                        show({{"type", "trust"}, {"address", address}, {"fingerprint", pending_pin}});
                    } else if (e->kind == net::EventKind::message)
                        show(e->data);
                    else if (e->kind == net::EventKind::disconnected) {
                        remote = 0;
                        auto reason = value(e->data, "reason");
                        show({{"type", "left"}, {"reason", reason}});
                        show({{"type", "error"},
                              {"code", "disconnected"},
                              {"message", "连接已断开：" + reason}});
                    }
                }
            }
        if (host)
            host->pump();
        if (!playback.is_null() && !stopped && Clock::now() >= nextframe) {
            if (next < playback["frames"].size()) {
                J v = playback["frames"][next++];
                v["type"] = "snapshot";
                show(v);
                nextframe += std::chrono::microseconds((int64_t)(50000. / rate));
            } else {
                auto r = playback.at("result");
                r["type"] = "result";
                show(r);
                stopped = true;
            }
        }
    }
};
} // namespace
int main(int argc, char **argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    try {
        Bridge b;
        auto lines = std::make_shared<Lines>();
        lines->start();
        while (true) {
            for (auto &line : lines->take())
                try {
                    b.command(J::parse(line));
                } catch (const Error &e) {
                    out({{"type", "error"}, {"code", e.code}, {"message", e.what()}});
                } catch (const std::exception &e) {
                    out({{"type", "error"}, {"code", "bridge_error"}, {"message", e.what()}});
                }
            b.pump();
            if (lines->eof)
                break;
            if (b.playing())
                wait_active(1ms);
            else
                std::this_thread::sleep_for(8ms);
        }
        return 0;
    } catch (const std::exception &e) {
        out({{"type", "error"}, {"code", "startup"}, {"message", e.what()}});
        return 1;
    }
}
