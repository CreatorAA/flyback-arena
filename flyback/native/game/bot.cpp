#include "bot.hpp"
#include <stdexcept>
namespace flyback::ai {
Bot::Bot(const std::filesystem::path& exe,const dart::GameInfo& gi):proc(Process::spawn(exe)),cfg(gi.cfg) {
    proc->write(dart::protocol_header(gi));
}
void Bot::fail(const char* code,std::string why) {
    ++faults; last_code=code;last_error=std::move(why);
    if(faults>=20) {retired=true;proc.reset();requested.reset();}
}
void Bot::ask(const dart::Obs& obs) {
    if(retired) return;
    try {
        if(requested) {
            if(!timed_out) return;
            if(!proc->read()) {
                if(++missed>=20) { retired=true; last_code="unresponsive";last_error="选手持续未回复";proc.reset(); }
                return;
            }
            requested.reset(); // Discard the late untagged line, never credit it to this tick.
        }
        if(proc->read()) throw std::runtime_error("选手主动输出了多余指令");
        base_cpu=proc->cpu_ms(); deadline=Clock::now()+std::chrono::milliseconds(obs.t==0?5000:30);
        proc->write(dart::protocol_step(obs)); requested=obs.t;timed_out=false;missed=0;
    } catch(const std::exception& e) {retired=true;last_code="io";last_error=e.what();++faults;proc.reset();}
}
bool Bot::collect(dart::i64 tick,dart::Action& out) {
    if(retired || requested!=tick || timed_out) return true;
    try {
        auto line=proc->read();
        if(Clock::now()>deadline) {
            timed_out=true;if(line)requested.reset();
            fail("wall_deadline","选手超过墙钟截止");return true;
        }
        if(!line) return false;
        requested.reset();
        const auto used=std::max(0.,proc->cpu_ms()-base_cpu);
        if(tick==0)first_ms=used;else {++samples;total_ms+=used;max_ms=std::max(max_ms,used);}
        if(used>(tick==0?1000:10)) {fail("cpu_budget","选手超出 CPU 预算");return true;}
        const auto act=dart::parse_action(*line,cfg);
        if(!act.ok){fail("invalid_action","选手输出不合法");return true;}
        out=act;
    } catch(const std::exception& e) {retired=true;last_code="io";last_error=e.what();++faults;proc.reset();}
    return true;
}
}
