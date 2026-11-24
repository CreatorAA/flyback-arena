#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <userenv.h>
#include <psapi.h>
#include "bot.hpp"
#include <intrin.h>
#include <thread>
#include <stdexcept>
namespace flyback::ai {
namespace {
namespace fs=std::filesystem;
[[noreturn]] void fail(const char* where){throw std::runtime_error(std::string(where)+" (Win32 "+std::to_string(GetLastError())+")");}
void ensure(BOOL r,const char* where){if(!r)fail(where);}
std::string win_err(DWORD e){return std::to_string(e);}
struct Handle {
    HANDLE h=nullptr;
    Handle()=default;explicit Handle(HANDLE v):h(v){} Handle(const Handle&)=delete;
    ~Handle(){if(h && h!=INVALID_HANDLE_VALUE) CloseHandle(h);}
};
struct Profile {
    std::wstring name,dir,exe;
    PSID sid=nullptr; Handle lease;
    ~Profile(){if(!exe.empty())DeleteFileW(exe.c_str());if(!dir.empty())RemoveDirectoryW(dir.c_str());if(sid)FreeSid(sid);}
    std::string prepare(const fs::path& src_exe) {
        for(int i=0;i<4096;++i){
            name=L"Flyback.Arena.Bot.v1."+std::to_wstring(i);
            auto h=CreateEventW(nullptr,TRUE,FALSE,(L"Global\\"+name).c_str());
            if(!h)fail("CreateEvent");
            if(GetLastError()!=ERROR_ALREADY_EXISTS){lease.h=h;break;}CloseHandle(h);
        }
        if(!lease.h)return "Bot identity pool full";
        HRESULT hr=CreateAppContainerProfile(name.c_str(),name.c_str(),L"Flyback arena player",nullptr,0,&sid);
        if(hr==HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS))hr=DeriveAppContainerSidFromAppContainerName(name.c_str(),&sid);
        if(FAILED(hr))return "AppContainer unavailable: "+std::to_string(hr);
        const wchar_t* local=_wgetenv(L"LOCALAPPDATA");if(!local)return "LOCALAPPDATA unavailable";
        const auto root=fs::path(local)/L"FlybackArena"/L"bot-boxes";fs::create_directories(root);
        dir=(root/name).wstring(); // reclaimed only while exclusively holding this identity's lease
        DeleteFileW((fs::path(dir)/L"bot.exe").c_str());RemoveDirectoryW(dir.c_str());
        // AppContainer ACLs and minimal environment follow the upstream competition sandbox.
        {
            // AppContainer 自己的存储目录（%LOCALAPPDATA%\Packages\<名字>\，TEMP 也在里面）默认可写：
            // 撤掉容器在 AC\ 上的显式权限（实测只加拒绝 ACE 挡不住写入，原因没深究），
            // 根目录再加一条继承的拒绝写入。这样选手完全不能写盘（防止写满磁盘、跨局留东西）。见 测试\test_arena.py write_temp
            wchar_t la[MAX_PATH];
            const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH);
            if (n == 0 || n >= MAX_PATH) return "取不到 LOCALAPPDATA";
            const std::wstring pk0 = std::wstring(la) + L"\\Packages\\" + name;
            for (const std::wstring& pk_c : {pk0, pk0 + L"\\AC"}) {
            std::wstring pk = pk_c;
            EXPLICIT_ACCESSW deny{};
            deny.grfAccessPermissions = FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES |
                                        FILE_DELETE_CHILD | DELETE | WRITE_DAC | WRITE_OWNER;
            // 根目录：加继承的拒绝写入；AC\：撤掉系统给容器的显式"完全控制"（它排在继承来的拒绝之前）
            deny.grfAccessMode = pk_c == pk0 ? DENY_ACCESS : REVOKE_ACCESS;
            deny.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
            deny.Trustee.TrusteeForm = TRUSTEE_IS_SID;
            deny.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
            deny.Trustee.ptstrName = (LPWSTR)sid;
            PACL oacl = nullptr, nacl = nullptr;
            PSECURITY_DESCRIPTOR psd = nullptr;
            DWORD e = GetNamedSecurityInfoW(pk.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &oacl,
                                            nullptr, &psd);
            PACL base = nullptr;
            if (e == ERROR_SUCCESS && pk_c == pk0) {
                deny.grfAccessMode = REVOKE_ACCESS;
                e = SetEntriesInAclW(1, &deny, oacl, &base);
                deny.grfAccessMode = DENY_ACCESS;
            }
            if (e == ERROR_SUCCESS) e = SetEntriesInAclW(1, &deny, base ? base : oacl, &nacl);
            if (base) LocalFree(base);
            if (e == ERROR_SUCCESS)
                e = SetNamedSecurityInfoW(&pk[0], SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, nacl, nullptr);
            if (nacl) LocalFree(nacl);
            if (psd) LocalFree(psd);
            if (e != ERROR_SUCCESS) return "封住沙箱存储目录的写权限失败（错误 " + win_err(e) + "）";
            }
        }
        if (!CreateDirectoryW(dir.c_str(), nullptr)) return "建沙箱目录失败（错误 " + win_err(GetLastError()) + "）";
        PSID owner = nullptr; PSECURITY_DESCRIPTOR oldsd = nullptr, priv = nullptr;
        DWORD e = GetNamedSecurityInfoW(dir.c_str(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
                                        &owner, nullptr, nullptr, nullptr, &oldsd);
        LPWSTR ownstr = nullptr, sidstr = nullptr;
        if (e == ERROR_SUCCESS && (!ConvertSidToStringSidW(owner, &ownstr) || !ConvertSidToStringSidW(sid, &sidstr))) e = GetLastError();
        if (e == ERROR_SUCCESS) {
            const std::wstring acl = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;" + std::wstring(ownstr) + L")(A;OICI;GRGX;;;" + sidstr + L")";
            if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(), SDDL_REVISION_1, &priv, nullptr)) e = GetLastError();
            else if (!SetFileSecurityW(dir.c_str(), DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, priv)) e = GetLastError();
        }
        if (ownstr) LocalFree(ownstr); if (sidstr) LocalFree(sidstr);
        if (priv) LocalFree(priv); if (oldsd) LocalFree(oldsd);
        if (e != ERROR_SUCCESS) return "设置沙箱专用目录权限失败（错误 " + win_err(e) + "）";
        exe = dir + L"\\bot.exe";
        if (!CopyFileW(src_exe.c_str(), exe.c_str(), TRUE)) {
            const DWORD ce = GetLastError();
            return "复制程序失败（错误 " + win_err(ce) + "）";
        }
        return {};
    }
};
std::vector<wchar_t> minimal_env() {
    std::vector<std::wstring> kv;
    // TEMP、TMP、LOCALAPPDATA 等由系统改写到这个容器自己的目录（缺了它们 CreateProcess 会报 203）
    for (const wchar_t* k : {L"SystemRoot", L"windir", L"SystemDrive", L"NUMBER_OF_PROCESSORS", L"PROCESSOR_ARCHITECTURE",
                             L"USERPROFILE", L"LOCALAPPDATA", L"APPDATA", L"TEMP", L"TMP"}) {
        wchar_t v[1024];
        const DWORD n = GetEnvironmentVariableW(k, v, 1024);
        if (n > 0 && n < 1024) kv.push_back(std::wstring(k) + L"=" + v);
    }
    wchar_t sr[512];
    const DWORD n = GetEnvironmentVariableW(L"SystemRoot", sr, 512);
    const std::wstring root = n > 0 && n < 512 ? sr : L"C:\\Windows";
    kv.push_back(L"PATH=" + root + L"\\system32;" + root);
    std::sort(kv.begin(), kv.end(), [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    std::vector<wchar_t> blk;
    for (auto& s : kv) {
        blk.insert(blk.end(), s.begin(), s.end());
        blk.push_back(L'\0');
    }
    blk.push_back(L'\0');
    return blk;
}
double cycles_per_ms(){
    static const double cached=[] {
        auto t=Clock::now();auto c=__rdtsc();
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        return (__rdtsc()-c)/std::chrono::duration<double,std::milli>(Clock::now()-t).count();
    }();
    return cached;
}
class WinProcess final:public Process {
    Profile profile; Handle job,pid,input,output; std::string pending;
public:
    explicit WinProcess(const fs::path& path) {
        const auto err=profile.prepare(path);if(!err.empty())throw std::runtime_error(err);
        job.h=CreateJobObjectW(nullptr,nullptr);if(!job.h)fail("CreateJobObject");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION lim{};
        lim.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_ACTIVE_PROCESS|JOB_OBJECT_LIMIT_JOB_MEMORY|JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
        lim.BasicLimitInformation.ActiveProcessLimit=1;lim.JobMemoryLimit=SIZE_T(1)<<30;
        ensure(SetInformationJobObject(job.h,JobObjectExtendedLimitInformation,&lim,sizeof(lim)),"SetJobLimits");
        JOBOBJECT_BASIC_UI_RESTRICTIONS ui{};ui.UIRestrictionsClass=0xff;
        ensure(SetInformationJobObject(job.h,JobObjectBasicUIRestrictions,&ui,sizeof ui),"SetJobUI");
        SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
        Handle child_in,child_out;Handle child_err(CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr));
        if(child_err.h==INVALID_HANDLE_VALUE)fail("NUL");
        ensure(CreatePipe(&child_in.h,&input.h,&sa,65536),"stdin");
        ensure(CreatePipe(&output.h,&child_out.h,&sa,65536),"stdout");
        ensure(SetHandleInformation(input.h,HANDLE_FLAG_INHERIT,0),"private input pipe");
        ensure(SetHandleInformation(output.h,HANDLE_FLAG_INHERIT,0),"private output pipe");
        DWORD mode=PIPE_NOWAIT; ensure(SetNamedPipeHandleState(input.h,&mode,nullptr,nullptr),"nonblocking write pipe");
        HANDLE inherited[]={child_in.h,child_out.h,child_err.h};SECURITY_CAPABILITIES caps{};caps.AppContainerSid=profile.sid;
        SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,3,0,&bytes);
        std::vector<char> mem(bytes);auto attrs=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(mem.data());
        ensure(InitializeProcThreadAttributeList(attrs,3,0,&bytes),"initialize spawn attributes");
        struct Guard{LPPROC_THREAD_ATTRIBUTE_LIST a;~Guard(){DeleteProcThreadAttributeList(a);}} g{attrs};
        ensure(UpdateProcThreadAttribute(attrs,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr),"inherit pipe list");
        ensure(UpdateProcThreadAttribute(attrs,0,PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,&caps,sizeof caps,nullptr,nullptr),"AppContainer capabilities");
        ensure(UpdateProcThreadAttribute(attrs,0,PROC_THREAD_ATTRIBUTE_JOB_LIST,&job.h,sizeof job.h,nullptr,nullptr),"atomic job membership");
        STARTUPINFOEXW si{};si.StartupInfo.cb=sizeof si;si.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
        si.StartupInfo.hStdInput=child_in.h;si.StartupInfo.hStdOutput=child_out.h;si.StartupInfo.hStdError=child_err.h;si.lpAttributeList=attrs;
        PROCESS_INFORMATION pi{};std::wstring cmd=L"\""+profile.exe+L"\"";auto env=minimal_env();
        ensure(CreateProcessW(profile.exe.c_str(),cmd.data(),nullptr,nullptr,TRUE,EXTENDED_STARTUPINFO_PRESENT|CREATE_UNICODE_ENVIRONMENT|CREATE_NO_WINDOW,env.data(),profile.dir.c_str(),&si.StartupInfo,&pi),"CreateProcess(AppContainer)");
        pid.h=pi.hProcess;CloseHandle(pi.hThread);cycles_per_ms();
    }
    ~WinProcess() override{if(job.h && pid.h){TerminateJobObject(job.h,0);WaitForSingleObject(pid.h,500);}}
    void write(const std::string& s) override {DWORD n=0;ensure(WriteFile(input.h,s.data(),DWORD(s.size()),&n,nullptr),"bot input");if(n!=s.size())throw std::runtime_error("bot input backpressure");}
    std::optional<std::string> read() override{
        DWORD n=0;ensure(PeekNamedPipe(output.h,nullptr,0,nullptr,&n,nullptr),"bot pipe ended");
        if(n){if(n+pending.size()>2048)throw std::runtime_error("bot output overflow");char b[2048];DWORD got=0;ensure(ReadFile(output.h,b,n,&got,nullptr),"bot output");pending.append(b,got);}
        auto nl=pending.find('\n');if(nl==std::string::npos){if(WaitForSingleObject(pid.h,0)==WAIT_OBJECT_0)throw std::runtime_error("bot exited");return {};}
        std::string result=pending.substr(0,nl);pending.erase(0,nl+1);return result;
    }
    double cpu_ms() override{ULONG64 count=0;ensure(QueryProcessCycleTime(pid.h,&count),"bot cpu sample");return count/cycles_per_ms();}
    std::uint64_t rss_bytes() const override{PROCESS_MEMORY_COUNTERS mem{};mem.cb=sizeof mem;return GetProcessMemoryInfo(pid.h,&mem,sizeof mem)?mem.WorkingSetSize:0;}
};
}
std::unique_ptr<Process> Process::spawn(const fs::path& binary){return std::make_unique<WinProcess>(binary);}
}
