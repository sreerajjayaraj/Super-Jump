#define SUPERJUMP_TESTING
#include "../source/SuperJump.cpp"
#include <cstdio>
#include <tlhelp32.h>
static unsigned Threads(){unsigned n=0;HANDLE h=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);if(h==INVALID_HANDLE_VALUE)return 0;THREADENTRY32 e{};e.dwSize=sizeof(e);if(Thread32First(h,&e))do{if(e.th32OwnerProcessID==GetCurrentProcessId())++n;}while(Thread32Next(h,&e));CloseHandle(h);return n;}
int main(){SetPriorityClass(GetCurrentProcess(),BELOW_NORMAL_PRIORITY_CLASS);unsigned checks=0,failed=0;auto ck=[&](bool x,const char*s){++checks;if(!x){++failed;printf("FAIL %s\n",s);}};
 const auto threads=Threads();g_testHostAcceptance=-1;HMODULE own=GetModuleHandleW(nullptr);ck(!IsCrimsonDesertProcess(),"ordinary authored process fails exact basename guard");
 ck(superjump::host::IsGameName(L"CrimsonDesert.exe") &&
    superjump::host::IsGameName(L"C:\\Games\\CRIMSONDESERT.EXE"),"exact basename is case-insensitive");
 ck(!superjump::host::IsGameName(L"C:\\Games\\other-CrimsonDesert.exe") &&
    !superjump::host::IsGameName(L"CrimsonDesert.exe.bak"),"similar basenames are refused");
 ck(!superjump::host::IsGameCode(own,reinterpret_cast<uintptr_t>(&Threads),1),
    "executable fixture mapping is refused in an unrelated process");
 ck(DllMain(own,DLL_PROCESS_ATTACH,nullptr)!=FALSE,"non-game attach remains inert success");
 ck(!g_startedInGame&&!g_variantMutex&&!g_ownModule&&!g_ownPinned.load(),"no variant lease or module pin is acquired");
 ck(!g_hookSite&&!g_motionHook,"no hook/cave entry is published");
 ck(!g_log.is_open()&&g_iniPath.empty(),"no config/log path is initialized");
 ck(Threads()==threads,"no worker thread is published");
 DllMain(own,DLL_PROCESS_DETACH,nullptr);ck(!g_variantMutex&&!g_hookSite&&!g_motionHook,"non-game detach remains inert");
 printf("Source process guard checks=%u failures=%u actualBase=%p TLSsourceIncluded=1 noASIloaded=1 authoredHostOnly=1\n",checks,failed,own);return failed?1:0;
}

