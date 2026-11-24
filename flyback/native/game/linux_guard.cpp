// Executed before the static Bot: no code supplied by the player runs outside this boundary.
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/landlock.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#define ALLOW(n) BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,__NR_##n,0,1),BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW)
int main(int argc,char**argv){
    if(argc!=2)return 2;
    int bot=open(argv[1],O_PATH|O_CLOEXEC);if(bot<0)return 3;
    if(bot!=3){if(dup3(bot,3,O_CLOEXEC)<0)return 4;close(bot);bot=3;}
    rlimit stack{64u<<20,64u<<20},mem{1ull<<30,1ull<<30},core{},files{16,16};
    if(setrlimit(RLIMIT_STACK,&stack)||setrlimit(RLIMIT_AS,&mem)||setrlimit(RLIMIT_CORE,&core)||setrlimit(RLIMIT_NOFILE,&files))return 5;
    if(prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0))return 6;
    long abi=syscall(SYS_landlock_create_ruleset,nullptr,0,LANDLOCK_CREATE_RULESET_VERSION);
    if(abi<1)return 7;
    landlock_ruleset_attr rs{};
    rs.handled_access_fs=(1ull<<13)-1; // ABI 1: execute/read/write/remove/create.
    if(abi>=2)rs.handled_access_fs|=1ull<<13; // REFER
    if(abi>=3)rs.handled_access_fs|=1ull<<14; // TRUNCATE
    int rule=syscall(SYS_landlock_create_ruleset,&rs,sizeof(rs),0);if(rule<0)return 8;
    landlock_path_beneath_attr path{};path.parent_fd=bot;path.allowed_access=LANDLOCK_ACCESS_FS_EXECUTE|LANDLOCK_ACCESS_FS_READ_FILE;
    if(syscall(SYS_landlock_add_rule,rule,LANDLOCK_RULE_PATH_BENEATH,&path,0)||syscall(SYS_landlock_restrict_self,rule,0))return 9;
    close(rule);
    // With no open/socket/dup/clone allowed, the executable descriptor is destroyed by the one successful execveat.
    sock_filter filter[]={
        BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(seccomp_data,arch)),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,AUDIT_ARCH_X86_64,1,0),BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_KILL_PROCESS),
        BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(seccomp_data,nr)),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,__NR_execveat,0,6),
        BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(seccomp_data,args[0])),BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,3,0,3),
        BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(seccomp_data,args[4])),BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,AT_EMPTY_PATH,0,1),
        BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW),BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM),
        ALLOW(read),ALLOW(write),ALLOW(writev),ALLOW(readv),ALLOW(close),ALLOW(fstat),ALLOW(lseek),ALLOW(brk),
        ALLOW(mmap),ALLOW(mprotect),ALLOW(munmap),ALLOW(mremap),ALLOW(madvise),ALLOW(arch_prctl),
        ALLOW(set_tid_address),ALLOW(set_robust_list),ALLOW(rseq),ALLOW(futex),ALLOW(clock_gettime),ALLOW(gettimeofday),
        ALLOW(rt_sigaction),ALLOW(rt_sigprocmask),ALLOW(rt_sigreturn),ALLOW(getrandom),ALLOW(getpid),ALLOW(gettid),
        ALLOW(getrlimit),ALLOW(exit_group),ALLOW(exit),
        BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM)};
    sock_fprog program{static_cast<unsigned short>(sizeof(filter)/sizeof(filter[0])),filter};
    if(prctl(PR_SET_SECCOMP,SECCOMP_MODE_FILTER,&program))return 10;
    char bot_name[]="player";char lang[]="LANG=C";char* args[]={bot_name,nullptr};char* env[]={lang,nullptr};
    syscall(SYS_execveat,bot,"",args,env,AT_EMPTY_PATH);return 11;
}
