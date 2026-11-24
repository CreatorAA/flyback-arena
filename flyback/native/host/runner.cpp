#include "runner.hpp"
#include <algorithm>
namespace fbar {
Runner::Runner(J manifest, double speed, bool capture)
    : inputs(manifest.at("seats").size()), speed_(speed), capture_archive(capture) {
    worker = std::thread([this, manifest = std::move(manifest)] { run(manifest); });
}
Runner::~Runner() {
    {
        std::lock_guard l(m);
        stop_ = true;
    }
    cv.notify_all();
    if (worker.joinable())
        worker.join();
}
void Runner::emit(std::string k, J j, std::shared_ptr<Frame> f) {
    std::lock_guard l(m);
    output.push_back({std::move(k), std::move(j), std::move(f)});
}
std::deque<RoundEvent> Runner::take() {
    std::lock_guard l(m);
    std::deque<RoundEvent> q;
    output.swap(q);
    return q;
}
void Runner::neutral(int seat) {
    if (seat < 0 || seat >= int(inputs.size()))
        return;
    std::lock_guard l(m);
    auto &i = inputs[seat];
    i.held = nullptr;
    i.edges.clear();
}
void Runner::pause(bool v) {
    std::lock_guard l(m);
    paused_ = v;
    for (auto &i : inputs) {
        i.held = nullptr;
        i.edges.clear();
    }
    cv.notify_all();
}
void Runner::speed(double v) {
    std::lock_guard l(m);
    speed_ = v;
    cv.notify_all();
}
void Runner::start() {
    std::lock_guard l(m);
    started_ = true;
    cv.notify_all();
}
void Runner::input(int seat, const J &j) {
    std::lock_guard l(m);
    if (seat < 0 || seat >= int(inputs.size()) || paused_ || !started_)
        return;
    auto &i = inputs.at(seat);
    auto seq = j.value("seq", int64_t(-1));
    if (seq <= i.latest)
        return;
    i.latest = seq;
    i.at = Clock::now();
    auto canon = [](const J &v) {
        J a = J::object();
        for (auto *k : {"mx", "my", "ax", "ay"})
            a[k] = std::clamp(v.value(k, 0), -1000, 1000);
        a["act"] = std::clamp(v.value("act", 0), 0, 4);
        a["msg"] = 0;
        a["seq"] = v.value("seq", int64_t(-1));
        return a;
    };
    auto a = canon(j);
    i.held = a;
    i.held["act"] = 0;
    auto enqueue = [&](const J &v) {
        auto no = v.value("seq", int64_t(-1));
        if (v.value("act", 0) > 0 && no > i.last_edge && no <= seq && seq - no <= 8 && i.edges.size() < 4) {
            i.edges.push_back(canon(v));
            i.last_edge = no;
        }
    };
    auto extra = j.find("edges");
    if (extra != j.end() && extra->is_array() && extra->size() <= 3)
        for (auto &v : *extra)
            if (v.is_object())
                enqueue(v);
    enqueue(a);
}
void Runner::run(J request) {
    try {
        flyback::Match game(request);
        J man = game.manifest();
        man.erase("data_root");
        emit("manifest", man);
        bool was_paused = false;
        std::vector<int64_t> useq(inputs.size(), -1);
        J archive = J::array();
        std::vector<bool> bot;
        for (auto &s : request.at("seats"))
            bot.push_back(!s.value("bot", std::string{}).empty());
        double late_ms = 0;
        auto capture = [&] {
            auto f = std::make_shared<Frame>();
            f->wall = Clock::now();
            f->late_ms = late_ms;
            f->applied = useq;
            J all = game.snapshot(-1);
            f->views.push_back(all);
            for (int i = 0; i < (int)inputs.size(); i++)
                f->views.push_back(game.snapshot(i));
            if (all.value("tick", 0) % 20 == 0)
                f->stats = game.bot_stats();
            if(capture_archive)archive.push_back(std::move(all));
            emit("frame", J::object(), std::move(f));
        };
        capture();
        Time due;
        {
            std::unique_lock l(m);
            cv.wait(l, [&] { return stop_ || started_; });
            if (stop_)
                return;
            // Tick zero was presented before the countdown. Leave one complete
            // input interval from GO to the first simulated movement.
            due = Clock::now() + std::chrono::microseconds(int64_t(50000. / speed_));
        }
        for (;;) {
            J acts = J::array();
            double speed;
            {
                std::unique_lock l(m);
                if (stop_)
                    return;
                if (paused_) {
                    if (!was_paused) {
                        was_paused = true;
                        output.push_back({"paused", {}, nullptr});
                    }
                    cv.wait(l, [&] { return stop_ || !paused_; });
                    if (stop_)
                        return;
                    was_paused = false;
                    output.push_back({"playing", {}, nullptr});
                    due = Clock::now();
                }
#ifdef _WIN32
                while (!stop_ && !paused_ && Clock::now() < due) {
                    auto wait = std::chrono::duration_cast<std::chrono::microseconds>(due - Clock::now());
                    l.unlock();
                    wait_active(std::min(wait, 20000us));
                    l.lock();
                }
                if (stop_)
                    return;
                if (paused_)
                    continue;
#else
                if (cv.wait_until(l, due, [&] { return stop_ || paused_; })) {
                    if (stop_)
                        return;
                    continue;
                }
#endif
                speed = speed_;
                for (size_t k = 0; k < inputs.size(); k++) {
                    auto &i = inputs[k];
                    J a = nullptr;
                    if (!bot[k]) {
                        if (!i.edges.empty()) {
                            a = std::move(i.edges.front());
                            i.edges.pop_front();
                        } else if (Clock::now() - i.at <= 100ms && !i.held.is_null())
                            a = i.held;
                        if (!a.is_null()) {
                            i.used = std::max(i.used, a.value("seq", i.used));
                            a.erase("seq");
                        }
                        useq[k] = i.used;
                    }
                    acts.push_back(a);
                }
            }
            late_ms = std::max(0., std::chrono::duration<double, std::milli>(Clock::now() - due).count());
            game.tick(acts);
            capture();
            if (game.finished()) {
                J file = {{"format", "FBAR-REPLAY 1"},
                          {"manifest", man},
                          {"frames", std::move(archive)},
                          {"result", game.result()}};
                emit("ended", {{"record", std::move(file)}, {"dart", capture_archive?game.replay():std::string{}}});
                break;
            }
            auto step = std::chrono::microseconds((int64_t)(50000. / speed));
            due += step;
            if (due + step < Clock::now())
                due = Clock::now();
        }
    } catch (const std::exception &e) {
        emit("failed", {{"message", e.what()}});
    }
}
} // namespace fbar
