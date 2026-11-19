
#define SUPERJUMP_TESTING
#if defined(KEYPRESS_VARIANT)
#include "../keypress/source/SuperJump.cpp"
#include "../keypress/tests/TraversalFixture.h"
#else
#include "../double-jump/source/SuperJump.cpp"
#include "../double-jump/tests/TraversalFixture.h"
#endif
#include <cstdio>
#include <chrono>

namespace {
unsigned checks=0, failures=0;
void Check(bool ok,const char* name) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL %s\n",name); }
}

constexpr auto kMotionOffset=MOTION_PRODUCER_RVA+sizeof(MOTION_PRODUCER);

void Producer() {
    auto* image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x4610000,MEM_RESERVE,PAGE_READWRITE));
    Check(image!=nullptr,"reserve authored sparse image");
    if (!image) return;
    auto* code=static_cast<uint8_t*>(VirtualAlloc(image+0x4603000,0x2000,MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Check(code!=nullptr,"commit producer and Menu fixture pages");
    if (code) {
        auto* site=image+kMotionOffset;
        auto* delta=image+superjump::menu60::kDeltaRva;
        g_bossProfile.base=reinterpret_cast<uintptr_t>(image);
        g_hookSite=site;
        memcpy(site-sizeof(MOTION_PRODUCER),MOTION_PRODUCER,sizeof(MOTION_PRODUCER));
        memcpy(site,kMotionEntry.data(),kMotionEntry.size());
        memcpy(site+0x40,kMotionBranchTarget.data(),kMotionBranchTarget.size());
        memcpy(delta,superjump::menu60::kStockDelta.data(),superjump::menu60::kStockDelta.size());
        Check(MotionSiteMatches(site),"native producer, relocated branch and both continuation witnesses match");
        for (int offset : {-9,-1,0,4,0x40,0x46}) {
            site[offset]^=1;
            Check(!MotionSiteMatches(site),"changed native producer or continuation refuses");
            site[offset]^=1;
        }
        Check(!InstallHook() && !g_motionHook && MotionSiteMatches(site),
              "exact bytes in a private executable allocation refuse publication");
        Check(!memcmp(delta,superjump::menu60::kStockDelta.data(),superjump::menu60::kStockDelta.size()),
              "refusal leaves the downstream Menu scan and writer intact");
        g_hookSite=nullptr;
        g_bossProfile={};
    }
    VirtualFree(image,0,MEM_RELEASE);
}

extern "C" int MotionFixture(uintptr_t body,float* values,int selector);
extern "C" const uint8_t MotionFixtureSite[];

void Replay() {
    auto* target=const_cast<uint8_t*>(MotionFixtureSite);
    auto hook=safetyhook::MidHook::create(target,OnMotionFrame,safetyhook::MidHook::StartDisabled);
    const auto at=reinterpret_cast<uintptr_t>(target);
    const bool installed=hook && MotionTrampolineMatches(*hook,at) && hook->enable();
    Check(installed,"SafetyHook relocates the authored native JE and MOVAPS");
    if (!installed) return;
    guardfixture::Fixture f;
    g_bossProfile=f.p;
    g_traversalProfileValid=true;
    g_testGuardReader=f.Reader();
    g_testForeground=1;
    g_enabled=true;
    g_boostY=41.0f;
    g_boostFrames=1;
    for (int branch : {1,2}) {
        float values[4]{1,2,3,4};
        Check(!RefreshBoostContext() && QueueBoost(),"grounded native fixture queues a classic boost");
        Check(MotionFixture(f.body+0x100,values,branch)==branch && values[1]==2 && g_boostRemaining==1,
              "foreign physics body preserves branch and the queued boost");
        const auto mxcsr=_mm_getcsr();
        Check(MotionFixture(f.body,values,branch)==branch && values[0]==1 && values[1]==g_boostY &&
              values[2]==3 && values[3]==4 && g_boostRemaining==0 && _mm_getcsr()==mxcsr,
              "current player writes only the vertical lane and preserves native branch and MXCSR");
    }
    float values[4]{1,2,3,4};
    Check(QueueBoost(),"foreground fixture can queue again");
    g_testForeground=0;
    Check(MotionFixture(f.body,values,1)==1 && values[1]==2 && !g_boostRemaining,
          "focus loss before a native frame cancels pending delivery");
    Check(hook->disable().has_value() && !memcmp(target,kMotionEntry.data(),kMotionEntry.size()),
          "SafetyHook restores authored fixture bytes");
    g_testGuardReader={};
    g_testForeground=-1;
    g_bossProfile={};
    g_traversalEpoch={};
    g_currentFrameBody.store(0);
}

unsigned keyboardReads=0,controllerReads=0;
bool loseFocusDuringPoll=false;
SHORT WINAPI KeyboardPoll(int key) {
    ++keyboardReads;
    if (loseFocusDuringPoll) g_testForeground=0;
    return key==VK_SPACE ? static_cast<SHORT>(0x8000u) : 0;
}
DWORD WINAPI ControllerPoll(DWORD slot,XINPUT_STATE* state) {
    ++controllerReads;
    if (loseFocusDuringPoll) g_testForeground=0;
    if (slot!=0) return ERROR_DEVICE_NOT_CONNECTED;
    state->Gamepad.wButtons=XINPUT_GAMEPAD_A;
    return ERROR_SUCCESS;
}

void ForegroundPolling() {
    cdinput::g_testKeyboardPoll=KeyboardPoll;
    cdinput::g_testControllerPoll=ControllerPoll;
    g_testForeground=1;
    uint8_t slots=0,provider=0;
    Check(cdinput::KbDown(VK_SPACE) && cdinput::XInputButtons(&slots,&provider)==XINPUT_GAMEPAD_A &&
          slots==1 && provider==1 && keyboardReads==1 && controllerReads==XUSER_MAX_COUNT,
          "foreground polling reaches the keyboard and controller providers");
    g_testForeground=0;
    Check(!cdinput::KbDown(VK_SPACE) && cdinput::XInputButtons(&slots,&provider)==0 &&
          !slots && !provider && keyboardReads==1 && controllerReads==XUSER_MAX_COUNT,
          "background polling reaches neither provider");
    g_testForeground=1;
    loseFocusDuringPoll=true;
    Check(!cdinput::KbDown(VK_SPACE) && keyboardReads==2,
          "focus loss during a keyboard sample discards it");
    g_testForeground=1;
    Check(cdinput::XInputButtons(&slots,&provider)==0 && !slots && !provider &&
          controllerReads==XUSER_MAX_COUNT+1,
          "focus loss during a controller sample discards it and stops remaining slots");
    loseFocusDuringPoll=false;
    cdinput::g_testKeyboardPoll=nullptr;
    cdinput::g_testControllerPoll=nullptr;
    g_testForeground=-1;
}

void ResidentFlight(const superjump::menu60::Profile& profile) {
    namespace menu=superjump::menu60;
    guardfixture::Fixture f;
    const auto base=f.p.base, site=base+0x4603597, delta=site-8, cave=base+0x3A008;
    const uintptr_t module=0x180000000;
    const menu::Provider provider{module,&profile};
    const auto& writer=*menu::FlightWriterFor(&profile);
    const bool armedIdle=provider.profile==&menu::kMenu601 || provider.profile==&menu::kMenu604 || profile.adaptive;
    const auto region=[](void*,uintptr_t at,size_t n){return at==0x140000000+0x3A008 && n==165;};
    auto code=menu::kFlightCave;
    for (size_t i=0;i<menu::kFlightCaveAddresses.size();++i) {
        const auto address=i<writer.slots.size()?module+writer.slots[i]:site+10;
        memcpy(code.data()+menu::kFlightCaveAddresses[i],&address,8);
    }
    std::array<uint8_t,18> entry{0x0F,0x28,0xC6,0xF3,0x45,0x0F,0x5C,0xC8,0xE9,0,0,0,0,0x90,0x90,0x90,0x90,0x90};
    const auto displacement=static_cast<int32_t>(intptr_t(cave)-intptr_t(site+5));
    memcpy(entry.data()+9,&displacement,4);
    std::array<uint8_t,10> patch{};
    memcpy(patch.data(),entry.data()+8,patch.size());
    f.Put(delta,entry);
    f.Put(cave,code);
    f.Put(module+writer.published,site);
    f.Put(module+writer.patch,patch);
    f.Put(module+writer.target,site);
    f.Put(module+writer.sharedBridge,uintptr_t(0));
    f.Put(module+writer.slots[2],std::array<uint32_t,4>{0x3F800000,0x3F800000,0x3F800000,0x3F800000});
    f.Put(module+writer.slots[3],std::array<uint32_t,4>{});
    f.Put(module+writer.slots[5],std::array<uint32_t,3>{0,0x3DCCCCCD,0});
    f.Put(module+provider.profile->flightEnabled,std::array<uint32_t,2>{});
    f.Put(module+provider.profile->jumpEnabled,uint32_t(0));
    const auto ownership=[&]{return menu::DeltaReady(f.Reader(),base,provider,false,region);};
    const auto delivery=[&]{return menu::DeltaReady(f.Reader(),base,provider,true,region);};
    Check(ownership() && delivery(),"exact inactive resident writer admits installation and boost");
    f.Put(module+provider.profile->flightEnabled,uint32_t(1));
    Check(ownership() && delivery()==armedIdle,"idle armed provider admits only a verified neutral resident writer");
    f.Put(module+writer.slots[3]+4,uint32_t(0x3F800000));
    Check(ownership() && !delivery(),"idle flight with a non-neutral additive term admits passive producer only");
    f.Put(module+writer.slots[3]+4,uint32_t(0));
    f.Put(module+provider.profile->flightEnabled+4,uint32_t(1));
    Check(!delivery(),"engaged flight blocks delivery");
    f.Put(module+provider.profile->flightEnabled,uint32_t(0));
    Check(!delivery(),"disabling flight while still engaged blocks delivery");
    f.Put(module+provider.profile->flightEnabled+4,uint32_t(0));
    f.Put(module+writer.slots[3]+4,uint32_t(0x3F800000));
    Check(!delivery(),"non-neutral additive transform blocks delivery");
    f.Put(module+writer.slots[3]+4,uint32_t(0));
    f.Put(module+writer.slots[2],uint32_t(0x40000000));
    Check(!delivery(),"non-neutral multiplicative transform blocks delivery");
    f.Put(module+writer.slots[2],uint32_t(0x3F800000));
    f.Put(module+writer.slots[5],uint32_t(0x3F800000));
    Check(!delivery(),"non-neutral vertical override blocks delivery");
    f.Put(module+writer.slots[5],uint32_t(0));
    f.Put(module+writer.slots[5]+8,uint32_t(0xBF800000));
    Check(!delivery(),"changed vertical threshold blocks delivery even with zero multiplier");
    f.Put(module+writer.slots[5]+8,uint32_t(0));
    Check(delivery(),"explicit off and neutral transforms let standalone jump resume");
    for (const auto address : {delta,site+8,cave,cave+12,cave+157,module+writer.published,module+writer.patch,module+writer.target,module+writer.sharedBridge}) {
        uint8_t original=0;
        f.Reader().Get(address,original);
        f.Put(address,uint8_t(original^1));
        Check(!ownership() && !delivery(),"resident writer mutation refuses startup and delivery");
        f.Put(address,original);
    }
    Check(!menu::DeltaReady(f.Reader(),base,{module,&menu::kEarlier},false,region),"earlier Menu cannot borrow updated resident writer");
    Check(!menu::DeltaReady(f.Reader(),base,{1,nullptr},false,region),"unknown Menu cannot borrow resident writer");
    f.Put(delta,menu::kStockDelta);
    Check(menu::DeltaReady(f.Reader(),base,{1,nullptr},true,region),
          "foreign module identity alone does not block a stock native writer");
    f.Put(delta,entry);
    const auto badRegion=[](void*,uintptr_t,size_t){return false;};
    Check(!menu::DeltaReady(f.Reader(),base,provider,true,badRegion),"non-executable foreign cave refuses");
    f.changing=cave; f.reads=0; f.tear=true;
    Check(!delivery(),"resident code mutation between snapshots refuses");

    const auto* page=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
    Check(page!=nullptr,"private fixture cave allocation");
    if (page) {
        const auto address=reinterpret_cast<uintptr_t>(page);
        Check(menu::FlightCaveRegion(reinterpret_cast<void*>(base),address,code.size()),
              "expected private executable cave allocation admits");
        Check(!menu::FlightCaveRegion(reinterpret_cast<void*>(base),address+8,code.size()),
              "foreign private allocation interior refuses");
        DWORD old=0;
        Check(VirtualProtect(const_cast<void*>(page),4096,PAGE_READWRITE,&old) &&
              !menu::FlightCaveRegion(reinterpret_cast<void*>(base),address,code.size()),
              "private non-executable data refuses");
        VirtualFree(const_cast<void*>(page),0,MEM_RELEASE);
    }
}


void ResidentProducer(const superjump::menu60::Profile& profile) {
    namespace menu=superjump::menu60;
    const auto& proof=*menu::FlightWriterFor(&profile);
    constexpr size_t imageSize=0x4610000;
    auto* image=static_cast<uint8_t*>(VirtualAlloc(nullptr,imageSize,MEM_RESERVE,PAGE_READWRITE));
    Check(image!=nullptr,"authored producer region reserves");
    if (!image) return;
    auto* producerPage=static_cast<uint8_t*>(VirtualAlloc(image+0x4603000,0x1000,MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    auto* state=static_cast<uint8_t*>(VirtualAlloc(nullptr,profile.imageSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    auto* resident=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Check(producerPage && state && resident,"authored resident movement regions commit");
    if (!producerPage || !state || !resident) return;

    auto* producer=image+MOTION_PRODUCER_RVA;
    auto* delta=image+menu::kDeltaRva;
    auto* writer=delta+8;
    const auto base=reinterpret_cast<uintptr_t>(image), module=reinterpret_cast<uintptr_t>(state);
    const auto cave=reinterpret_cast<uintptr_t>(resident), site=reinterpret_cast<uintptr_t>(writer);
    const auto relative=int64_t(cave)-int64_t(site+5);
    Check(relative>=INT32_MIN && relative<=INT32_MAX,"authored resident writer fits native E9");
    if (relative<INT32_MIN || relative>INT32_MAX) return;
    const auto disp=static_cast<int32_t>(relative);
    auto* entry=producer+sizeof(MOTION_PRODUCER);
    memcpy(producer,MOTION_PRODUCER,sizeof(MOTION_PRODUCER));
    memcpy(entry,kMotionEntry.data(),kMotionEntry.size());
    memcpy(entry+0x40,kMotionBranchTarget.data(),kMotionBranchTarget.size());
    memcpy(delta,menu::kStockDelta.data(),menu::kStockDelta.size());
    writer[0]=0xE9;
    memcpy(writer+1,&disp,sizeof(disp));
    memset(writer+5,0x90,5);
    auto code=menu::kFlightCave;
    for (size_t i=0;i<menu::kFlightCaveAddresses.size();++i) {
        const auto address=i<proof.slots.size()?module+proof.slots[i]:site+10;
        memcpy(code.data()+menu::kFlightCaveAddresses[i],&address,sizeof(address));
    }
    memcpy(resident,code.data(),code.size());
    memcpy(state+proof.published,&site,sizeof(site));
    memcpy(state+proof.patch,writer,10);
    memcpy(state+proof.target,&site,sizeof(site));
    const std::array<uint32_t,2> active{1,1};
    memcpy(state+profile.flightEnabled,active.data(),sizeof(active));

    const menu::Provider provider{module,&profile};
    g_menuOwner.Publish(provider);
    g_bossProfile.base=base;
    g_hookSite=entry;
    const menu::Reader reader{nullptr,ReadBossMemory};
    resident[157]^=1;
    Check(!menu::DeltaReady(reader,base,provider,false),
          "unknown resident writer refuses the passive producer contract");
    resident[157]^=1;
    Check(MotionSiteMatches(entry) && menu::DeltaReady(reader,base,provider,false),
          "verified active resident flight permits a passive native producer");
    Check(!InstallHook() && !g_motionHook && MotionSiteMatches(entry) &&
          !memcmp(writer,state+proof.patch,10) && !memcmp(resident,code.data(),code.size()),
          "private fixture refuses publication and preserves all producer and resident bytes");
    Check(!menu::DeltaReady(reader,base,provider,true),"actual active resident route still refuses boost delivery");
    const auto start=std::chrono::steady_clock::now();
    unsigned accepted=0;
    constexpr unsigned samples=10000;
    for (unsigned i=0;i<samples;++i) accepted+=menu::DeltaReady(reader,base,provider,true)?1:0;
    const auto activeTime=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/samples;
    Check(accepted==0,"active refusal remains stable across samples");
    const std::array<uint32_t,2> idle{(&profile==&menu::kMenu601 || &profile==&menu::kMenu604 || profile.adaptive) ? 1u : 0u,0u};
    memcpy(state+profile.flightEnabled,idle.data(),sizeof(idle));
    const std::array<uint32_t,4> unity{0x3F800000,0x3F800000,0x3F800000,0x3F800000};
    memcpy(state+proof.slots[2],unity.data(),sizeof(unity));
    const uint32_t upper=0x3DCCCCCD;
    memcpy(state+proof.slots[5]+4,&upper,sizeof(upper));
    const auto offStart=std::chrono::steady_clock::now();
    for (unsigned i=0;i<samples;++i) accepted+=menu::DeltaReady(reader,base,provider,true)?1:0;
    const auto inactiveTime=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-offStart).count()/samples;
    Check(accepted==samples,"real Windows memory policy admits the same writer only when neutral");
    std::printf("resident DELTA guard 0x%08X mean active_us=%.3f inactive_us=%.3f samples=%u passive_data=1\n",
                profile.timestamp,activeTime,inactiveTime,samples);
    g_menuOwner.Publish({});
    g_bossProfile={};
    g_hookSite=nullptr;
    VirtualFree(resident,0,MEM_RELEASE);
    VirtualFree(state,0,MEM_RELEASE);
    VirtualFree(image,0,MEM_RELEASE);
}

void Movement(const superjump::menu60::Profile& profile) {
    namespace menu=superjump::menu60;
    guardfixture::Fixture f;
    const auto entry=f.p.base+menu::kStepper;
    const uintptr_t owner=0x153000000, bridge=0x149000000;
    const menu::Provider provider{owner,&profile};
    f.Put(owner+profile.stepperOriginal,bridge);
    f.Put(owner+profile.stepperTarget,entry);
    f.Put(owner+profile.flightEnabled,uint32_t{0});
    f.Put(owner+profile.flightEnabled+sizeof(uint32_t),uint32_t{0});
    f.Put(owner+profile.jumpEnabled,uint32_t{0});
    auto entryPatch=menu::kStepperPrefix;
    entryPatch[0]=0xE9;
    const auto displacement=static_cast<int32_t>(bridge+menu::kRelayOffset-(entry+5));
    memcpy(entryPatch.data()+1,&displacement,sizeof(displacement));
    menu::StepperBridge stub{};
    memcpy(stub.data(),menu::kStepperPrefix.data(),menu::kStepperPrefix.size());
    const auto writeJump=[&](size_t at,uintptr_t to) {
        memcpy(stub.data()+at,menu::kAbsoluteJump.data(),menu::kAbsoluteJump.size());
        memcpy(stub.data()+at+menu::kAbsoluteJump.size(),&to,sizeof(to));
    };
    writeJump(menu::kStepperPrefix.size(),entry+menu::kStepperPrefix.size());
    writeJump(menu::kRelayOffset,owner+profile.stepperHandler);
    f.Put(entry,entryPatch);
    f.Put(bridge,stub);
    const auto accept=[](void*,uintptr_t at,size_t n) {return at==0x149000000 && n==menu::StepperBridge{}.size();};
    menu::TraversalView view{f.Reader(),f.Reader(),entry,provider,accept};
    std::array<uint8_t,7> bytes{};
    const auto read=[&]{return menu::TraversalView::Read(&view,entry,bytes.data(),bytes.size());};
    Check(read() && bytes==menu::kStepperPrefix,"exact Menu bridge restores stock bytes only in read copy");
    Check(f.Reader().Get(entry,bytes) && bytes==entryPatch,"Menu entry bytes stay owned by Menu");
    f.Put<uint8_t>(bridge+menu::kRelayOffset,0x90);
    Check(!read(),"wrong Menu relay opcode fails closed");
    f.Put(bridge,stub);
    f.Put(owner+profile.stepperTarget,entry+1);
    Check(!read(),"wrong Menu target slot fails closed");
    f.Put(owner+profile.stepperTarget,entry);
    f.changing=entry; f.tear=true;
    Check(!read(),"racing entry snapshot fails closed");
    f.changing=0; f.Put(entry,entryPatch);
    const auto locomotion=f.p.base+menu::kLocomotion;
    const uintptr_t locomotionBridge=0x149000100;
    const auto acceptPair=[](void*,uintptr_t at,size_t n) {
        return (at==0x149000000 || at==0x149000100) && n==menu::StepperBridge{}.size();
    };
    auto locomotionPatch=menu::kLocomotionPrefix;
    locomotionPatch[0]=0xE9;
    const auto locomotionDisp=static_cast<int32_t>(locomotionBridge+menu::kRelayOffset-(locomotion+5));
    memcpy(locomotionPatch.data()+1,&locomotionDisp,sizeof(locomotionDisp));
    f.Put(locomotion,locomotionPatch);
    memcpy(stub.data(),menu::kLocomotionPrefix.data(),menu::kLocomotionPrefix.size());
    writeJump(menu::kLocomotionPrefix.size(),locomotion+menu::kLocomotionPrefix.size());
    writeJump(menu::kRelayOffset,owner+profile.locomotionHandler);
    f.Put(locomotionBridge,stub);
    f.Put(owner+profile.locomotionOriginal,locomotionBridge);
    f.Put(owner+profile.locomotionTarget,locomotion);
    f.Put(owner+profile.movementReady,std::array<uint8_t,2>{1,1});
    Check(menu::LocomotionReady(f.Reader(),f.p.base,provider,acceptPair),"exact published Menu movement pair passes");
    f.Put(owner+profile.movementReady,std::array<uint8_t,2>{1,0});
    Check(!menu::LocomotionReady(f.Reader(),f.p.base,provider,acceptPair),"partial Menu movement publication refuses");
    f.Put(owner+profile.movementReady,std::array<uint8_t,2>{1,1});
    f.Put<uint8_t>(locomotionBridge+menu::kLocomotionPrefix.size()+6,0);
    Check(!menu::LocomotionReady(f.Reader(),f.p.base,provider,acceptPair),"altered locomotion jump-back refuses");
    f.Put(locomotionBridge,stub);
    f.Put(owner+profile.locomotionTarget,locomotion+1);
    Check(!menu::LocomotionReady(f.Reader(),f.p.base,provider,acceptPair),"altered locomotion target slot refuses");
    f.Put(owner+profile.locomotionTarget,locomotion);
    const auto& other=&profile==&menu::kEarlier ? menu::kUpdated : menu::kEarlier;
    Check(!menu::LocomotionReady(f.Reader(),f.p.base,{owner,&other},acceptPair),"other Menu build's slots cannot admit this detour");
    f.Put(owner+profile.movementReady,std::array<uint8_t,2>{0,0});
    f.Put(locomotion,menu::kLocomotionPrefix);
    f.Put(entry,menu::kStepperPrefix);
    Check(menu::LocomotionReady(f.Reader(),f.p.base,{1,nullptr},acceptPair),
          "foreign module identity alone does not block stock movement");
    f.changing=locomotion; f.reads=0; f.tear=true;
    Check(!menu::LocomotionReady(f.Reader(),f.p.base,provider,acceptPair),"racing stock movement patch refuses");
    f.changing=0; f.tear=false;


#ifndef THIRD_VARIANT
    g_bossProfile=f.p;g_traversalProfileValid=true;g_testGuardReader=f.Reader();
    g_testForeground=1;g_traversalEpoch={};g_enabled=true;g_menuOwner.Publish(provider);
    Check(!EvaluateBoostEligibility().blocked,"idle Menu and grounded typed player can establish eligibility");
    f.Ground(false);
    g_boostY=40;g_boostFrames=5;
    Check(QueueBoost(),"standalone boost queues with Menu features inactive");
    if (&profile==&menu::kUpdated) {
        f.Put(owner+profile.flightEnabled,uint32_t{1});
        Check(!MenuAllowsBoost(f.Reader()),"updated Menu can arm flight and blocks boosts as soon as enabled");
        f.Put(owner+profile.flightEnabled,uint32_t{0});
    }
    f.Put(owner+profile.flightEnabled+sizeof(uint32_t),uint32_t{1});
    float delta[4]{0,2,0,0};
    ConsumeBoostFrame(delta,f.body,f.body+0x90);
    Check(delta[1]==2 && !g_boostRemaining,"engaged Menu flight cancels a queued boost before delta write");
    f.Put(owner+profile.flightEnabled+sizeof(uint32_t),uint32_t{0});
    Check(EvaluateBoostEligibility().blocked,"airborne flight exit requires new grounded idle");
    f.Ground(true);
    f.Put(owner+profile.jumpEnabled,uint32_t{1});
    if (&profile==&menu::kMenu601 || &profile==&menu::kMenu604 || profile.adaptive) {
        f.Put(owner+profile.flightEnabled,uint32_t{1});
        Check(MenuAllowsBoost(f.Reader()),"6.0.1 parked flight and embedded jump jointly allow standalone eligibility");
    }
    Check(!EvaluateBoostEligibility().blocked,"embedded Menu jump can coexist after flight ends and the player lands");
    ConsumeBoostFrame(delta,f.body,f.body+0x90);
    Check(delta[1]==2,"ordinary Menu jump delta passes without a standalone request");
    Check(QueueBoost(),"deliberate standalone request queues while Menu jump is enabled");
    ConsumeBoostFrame(delta,f.body,f.body+0x90);
    Check(delta[1]==40,"standalone request applies configured absolute delta without multiplying a Menu-scaled frame");
    if (&profile==&menu::kMenu601 || &profile==&menu::kMenu604 || profile.adaptive) {
        f.Put(owner+profile.flightEnabled+4,uint32_t{1});
        delta[1]=2;
        ConsumeBoostFrame(delta,f.body,f.body+0x90);
        Check(delta[1]==2 && !g_boostRemaining,"flight engagement with both jump options on cancels remaining boost frames");
        f.Put(owner+profile.flightEnabled+4,uint32_t{0});
    }

    g_boostRemaining=0;
    f.Put(owner+profile.jumpEnabled,uint32_t{2});
    delta[1]=2;
    Check(EvaluateBoostEligibility().blocked && !QueueBoost(),"invalid Menu jump state cannot authorize a boost");
    f.Put(owner+profile.jumpEnabled,uint32_t{1});
    Check(QueueBoost(),"stable valid flag requalifies on grounded player");
    f.changing=owner+profile.jumpEnabled; f.reads=0; f.tear=true;
    ConsumeBoostFrame(delta,f.body,f.body+0x90);
    Check(delta[1]==2 && !g_boostRemaining,"changing Menu jump state cancels pending delivery");
    f.changing=0; f.tear=false;
    f.Put(owner+profile.jumpEnabled,uint32_t{0});
    g_menuOwner.Publish({1,nullptr});
    Check(!MenuAllowsBoost(f.Reader()),"unverified Menu blocks boost");
    g_menuOwner.Publish({});
    Check(MenuAllowsBoost(f.Reader()),"no Menu uses standalone guards");
#endif
}
}

int main() {
    SetPriorityClass(GetCurrentProcess(),BELOW_NORMAL_PRIORITY_CLASS);
    Producer();
    Replay();
    ForegroundPolling();
    ResidentFlight(superjump::menu60::kUpdated);
    ResidentFlight(superjump::menu60::kMenu601);
    ResidentFlight(superjump::menu60::kMenu604);
    ResidentProducer(superjump::menu60::kUpdated);
    ResidentProducer(superjump::menu60::kMenu601);
    ResidentProducer(superjump::menu60::kMenu604);
    Movement(superjump::menu60::kEarlier);
    Movement(superjump::menu60::kUpdated);
    Movement(superjump::menu60::kMenu601);
    Movement(superjump::menu60::kMenu604);
    superjump::menu60::AdaptiveLayout layout{};
    layout.flight=superjump::menu60::kMenu604Writer;
    auto discovered=superjump::menu60::kMenu604;
    discovered.timestamp=0xA0920731;
    discovered.adaptive=&layout;
    ResidentFlight(discovered);
    ResidentProducer(discovered);
    Movement(discovered);
    std::printf("Menu60 integration checks=%u failures=%u authoredOnly=1 gameOrForeignAsiExecuted=0\n",checks,failures);
    return failures ? 1 : 0;
}

