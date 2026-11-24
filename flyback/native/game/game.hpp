#pragma once
#include <memory>
#include <string>
#include <nlohmann/json.hpp>
namespace flyback {
using json=nlohmann::json;
json catalog(const std::string& data_root);
// Visual prediction on the local bridge, using only an already-authorized seat view.
// At most two 50 ms actions; the output describes own geometry, not opponent contacts,
// damage, elimination, winners or authoritative effects. Reconcile on the next snapshot.
json predict_self(const json& manifest,const json& seat_snapshot,int seat,const json& inputs);
class Match {
    struct Impl;
    std::unique_ptr<Impl> p;
public:
    explicit Match(const json& manifest);
    ~Match();
    Match(Match&&) noexcept;
    Match& operator=(Match&&) noexcept;
    void tick(const json& inputs);
    json snapshot(int seat=-1) const;
    const json& manifest() const;
    bool finished() const;
    json result() const;
    json bot_stats() const;
    std::string replay() const;
};
}
