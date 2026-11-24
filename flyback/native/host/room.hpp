#pragma once
#include "runner.hpp"
#include "series.hpp"
#include <functional>
#include <map>
#include <random>
#include <set>
namespace fbar {
using Send = std::function<void(const std::string &, J, bool)>;
struct Member {
    std::string id, name;
    int seat = -1, view = -1;
    bool ready = false, participant = false, subscribed = false;
    bool field_ready_capable = false, field_ready = false;
    int64_t last_view_tick = -1;
};
class Room {
    Send send;
    std::unique_ptr<Runner> runner;
    std::mt19937 rng, map_rng, bot_rng;
    Series series;
    std::vector<std::string> contestant_ids;
    bool continue_series = false;
    std::string last_map, queued_map;
    J manifest_, final_, stats_;
    std::deque<std::shared_ptr<Frame>> hist;
    std::deque<std::pair<Time, double>> pacing;
    struct Ballot {
        bool active = false;
        std::string proposer;
        Time end;
        std::map<std::string, bool> cast;
    };
    Ballot ballot;
    bool time_pending = false, had_humans = false;
    int countdown_seconds = 0;
    Time field_ready_deadline{}, countdown_end{};
    Time ended_at{}, last_cast{}, last_spectate{};
    struct Clip {
        Time release;
        J frames;
        int death_tick, cause, killer;
    };
    std::map<int, Clip> clips;
    std::vector<std::string> electorate() const;
    void change_clock(bool);
    void clear_inputs();
    void deliver_view(Member &, const Frame &, const std::vector<J> &);
    void egress(const std::shared_ptr<Frame> &);
    void manifest_to(const std::string &);
    void send_room(const std::string &);
    void check_ballot();
    void reset_ballot();
    void room_error(std::string, std::string);

  public:
    std::string id, mode, host, title, password, advertise;
    int port = 0, spec_limit = -1, spec_hz = 10;
    int pressure = 0;bool archive_enabled=true;
    int effective_hz() const { return pressure ? std::min(spec_hz, 5) : spec_hz; }
    int rev = 1, policy_rev = 1, round = 0, timeline = 0;
    double rate = 1;
    std::string state = "ready";
    Time empty_since = Clock::now();
    J settings;
    std::map<std::string, Member> members;
    Room(std::string id, std::string mode, Send);
    bool active() const;
    bool holds_slot() const { return active() || continue_series; }
    bool series_due(Time now) const { return continue_series && state == "finished" && now - ended_at >= 4s; }
    void advance_series(Time, const J &, const fs::path &, bool);
    int spectators() const;
    int humans() const;
    int alive_members() const;
    bool spec_full() const;
    void configure(const J &, const J &catalog, bool clear);
    void join(std::string, std::string, bool, bool field_ready_capable = false);
    void leave(const std::string &);
    void op(const std::string &, const J &, const J &catalog, const fs::path &assets, bool may_start);
    void update(Time now, const std::function<void(J&&,std::string&&,const J&)> &archive);
    J summary() const;
    J admin() const;
    const J &stats() const { return stats_; }
    void broadcast();
    void close();
    void next_map(const std::string &, const J &catalog);
};
} // namespace fbar
