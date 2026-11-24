#include "replay.hpp"
#include <algorithm>
#include <sstream>
namespace fbar {
// Read archived official 2/4-player broadcasts directly. Do not run or substitute bots.
J read_replay_file(const fs::path &p, const J &cat) {
    if (!fs::is_regular_file(p) || fs::file_size(p) > 64 * 1024 * 1024)
        throw Error("invalid_replay", "回放不存在或超过 64 MiB");
    auto text = load_text(p);
    if (text.find("DART-REPLAY 2") != 0 && text.find("FLYBACK-REPLAY 1") != 0) {
        auto r = J::parse(text);
        if (r.value("format", std::string{}) != "FBAR-REPLAY 1" || !r.contains("manifest") ||
            !r["frames"].is_array() || r["frames"].empty() || r["frames"].size() > 20000 ||
            !r.contains("result"))
            throw Error("invalid_replay", "不支持的回放格式");
        return r;
    }
    std::istringstream f(text);
    auto line = [&]() {
        std::string s;
        if (!std::getline(f, s))
            throw Error("truncated_replay", "回放内容不完整");
        if (!s.empty() && s.back() == '\r')
            s.pop_back();
        return s;
    };
    auto guard = [](bool condition) {
        if (!condition)
            throw Error("invalid_replay", "旧回放的字段或状态数量不正确");
    };
    line();
    int w = 0, h = 0, cell = 500, n = 0;
    J map = {{"rows", J::array()}, {"waves", J::array()}},
      m = {{"seats", J::array()}, {"ruleset", "classic20"}}, frames = J::array(), ev = J::array(),
      results = J::array();
    int winner = -2, end_tick = -1;
    while (f.peek() != std::char_traits<char>::eof()) {
        auto raw = line();
        std::istringstream q(raw);
        std::string key;
        q >> key;
        if (key == "SEED") {
            int64_t seed;
            q >> seed;
            guard(bool(q));
            m["seed"] = seed;
        } else if (key == "MAPNAME") {
            std::string id;
            q >> id;
            m["map"] = id;
            m["map_name"] = id;
            for (auto &b : cat.at("maps"))
                if (b.at("id") == id)
                    m["map_name"] = b.at("name");
        } else if (key == "MAP") {
            q >> w;
            guard(bool(q));
            if (!(q >> h))
                h = w;
            else if (!(q >> cell))
                cell = 1000;
            guard(w > 0 && h > 0 && w <= 512 && h <= 512 && cell > 0);
            map["width"] = w;
            map["height"] = h;
            map["cell_mm"] = cell;
            for (int y = 0; y < h; y++) {
                auto row = line();
                guard(row.size() == (size_t)w && row.find_first_not_of("0123") == row.npos);
                map["rows"].push_back(row);
            }
        } else if (key == "WAVE") {
            for (int y = 0; y < h; y++) {
                auto row = line();
                guard(row.size() == (size_t)w);
                map["waves"].push_back(row);
            }
        } else if (key == "WAVES") {
            int k;
            q >> k;
            guard(k >= 0 && k <= 36);
            J ticks = J::array();
            for (int i = 0; i < k; i++) {
                int t;
                q >> t;
                guard(bool(q));
                ticks.push_back(t);
            }
            map["wave_ticks"] = ticks;
        } else if (key == "PLAYERS") {
            q >> n;
            guard(n >= 2 && n <= 4);
        } else if (key == "PLAYER") {
            int i, team;
            std::string name;
            q >> i >> team >> name;
            guard(bool(q) && i == (int)m["seats"].size() && i < n);
            J s = {{"name", name}, {"bot", name}, {"model", "community"}};
            // The classic referee recorded executable stems, not canonical bot IDs.
            auto stem = name;
            if (stem.size() > 7 && stem.compare(stem.size() - 7, 7, "-player") == 0)
                stem.resize(stem.size() - 7);
            for (auto &b : cat.at("bots"))
                if (b.at("id") == name || b.at("name") == name || b.value("model", std::string{}) == name ||
                    (b.value("official", false) && b.at("id") == stem)) {
                    s["name"] = b.at("name");
                    s["model"] = b.at("model");
                    s["bot"] = b.at("id");
                    break;
                }
            m["seats"].push_back(s);
        } else if (key == "MANIFEST") {
            J full = J::parse(raw.substr(9));
            m = full;
        } else if (key == "S") {
            int t;
            q >> t;
            guard(n >= 2 && n <= 4 && t == (int)frames.size() && t < 20000);
            J players = J::array(), darts = J::array();
            char bar;
            const char *names[] = {"alive", "x",     "y",        "vx",     "vy",    "face",
                                   "st",    "timer", "ax",       "ay",     "cd",    "expose",
                                   "kills", "msg",   "vis_mask", "dead_t", "cause", "killer"};
            for (int i = 0; i < n; i++) {
                q >> bar;
                guard(bar == '|');
                J v = {{"seat", i}, {"vis", true}};
                for (auto k : names) {
                    int64_t x;
                    q >> x;
                    guard(bool(q));
                    v[k] = x;
                }
                v["alive"] = v["alive"] != 0;
                if (t == 0) {
                    m["seats"].at(i)["x"] = v.at("x");
                    m["seats"].at(i)["y"] = v.at("y");
                }
                players.push_back(v);
            }
            for (int i = 0; i < n; i++) {
                q >> bar;
                guard(bar == '|');
                J v = {{"seat", i}};
                for (auto k : {"st", "x", "y", "vx", "vy", "left", "age"}) {
                    int64_t x;
                    q >> x;
                    guard(bool(q));
                    v[k] = x;
                }
                v["vis"] = v.at("st") != 5;
                darts.push_back(v);
            }
            int fallen, warn;
            q >> bar >> fallen >> warn;
            guard(bool(q) && bar == '|');
            frames.push_back({{"tick", t},
                              {"players", std::move(players)},
                              {"darts", std::move(darts)},
                              {"events", std::move(ev)},
                              {"fallen", fallen},
                              {"warn", warn},
                              {"finished", false}});
            ev = J::array();
        } else if (key == "E") {
            int tick, x, y, z, v;
            std::string kind;
            q >> tick >> kind >> x >> y >> z >> v;
            guard(bool(q));
            ev.push_back({{"tick", tick},
                          {"ordinal", ev.size()},
                          {"type", kind},
                          {"a", x},
                          {"b", y},
                          {"c", z},
                          {"d", v}});
        } else if (key == "END") {
            q >> end_tick >> winner;
            guard(bool(q));
        } else if (key == "RESULT") {
            int i, rank, dead, cause, killer, kills;
            q >> i >> rank >> dead >> cause >> killer >> kills;
            guard(bool(q) && i >= 0 && i < n);
            results.push_back({{"seat", i},
                               {"rank", rank},
                               {"death_tick", dead},
                               {"killer", killer},
                               {"cause", cause},
                               {"alive", dead < 0},
                               {"kills", kills},
                               {"points", nullptr}});
        }
    }
    guard(!frames.empty() && end_tick == (int)frames.size() - 1 && results.size() == (size_t)n &&
          m["seats"].size() == (size_t)n && map["rows"].size() == (size_t)h);
    frames.back()["finished"] = true;
    m["terrain"] = map;
    if (n > 2)
        for (auto &r : results) {
            int rank = r.at("rank");
            int tied =
                std::count_if(results.begin(), results.end(), [&](auto &v) { return v.at("rank") == rank; });
            double placement = n - rank - (tied - 1) * .5;
            r["placement_points"] = placement;
            r["points"] = placement + r.at("kills").get<int>();
        }
    return {{"format", "FBAR-REPLAY 1"},
            {"manifest", m},
            {"frames", std::move(frames)},
            {"result", {{"winner", winner}, {"tick", end_tick}, {"standings", results}}}};
}
} // namespace fbar
