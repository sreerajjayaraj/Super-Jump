#include <Windows.h>
static bool fixtureRecorderPresent=false;
static auto fixtureRealModuleLookup=&GetModuleHandleW;
static HMODULE WINAPI FixtureModuleLookup(LPCWSTR name){if(name&&wcscmp(name,L"SJNativeMotionTrace.asi")==0&&fixtureRecorderPresent)return reinterpret_cast<HMODULE>(uintptr_t(1));return fixtureRealModuleLookup(name);}
static FARPROC WINAPI FixtureProcedureLookup(HMODULE,const char*);
#define GetProcAddress FixtureProcedureLookup
#define GetModuleHandleW FixtureModuleLookup
#define SUPERJUMP_TESTING
#define PALM_COLLECTOR_TESTING
#include "../source/SuperJump.cpp"
#undef GetModuleHandleW
#undef GetProcAddress
static unsigned keyboardReads=0,controllerReads=0;
static bool loseFocusDuringPoll=false;
static SHORT WINAPI FixtureKeyboard(int){++keyboardReads;return static_cast<SHORT>(0x8000);}
static DWORD WINAPI FixtureController(DWORD slot,XINPUT_STATE* state){
 ++controllerReads;
 if(loseFocusDuringPoll)g_testForeground=0;
 if(slot) return ERROR_DEVICE_NOT_CONNECTED;
 state->Gamepad.wButtons=XINPUT_GAMEPAD_A;return ERROR_SUCCESS;
}
static FARPROC WINAPI FixtureProcedureLookup(HMODULE module,const char* name){
 if(strcmp(name,"GetAsyncKeyState")==0)return reinterpret_cast<FARPROC>(FixtureKeyboard);
 if(strcmp(name,"XInputGetState")==0)return reinterpret_cast<FARPROC>(FixtureController);
 return GetProcAddress(module,name);
}
#define main ExcludedBindingTestMain
#include "PalmBindingTests.cpp"
#undef main
#include <chrono>
namespace superjump::palm {
struct CollectorTestAccess {
 static bool Verify(void*){return true;}
 static void Configure(Collector&c,const motiontrace::Reader&r,const motiontrace::Profile&p){c.observer_.Stop();motiontrace::TraceEvent discarded{};while(c.observer_.Pop(discarded)){};c.observer_.Configure(r,p,Verify,nullptr,PalmTags,nullptr);c.observer_.ConfigureBinder(BindPalmNative,nullptr);c.observer_.Arm();c.state_=Collector::State::Running;}
 static void Event(Collector&c,uintptr_t state,bool success=true){safetyhook::Context x{};x.rax=success?1:0;x.rsi=state;c.observer_.Observe(x,0);}
 static void Invalidate(Collector&c){c.observer_.InvalidatePending();}
};
}
struct IntegratedFixture:BindingFixture {
 static constexpr uintptr_t ui_root=0x90100,mode=0x90200,stack=0x90300,entry=0x90400,component=0x90500,body=0x93000;
 IntegratedFixture():BindingFixture(4){
  m.bytes.resize(0x300000);p.root_slot=0x90000;m.Put(p.root_slot,ui_root);m.Put(ui_root+0x98,mode);m.Put(mode+0x30,uint8_t(4));m.Put(mode+0x32,uint8_t(17));m.Put(root,stack);m.Put(stack+0x48,uint8_t(4));m.Put(stack+0x30,entry);m.Put(entry+9,uint8_t(1));m.Put(parts+0x78,component);m.Put(component,p.base+0x5994CC8);m.Put(component+8,actor);m.Put(component+0xa4c,uint8_t(7));m.Put(component+0xbec,uint8_t(7));m.Put(contact+0x2d0,body);Use(4);Ground(true);
 }
 void Ground(bool yes){m.Put(ground+0x1098,uint8_t(yes));m.Put(ground+0x1078,int32_t(yes?0:-1));m.Put(ground+0x1099,uint8_t(1));m.Put(contact+0x468,uint8_t(0));m.Put(contact+0x2fe,uint8_t(1));}
 void Use(unsigned index){Use(superjump::palm::native::kProfiles[index]);}
 void Use(const superjump::palm::native::Profile& v){
  const uintptr_t pkg=0x100000,meta=0x110000,recs=0x1B0000,str=0x1D0000,off=0x1E0000;
  memset(m.bytes.data()+meta-0x10000,0,size_t(v.metadata_count)*288);Copy(meta+size_t(v.index)*288,v.metadata,288);m.Put(pkg+0x238,meta);m.Put(pkg+0x2b0,uint16_t(v.metadata_count));m.Put(pkg+0x248,recs);m.Put(pkg+0x250,v.record_count);m.Put(pkg+0x38,str);m.Put(pkg+0x40,off);m.Put(pkg+0x48,v.animation_count);
  unsigned j=0;for(const auto&z:v.variants){const uintptr_t r=0x200000+j*0x100,h=0x210000+j*0x8000,n=str+j*512;m.Put(recs+size_t(z.index)*8,r);m.Put(r+0x10,pkg);m.Put(r+0x28,h);Copy(h,z.bytes,z.size);Copy(n,z.name,z.name_size);m.Put(off+size_t(z.clip)*4,uint32_t(j*512));++j;}
  m.Put(umeta+0x111,uint8_t(0));m.Put(lmeta+0x111,uint8_t(0));m.Put(upper+0x10,umeta);m.Put(lower+0x10,lmeta);m.Put(upper+0x118,uintptr_t(0));m.Put(lower+0x118,uintptr_t(0));const auto target=v.lane==superjump::palm::native::Lane::Upper?upper:lower;m.Put(target+8,pkg);m.Put(target+0x10,meta+size_t(v.index)*288);m.Put(target+0x118,uintptr_t(0x123600));
 }
 void SetUp(){g_bossProfile=p;g_testGuardReader={&m,Memory::Read};g_testForeground=1;g_enabled=true;g_traversalProfileValid=true;g_nativeMovementChecksReady=false;g_traversalEpoch={};g_queuedEpoch={};g_boostRemaining=0;g_epochInvalidateRequested=0;g_queuedHero=0;g_currentHero=0;g_currentFrameBody=0;g_firstFrameDeadline=0;g_palmPolicy.Reset();superjump::palm::CollectorTestAccess::Configure(g_palmCollector,g_testGuardReader,p);g_seenCollectorEpoch=g_palmCollector.CancellationEpoch();g_seenCollectorDrops=g_palmCollector.Dropped();}
 PalmCurrent Current(){GuardDecision d{};RefreshBoostContext(false,&d);return ReadPalmCurrent(d);}
 bool Worker(){return ProcessPalmPackets(Current());}
 void Native(bool yes=true){superjump::palm::CollectorTestAccess::Event(g_palmCollector,state,yes);}
 void Ready(){Use(4);Ground(true);Worker();Worker();Sleep(20);}
 void Initial(){Native();Ground(false);Worker();}
 bool Stage(unsigned p){Use(p);Ground(false);Native();return Worker();}
 bool Chain(){Ready();Initial();Stage(12);Stage(12);Stage(13);return Stage(11);}
};
static unsigned checks=0,failed=0;
static void Check(bool v,const char*n){++checks;if(!v){++failed;printf("FAIL %s\n",n);}}
static std::string ReadFixtureLog(const char* path){
 std::ifstream input(path);
 return {std::istreambuf_iterator<char>(input),{}};
}
extern "C" int MotionFixture(uintptr_t body,float* values,int selector);
extern "C" const uint8_t MotionFixtureSite[];
int main(){SetPriorityClass(GetCurrentProcess(),BELOW_NORMAL_PRIORITY_CLASS);


 {
  using superjump::host::IsGameName;
  Check(IsGameName(L"CrimsonDesert.exe") && IsGameName(L"C:\\Games\\CRIMSONDESERT.EXE"),
        "game basename accepted case-insensitively");
  Check(!IsGameName(L"C:\\Games\\helper-CrimsonDesert.exe") &&
        !IsGameName(L"CrimsonDesert.exe.bak") && !IsGameName(nullptr),
        "other executable names rejected");
  Check(!superjump::host::IsGameCode(GetModuleHandleW(nullptr),
        reinterpret_cast<uintptr_t>(MotionFixtureSite),kMotionEntry.size()),
        "fixture process code is rejected as a production hook target");
  g_testForeground=1;
  uint8_t slots=0,provider=0;
  Check(cdinput::KbDown(VK_SPACE) && cdinput::XInputButtons(&slots,&provider)==XINPUT_GAMEPAD_A &&
        slots==1 && provider==1 && keyboardReads==1 && controllerReads==XUSER_MAX_COUNT,
        "foreground polling reaches keyboard and controller providers");
  g_testForeground=0;
  Check(!cdinput::KbDown(VK_SPACE) && cdinput::XInputButtons(&slots,&provider)==0 &&
        !slots && !provider && keyboardReads==1 && controllerReads==XUSER_MAX_COUNT,
        "background polling makes zero keyboard or controller provider calls");
  g_testForeground=1;
  loseFocusDuringPoll=true;
  Check(cdinput::XInputButtons(&slots,&provider)==0 && !slots && !provider &&
        controllerReads==XUSER_MAX_COUNT+1,
        "focus loss during a controller sample discards it and stops polling slots");
  loseFocusDuringPoll=false;
  g_testForeground=-1;

  auto* target=const_cast<uint8_t*>(MotionFixtureSite);
  auto hook=safetyhook::MidHook::create(target,OnMotionFrame,safetyhook::MidHook::StartDisabled);
  const bool installed=hook && MotionTrampolineMatches(*hook,reinterpret_cast<uintptr_t>(target)) && hook->enable();
  Check(installed,"bundled SafetyHook relocates both paths of the motion branch");
  if (installed) {
   for (int branch : {1,2}) {
    IntegratedFixture f;
    f.SetUp();
    Check(f.Chain(),"native third palm queues a boost for the hooked fixture");
    float values[4]{1,2,3,4};
    Check(MotionFixture(f.body+0x2000,values,branch)==branch && values[1]==2 &&
          g_boostRemaining>0,"foreign physics body preserves both native path and queued boost");
    const auto before=_mm_getcsr();
    const bool branchPreserved=MotionFixture(f.body,values,branch)==branch;
    Check(branchPreserved && values[0]==1 && values[1]==g_boostY &&
          values[2]==3 && values[3]==4 && _mm_getcsr()==before,
          "SafetyHook delivers vertical boost and preserves other lanes, branch, and MXCSR");
   }
   Check(hook->disable().has_value(),"fixture SafetyHook restores authored native code");
  }
 }
 {
  IntegratedFixture f;
  f.SetUp();
  f.m.Put(f.info+8,uint32_t{6});
  f.Current();f.Current();
  Check(g_currentFrameBody.load()==f.body && !g_contextBlocked,
        "Oongka fixture establishes a grounded native epoch");
  float frame[4]{1,2,3,4};
  const LONG generation=g_cancelGeneration;
  AcquireSRWLockExclusive(&g_eligibilityLock);
  ConsumeBoostFrame(frame,f.body,f.body+0x90);
  ReleaseSRWLockExclusive(&g_eligibilityLock);
  Check(frame[1]==2 && g_cancelGeneration==generation && !g_epochInvalidateRequested,
        "busy idle frame preserves the input generation before a second press");
  f.Ground(false);
  Check(QueueBoost(generation,6,GetTickCount64()+250),
        "Oongka can still queue the second press after idle contention");
  AcquireSRWLockExclusive(&g_eligibilityLock);
  ConsumeBoostFrame(frame,f.body,f.body+0x90);
  ReleaseSRWLockExclusive(&g_eligibilityLock);
  Check(frame[1]==2 && !g_boostRemaining && g_cancelGeneration!=generation &&
        g_epochInvalidateRequested,
        "busy delivery frame still retires a queued boost without writing");
 }
 {
  IntegratedFixture f;f.SetUp();f.Ready();g_palmPacketLogs=0;
  f.m.Put(uintptr_t(0x110000)+size_t(superjump::palm::native::kProfiles[4].index)*288+0xa8,uint32_t(99999));
  const auto epoch=g_palmCollector.CancellationEpoch();
  g_log.open("build/authored-opener-diagnostic.log",std::ios::trunc);
  f.Native();f.Worker();g_log.close();
  const auto log=ReadFixtureLog("build/authored-opener-diagnostic.log");
  Check(g_palmCollector.CancellationEpoch()==epoch+1&&g_palmPolicy.CurrentPhase()==Phase::Idle,
        "unbound opener retains the cancellation outcome");
  Check(log.find("palm diagnostic:")!=std::string::npos&&log.find("bind=profile")!=std::string::npos&&
        log.find("profile=NoProfile")!=std::string::npos&&log.find("lower=")!=std::string::npos&&
        log.find("upper=")!=std::string::npos&&log.find("sample=Valid second=- changed=00")!=std::string::npos,
        "worker emits ordered refusal, sampler fields and both lanes for a rejected opener");
 }
 {
  IntegratedFixture f;f.SetUp();f.Ready();g_palmPacketLogs=0;
  f.InitSlot(f.upper,f.umeta,15035,0x1690D50E,2);
  const uintptr_t key=0x110000+size_t(superjump::palm::native::kProfiles[4].index)*288+0xa8;
  unsigned keyReads=0;
  f.m.mutation=[&](uintptr_t at){if(at==key&&++keyReads==2)f.m.Put(key,uint32_t(99999));};
  g_log.open("build/authored-sampler-diagnostic.log",std::ios::trunc);
  f.Native();f.m.mutation={};f.Worker();g_log.close();
  const auto log=ReadFixtureLog("build/authored-sampler-diagnostic.log");
  Check(log.find("bind=sampler")!=std::string::npos&&log.find("sample=Changed second=Valid changed=10 valid=0")!=std::string::npos&&
        log.find("ground=1/1")!=std::string::npos&&log.find("lower=10009/66D6DEF3/101")!=std::string::npos&&
        log.find("upper=15035/1690D50E/110")!=std::string::npos,
        "double-sample rejection reports both actual lanes including upper suppression of the lower jump");
 }
 {
  IntegratedFixture f;f.SetUp();f.Ready();g_palmPacketLogs=0;
  g_log.open("build/authored-binder-diagnostic.log",std::ios::trunc);
  f.m.Put(f.component+0xa4c,uint8_t(6));
  f.m.Put(f.mode+0x30,uint8_t(3));f.Native();f.Worker();
  f.m.Put(f.mode+0x30,uint8_t(4));f.Native();f.Worker();
  f.m.Put(f.component+0xa4c,uint8_t(7));
  f.m.mutation=[&](uintptr_t at){if(at==0x100000+0x238)InterlockedIncrement(&g_cancelGeneration);};
  f.Native();f.m.mutation={};f.Worker();
  superjump::palm::CollectorTestAccess::Invalidate(g_palmCollector);f.Worker();
  g_log.close();
  const auto log=ReadFixtureLog("build/authored-binder-diagnostic.log");
  const auto ui=log.find("bind=ui"),owner=log.find("bind=owner"),generation=log.find("bind=generation");
  Check(ui<owner&&owner<generation&&generation!=std::string::npos&&
        log.find("traversal=8 owner=8 profile=-")!=std::string::npos&&
        log.find("profile=Valid",generation)!=std::string::npos,
        "UI refuses before owner, a valid traversal reports its lane mismatch, and a generation change refuses after classification");
  Check(log.find("no packet")!=std::string::npos&&log.find("nativeSuccess=")!=std::string::npos,
        "worker records an epoch reset with no packet and existing observer counters");
 }
 {
  IntegratedFixture f;f.SetUp();f.Ready();g_palmPacketLogs=127;
  g_log.open("build/authored-budget-diagnostic.log",std::ios::trunc);
  f.Native();f.Native();f.Worker();g_log.close();
  const auto log=ReadFixtureLog("build/authored-budget-diagnostic.log");
  const auto first=log.find("palm diagnostic:");
  Check(g_palmPacketLogs==128&&first!=std::string::npos&&log.find("palm diagnostic:",first+1)==std::string::npos&&
        log.find("bind=bound")!=std::string::npos&&log.find("phase=0/1 ready=1/0")!=std::string::npos,
        "one remaining worker record logs bound opener readiness and phase without exceeding the cap");
 }

 {
 IntegratedFixture f;f.SetUp();f.Ready();Check(g_palmPolicy.Ready(),"actual worker preserves coherent ground readiness without clearing it while idle");f.Native();f.Ground(false);Check(!f.Worker()&&g_palmPolicy.CurrentPhase()==Phase::Initial,"callback grounded initial packet survives current airborne initial snapshot ordering");
 Check(!f.Stage(12)&&g_palmPolicy.CurrentPhase()==Phase::Palm1,"bound actual first native stage does not boost");Check(!f.Stage(12)&&g_palmPolicy.CurrentPhase()==Phase::Palm1,"duplicate native first-stage pulse cannot advance twice");Check(!f.Stage(13)&&g_palmPolicy.CurrentPhase()==Phase::Palm2,"actual second stage does not boost");Check(f.Stage(11)&&g_boostRemaining==g_boostFrames,"only actual third successful native stage publishes queue");
 g_palmDeliveryCount=0;
 Sleep(60);float d[4]={1,2,3,4};ConsumeBoostFrame(d,f.body,f.body+0x90);Check(d[1]==g_boostY&&g_boostRemaining==g_boostFrames-1,"first delivery after60ms works at ordinary20FPS independently of packet freshness");
 Check(g_palmDeliveryCount==1&&g_palmDeliveries[0].before==2.f&&g_palmDeliveries[0].after==g_boostY&&g_palmDeliveries[0].remaining==g_boostFrames-1,"actual physics write enqueues one third-palm delivery with measured before/after delta");
 f.m.Put(f.mode+0x30,uint8_t(3));d[1]=2;ConsumeBoostFrame(d,f.body,f.body+0x90);Check(d[1]==2&&g_boostRemaining==0,"current UI cancellation prevents remaining active boost frames immediately");
 Check(g_palmDeliveryCount==1,"cancelled frame does not generate a boost delivery");
 }
 for(unsigned loss=0;loss<8;++loss){IntegratedFixture f;f.SetUp();Check(f.Chain(),"valid authored native-time chain reaches pending boost before cancellation case");if(loss==0)f.Ground(true);if(loss==1)f.m.Put(f.info+8,uint32_t(4));if(loss==2)f.m.Put(f.upper+0x118,uintptr_t(0));if(loss==3)f.m.Put(f.mode+0x30,uint8_t(1));if(loss==4)g_testForeground=0;if(loss==5)f.m.Put(f.component+0xa4c,uint8_t(2));if(loss==6)superjump::palm::CollectorTestAccess::Invalidate(g_palmCollector);if(loss==7)g_firstFrameDeadline=GetTickCount64()-1;float d[4]={1,2,3,4};ConsumeBoostFrame(d,f.body,f.body+0x90);Check(d[1]==2&&g_boostRemaining==0,"landing/hero/phase/UI/focus/FreedomFlyer/observer/stale loss cancels already queued frames");}
 {IntegratedFixture f;f.SetUp();f.Ready();f.Initial();f.Stage(12);f.Use(13);f.Worker();f.Use(11);Check(!f.Worker()&&g_boostRemaining==0,"periodic snapshots cannot substitute for missing successful second and third native actions");}
 {IntegratedFixture f;f.SetUp();f.Ready();f.Initial();f.Stage(12);f.m.Put(f.mode+0x30,uint8_t(3));f.Worker();f.m.Put(f.mode+0x30,uint8_t(4));f.Stage(13);Check(!f.Stage(11)&&g_boostRemaining==0,"context exit before next success cannot bridge or produce delayed boost");}
 {IntegratedFixture f;f.SetUp();f.Use(12);f.Ground(false);f.Worker();f.Native();f.Worker();f.Stage(13);Check(!f.Stage(11)&&g_boostRemaining==0,"inflight/grapple/fall start without native initial epoch cannot qualify");}
 {IntegratedFixture f;f.SetUp();f.Ready();f.Native(false);f.Ground(false);f.Worker();f.Stage(12);f.Stage(13);Check(!f.Stage(11)&&g_boostRemaining==0,"native false result never arms the chain");}
 {IntegratedFixture f;f.SetUp();f.Ready();f.Initial();f.Use(12);f.Ground(true);f.Native();f.Worker();f.Stage(13);Check(!f.Stage(11)&&g_boostRemaining==0,"grounded native palm cannot authorize aerial combo");}
 {IntegratedFixture f;f.SetUp();f.Ready();f.Initial();f.Stage(12);f.Use(13);f.Ground(false);const auto oldTick=GetTickCount64();Sleep(20);f.Native();Check(f.Worker()==false&&g_palmPolicy.CurrentPhase()==Phase::Palm2&&GetTickCount64()>oldTick,"packet after earlier loop timestamp uses fresh monotonic consumption time");}
 {IntegratedFixture f;f.SetUp();f.Ready();f.Initial();f.Stage(12);f.Use(13);f.Native();f.Use(11);f.Native();Check(f.Worker()&&g_boostRemaining>0,"multiple forward native stages queued in one worker interval reconcile before exact current-phase check");}
 {IntegratedFixture f;f.SetUp();f.Ready();f.Initial();f.Use(12);f.Native();Sleep(70);f.Worker();f.Stage(13);Check(!f.Stage(11)&&g_boostRemaining==0,"stale callback packet cannot publish a delayed boost");}
 for(unsigned active=0;active<2;++active)for(unsigned mutation=0;mutation<12;++mutation){IntegratedFixture f;f.SetUp();Check(f.Chain(),"strict phase owner mutation fixture queues from a fully bound actual native chain");float d[4]={1,2,3,4};if(active)ConsumeBoostFrame(d,f.body,f.body+0x90);const uintptr_t metadata=0x110000+72*288,package=0x100000,record=0x200000,header=0x210000;
  if(mutation==0)f.m.Put(metadata+0x54,uint32_t(1));if(mutation==1)f.m.Put(metadata+0xee,uint16_t(2));if(mutation==2)f.m.Put(package+0x238,uintptr_t(0x110120));if(mutation==3)f.m.Put(package+0x248,uintptr_t(0x1B0008));if(mutation==4)f.m.Put(0x1B0000+1538*8,uintptr_t(0));if(mutation==5)f.m.Put(record+0x10,package+16);if(mutation==6)f.m.Put(record+0x28,header+16);if(mutation==7)f.m.Put(header+0x9e,uint16_t(1));if(mutation==8)f.m.Put(header+0xa8,uint8_t(99));if(mutation==9)f.m.Put(0x1D0000,uint8_t('X'));if(mutation==10){f.m.Put(package+0x40,uintptr_t(0x1E0004));f.m.Put(0x1E0004+610*4,uint32_t(1));}if(mutation==11)f.m.Put(metadata+0x20,uint8_t(99));
  d[1]=2;ConsumeBoostFrame(d,f.body,f.body+0x90);if(d[1]!=2||g_boostRemaining!=0)printf("mutation active=%u case=%u remaining=%d\n",active,mutation,int(g_boostRemaining));Check(d[1]==2&&g_boostRemaining==0,"complete metadata/package/record/+9E/clip mutation cancels queued or active write despite retained key/hash/pointer");
 }
 {
 IntegratedFixture f;f.SetUp();f.Ready();f.Ground(false);g_boostRemaining=0;float d[4]={1,2,3,4};const auto before=f.m.reads;const auto t=std::chrono::steady_clock::now();for(unsigned i=0;i<100000;++i)ConsumeBoostFrame(d,f.body,f.body+0x90);const auto elapsed=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-t).count()/100000;Check(f.m.reads==before&&d[1]==2,"Kliff no-pending actual frame callback performs zero metadata/guard reads");printf("benchmark Kliff idle frame mean_ns=%.2f iterations=100000 reads=0\n",elapsed);
 const auto t2=std::chrono::steady_clock::now();for(unsigned i=0;i<100000;++i)ConsumeBoostFrame(d,f.body+0x1000,f.body+0x1090);const auto elapsed2=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-t2).count()/100000;Check(f.m.reads==before,"foreign frame callback performs zero reads");printf("benchmark nonlocal frame mean_ns=%.2f iterations=100000 reads=0\n",elapsed2);
 }

 for(unsigned start=0;start<11;++start){IntegratedFixture f;f.SetUp();f.Use(start);f.Ground(true);f.Worker();f.Worker();Sleep(20);f.Native();f.Ground(false);f.Worker();Check(g_palmPolicy.CurrentPhase()==Phase::Initial,"all11 current proved ordinary standing/walk/run initial descriptors arm through actual callback");f.Use(12);f.Native();f.Worker();f.Use(13);f.Native();f.Worker();f.Use(11);f.Native();Check(f.Worker(),"each proved initial profile completes actual native three-stage combo");}
 {IntegratedFixture f;f.SetUp();f.Ready();f.Native();f.Worker();f.Native();f.Worker();Check(g_palmPolicy.CurrentPhase()==Phase::Initial,"duplicate initial native success while still grounded does not cancel or advance");f.Ground(false);f.Stage(12);f.Stage(13);Check(f.Stage(11),"duplicate grounded initial pulse still yields only one ordered native combo");}
 {
 IntegratedFixture f;f.SetUp();Check(f.Chain(),"actual pending-frame benchmark fixture qualifies");std::vector<double> timings;for(unsigned i=0;i<200;++i){float d[4]={1,2,3,4};g_boostRemaining=1;g_firstFrameDeadline=0;const auto at=std::chrono::steady_clock::now();ConsumeBoostFrame(d,f.body,f.body+0x90);timings.push_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-at).count());Check(d[1]==g_boostY,"bounded authored pending-frame benchmark writes only after full current proof");}std::sort(timings.begin(),timings.end());double total=0;for(auto x:timings)total+=x;printf("benchmark queued Kliff full typed frame mean_us=%.3f p99_us=%.3f max_us=%.3f iterations=200 fixture=authoredDATA noFileIO=1\n",total/timings.size(),timings[197],timings.back());
 auto readerCalls=f.m.reads;const auto falseStart=std::chrono::steady_clock::now();for(unsigned i=0;i<100000;++i)f.Native(false);const double falseNs=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-falseStart).count()/100000;Check(f.m.reads==readerCalls,"production observer native-false benchmark performs zero data reads");printf("benchmark actual observer nativeFalse mean_ns=%.3f iterations=100000 reads=0\n",falseNs);
 timings.clear();for(unsigned i=0;i<100;++i){const auto at=std::chrono::steady_clock::now();f.Native();timings.push_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-at).count());motiontrace::TraceEvent e{};Check(g_palmCollector.Pop(e)&&e.palm.bound,"actual bound callback benchmark retains native packet");}std::sort(timings.begin(),timings.end());total=0;for(auto x:timings)total+=x;printf("benchmark actual bound observer excluding contractVerify mean_us=%.3f p99_us=%.3f max_us=%.3f iterations=100 fixture=authoredDATA noFileIO=1\n",total/timings.size(),timings[98],timings.back());
 }


 for(uint32_t hero:{4u,6u}){IntegratedFixture f;f.SetUp();f.m.Put(f.info+8,hero);f.Ground(true);f.Current();f.Current();Check(superjump::palm::Route(g_currentHero.load())==Mode::DoubleJump,"actual typed Damiane/Oongka identity routes to retained double jump");superjump::GameDoubleJumpState state{};const superjump::GameDoubleJumpConfig config{1.f,.2f,80,900,900};Check(superjump::ObserveGameJumpDelta(state,config,100,5.f,false)==superjump::GameDoubleJumpEvent::FirstImpulse,"retained character detector arms on normal first jump impulse");superjump::ObserveGameJumpDelta(state,config,200,0.f,false);Check(superjump::ObserveGameJumpDelta(state,config,300,5.f,false)==superjump::GameDoubleJumpEvent::Triggered,"retained character normal second jump impulse qualifies");Check(QueueBoost(InterlockedCompareExchange(&g_cancelGeneration,0,0),hero,GetTickCount64()+250),"retained character actual full guard queues double-jump boost");f.Ground(false);f.Native();float d[4]={1,2,3,4};ConsumeBoostFrame(d,f.body,f.body+0x90);Check(d[1]==g_boostY,"unbound palm observer packet cannot cancel another character's normal boost");g_palmCollector.Stop();d[1]=2;ConsumeBoostFrame(d,f.body,f.body+0x90);Check(d[1]==2&&g_boostRemaining==0,"collector contract stop cancels already pending retained-character frames too");}
 {Check(AcquireVariantLease(),"third variant acquires the same exclusive process lease as normal alternatives");Check(!AcquireVariantLease(),"second SuperJump alternative lease refuses in the same process");CloseHandle(g_variantMutex);g_variantMutex=nullptr;
  fixtureRecorderPresent=true;Check(fixtureRecorderPresent,"authored module-presence seam represents recorder conflict");const auto hook=g_hookSite;g_log.open("build/authored-recorder/conflict.log");ModThread();g_log.flush();g_log.close();std::ifstream in("build/authored-recorder/conflict.log");std::string line;std::getline(in,line);Check(line.find("Disable it and both normal SuperJump alternatives")!=std::string::npos&&g_hookSite==hook,"actual ModThread explicitly diagnoses recorder conflict before native setup or frame hook");fixtureRecorderPresent=false;Check(!fixtureRecorderPresent,"authored module-presence seam resets without ASI loading");
 }
 {IntegratedFixture f;f.SetUp();f.Current();StopThirdVariant();const auto reads=f.m.reads;float d[4]={1,2,3,4};ConsumeBoostFrame(d,f.body,f.body+0x90);Check(!g_palmCollector.Armed()&&g_currentFrameBody.load()==0&&g_boostRemaining==0&&f.m.reads==reads&&d[1]==2,"stopped variant clears frame cache and leaves retained cave fully inert");}
 for (const auto* provider:{&superjump::menu60::kUpdated,&superjump::menu60::kMenu601,&superjump::menu60::kMenu604}) {
  const bool armedIdle=provider==&superjump::menu60::kMenu601 || provider==&superjump::menu60::kMenu604;
  IntegratedFixture f;f.SetUp();
  f.m.bytes.resize(0x800000);
  constexpr uintptr_t module=0x10000;
  const uintptr_t state=module+provider->flightEnabled;
  f.m.Put(module+provider->jumpEnabled,uint32_t(0));
  g_menuOwner.Publish({module,provider});
  f.m.Put(state,uint32_t(0));
  f.m.Put(state+4,uint32_t(0));
  Check(TrueFlightAllowsBoost(g_testGuardReader),"explicitly disabled updated Menu permits ordinary jump");
  Check(f.Chain(),"verified inactive flight permits a qualifying native third palm");
  f.m.Put(state,uint32_t(1));
  Check(TrueFlightAllowsBoost(g_testGuardReader)==armedIdle,"6.0.1 may boost while armed and idle, legacy Menu remains off-only");
  f.m.Put(state+4,uint32_t(1));
  float d[4]={1,2,3,4};
  ConsumeBoostFrame(d,f.body,f.body+0x90);
  Check(d[1]==2&&g_boostRemaining==0,"engaged True Flight cancels an already queued boost on physics frame");
  f.m.Put(state+4,uint32_t(0));
  float afterFlight[4]={1,2,3,4};
  ConsumeBoostFrame(afterFlight,f.body,f.body+0x90);
  Check(afterFlight[1]==2&&g_boostRemaining==0,"disengagement cannot revive the canceled native request");
  f.m.Put(state+4,uint32_t(1));
  f.m.Put(state,uint32_t(0));
  Check(!TrueFlightAllowsBoost(g_testGuardReader),"disable transition waits for engaged state to clear");
  f.m.Put(state+4,uint32_t(0));
  Check(TrueFlightAllowsBoost(g_testGuardReader),"fully disabled True Flight permits normal eligibility");
  f.m.Put(module+provider->jumpEnabled,uint32_t(1));
  Check(TrueFlightAllowsBoost(g_testGuardReader),"Menu owns its jump multiplier with flight idle");
  if (armedIdle) f.m.Put(state,uint32_t(1));
  Check(f.Chain(),"full native chain still queues with the Menu multiplier and idle True Flight");
  float dualBoost[4]={1,2,3,4};
  ConsumeBoostFrame(dualBoost,f.body,f.body+0x90);
  Check(dualBoost[1]==g_boostY,"third palm boost reaches motion delivery with Menu jump on");
  for(const auto& profile:superjump::palm::native::kTrueDoubleJump107OpeningProfiles){
   f.SetUp();f.Use(profile);f.Ground(true);f.Worker();f.Worker();Sleep(20);
   f.Initial();
   Check(g_palmPolicy.CurrentPhase()==Phase::Initial,"replacement opening needs native success from grounded state");
   f.Stage(12);f.Stage(13);
   Check(f.Stage(11),"known replacement opener completes the same native palm sequence");
   float next[4]={1,2,3,4};
   ConsumeBoostFrame(next,f.body,f.body+0x90);
   Check(next[1]==g_boostY,"known replacement opener reaches third-palm motion delivery");
  }
  f.m.Put(state,std::array<uint32_t,2>{1,1});
  Check(!TrueFlightAllowsBoost(g_testGuardReader),"engaged True Flight retains priority while Menu Jump is enabled");
  f.m.Put(state+4,uint32_t(2));
  Check(!TrueFlightAllowsBoost(g_testGuardReader),"invalid flight slot is fail-closed");
  g_menuOwner.Publish({kUnknownFlightOwner,nullptr});
  Check(!TrueFlightAllowsBoost(g_testGuardReader),"unknown Menu lease is fail-closed");
  g_menuOwner.Publish({});
  Check(TrueFlightAllowsBoost(g_testGuardReader),"verified absence of Menu permits standalone operation");
 }
 for (const auto* provider : {&superjump::menu60::kMenu601,&superjump::menu60::kMenu604}) {
  for (uint32_t hero : {4u,6u}) {
   IntegratedFixture f;
   f.SetUp();
   f.m.bytes.resize(0x800000);
   constexpr uintptr_t module=0x10000;
   const auto& menu=*provider;
   g_menuOwner.Publish({module,&menu});
   f.m.Put(module+menu.jumpEnabled,uint32_t(1));
   f.m.Put(module+menu.flightEnabled,std::array<uint32_t,2>{1,0});
   f.m.Put(f.info+8,hero);
   f.Ground(true);
   f.Current();f.Current();f.Current();
   g_palmDeliveryCount=0;
   Check(QueueBoost(),
         "Damiane or Oongka double jump can queue while DLMM jump and idle flight are enabled");
   f.Ground(false);
   float frame[4]={1,2,3,4};
   ConsumeBoostFrame(frame,f.body,f.body+0x90);
   Check(frame[1]==g_boostY,"retained-character boost delivers independently of Menu opening multiplier");
   Check(g_palmDeliveryCount==1 && g_palmDeliveries[0].hero==hero,
         "fallback reports a delivered motion write for the selected hero");
   g_menuOwner.Publish({});
  }
 }

 printf("PALM ACTUAL INTEGRATION checks=%u failures=%u foreignGameCodeExecuted=0\n",checks,failed);return failed?1:0;
}
