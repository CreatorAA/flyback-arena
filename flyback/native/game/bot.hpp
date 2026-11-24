#pragma once
#include "dart.hpp"
#include <filesystem>
#include <chrono>
#include <optional>
namespace flyback::ai {
using Clock=std::chrono::steady_clock;
// One sandbox, anonymous private pipes and at most one untagged DART request.
class Process {
public:
    virtual ~Process()=default;
    virtual void write(const std::string&)=0;
    virtual std::optional<std::string> read()=0; // nonblocking; throws on EOF or excess output
    virtual double cpu_ms()=0;
    virtual std::uint64_t rss_bytes() const=0;
    static std::unique_ptr<Process> spawn(const std::filesystem::path& binary);
};
struct Bot {
    explicit Bot(const std::filesystem::path&,const dart::GameInfo&);
    void ask(const dart::Obs&);
    bool collect(dart::i64 tick,dart::Action& action);
    void fail(const char* code,std::string reason);
    std::unique_ptr<Process> proc;
    dart::Config cfg;
    std::optional<dart::i64> requested;
    Clock::time_point deadline;
    bool timed_out=false, retired=false;
    int faults=0,samples=0,missed=0;
    double first_ms=0,total_ms=0,max_ms=0,base_cpu=0;
    std::string last_error,last_code;
};
}
