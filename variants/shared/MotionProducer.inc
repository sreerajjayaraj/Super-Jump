#pragma once

static bool MotionSiteMatches(const uint8_t* site) {
    return !memcmp(site-sizeof(MOTION_PRODUCER),MOTION_PRODUCER,sizeof(MOTION_PRODUCER)) &&
           !memcmp(site,kMotionEntry.data(),kMotionEntry.size()) &&
           !memcmp(site+0x40,kMotionBranchTarget.data(),kMotionBranchTarget.size());
}

static bool IsProducerContract(const uint8_t* site) {
    const auto base=g_bossProfile.base;
    const auto target=reinterpret_cast<uintptr_t>(site);
    const auto game=reinterpret_cast<HMODULE>(base);
    return target==base+MOTION_PRODUCER_RVA+sizeof(MOTION_PRODUCER) &&
           !IsFreedomFlyerLoaded() &&
           superjump::host::IsGameCode(game,target-sizeof(MOTION_PRODUCER),
               sizeof(MOTION_PRODUCER)+0x40+kMotionBranchTarget.size()) &&
           MotionSiteMatches(site) &&
           superjump::menu60::DeltaReady({nullptr,ReadBossMemory},base,g_menuOwner.Load(),false);
}

static void OnMotionFrame(safetyhook::Context& ctx) {
    if (!ctx.rbx || ctx.rbx!=g_currentFrameBody.load(std::memory_order_acquire)
        || ctx.r13!=ctx.rbx+0x90) return;

    const auto mxcsr=_mm_getcsr();
    ConsumeBoostFrame(ctx.xmm6.f32,ctx.rbx,ctx.r13);
    _mm_setcsr(mxcsr);
}

static bool MotionTrampolineMatches(const safetyhook::MidHook& hook, uintptr_t site) {
    const auto& original=hook.original_bytes();
    if (original.size()!=kMotionEntry.size() ||
        !std::equal(original.begin(),original.end(),kMotionEntry.begin())) return false;

    // SafetyHook widens the relocated JE and replays the fallthrough movaps.
    const auto address=hook.owned_trampoline_address();
    uint8_t code[14]{};
    if (!address || !ReadBossMemory(nullptr,address,code,sizeof(code))) return false;
    int32_t taken=0,resume=0;
    memcpy(&taken,code+2,sizeof(taken));
    memcpy(&resume,code+10,sizeof(resume));
    return code[0]==0x0F && code[1]==0x84 &&
           address+6+taken==site+0x40 &&
           !memcmp(code+6,kMotionEntry.data()+2,3) && code[9]==0xE9 &&
           address+14+resume==site+kMotionEntry.size();
}

static bool InstallHook() {
    if (g_motionHook || !IsProducerContract(g_hookSite) || !EnsureOwnPinned()) return false;
    auto prepared=safetyhook::MidHook::create(g_hookSite,OnMotionFrame,safetyhook::MidHook::StartDisabled);
    if (!prepared) {
        LogFmt("ERROR: motion SafetyHook preparation refused (%u)",unsigned(prepared.error().type));
        return false;
    }
    if (!MotionTrampolineMatches(*prepared,reinterpret_cast<uintptr_t>(g_hookSite)) ||
        !IsProducerContract(g_hookSite)) {
        Log("ERROR: motion SafetyHook branch contract refused");
        return false;
    }
    auto* hook=new(std::nothrow) safetyhook::MidHook(std::move(*prepared));
    if (!hook) return false;
    // Keep the hook alive for the process even if publication is interrupted.
    g_motionHook=hook;
    if (!hook->enable()) return false;
    LogFmt("motion SafetyHook installed in CrimsonDesert.exe at +%X",MOTION_PRODUCER_RVA+unsigned(sizeof(MOTION_PRODUCER)));
    return true;
}
