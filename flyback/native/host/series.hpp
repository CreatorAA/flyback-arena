#pragma once
#include "../common/util.hpp"
#include <algorithm>
#include <map>
#include <random>
namespace fbar {
// The cursor counts completed matches only. Loading, pausing and disconnects cannot spend one.
class Series {
    struct Stage {
        std::string map;
        int map_index, round, rounds;
    };
    std::vector<Stage> schedule;
    std::map<std::string, J> scores;
    std::string id;
    size_t completed = 0, shown = 0;
    int map_count = 0;

  public:
    void clear() {
        schedule.clear();
        scores.clear();
        completed = shown = 0;
        map_count = 0;
        id.clear();
    }
    bool pending() const { return completed < schedule.size(); }
    bool empty() const { return schedule.empty(); }
    void begin(const J &settings, std::mt19937 &rng, const std::string &previous,
               const std::string &preferred = {}) {
        clear();
        id = "series-" + random_token(5);
        auto maps = settings.at("maps").get<std::vector<std::string>>();
        auto policy = settings.at("map_policy");
        if (policy == "fixed")
            maps.resize(1);
        else if (policy == "random") {
            std::shuffle(maps.begin(), maps.end(), rng);
            if (maps.size() > 1 && maps.front() == previous)
                std::swap(maps.front(), maps.back());
        }
        if (!preferred.empty()) {
            auto at = std::find(maps.begin(), maps.end(), preferred);
            if (at != maps.end())
                std::iter_swap(maps.begin(), at);
            else
                maps.front() = preferred;
        }
        map_count = int(maps.size());
        int i = 0;
        for (auto &map : maps) {
            ++i;
            int n = settings.at("map_rounds").value(map, settings.value("rounds_per_map", 1));
            for (int r = 1; r <= n; r++)
                schedule.push_back({map, i, r, n});
        }
    }
    std::string current_map() {
        shown = completed;
        return schedule.at(shown).map;
    }
    size_t next_stage() const {
        if (schedule.empty())
            return 0;
        size_t i = shown + 1;
        while (i < schedule.size() && schedule[i].map_index == schedule[shown].map_index)
            ++i;
        return i;
    }
    std::string upcoming(const std::string &fallback) const {
        auto i = next_stage();
        return i < schedule.size() ? schedule[i].map : fallback;
    }
    bool set_next(const std::string &key, const J &settings) {
        auto start = next_stage();
        if (start >= schedule.size())
            return false;
        std::vector<std::string> maps;
        for (size_t i = start; i < schedule.size(); i++)
            if (schedule[i].round == 1)
                maps.push_back(schedule[i].map);
        auto it = std::find(maps.begin(), maps.end(), key);
        if (it == maps.end())
            maps.front() = key;
        else
            std::iter_swap(maps.begin(), it);
        int index = schedule[start].map_index;
        schedule.resize(start);
        for (auto &m : maps) {
            int n = settings.at("map_rounds").value(m, settings.at("rounds_per_map").get<int>());
            for (int round = 1; round <= n; round++)
                schedule.push_back({m, index, round, n});
            ++index;
        }
        return true;
    }
    void complete(const J &result, const J &seats, const std::vector<std::string> &ids) {
        if (!pending())
            return;
        for (auto &r : result.at("standings")) {
            auto i = r.at("seat").get<size_t>();
            auto &s = scores[ids.at(i)];
            if (!s.is_object())
                s = {{"seat", i},    {"name", seats.at(i).at("name")},
                     {"points", 0.}, {"kills", 0},
                     {"wins", 0},    {"rounds", 0}};
            double p;
            bool win = false;
            if (seats.size() == 2) {
                int winner = result.value("winner", -1);
                win = winner == (int)i;
                p = winner < 0 ? .5 : win ? 1. : 0.;
            } else {
                p = r.at("points").get<double>();
                win = r.value("rank", 0) == 1;
            }
            s["points"] = s["points"].get<double>() + p;
            s["kills"] = s["kills"].get<int>() + r.value("kills", 0);
            s["wins"] = s["wins"].get<int>() + (win ? 1 : 0);
            s["rounds"] = s["rounds"].get<int>() + 1;
        }
        ++completed;
    }
    J status(bool with_scores = false) const {
        if (schedule.empty())
            return nullptr;
        const auto &s = schedule.at(shown);
        J v = {{"id", id},
               {"map_id", s.map},
               {"map_index", s.map_index},
               {"map_count", map_count},
               {"map_round", s.round},
               {"map_rounds", s.rounds},
               {"completed", completed},
               {"total", schedule.size()},
               {"finished", !pending()}};
        if (with_scores) {
            J rows = J::array();
            for (auto &[_, s] : scores)
                rows.push_back(s);
            std::sort(rows.begin(), rows.end(),
                      [](const J &a, const J &b) { return a.at("points") > b.at("points"); });
            int place = 0, rank = 0;
            double prev = -1.;
            for (auto &r : rows) {
                ++place;
                double score = r.at("points");
                if (score != prev)
                    rank = place;
                r["rank"] = rank;
                prev = score;
            }
            v["standings"] = rows;
        }
        return v;
    }
};
} // namespace fbar
