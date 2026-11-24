#pragma once
#include "../common/util.hpp"
#include "../game/game.hpp"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
namespace fbar {
struct Frame {
    Time wall;
    double late_ms = 0;
    std::vector<J> views;
    std::vector<int64_t> applied;
    J stats;
};
struct RoundEvent {
    std::string kind;
    J data;
    std::shared_ptr<Frame> frame;
};
class Runner {
    std::mutex m;
    std::condition_variable cv;
    std::thread worker;
    std::deque<RoundEvent> output;
    struct Input {
        J held;
        std::deque<J> edges;
        int64_t latest = -1, used = -1, last_edge = -1;
        Time at{};
    };
    std::vector<Input> inputs;
    bool stop_ = false, paused_ = false, started_ = false;
    double speed_ = 1;
    bool capture_archive;
    void run(J);
    void emit(std::string, J = J::object(), std::shared_ptr<Frame> frame = {});

  public:
    Runner(J manifest, double speed, bool capture);
    ~Runner();
    void start();
    void pause(bool);
    void speed(double);
    void input(int, const J &);
    void neutral(int);
    std::deque<RoundEvent> take();
};
} // namespace fbar
