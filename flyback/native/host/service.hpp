#pragma once
#include "../admin/web.hpp"
#include "../net/net.hpp"
#include "room.hpp"
#include <future>
namespace fbar {
class Service {
    struct Client {
        std::string name, room, address;
        std::map<std::string, J> requests;
        Time auth_since{}, rate_since{}, sample{};
        int attempts = 0, calls = 0;
        J transport;
        double kbps = 0;
    };
    Config cfg;
    std::string mode;
    Send output;
    J roster, logs = J::array(), audit = J::array(), replays = J::array();
    std::set<std::string> disabled_maps, disabled_bots;
    std::map<std::string, Client> clients;
    std::map<std::string, std::unique_ptr<Room>> rooms;
    struct Pending {
        std::string peer;
        J request;
        std::future<std::string> task;
    };
    std::vector<Pending> pending;
    std::map<std::string, std::deque<Time>> auth_attempts;
    std::map<std::string, Time> release_after;
    bool draining = false;
    int config_rev = 1, port_ = 0, budget = 0, pressure = 0, over = 0, under = 0;
    double cpu_prior = 0, cpu_percent = -1;
    Time last_resource{};
    std::string fingerprint_;
    Time started = Clock::now(), next_admin{}, last_dir{}, next_clean{};
    bool dir_dirty = true;
    int64_t log_id = 0, audit_id = 0;
    void fail(const std::string &, std::string, std::string);
    void complete(const std::string &, const J &, const std::string &);
    void leave(const std::string &);
    struct Save { J record,meta; std::string native; };
    std::deque<Save> saves;
    std::future<J> saving;
    std::string saving_room;
    void archive(J&&,std::string&&,const J&);
    void save_progress(bool drain);
    void save_error(const std::string&,const std::string&);
    void prune();
    void policy(bool write);
    void command(const J &);

  public:
    std::unique_ptr<Admin> web;
    Service(Config, std::string, Send);
    void network_info(int, std::string);
    void start_admin();
    void attached(const std::string &, std::string address = {});
    void detached(const std::string &);
    void handle(const std::string &, const J &);
    void pump();
    J directory() const;
    J state() const;
    J catalog() const;
    void log(std::string, std::string, std::string = {});
    void shutdown();
    bool idle() const;
    void drain(bool v);
    void net_stats(const std::string &, J);
};
} // namespace fbar
