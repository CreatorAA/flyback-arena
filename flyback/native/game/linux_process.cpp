#include "bot.hpp"
#include <cerrno>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <condition_variable>
#include <deque>
#include <fcntl.h>
#include <fstream>
#include <future>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <unistd.h>
namespace flyback::ai {
namespace {
namespace fs=std::filesystem;
[[noreturn]]void err(const std::string& op){throw std::runtime_error(op+": "+std::strerror(errno));}
struct FD{int v=-1;~FD(){if(v>=0)close(v);} void reset(){if(v>=0)close(v);v=-1;}};
ssize_t pipe_write(int fd,const void* buf,size_t len){
    // Keep a broken anonymous pipe local to this Bot, not a signal for the whole game host.
    sigset_t set,old,pending;sigemptyset(&set);sigaddset(&set,SIGPIPE);pthread_sigmask(SIG_BLOCK,&set,&old);
    sigpending(&pending);auto n=::write(fd,buf,len);const int e=errno;
    if(n<0 && e==EPIPE && !sigismember(&pending,SIGPIPE)){timespec z{};sigtimedwait(&set,nullptr,&z);}
    pthread_sigmask(SIG_SETMASK,&old,nullptr);errno=e;return n;
}
void put(const fs::path& file,const std::string& value){std::ofstream s(file);s<<value;s.flush();if(!s)throw std::runtime_error("需要委托可写的 cgroup v2 memory、pids、cpu: "+file.string());}
struct Group{
    fs::path dir;bool owned=false;
    ~Group(){if(owned){std::error_code ec;fs::remove(dir,ec);}}
    void create(pid_t pid){
        fs::path root;
        if(const auto* env=std::getenv("FLYBACK_CGROUP"))root=env;
        else {std::ifstream s("/proc/self/cgroup");std::string line;while(std::getline(s,line))if(line.rfind("0::",0)==0)root=fs::path("/sys/fs/cgroup")/fs::path(line.substr(3)).relative_path();}
        if(root.empty() || !fs::exists(root/"cgroup.controllers"))throw std::runtime_error("未找到已委托 cgroup v2；设置 FLYBACK_CGROUP");
        dir=root/("fbar-bot-"+std::to_string(pid));if(!fs::create_directory(dir))throw std::runtime_error("Bot cgroup 名称已占用");owned=true;
        put(dir/"memory.max","1073741824");put(dir/"memory.swap.max","0");put(dir/"pids.max","4");put(dir/"cgroup.procs",std::to_string(pid));
    }
};
class LinuxProcess final:public Process {
    Group cg;pid_t pid=-1; FD input,output,process,cpu;std::string pending;
    void stop(){if(pid>0){if(process.v>=0)syscall(SYS_pidfd_send_signal,process.v,SIGKILL,nullptr,0);while(waitpid(pid,nullptr,0)<0 && errno==EINTR){} pid=-1;}}
public:
    explicit LinuxProcess(const fs::path& exe){
        auto guard=(exe.parent_path()/"arena-bot-guard").string();
        int in[2],out[2],gate[2];if(pipe2(in,O_CLOEXEC))err("pipe2");
        FD child_in{in[0]};input.v=in[1];if(pipe2(out,O_CLOEXEC))err("pipe2");output.v=out[0];FD child_out{out[1]};
        if(pipe2(gate,O_CLOEXEC))err("pipe2"); FD child_gate{gate[0]}, start{gate[1]};
        FD nullfd{open("/dev/null",O_WRONLY|O_CLOEXEC)};if(nullfd.v<0)err("/dev/null");
        std::string exe_string=exe.string();char* args[]={guard.data(),exe_string.data(),nullptr};
        char lang[]="LANG=C";char* env[]={lang,nullptr};
        const pid_t parent=getpid();
        pid=fork();if(pid<0)err("fork");
        if(pid==0){
            if(prctl(PR_SET_PDEATHSIG,SIGKILL) || getppid()!=parent)_exit(120);
            close(start.v);char token;if(::read(child_gate.v,&token,1)!=1)_exit(121);
            if(dup2(child_in.v,0)<0 || dup2(child_out.v,1)<0 || dup2(nullfd.v,2)<0)_exit(122);
            if(syscall(SYS_close_range,3u,~0u,0))_exit(123);
            execve(guard.c_str(),args,env);_exit(124);
        }
        try{
            child_in.reset();child_out.reset();child_gate.reset();
            process.v=syscall(SYS_pidfd_open,pid,0);
            if(process.v<0)err("pidfd_open");
            cg.create(pid);cpu.v=open((cg.dir/"cpu.stat").c_str(),O_RDONLY|O_CLOEXEC);if(cpu.v<0)err("cpu.stat");
            if(pipe_write(start.v,"1",1)!=1)err("启动 Bot"); start.reset();
            if(fcntl(input.v,F_SETFL,O_NONBLOCK)<0 || fcntl(output.v,F_SETFL,O_NONBLOCK)<0)err("fcntl");
        }catch(...){start.reset();stop();throw;}
    }
    ~LinuxProcess() override{stop();}
    void write(const std::string& data) override{
        auto n=pipe_write(input.v,data.data(),data.size());
        if(n!=ssize_t(data.size()))throw std::runtime_error("Bot 输入背压或已退出");
    }
    std::optional<std::string> read() override{
        char b[2048];ssize_t got=::read(output.v,b,sizeof b);
        if(!got)throw std::runtime_error("Bot guard / binary 已退出；检查 Landlock、seccomp 与静态 Bot");
        if(got<0 && errno!=EAGAIN && errno!=EINTR)err("bot read");
        if(got>0) pending.append(b,size_t(got)); if(pending.size()>2048)throw std::runtime_error("Bot 输出超限");
        auto n=pending.find('\n');if(n==std::string::npos)return {};auto line=pending.substr(0,n);pending.erase(0,n+1);return line;
    }
    double cpu_ms() override{char text[1024];auto n=pread(cpu.v,text,sizeof(text)-1,0);if(n<=0)err("cpu.stat");text[n]=0;
        std::istringstream s(text);std::string key;long long val;
        while(s>>key>>val)if(key=="usage_usec")return val/1000.;throw std::runtime_error("无法读取 Bot cgroup cpu.stat");}
    std::uint64_t rss_bytes() const override {std::ifstream f("/proc/"+std::to_string(pid)+"/statm");std::uint64_t vm=0,rss=0;f>>vm>>rss;return rss*std::uint64_t(sysconf(_SC_PAGESIZE));}
};
// Linux's parent-death signal follows the thread that forked, not its thread group.
// A transient room/preload worker may finish while its Bot should still be playing.
// Keep one host-lifetime fork owner; the Bots still initialize and play concurrently.
class ForkOwner {
    using Job=std::packaged_task<std::unique_ptr<Process>()>;
    std::mutex m;std::condition_variable cv;std::deque<Job> jobs;
    bool stopping=false;
    std::thread worker{[this] {
        std::unique_lock lock(m);
        for(;;){
            cv.wait(lock,[this]{return stopping||!jobs.empty();});
            if(jobs.empty())return;
            auto job=std::move(jobs.front());jobs.pop_front();lock.unlock();job();lock.lock();
        }
    }};
public:
    ~ForkOwner(){{std::lock_guard lock(m);stopping=true;}cv.notify_one();worker.join();}
    std::unique_ptr<Process> spawn(const fs::path& exe){
        Job job([exe]{return std::make_unique<LinuxProcess>(exe);});auto out=job.get_future();
        {std::lock_guard lock(m);jobs.push_back(std::move(job));}
        cv.notify_one();return out.get();
    }
};
}
std::unique_ptr<Process> Process::spawn(const std::filesystem::path& exe){static ForkOwner owner;return owner.spawn(exe);}
}
