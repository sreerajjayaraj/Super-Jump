// SuperJump.cpp ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â Crimson Desert ASI mod  v1.3
// Hold Shift + C + press Space while stationary -> upward launch
// Hooks the physics DELTA accumulator to inject vertical velocity
// Attempts crouch animation via key simulation
//
// v1.3 (2026-05-16):
//   * Stationary gate now reads the actor's velocity vector directly from
//     [actor+0xC0] (X) and [actor+0xC8] (Z) instead of relying on the cave-
//     published g_lastDelta. Empirically confirmed in Miner1.log: these
//     fields are 0 while standing still and ramp to ~6 m/s on sprint.
//     New INI key: VelThresh (default 0.3 m/s). Old StationaryThresh is
//     kept for backwards-compat but no longer used.
//   * The cave hook is unchanged ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â it still publishes g_lastDelta and
//     handles the boost write. Only the trigger-side check moved.
//
// v1.2 (Nexus release):
//   * Adds HotkeyMod2 (default C, 0x43) ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â a second modifier key that must be
//     held alongside HotkeyMod for the chord to fire. Set HotkeyMod2=0 in the
//     INI to disable the second modifier and fall back to v1.1 behaviour.
//
// The current Third Palm variant owns the pre-collision motion producer.
// DesertLink True Flight owns its separate downstream DELTA writer.
// Build: x64 DLL, rename to SuperJump.asi, drop in bin64/
// Targets CrimsonDesert.exe v1.0.0.1670

#define WIN32_LEAN_AND_MEAN
#include "SuperJumpBossNativeProfile.h"
#include "SuperJumpContextGuard.h"
#include <windows.h>
#include <array>
#include <bit>
#include <xinput.h>
#include <tlhelp32.h>
#include <winternl.h>     // PEB struct for kernel-tracked image base
#include <intrin.h>       // __readgsqword
#include <cstdint>
#include <cstring>
#include <cmath>
#include <atomic>
#include <thread>
#include <fstream>
#include <string>

#include "SuperJumpHookPolicy.h"
#include "SuperJumpInputPolicy.h"
#include "SuperJumpGameDoubleJumpPolicy.h"
#include "SuperJumpAxiomGuardPolicy.h"
#include "SuperJumpTraversalNativeProfile.h"
static bool g_nativeMovementChecksReady=false;
#include "SuperJumpHostPolicy.h"
#include "SuperJumpActivationPolicy.h"
#include "PalmBinding.h"
#include "PalmCollector.h"
#include "safetyhook/mid_hook.hpp"

struct ForegroundSnapshot {
    uintptr_t window=0;
    DWORD owner=0,thread=0;
    unsigned reason=1;
    bool focused=false;
};
#ifdef SUPERJUMP_TESTING
static int g_testForeground=-1;
#endif
static ForegroundSnapshot ReadForegroundWindow() {
    ForegroundSnapshot out{};
    const HWND window=GetForegroundWindow();
    out.window=reinterpret_cast<uintptr_t>(window);
    if(!window){out.reason=2;return out;}
    out.thread=GetWindowThreadProcessId(window,&out.owner);
    if(!out.thread || !out.owner){out.reason=3;return out;}
    out.focused=out.owner==GetCurrentProcessId() && GetForegroundWindow()==window;
    out.reason=out.focused?0U:4U;
    return out;
}

static bool GameHasForeground() {
#ifdef SUPERJUMP_TESTING
    if (g_testForeground>=0) return g_testForeground!=0;
#endif
    return ReadForegroundWindow().focused;
}

namespace cdinput {
using GetAsyncKeyStateFn = SHORT (WINAPI*)(int);
static GetAsyncKeyStateFn ResolveGetAsyncKeyState() {
    static HMODULE user32 = LoadLibraryA("user32.dll");
    static GetAsyncKeyStateFn fn = user32
        ? (GetAsyncKeyStateFn)GetProcAddress(user32, "GetAsyncKeyState")
        : nullptr;
    return fn;
}

static bool KbDown(int vk) {
    if (!vk || !GameHasForeground()) return false;
    const auto getAsyncKeyState = ResolveGetAsyncKeyState();
    return getAsyncKeyState && GameHasForeground() && (getAsyncKeyState(vk) & 0x8000) != 0;
}

static uint16_t XInputButtons(
    uint8_t* connectedMaskOut = nullptr,
    uint8_t* providerMaskOut = nullptr) {
    if (connectedMaskOut) *connectedMaskOut = 0;
    if (providerMaskOut) *providerMaskOut = 0;
    if (!GameHasForeground()) return 0;
    using XInputGetStateFn = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);
    static std::array<HMODULE, 3> modules{};
    static std::array<XInputGetStateFn, 3> providers{};
    static std::array<bool, 3> resolved{};

    for (size_t providerIndex = 0; providerIndex < providers.size(); ++providerIndex) {
        if (!resolved[providerIndex]) {
            resolved[providerIndex] = true;
            const char* name = superjump::PreferredXInputProviderName(providerIndex);
            HMODULE module = name ? GetModuleHandleA(name) : nullptr;
            if (!module && name) module = LoadLibraryA(name);
            modules[providerIndex] = module;
            providers[providerIndex] = module
                ? reinterpret_cast<XInputGetStateFn>(
                    GetProcAddress(module, "XInputGetState"))
                : nullptr;
        }

        const XInputGetStateFn getState = providers[providerIndex];
        if (!getState) continue;

        uint16_t providerButtons = 0;
        uint8_t providerConnectedMask = 0;
        for (DWORD slotIndex = 0; slotIndex < XUSER_MAX_COUNT; ++slotIndex) {
            if (!GameHasForeground()) return 0;
            XINPUT_STATE state = {};
            const bool connected =
                getState(slotIndex, &state) == ERROR_SUCCESS;
            superjump::AccumulateXInputSlot(
                connected,
                state.Gamepad.wButtons,
                static_cast<uint8_t>(slotIndex),
                providerButtons,
                providerConnectedMask);
        }
        if (!GameHasForeground()) return 0;
        if (providerConnectedMask != 0) {
            if (connectedMaskOut) *connectedMaskOut = providerConnectedMask;
            if (providerMaskOut) {
                *providerMaskOut = static_cast<uint8_t>(1u << providerIndex);
            }
            return providerButtons;
        }
    }
    if (connectedMaskOut) *connectedMaskOut = 0;
    if (providerMaskOut) *providerMaskOut = 0;
    return 0;
}

[[maybe_unused]] static bool XInputAllHeld(uint16_t mask) {
    if (!mask) return false;
    uint16_t buttons = XInputButtons();
    return (buttons & mask) == mask;
}
}

using GetCursorInfoFn = BOOL (WINAPI*)(PCURSORINFO);
static GetCursorInfoFn ResolveGetCursorInfo() {
    static HMODULE user32 = LoadLibraryA("user32.dll");
    static GetCursorInfoFn fn = user32
        ? (GetCursorInfoFn)GetProcAddress(user32, "GetCursorInfo")
        : nullptr;
    return fn;
}


// ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ Config ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬
static float    g_boostY            = 40.0f;
static int      g_boostFrames       = 5;
static float    g_stationaryThresh  = 0.04f;   // legacy v1.2 (delta-based, no longer used)
static float    g_velThresh         = 0.3f;    // v1.3: horizontal velocity gate (m/s)
static bool     g_requireStationary = true;
static int      g_hotkeyMod         = VK_SHIFT;
static int      g_hotkeyMod2        = 0x43;   // 'C'  (set to 0 to disable second modifier)
static int      g_hotkeyKey         = VK_SPACE;
// XInput binding: LB (0x0100) modifier + A (0x1000) trigger.
// Controller activation requires a clean intentional hold. Short interaction
// taps and follow-up menu/navigation buttons start a temporary lockout.
static uint16_t g_xiBtn             = 0x1000;  // A button
static uint16_t g_xiMod             = 0x0100;  // LB modifier
static int      g_controllerHoldMs  = 1500;
static int      g_controllerLockoutMs = 4000;
static int      g_controllerIdleMs  = 500;
static int      g_initDelayMs       = 3000;
static bool     g_inputDiagnostics   = true;
static bool     g_blockCursorUi     = true;
static std::atomic<bool> g_enabled{true};
static bool     g_animCrouch        = true;
static int      g_crouchKey         = VK_CONTROL;
static int      g_crouchDurationMs  = 120;
static int      g_triggerMode       = 2;      // 0=ChordHold, 1=InputDoubleJump, 2=GameDoubleJump
static int      g_jumpKey           = VK_SPACE;
static uint16_t g_xiJumpBtn         = 0x4000; // XInput X button, PlayStation Square via Xbox mapping
static int      g_doubleJumpWindowMs = 900;
static int      g_doubleJumpFireDelayMs = 0;
static int      g_doubleJumpCooldownMs = 900;
static bool     g_requireAirborneSecondPress = true;
static bool     g_requireGroundedFirstPress = true;
static float    g_airborneDeltaYMin = 0.015f;
static int      g_firstPressAirborneGraceMs = 250;
static float    g_gameJumpImpulseY  = 0.20f;
static float    g_gameJumpResetY    = 0.05f;
static int      g_gameDoubleJumpMinMs = 80;
static int      g_gameDoubleJumpMaxMs = 900;
static bool     g_axiomGuardEnabled = true;
static int      g_axiomKey          = VK_TAB;
static uint16_t g_xiAxiomButton     = XINPUT_GAMEPAD_LEFT_THUMB;
static uint16_t g_xiAxiomExcludeButtons = XINPUT_GAMEPAD_RIGHT_THUMB;
static int      g_axiomActivationHoldMs = 120;
static int      g_axiomLatchTimeoutMs = 12000;
static int      g_axiomFirstJumpWindowMs = 1500;

// ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ Game addresses (v1.06.00) ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬
// AOB-resolved at install time. Default is v1.06.00; overwritten on first
// successful CD_Player AOB hit (see InstallHooks / FindHookSite).
static uintptr_t g_playerStaticVA = 0x145F27F60ULL;

// DELTA hook bytes: MOVAPS xmm0,xmm6 / SUBSS xmm9,xmm8 / ADDPS xmm0,[r13] / MOVUPS [r13],xmm0
static constexpr uint32_t MOTION_PRODUCER_RVA = 0x460331A;
static constexpr uint8_t MOTION_PRODUCER[] = {
    0x83,0x7C,0x24,0x30,0x02, // cmp dword ptr [rsp+30h],2
    0x0F,0x28,0x75,0x90       // movaps xmm6,[rbp-70h]
};
// The short JE runs after the native XMM6 load. Its fallthrough starts
// with movaps xmm0,xmm6, so SafetyHook should displace exactly five bytes.
static constexpr std::array<uint8_t,5> kMotionEntry{0x74,0x3E,0x0F,0x28,0xC6};
static constexpr std::array<uint8_t,7> kMotionBranchTarget{0x48,0x8B,0x8D,0xD0,0x03,0x00,0x00};
struct alignas(16) Vec4f { float x, y, z, w; };
static volatile Vec4f    g_lastDelta      = {};
static volatile int32_t  g_boostRemaining = 0;
// The worker publishes context through InterlockedExchange.
static volatile LONG g_contextBlocked = 1;
static superjump::boss::Profile g_bossProfile{};

static SRWLOCK g_eligibilityLock=SRWLOCK_INIT;
static superjump::traversal::Epoch g_traversalEpoch{};
static superjump::traversal::Epoch g_queuedEpoch{};
static LONG g_queuedGeneration=0;
static uintptr_t g_deltaBody=0;
static LONG g_deltaGeneration=0;
static std::atomic<uintptr_t> g_currentFrameBody{0};
static bool g_traversalProfileValid=false;
static volatile LONG g_cancelGeneration=0;
static volatile LONG g_epochInvalidateRequested=0;
static HANDLE g_variantMutex=nullptr;
// All native hook objects remain pinned for process lifetime, including failure.
static superjump::palm::Collector g_palmCollector{};
static constexpr uintptr_t kUnknownFlightOwner=1;
// 0: no Menu at startup, 1: Menu ownership unproved, otherwise exact retained state.
static superjump::menu60::PublishedProvider g_menuOwner{};
static superjump::palm::Policy g_palmPolicy{};
static std::atomic<uint32_t> g_currentHero{0};
static uint32_t g_queuedHero=0;
static uint64_t g_firstFrameDeadline=0,g_queuedCollectorEpoch=0;
static uintptr_t g_queuedPalmMetadata=0;
static uint64_t g_seenCollectorEpoch=0,g_seenCollectorDrops=0;
static LONG g_palmWorkerGeneration=0;
static unsigned g_palmFireLogs=0,g_palmPacketLogs=0;

#ifdef SUPERJUMP_TESTING
static superjump::boss::Reader g_testGuardReader{};
#endif
static bool AcquireVariantLease() {
    wchar_t name[96]{};
    swprintf_s(name,L"Local\\CrimsonDesert.SuperJump.NormalVariants.%lu",GetCurrentProcessId());
    SetLastError(ERROR_SUCCESS);
    HANDLE lease=CreateMutexW(nullptr,FALSE,name);
    if(!lease)return false;
    if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(lease);return false;}
    g_variantMutex=lease;return true;
}
static void ConsumeBoostFrame(float* delta,uintptr_t nativeBody,uintptr_t nativeDestination);
static std::atomic<uint32_t> g_boostYBits{0};

// ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ Hook state ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬
static uint8_t* g_hookSite  = nullptr;
static safetyhook::MidHook* g_motionHook = nullptr;
static bool     g_startedInGame = false;
static std::atomic<bool> g_ownPinned{false};
static HMODULE g_ownModule=nullptr;
using OwnPinApi=BOOL(WINAPI*)(DWORD,LPCWSTR,HMODULE*);
#ifdef SUPERJUMP_TESTING
static OwnPinApi g_testOwnPinApi=nullptr;
static int g_testHostAcceptance=-1;
[[maybe_unused]] static void SetOwnPinApiForTests(OwnPinApi fn){g_testOwnPinApi=fn;g_ownPinned.store(false);g_ownModule=nullptr;}
#endif
static bool EnsureOwnPinned(){
 if(g_ownPinned.load(std::memory_order_acquire))return true;
 const auto address=reinterpret_cast<LPCWSTR>(&EnsureOwnPinned);
 HMODULE actual=nullptr;
 if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,address,&actual)||!actual||(g_ownModule&&actual!=g_ownModule))return false;
 HMODULE pinned=nullptr;
 OwnPinApi pin=GetModuleHandleExW;
#ifdef SUPERJUMP_TESTING
 if(g_testOwnPinApi)pin=g_testOwnPinApi;
#endif
 if(!pin(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,address,&pinned)||pinned!=actual)return false;
 g_ownPinned.store(true,std::memory_order_release);return true;
}

// ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ Log ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬
static std::ofstream g_log;
static void Log(const char* msg) {
    if (g_log.is_open()) { g_log << "SuperJump: " << msg << "\n"; g_log.flush(); }
}
static void LogFmt(const char* fmt, ...) {
    char buf[512]; va_list ap; va_start(ap,fmt); vsnprintf(buf,512,fmt,ap); va_end(ap); Log(buf);
}

static bool IsCrimsonDesertProcess() {
#ifdef SUPERJUMP_TESTING
    if(g_testHostAcceptance>=0)return g_testHostAcceptance!=0;
#endif
    return superjump::host::GameExecutable()!=nullptr;
}

// ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ INI ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬
static bool IsFreedomFlyerLoaded() {
    return GetModuleHandleW(L"FreedomFlyer.asi") ||
           GetModuleHandleW(L"FreedomFlyer.dll");
}

static std::string g_iniPath;
static float IniFloat(const char* k, float d) {
    char b[64]={}; GetPrivateProfileStringA("SuperJump",k,"",b,64,g_iniPath.c_str());
    return b[0]?(float)atof(b):d;
}
static int IniInt(const char* k, int d) {
    return GetPrivateProfileIntA("SuperJump",k,d,g_iniPath.c_str());
}
static std::string IniString(const char* k, const char* d) {
    char b[96] = {};
    GetPrivateProfileStringA("SuperJump", k, d ? d : "", b, sizeof(b), g_iniPath.c_str());
    return std::string(b);
}
static void LoadConfig() {
    g_boostY            = IniFloat("BoostY",           40.0f);
    g_boostFrames       = IniInt  ("BoostFrames",      5);
    g_stationaryThresh  = IniFloat("StationaryThresh", 0.04f);
    g_velThresh         = IniFloat("VelThresh",        0.3f);
    g_requireStationary = !!IniInt("RequireStationary",1);
    g_hotkeyMod         = IniInt  ("HotkeyMod",        VK_SHIFT);
    g_hotkeyMod2        = IniInt  ("HotkeyMod2",       0x43);
    g_hotkeyKey         = IniInt  ("HotkeyKey",        VK_SPACE);
    g_xiBtn             = (uint16_t)IniInt("ControllerButton",   0x1000);
    g_xiMod             = (uint16_t)IniInt("ControllerModifier", 0x0100);
    g_controllerHoldMs  = IniInt  ("ControllerHoldMs", 1500);
    g_controllerLockoutMs = IniInt("ControllerInteractionLockoutMs", 4000);
    g_controllerIdleMs  = IniInt  ("ControllerIdleMs", 500);
    g_initDelayMs       = IniInt  ("InitDelayMs",      3000);
    g_inputDiagnostics  = !!IniInt("InputDiagnostics", 1);
    g_blockCursorUi     = !!IniInt("ControllerBlockCursorUi", 1);
    g_enabled           = !!IniInt("Enabled",          1);
    g_animCrouch        = !!IniInt("AnimCrouch",       1);
    g_crouchKey         = IniInt  ("CrouchKey",        VK_CONTROL);
    g_crouchDurationMs  = IniInt  ("CrouchDurationMs", 120);
    std::string mode    = IniString("TriggerMode", "GameDoubleJump");
    if (_stricmp(mode.c_str(), "GameDoubleJump") == 0 ||
        _stricmp(mode.c_str(), "AutoDoubleJump") == 0) {
        g_triggerMode = 2;
    } else if (_stricmp(mode.c_str(), "InputDoubleJump") == 0 ||
               _stricmp(mode.c_str(), "DoubleJump") == 0 ||
               _stricmp(mode.c_str(), "DoublePress") == 0) {
        g_triggerMode = 1;
    } else {
        g_triggerMode = 0;
    }
    g_jumpKey           = IniInt  ("JumpKey",           VK_SPACE);
    g_xiJumpBtn         = (uint16_t)IniInt("ControllerJumpButton", 0x4000);
    g_doubleJumpWindowMs = IniInt ("DoubleJumpWindowMs", 900);
    g_doubleJumpFireDelayMs = IniInt("DoubleJumpFireDelayMs", 0);
    g_doubleJumpCooldownMs = IniInt("DoubleJumpCooldownMs", 900);
    g_requireAirborneSecondPress = !!IniInt("RequireAirborneSecondPress", 1);
    g_requireGroundedFirstPress = !!IniInt("RequireGroundedFirstPress", 1);
    g_airborneDeltaYMin = IniFloat("AirborneDeltaYMin", 0.015f);
    g_firstPressAirborneGraceMs = IniInt("FirstPressAirborneGraceMs", 250);
    g_gameJumpImpulseY  = IniFloat("GameJumpImpulseY", 0.20f);
    g_gameJumpResetY    = IniFloat("GameJumpResetY", 0.05f);
    g_gameDoubleJumpMinMs = IniInt("GameDoubleJumpMinMs", 80);
    g_gameDoubleJumpMaxMs = IniInt("GameDoubleJumpMaxMs", 900);
    g_axiomGuardEnabled = !!IniInt("AxiomGuardEnabled", 1);
    g_axiomKey          = IniInt("AxiomKey", VK_TAB);
    g_xiAxiomButton     = static_cast<uint16_t>(
        IniInt("ControllerAxiomButton", XINPUT_GAMEPAD_LEFT_THUMB));
    g_xiAxiomExcludeButtons = static_cast<uint16_t>(
        IniInt("ControllerAxiomExcludeButtons", XINPUT_GAMEPAD_RIGHT_THUMB));
    g_axiomActivationHoldMs = IniInt("AxiomActivationHoldMs", 120);
    g_axiomLatchTimeoutMs = IniInt("AxiomLatchTimeoutMs", 12000);
    g_axiomFirstJumpWindowMs = IniInt("AxiomFirstJumpWindowMs", 1500);
    g_boostYBits        = *reinterpret_cast<const uint32_t*>(&g_boostY);
}

// ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ SEH-wrapped scalar reads ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬
// Kept as separate leaf functions because MSVC /EHsc rejects __try in any
// function that needs C++ object unwinding ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â ModThread spawns std::thread,
// so the SEH-deref has to live in a function without C++ destructors.
[[maybe_unused]] static uintptr_t SafeReadPtr(uintptr_t addr, bool* okOut) {
    __try {
        uintptr_t v = *reinterpret_cast<volatile uintptr_t*>(addr);
        if (okOut) *okOut = true;
        return v;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if (okOut) *okOut = false;
        return 0;
    }
}
[[maybe_unused]] static float SafeReadF32(uintptr_t addr, bool* okOut) {
    __try {
        float v = *reinterpret_cast<volatile float*>(addr);
        if (okOut) *okOut = true;
        return v;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if (okOut) *okOut = false;
        return 0.0f;
    }
}

// ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ Find game module base ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬
static uint8_t* GetGameBase() {
    return reinterpret_cast<uint8_t*>(superjump::host::GameExecutable());
}

static bool ReadBossMemory(void*,uintptr_t,void*,size_t);

static bool IsProducerContract(const uint8_t* site) {
    const auto base=g_bossProfile.base;
    const auto target=reinterpret_cast<uintptr_t>(site);
    const auto game=reinterpret_cast<HMODULE>(base);
    if (target!=base+MOTION_PRODUCER_RVA+sizeof(MOTION_PRODUCER) ||
        IsFreedomFlyerLoaded() ||
        !superjump::host::IsGameCode(game,target-sizeof(MOTION_PRODUCER),sizeof(MOTION_PRODUCER)+0x40+kMotionBranchTarget.size()))
        return false;
    return !memcmp(site-sizeof(MOTION_PRODUCER),MOTION_PRODUCER,sizeof(MOTION_PRODUCER)) &&
           !memcmp(site,kMotionEntry.data(),kMotionEntry.size()) &&
           !memcmp(site+0x40,kMotionBranchTarget.data(),kMotionBranchTarget.size()) &&
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
    if (g_motionHook || !EnsureOwnPinned() || !IsProducerContract(g_hookSite)) return false;
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

static void SimulateCrouch(UINT key, DWORD duration) {
    const auto foreground=ReadForegroundWindow();
    if (!foreground.focused) return;
    const auto gameWindow=reinterpret_cast<HWND>(foreground.window);
    const auto scan=static_cast<LPARAM>(MapVirtualKeyW(key,MAPVK_VK_TO_VSC)&0xff)<<16;
    const auto message=scan|1;
    if (GetForegroundWindow()!=gameWindow || !PostMessageW(gameWindow,WM_KEYDOWN,key,message)) return;
    Sleep(duration);
    if (DWORD owner=0; GetWindowThreadProcessId(gameWindow,&owner) && owner==GetCurrentProcessId())
        PostMessageW(gameWindow,WM_KEYUP,key,message | (static_cast<LPARAM>(3)<<30));
}

static bool CursorVisibleDiagnostic() {
    auto getCursorInfo=ResolveGetCursorInfo();if(!getCursorInfo)return false;
    CURSORINFO ci{};ci.cbSize=sizeof(ci);return getCursorInfo(&ci)&&(ci.flags&CURSOR_SHOWING);
}

// ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ Main thread ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬
static bool ReadBossMemory(void*,uintptr_t address,void* out,size_t size) {
    __try { memcpy(out,reinterpret_cast<const void*>(address),size);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool TrueFlightAllowsBoost(const superjump::boss::Reader& reader) {
    using namespace superjump::menu60;
    const auto menu=g_menuOwner.Load();
    if (menu && !FlightAllowsBoost(reader,menu)) return false;
    if (menu || !menu.module) return true;
    return LocomotionReady(reader,g_bossProfile.base,menu) &&
           DeltaReady(reader,g_bossProfile.base,menu,true);
}

static void PollFlightOwner() {
 if (!g_menuOwner.Load().module && GetModuleHandleW(L"DesertLinkModMenu.asi")) {
  g_menuOwner.Publish({kUnknownFlightOwner,nullptr});
  Log("Menu True Flight: late provider; using verified native movement sites");
 }
}

struct GuardDecision {
 superjump::boss::Result encounter{};
 superjump::context::UiResult ui{};
 superjump::traversal::Snapshot native{};
 ForegroundSnapshot foreground{};bool focused=false,cursor=false,blocked=true;
};
static superjump::boss::Reader CurrentReader(){
 superjump::boss::Reader reader{nullptr,ReadBossMemory};
#ifdef SUPERJUMP_TESTING
 if(g_testGuardReader.read)reader=g_testGuardReader;
#endif
 return reader;
}
static uint32_t ReadCurrentHero(const superjump::boss::Reader&r,const superjump::traversal::Snapshot&x){
 if(!x.valid)return 0;
 auto once=[&]()->uint32_t{
  uint16_t index=0;
  uint32_t key=0;
  superjump::boss::InfoEntry info{};
  if(!r.Get(x.status+0x30,index)||
     !superjump::boss::ReadInfoEntry(r,g_bossProfile,index,info)||
     !r.Get(info.info+8,key))return 0;
  return superjump::palm::Route(key)==superjump::palm::Mode::None?0:key;};
 const auto key=once();return key&&once()==key?key:0;
}
// Caller holds g_eligibilityLock. Both worker and frame consumer use this path.
static GuardDecision EvaluateBoostEligibility() {
 using namespace superjump;
 boss::Reader reader{nullptr,ReadBossMemory};
#ifdef SUPERJUMP_TESTING
 if(g_testGuardReader.read)reader=g_testGuardReader;
#endif
 GuardDecision d{};
 if(InterlockedExchange(&g_epochInvalidateRequested,0))g_traversalEpoch={};
 const LONG generation=InterlockedCompareExchange(&g_cancelGeneration,0,0);
 d.encounter=boss::Sample(reader,g_bossProfile);
 d.ui=context::SampleUi(reader,g_bossProfile);
 d.foreground=ReadForegroundWindow();d.focused=d.foreground.focused;d.cursor=CursorVisibleDiagnostic();
#ifdef SUPERJUMP_TESTING
 if(g_testForeground>=0)d.focused=g_testForeground!=0;
#endif
 const auto menuOwner=g_menuOwner.Load();
#ifdef SUPERJUMP_TESTING
 const bool verifyMovement=g_nativeMovementChecksReady;
#else
 constexpr bool verifyMovement=true;
#endif
 const bool motionAllowed=!verifyMovement ||
    (menu60::LocomotionReady(reader,g_bossProfile.base,menuOwner) &&
     menu60::DeltaReady(reader,g_bossProfile.base,menuOwner,true));
 const bool context_allowed=g_enabled&&motionAllowed&&TrueFlightAllowsBoost(reader)&&!context::BlocksBoost(d.ui,d.encounter,d.focused,d.cursor);
 bool traversalValid=g_traversalProfileValid;
 if(g_nativeMovementChecksReady){
  menu60::TraversalView view{reader,reader,g_bossProfile.base+menu60::kStepper,menuOwner};
  traversalValid=traversalValid &&
      traversal::native::Validate({&view,menu60::TraversalView::Read},g_bossProfile.base);
 }
 d.native=traversal::Sample(reader,g_bossProfile,traversalValid);
 const bool coherent=d.native.valid&&d.native.root==d.encounter.world_root&&d.native.context==d.encounter.context&&d.native.actor==d.encounter.player&&d.native.uid==d.encounter.player_uid;
 const uint32_t hero=coherent?ReadCurrentHero(reader,d.native):0;
 if(g_currentHero.exchange(hero)!=hero){InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_boostRemaining),0);InterlockedIncrement(&g_cancelGeneration);g_traversalEpoch={};}
 const uintptr_t currentBody=d.native.valid&&coherent&&hero?d.native.frame_body:0;
 if(g_currentFrameBody.exchange(currentBody)!=currentBody){memset((void*)&g_lastDelta,0,sizeof(g_lastDelta));g_deltaBody=0;g_deltaGeneration=0;}
 // A valid newly grounded actor still cancels every request from the old
 // actor/context. Fresh eligibility and request ownership are separate.
 if(d.native.valid&&g_traversalEpoch.actor&&!traversal::SameIdentity(g_traversalEpoch,d.native)){
  InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_boostRemaining),0);InterlockedIncrement(&g_cancelGeneration);g_traversalEpoch={};memset((void*)&g_lastDelta,0,sizeof(g_lastDelta));g_deltaBody=0;g_deltaGeneration=0;
 }
 d.blocked=!traversal::Allows(g_traversalEpoch,d.native,context_allowed&&coherent&&hero);
 if(InterlockedCompareExchange(&g_epochInvalidateRequested,0,0)||generation!=InterlockedCompareExchange(&g_cancelGeneration,0,0)){d.blocked=true;g_traversalEpoch={};}
 InterlockedExchange(&g_contextBlocked,d.blocked?1:0);
 if(d.blocked){InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_boostRemaining),0);InterlockedIncrement(&g_cancelGeneration);}
 return d;
}
struct PalmDelivery {
    uint64_t tick=0;
    uint32_t uid=0;
    LONG generation=0,remaining=0;
    float before=0.f,after=0.f;
    uint32_t hero=1;
};
static SRWLOCK g_palmDeliveryLock=SRWLOCK_INIT;
static std::array<PalmDelivery,16> g_palmDeliveries{};
static size_t g_palmDeliveryCount=0;

static void RecordPalmDelivery(const PalmDelivery& delivery) {
    if (!TryAcquireSRWLockExclusive(&g_palmDeliveryLock)) return;
    if (g_palmDeliveryCount<g_palmDeliveries.size())
        g_palmDeliveries[g_palmDeliveryCount++]=delivery;
    ReleaseSRWLockExclusive(&g_palmDeliveryLock);
}

static void DrainPalmDeliveries() {
    if (!TryAcquireSRWLockExclusive(&g_palmDeliveryLock)) return;
    const size_t count=g_palmDeliveryCount;
    decltype(g_palmDeliveries) deliveries{};
    std::copy_n(g_palmDeliveries.begin(),count,deliveries.begin());
    g_palmDeliveryCount=0;
    ReleaseSRWLockExclusive(&g_palmDeliveryLock);
    for (size_t i=0;i<count;++i) {
        const auto& event=deliveries[i];
        LogFmt("%s boost delivered tick=%llu uid=%08X generation=%ld deltaY=%.4f->%.4f framesRemaining=%ld",
               event.hero==1?"third palm":"double-jump fallback",
               static_cast<unsigned long long>(event.tick),event.uid,event.generation,
               event.before,event.after,event.remaining);
    }
}

static void ConsumeBoostFrame(float* delta,uintptr_t nativeBody,uintptr_t nativeDestination) {
 const DWORD savedLastError=GetLastError();
 // Recheck ownership after the hook callback reads the current body.
 if(!nativeBody||nativeBody!=g_currentFrameBody.load(std::memory_order_acquire)||nativeDestination!=nativeBody+0x90){SetLastError(savedLastError);return;}
 // Kliff's trigger comes exclusively from native successful actions. An idle
 // Kliff frame need not publish velocity or audit any game metadata.
 if(g_currentHero.load(std::memory_order_acquire)==1&&InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(&g_boostRemaining),0,0)<=0){SetLastError(savedLastError);return;}
 // Physics never waits for worker sampling. An idle frame can skip its
 // velocity sample without invalidating a live first-press input arm.
 // A pending write still cancels when this frame cannot validate it.
 if(!TryAcquireSRWLockExclusive(&g_eligibilityLock)){
  if(nativeBody!=g_currentFrameBody.load()||nativeDestination!=nativeBody+0x90){SetLastError(savedLastError);return;}
  if(InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(&g_boostRemaining),0,0)<=0){
   SetLastError(savedLastError);
   return;
  }
  InterlockedExchange(&g_epochInvalidateRequested,1);
  InterlockedExchange(&g_contextBlocked,1);
  InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_boostRemaining),0);
  InterlockedIncrement(&g_cancelGeneration);SetLastError(savedLastError);return;
 }
 const auto decision=EvaluateBoostEligibility();
 if(!decision.native.valid||nativeBody!=decision.native.frame_body||nativeDestination!=nativeBody+0x90){ReleaseSRWLockExclusive(&g_eligibilityLock);SetLastError(savedLastError);return;}
 // Publish only the authoritative player's original (pre-boost) frame.
 memcpy((void*)&g_lastDelta,delta,sizeof(g_lastDelta));g_deltaBody=nativeBody;g_deltaGeneration=InterlockedCompareExchange(&g_cancelGeneration,0,0);
 bool delivery=true;
 if(g_boostRemaining>0&&g_queuedHero){
  delivery=g_currentHero.load()==g_queuedHero&&g_palmCollector.Armed();
  if(g_firstFrameDeadline&&GetTickCount64()>g_firstFrameDeadline)delivery=false;
  if(g_queuedHero==1){
   const auto reader=CurrentReader();const auto a=motiontrace::ReadOnce(reader,g_bossProfile,0),b=motiontrace::ReadOnce(reader,g_bossProfile,0);
   const bool coherent=a.valid&&b.valid&&!(motiontrace::ChangedFields(a,b)&~64U)&&b.root==decision.native.root&&b.context==decision.native.context&&b.actor==decision.native.actor&&b.uid==decision.native.uid&&b.controller==decision.native.controller&&b.movement_state==decision.native.movement&&b.physics==decision.native.physics&&b.contact==decision.native.contact&&b.ground==decision.native.ground&&b.ground_known&&!b.grounded&&b.hero_key==1;
   const auto bound=coherent?superjump::palm::binding::Classify(reader,b):superjump::palm::binding::Result{};
   // Complete native metadata/record/clip proof is paid only before an actual
   // pending write. The key/hash/pointer tuple alone is never authority.
   delivery=delivery&&!decision.native.grounded&&coherent&&bound.valid&&bound.action==superjump::palm::Action::Palm3&&b.upper.metadata==g_queuedPalmMetadata&&g_palmCollector.Armed()&&g_palmCollector.CancellationEpoch()==g_queuedCollectorEpoch;
  }
  if(!delivery){InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_boostRemaining),0);InterlockedIncrement(&g_cancelGeneration);}
 }
 const bool owned=delivery&&g_queuedGeneration==InterlockedCompareExchange(&g_cancelGeneration,0,0)&&superjump::traversal::SameIdentity(g_queuedEpoch,decision.native);
 if(!decision.blocked&&g_boostRemaining>0&&!owned){InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_boostRemaining),0);InterlockedIncrement(&g_cancelGeneration);}
 if(!decision.blocked&&owned&&g_boostRemaining>0){
  const auto reader=CurrentReader();
#ifdef SUPERJUMP_TESTING
  const bool verifyWriter=g_nativeMovementChecksReady;
#else
  constexpr bool verifyWriter=true;
#endif
  if(!TrueFlightAllowsBoost(reader) || (verifyWriter &&
       !superjump::menu60::DeltaReady(reader,g_bossProfile.base,g_menuOwner.Load(),true))){
   InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_boostRemaining),0);
   InterlockedIncrement(&g_cancelGeneration);
   g_traversalEpoch={};
   ReleaseSRWLockExclusive(&g_eligibilityLock);
   SetLastError(savedLastError);
   return;
  }
  // Atomic claim orders this frame against a concurrent fail-closed cancel.
  const LONG remaining=InterlockedDecrement(reinterpret_cast<volatile LONG*>(&g_boostRemaining));
  if (remaining>=0) {
   const bool firstPalmFrame=g_queuedHero==1 && g_firstFrameDeadline!=0;
   const auto hero=g_currentHero.load(std::memory_order_acquire);
   const bool firstDoubleFrame=superjump::palm::Route(hero)==superjump::palm::Mode::DoubleJump &&
       remaining==g_boostFrames-1;
   const float before=delta[1];
   g_firstFrameDeadline=0;
   const uint32_t bits=g_boostYBits;
   memcpy(delta+1,&bits,sizeof(bits));
   if (firstPalmFrame || firstDoubleFrame)
    RecordPalmDelivery({GetTickCount64(),decision.native.uid,g_queuedGeneration,remaining,before,delta[1],hero});
  } else InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_boostRemaining),0);
 }
 ReleaseSRWLockExclusive(&g_eligibilityLock);
 SetLastError(savedLastError);
}
static Vec4f ReadPlayerDelta(LONG generation) {
 Vec4f result{};AcquireSRWLockExclusive(&g_eligibilityLock);
 if(g_deltaBody&&g_deltaBody==g_currentFrameBody.load()&&g_deltaGeneration==generation)memcpy(&result,(const void*)&g_lastDelta,sizeof(result));
 ReleaseSRWLockExclusive(&g_eligibilityLock);return result;
}
static bool QueueBoost(LONG expectedGeneration=InterlockedCompareExchange(&g_cancelGeneration,0,0),uint32_t hero=0,uint64_t firstFrameDeadline=0,uintptr_t palmMetadata=0) {
 AcquireSRWLockExclusive(&g_eligibilityLock);
 const auto decision=EvaluateBoostEligibility();
 bool queued=!decision.blocked&&expectedGeneration==InterlockedCompareExchange(&g_cancelGeneration,0,0)&&(!hero||hero==g_currentHero.load());
 if(queued){
  g_queuedEpoch=g_traversalEpoch;g_queuedGeneration=expectedGeneration;g_queuedHero=hero;g_firstFrameDeadline=firstFrameDeadline;g_queuedPalmMetadata=palmMetadata;g_queuedCollectorEpoch=g_palmCollector.CancellationEpoch();
  uint32_t strengthBits=0;memcpy(&strengthBits,&g_boostY,sizeof(g_boostY));g_boostYBits.store(strengthBits);InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_boostRemaining),g_boostFrames);
  if(InterlockedCompareExchange(&g_epochInvalidateRequested,0,0)||expectedGeneration!=InterlockedCompareExchange(&g_cancelGeneration,0,0)){InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_boostRemaining),0);queued=false;}
 }
 ReleaseSRWLockExclusive(&g_eligibilityLock);return queued;
}

static bool RefreshBoostContext(bool forceDiagnostic=false,GuardDecision*out=nullptr) {
    using namespace superjump;
    AcquireSRWLockExclusive(&g_eligibilityLock);
    const auto decision=EvaluateBoostEligibility();
    ReleaseSRWLockExclusive(&g_eligibilityLock);
    if(out)*out=decision;
    const auto& encounter=decision.encounter;const auto& ui=decision.ui;const auto& foreground=decision.foreground;
    const bool focused=decision.focused,cursor=decision.cursor,blocked=decision.blocked;
    static uint32_t nativeMessages=0;static uintptr_t lastNativeActor=0;static unsigned lastNativeCode=~0U;
    const auto& native=decision.native;
    const unsigned nativeCode=(unsigned(native.reason)<<24)|(unsigned(native.lane0)<<16)|(unsigned(native.lane1)<<8)|(native.grounded?2U:0U)|(blocked?1U:0U);
    if(nativeMessages<128&&(forceDiagnostic||lastNativeActor!=native.actor||lastNativeCode!=nativeCode)){
        LogFmt("native traversal: actor=0x%llX uid=%08X component=0x%llX body=0x%llX valid=%d reason=%u lanes=%u/%u grounded=%d blocked=%d cancellation=%ld",(uint64_t)native.actor,native.uid,(uint64_t)native.component,(uint64_t)native.frame_body,native.valid?1:0,unsigned(native.reason),unsigned(native.lane0),unsigned(native.lane1),native.grounded?1:0,blocked?1:0,g_cancelGeneration);
        lastNativeActor=native.actor;lastNativeCode=nativeCode;++nativeMessages;
    }
    static uintptr_t previous_player=0,previous_vtable=0;
    static unsigned previous_state=~0U,previous_reason=~0U,previous_ui=~0U,previous_ui_reason=~0U;
    static uint8_t previous_mode=0xFF,previous_submode=0xFF;
    static bool previous_focused=false,previous_blocked=true;
    static uint32_t messages=0;
    static DWORD lastContextDiagnostic=0;
    const DWORD contextNow=GetTickCount();
    const bool changed=previous_player!=encounter.player || previous_vtable!=encounter.actual_vtable ||
        previous_state!=static_cast<unsigned>(encounter.state) || previous_reason!=static_cast<unsigned>(encounter.reason) ||
        previous_ui!=static_cast<unsigned>(ui.state) || previous_ui_reason!=static_cast<unsigned>(ui.reason) ||
        previous_mode!=ui.mode || previous_submode!=ui.submode || previous_focused!=focused || previous_blocked!=blocked;
    if((g_inputDiagnostics && forceDiagnostic) || (messages<128 && (messages==0 || changed)) || (g_inputDiagnostics && contextNow-lastContextDiagnostic>=15000)) {
        LogFmt("input context: nativeUi=%s uiReason=%u mode=%u subtype=%u dirty=%u queued=%u modeRequests=%08X subtypeRequests=%08X worldState=%u gameplayEnabled=%u foreground=%d foregroundReason=%u foregroundHwnd=0x%llX foregroundOwner=%lu foregroundThread=%lu selfPid=%lu osCursor=%d boss=%s bossReason=%u player=0x%llX vtable=0x%llX context=0x%llX uid=%08X bossUid=%08X blocked=%d caveBlocked=%ld",
            ui.state==context::UiState::Gameplay?"gameplay":ui.state==context::UiState::Prohibited?"prohibited":"unknown",
            static_cast<unsigned>(ui.reason),ui.mode,ui.submode,ui.dirty,ui.queued,ui.mode_requests,ui.subtype_requests,ui.world_state,ui.gameplay_enabled,focused?1:0,foreground.reason,static_cast<unsigned long long>(foreground.window),
            foreground.owner,foreground.thread,GetCurrentProcessId(),cursor?1:0,
            encounter.state==boss::State::Active?"active":encounter.state==boss::State::Inactive?"inactive":"unknown",
            static_cast<unsigned>(encounter.reason),static_cast<unsigned long long>(encounter.player),
            static_cast<unsigned long long>(encounter.actual_vtable),static_cast<unsigned long long>(encounter.context),
            encounter.player_uid,encounter.boss_uid,blocked?1:0,g_contextBlocked);
        if(g_inputDiagnostics) LogFmt("boss detail: uid=%08X stage=%u combat=0x%llX engaged=%d selector=%u direct=%u action=%u local=%u engagementCounts=%u/%u/%u listCounts=%u/%u/%u opponents=%u missing=%u infoSeen=%u firstUid=%08X firstInfo=%u firstBossFlag=%u",
            encounter.player_uid,encounter.trace_stage,static_cast<unsigned long long>(encounter.combat),encounter.legacy_engaged?1:0,
            encounter.selector,encounter.direct_flag,encounter.action_flag,encounter.local_count,
            encounter.engagement_counts[0],encounter.engagement_counts[1],encounter.engagement_counts[2],
            encounter.list_counts[0],encounter.list_counts[1],encounter.list_counts[2],encounter.opponents,encounter.missing_uids,
            encounter.info_seen,encounter.first_candidate_uid,encounter.first_info_index,encounter.first_boss_flag);
        ++messages;lastContextDiagnostic=contextNow;
    }
    previous_player=encounter.player;previous_vtable=encounter.actual_vtable;
    previous_state=static_cast<unsigned>(encounter.state);previous_reason=static_cast<unsigned>(encounter.reason);
    previous_ui=static_cast<unsigned>(ui.state);previous_ui_reason=static_cast<unsigned>(ui.reason);
    previous_mode=ui.mode;previous_submode=ui.submode;previous_focused=focused;previous_blocked=blocked;
    return blocked;
}


static superjump::palm::Identity PalmIdentity(const motiontrace::Snapshot&x,const superjump::traversal::Snapshot&n,LONG generation){
 return {x.root,x.context,x.actor,x.controller,x.movement_state,n.component,n.frame_body,x.uid,x.hero_key,generation};
}
static bool SamePalmOwner(const motiontrace::Snapshot&x,const superjump::traversal::Snapshot&n){
 return x.valid&&n.valid&&x.root==n.root&&x.context==n.context&&x.actor==n.actor&&x.uid==n.uid&&x.parts==n.parts&&x.controller==n.controller&&x.movement_state==n.movement&&x.physics==n.physics&&x.contact==n.contact&&x.ground==n.ground&&x.ground_known&&x.grounded==n.grounded&&n.lane0==7&&n.lane1==7;
}
static motiontrace::TraceTags PalmTags(void*,const motiontrace::Reader&r,const motiontrace::Profile&p){
 const auto b=superjump::boss::Sample(r,p);const auto ui=superjump::context::SampleUi(r,p);
 motiontrace::TraceTags t{};t.ui_state=int(ui.state);t.ui_reason=int(ui.reason);t.boss_state=int(b.state);t.boss_reason=int(b.reason);t.boss_uid=b.boss_uid;t.ui_mode=ui.mode;t.ui_submode=ui.submode;t.foreground=ReadForegroundWindow().focused;t.coherent_world_root=b.world_root;t.roots_coherent=b.player&&b.context&&b.world_root;
#ifdef SUPERJUMP_TESTING
 if(g_testForeground>=0)t.foreground=g_testForeground!=0;
#endif
 return t;
}
static bool BindPalmNative(void*,const motiontrace::Reader&r,const motiontrace::Profile&p,motiontrace::TraceEvent&e)noexcept{
 using namespace superjump;
 using Stage=motiontrace::PalmBindStage;
 const LONG generation=InterlockedCompareExchange(&g_cancelGeneration,0,0);
 const auto refuse=[&](Stage stage) noexcept {e.palm_bind_stage=stage;return false;};
 if(!g_enabled)return refuse(Stage::Disabled);
 if(InterlockedCompareExchange(&g_epochInvalidateRequested,0,0))return refuse(Stage::EpochPending);
 if(!e.snapshot.valid)return refuse(Stage::Sampler);
 if(e.snapshot.hero_key!=1)return refuse(Stage::Hero);
 if(e.tags.ui_state!=int(context::UiState::Gameplay))return refuse(Stage::Ui);
 if(e.tags.boss_state!=int(boss::State::Inactive))return refuse(Stage::Boss);
 if(!e.tags.foreground)return refuse(Stage::Foreground);
 if(!e.tags.roots_coherent)return refuse(Stage::Roots);
 if(e.tags.coherent_world_root!=e.snapshot.root)return refuse(Stage::WorldRoot);

 const auto n=traversal::Sample(r,p,true);
 e.palm_traversal_reason=static_cast<uint8_t>(n.reason);
 if(!SamePalmOwner(e.snapshot,n)){
  if(!n.valid)return refuse(Stage::Traversal);
  const auto& x=e.snapshot;
  // For a valid traversal sample, reuse the detail byte as an owner mismatch mask.
  e.palm_traversal_reason=static_cast<uint8_t>(
   (x.root!=n.root||x.context!=n.context||x.actor!=n.actor||x.uid!=n.uid||x.parts!=n.parts||x.controller!=n.controller||x.movement_state!=n.movement?1u:0u)|
   (x.physics!=n.physics||x.contact!=n.contact||x.ground!=n.ground?2u:0u)|
   (!x.ground_known||x.grounded!=n.grounded?4u:0u)|(n.lane0!=7||n.lane1!=7?8u:0u));
  return refuse(Stage::Owner);
 }

 const auto bossNow=boss::Sample(r,p);
 if(bossNow.state!=boss::State::Inactive)return refuse(Stage::BossRecheck);
 if(bossNow.world_root!=n.root||bossNow.context!=n.context||bossNow.player!=n.actor||bossNow.player_uid!=n.uid)return refuse(Stage::BossOwner);

 const auto binding=palm::binding::Classify(r,e.snapshot);
 e.palm_profile_reason=static_cast<uint8_t>(binding.reason);
 if(!binding.valid)return refuse(Stage::Profile);
 if(generation!=InterlockedCompareExchange(&g_cancelGeneration,0,0))return refuse(Stage::GenerationChanged);
 if(InterlockedCompareExchange(&g_epochInvalidateRequested,0,0))return refuse(Stage::EpochRecheck);
 e.palm.identity=PalmIdentity(e.snapshot,n,generation);e.palm.action=binding.action;e.palm.bound=true;e.palm.success=true;e.palm.grounded=e.snapshot.grounded;
 e.palm_bind_stage=Stage::Bound;
 return true;
}
static superjump::palm::Action CurrentPalmAction(const motiontrace::Reader&r,const motiontrace::Snapshot&x){
 using A=superjump::palm::Action;
 const auto b=superjump::palm::binding::Classify(r,x);if(b.valid)return b.action;
 const auto&slot=x.upper.present&&x.upper.effective?x.upper:x.lower;
 if(slot.present&&slot.effective&&!slot.suppressed){switch(slot.skill_key){
  case 15033:case 15155:case 15158:return A::GroundPalm;
  case 15035:case 10372:return A::OtherAir;
  case 10018:return A::Fall;
  case 10052:case 10053:return A::Freeclimb;
  case 15028:case 15031:case 15036:return A::Flight;
  case 15032:case 15037:return A::Swimming;
 }}
 return x.grounded?A::Ground:A::Unknown;
}
struct PalmCurrent {superjump::palm::Context context{};motiontrace::Snapshot snapshot{};};
static PalmCurrent ReadPalmCurrent(const GuardDecision&d){
 PalmCurrent out{};const auto r=CurrentReader();auto a=motiontrace::ReadOnce(r,g_bossProfile,0);const auto b=motiontrace::ReadOnce(r,g_bossProfile,0);
 // The continuously changing launch parameters are observations, never trigger
 // authority. Every identity, action, grounding, registry and hero field agrees.
 if(!a.valid||!b.valid||(motiontrace::ChangedFields(a,b)&~64U)||!SamePalmOwner(b,d.native))return out;
 out.snapshot=b;out.context={PalmIdentity(b,d.native,InterlockedCompareExchange(&g_cancelGeneration,0,0)),GetTickCount64(),true,!d.blocked,b.grounded,CurrentPalmAction(r,b)};return out;
}
static void CancelPalmRequest(uint64_t now){
 g_palmPolicy.Reset(now);
 AcquireSRWLockExclusive(&g_eligibilityLock);
 if(g_queuedHero==1&&g_boostRemaining>0){InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_boostRemaining),0);InterlockedIncrement(&g_cancelGeneration);}
 ReleaseSRWLockExclusive(&g_eligibilityLock);
}
static void StopThirdVariant(){
 g_palmCollector.Stop();g_palmPolicy.Reset(GetTickCount64());
 AcquireSRWLockExclusive(&g_eligibilityLock);
 InterlockedExchange(&g_epochInvalidateRequested,1);InterlockedExchange(&g_contextBlocked,1);InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_boostRemaining),0);InterlockedIncrement(&g_cancelGeneration);g_currentFrameBody.store(0);g_currentHero.store(0);g_traversalEpoch={};g_deltaBody=0;g_deltaGeneration=0;
 ReleaseSRWLockExclusive(&g_eligibilityLock);
}
static void LogPalmCollectorFailure(){
 const auto prepare=g_palmCollector.PrepareDiagnostic();
 if(prepare.step!=superjump::palm::Collector::PrepareFailure::Step::None)
  LogFmt("palm hook preparation: site=%u step=%u type=%u detail=%u allocator=%u stolen=%zu",
   prepare.site,unsigned(prepare.step),prepare.type,prepare.detail,prepare.allocator,prepare.stolen);
 motiontrace::diagnostics::Failure f{};if(!g_palmCollector.FirstFailure(f))return;
 LogFmt("palm first contract failure: stage=%u detail=%u block=%u site=%u rva=%08X address=0x%llX read=0x%llX size=%llu readOk=%d known=%04X expected0=%02X actual0=%02X exec=0x%llX vmState=%08X vmType=%08X vmProtect=%08X",unsigned(f.stage),unsigned(f.detail),f.block,f.site,f.rva,(uint64_t)f.address,(uint64_t)f.readAddress,(uint64_t)f.readSize,f.readOk?1:0,unsigned(f.known),unsigned(f.expected[0]),unsigned(f.actual[0]),(uint64_t)f.execAddress,f.vmState,f.vmType,f.vmProtect);
}
static bool StartThirdHooks() {
    const auto provider=superjump::palm::menu_mode::RetainExactMenu({nullptr,ReadBossMemory});
    g_menuOwner.Publish(provider.Movement());
    if (provider.module && !provider)
        Log("DesertLink movement contract unavailable; using verified native movement sites");
    if (!IsProducerContract(g_hookSite)) {
        if (provider) {
            g_menuOwner.Publish({1,nullptr});
            FreeLibrary(reinterpret_cast<HMODULE>(provider.module));
        }
        Log("ERROR: native motion producer or downstream DELTA contract refused");
        return false;
    }
    if (!g_palmCollector.Initialize(g_bossProfile.base, {nullptr,ReadBossMemory},
            g_bossProfile, BindPalmNative, nullptr, PalmTags, nullptr, &provider)) {
        LogFmt("ERROR: third-palm collector refused: %s; motion producer untouched; any collector hooks retained inert",
            g_palmCollector.ErrorText());
        LogPalmCollectorFailure();
        StopThirdVariant();
        return false;
    }
    if (provider) Log("DesertLink: read-only flight and movement checks active");
    if (InstallHook()) return true;
    Log("ERROR: motion producer setup refused after collector initialization; collector stopped; restart required");
    StopThirdVariant();
    return false;
}

static const char* PalmBindStageName(motiontrace::PalmBindStage stage){
 using Stage=motiontrace::PalmBindStage;
 switch(stage){
  case Stage::Unset:return "unset";
  case Stage::Sampler:return "sampler";
  case Stage::Disabled:return "disabled";
  case Stage::EpochPending:return "epoch-pending";
  case Stage::Hero:return "hero";
  case Stage::Ui:return "ui";
  case Stage::Boss:return "boss";
  case Stage::Foreground:return "foreground";
  case Stage::Roots:return "roots";
  case Stage::WorldRoot:return "world-root";
  case Stage::Traversal:return "traversal";
  case Stage::Owner:return "owner";
  case Stage::BossRecheck:return "boss-recheck";
  case Stage::BossOwner:return "boss-owner";
  case Stage::Profile:return "profile";
  case Stage::GenerationChanged:return "generation";
  case Stage::EpochRecheck:return "epoch-recheck";
  case Stage::Bound:return "bound";
 }
 return "?";
}
static const char* PalmSampleReasonName(motiontrace::Reason reason){
 using Reason=motiontrace::Reason;
 switch(reason){
  case Reason::Profile:return "Profile";
  case Reason::Root:return "Root";
  case Reason::Identity:return "Identity";
  case Reason::Registry:return "Registry";
  case Reason::Hero:return "Hero";
  case Reason::Controller:return "Controller";
  case Reason::Slot:return "Slot";
  case Reason::Ground:return "Ground";
  case Reason::Commit:return "Commit";
  case Reason::Changed:return "Changed";
  case Reason::Valid:return "Valid";
 }
 return "?";
}
static const char* PalmProfileReasonName(uint8_t reason){
 using Reason=superjump::palm::binding::Reason;
 switch(static_cast<Reason>(reason)){
  case Reason::Snapshot:return "Snapshot";
  case Reason::NoProfile:return "NoProfile";
  case Reason::Slot:return "Slot";
  case Reason::Metadata:return "Metadata";
  case Reason::Package:return "Package";
  case Reason::Record:return "Record";
  case Reason::Clip:return "Clip";
  case Reason::Changed:return "Changed";
  case Reason::Valid:return "Valid";
 }
 return "-";
}
static void LogPalmDiagnostic(const motiontrace::TraceEvent& packet,uint64_t now,
 const char* dispatch,superjump::palm::Phase before,bool readyBefore,bool fires,unsigned resetMask){
 const auto& x=packet.snapshot;
 const auto& tags=packet.tags;
 const bool ownerMismatch=packet.palm_bind_stage==motiontrace::PalmBindStage::Owner;
 const char* second=x.reason==motiontrace::Reason::Changed&&x.second_reason!=motiontrace::Reason::Profile
                   ?PalmSampleReasonName(x.second_reason):"-";
 // Lane fields are key/hash/present/effective/suppressed. Ground is known/value.
 // Owner bits are identity (1), physics owner (2), grounded state (4), traversal lanes (8).
 // Reset bits are collector epoch (1), queue drops (2), and cancel generation (4).
 LogFmt("palm diagnostic: seq=%llu tick=%llu ageMs=%llu dispatch=%s site=%u bind=%s action=%u sample=%s second=%s changed=%02X valid=%d ground=%d/%d lower=%u/%08X/%d%d%d upper=%u/%08X/%d%d%d traversal=%u owner=%X profile=%s hero=%u uid=%08X generation=%ld ui=%d/%d boss=%d/%d fg=%d phase=%u/%u ready=%d/%d fires=%d reset=%X",
  packet.sequence,packet.tick,now>=packet.tick?now-packet.tick:UINT64_MAX,dispatch,packet.site,
  PalmBindStageName(packet.palm_bind_stage),unsigned(packet.palm.action),PalmSampleReasonName(x.reason),
  second,x.sample_changed,x.valid?1:0,x.ground_known?1:0,x.grounded?1:0,
  x.lower.skill_key,x.lower.name_hash,x.lower.present?1:0,x.lower.effective?1:0,x.lower.suppressed?1:0,
  x.upper.skill_key,x.upper.name_hash,x.upper.present?1:0,x.upper.effective?1:0,x.upper.suppressed?1:0,
  ownerMismatch?unsigned(superjump::traversal::Reason::Valid):unsigned(packet.palm_traversal_reason),
  ownerMismatch?unsigned(packet.palm_traversal_reason):0u,PalmProfileReasonName(packet.palm_profile_reason),x.hero_key,x.uid,
  static_cast<long>(packet.palm.identity.generation),tags.ui_state,tags.ui_reason,tags.boss_state,tags.boss_reason,
  tags.foreground?1:0,unsigned(before),unsigned(g_palmPolicy.CurrentPhase()),
  readyBefore?1:0,g_palmPolicy.Ready()?1:0,fires?1:0,resetMask);
}
static unsigned g_palmResetLogs=0;

static bool ProcessPalmPackets(const PalmCurrent&current){
 using namespace superjump::palm;
 const auto epoch=g_palmCollector.CancellationEpoch(),drops=g_palmCollector.Dropped();
 const LONG generation=InterlockedCompareExchange(&g_cancelGeneration,0,0);
 const unsigned resetMask=(epoch!=g_seenCollectorEpoch?1u:0u)|(drops!=g_seenCollectorDrops?2u:0u)|
                          (generation!=g_palmWorkerGeneration?4u:0u);
 if(resetMask){CancelPalmRequest(GetTickCount64());g_seenCollectorEpoch=epoch;g_seenCollectorDrops=drops;g_palmWorkerGeneration=InterlockedCompareExchange(&g_cancelGeneration,0,0);}
 motiontrace::TraceEvent packet{};bool any=g_palmCollector.Pop(packet),qualifying=false;
 // Current UI/identity/ground/unsafe actions cancel before any publication.
 // Proven forward stages may be queued within one worker interval; reconcile
 // the exact current phase after those native-time packets have been consumed.
 auto currentContext=current.context;currentContext.tick=GetTickCount64();
 g_palmPolicy.ObserveContext(currentContext,any);
 if(!any&&resetMask&&g_inputDiagnostics&&g_palmResetLogs<16){
  ++g_palmResetLogs;
  LogFmt("palm diagnostic: no packet tick=%llu reset=%X epoch=%llu drops=%llu generation=%ld nativeSuccess=%llu nativeFalse=%llu contractFailures=%llu",
   currentContext.tick,resetMask,epoch,drops,generation,g_palmCollector.NativeSuccess(),g_palmCollector.NativeFalse(),g_palmCollector.ContractFailures());
 }
 for(unsigned count=0;any&&count<128;++count){
  const bool epochChanged=g_palmCollector.CancellationEpoch()!=epoch;
  const bool dropsChanged=!epochChanged&&g_palmCollector.Dropped()!=drops;
  if(epochChanged||dropsChanged){
   const auto now=GetTickCount64();
   const auto before=g_palmPolicy.CurrentPhase();const bool readyBefore=g_palmPolicy.Ready();
   CancelPalmRequest(now);
   if(g_inputDiagnostics&&g_palmPacketLogs<128){
    ++g_palmPacketLogs;
    const unsigned discardReset=(count==0?resetMask:0u)|(epochChanged?1u:2u);
    LogPalmDiagnostic(packet,now,"epoch-discard",before,readyBefore,false,discardReset);
   }
   break;
  }
  const uint64_t consumeNow=GetTickCount64();
  const auto before=g_palmPolicy.CurrentPhase();const bool readyBefore=g_palmPolicy.Ready();
  const bool fires=g_palmPolicy.OnNative(packet.palm,consumeNow);qualifying=fires||qualifying;
  if(g_palmPacketLogs<128){
   ++g_palmPacketLogs;
   if(g_inputDiagnostics)LogPalmDiagnostic(packet,consumeNow,"processed",before,readyBefore,fires,count==0?resetMask:0);
   else LogFmt("palm packet: seq=%llu ageMs=%llu bound=%d action=%u grounded=%d phase=%u/%u fires=%d hero=%u uid=%08X generation=%ld hash=%08X ui=%d boss=%d bossReason=%d bossUid=%08X foreground=%d",packet.sequence,consumeNow>=packet.tick?consumeNow-packet.tick:UINT64_MAX,packet.palm.bound?1:0,unsigned(packet.palm.action),packet.palm.grounded?1:0,unsigned(before),unsigned(g_palmPolicy.CurrentPhase()),fires?1:0,packet.palm.identity.hero,packet.snapshot.uid,packet.palm.identity.generation,packet.snapshot.upper.effective?packet.snapshot.upper.name_hash:packet.snapshot.lower.name_hash,packet.tags.ui_state,packet.tags.boss_state,packet.tags.boss_reason,packet.tags.boss_uid,packet.tags.foreground?1:0);
  }
  any=g_palmCollector.Pop(packet);
 }
 currentContext.tick=GetTickCount64();g_palmPolicy.ObserveContext(currentContext);
 if(g_palmPolicy.CurrentPhase()==Phase::Idle){
  AcquireSRWLockExclusive(&g_eligibilityLock);if(g_queuedHero==1&&g_boostRemaining>0){InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_boostRemaining),0);InterlockedIncrement(&g_cancelGeneration);}ReleaseSRWLockExclusive(&g_eligibilityLock);return false;
 }
 if(!qualifying)return false;
 GuardDecision freshGuard{};RefreshBoostContext(false,&freshGuard);const auto fresh=ReadPalmCurrent(freshGuard);
 g_palmPolicy.ObserveContext(fresh.context);
 if(!fresh.context.valid||!fresh.context.allowed||fresh.context.action!=Action::Palm3||fresh.context.grounded||g_palmPolicy.CurrentPhase()!=Phase::Consumed||g_palmCollector.CancellationEpoch()!=epoch||g_palmCollector.Dropped()!=drops||!g_palmCollector.Armed()){CancelPalmRequest(GetTickCount64());return false;}
 // Packet freshness was checked at consumption. This separate delivery bound
 // tolerates a 20 FPS next physics frame, while cancelling paused/stale work.
 if(!QueueBoost(fresh.context.identity.generation,1,GetTickCount64()+250,fresh.snapshot.upper.metadata))return false;
 if(g_palmFireLogs<64){++g_palmFireLogs;LogFmt("THIRD-PALM queued: hero=1 native Air1/Air2/Air3 success; uid=%08X generation=%ld",fresh.context.identity.uid,fresh.context.identity.generation);}
 return true;
}

static void ModThread() {
    if(GetModuleHandleW(L"SJNativeMotionTrace.asi")){Log("ERROR: SJNativeMotionTrace owns the paired native sites. Disable it and both normal SuperJump alternatives; enable only SuperJump_ThirdPalm and restart.");return;}
    g_bossProfile=superjump::boss::ValidateProfile(superjump::boss::Reader{nullptr,ReadBossMemory},
                                                  reinterpret_cast<uintptr_t>(GetGameBase()));
    g_nativeMovementChecksReady=true;
    g_traversalProfileValid=g_bossProfile.valid;
    const bool startupTraversal=g_traversalProfileValid &&
        superjump::traversal::native::Validate({nullptr,ReadBossMemory},g_bossProfile.base);
    LogFmt("native traversal guard: instruction contract %s; only grounded idle establishes eligibility",startupTraversal?"validated":"pending or unknown; boosts fail closed");
    LogFmt("boss guard: native instruction contract %s",g_bossProfile.valid?"validated":"unknown; boosts fail closed");
    LogFmt("input context: native UI and foreground guards enforced; legacy ControllerBlockCursorUi=%d (OS cursor diagnostic only)",g_blockCursorUi?1:0);
    LogFmt("waiting for game to load... InitDelayMs=%d", g_initDelayMs);
    Sleep((DWORD)(g_initDelayMs > 0 ? g_initDelayMs : 0));
    if (IsFreedomFlyerLoaded()) {
        Log("ERROR: Freedom Flyer is active. Third Palm's independent Menu route requires it disabled.");
        return;
    }
    if (!g_bossProfile.valid) return;
    g_hookSite = reinterpret_cast<uint8_t*>(g_bossProfile.base + MOTION_PRODUCER_RVA + sizeof(MOTION_PRODUCER));
    if (!StartThirdHooks()) return;
    const auto* initialMode=g_palmCollector.ModeContractName();
    LogFmt("ready: Kliff=third successful native downward Air Palm; Damiane/Oongka=configured double jump; modeContract=%s; exclusive variant. Matthias suppression remains unconfirmed.",
           initialMode?initialMode:"native");
    const char* triggerModeName = g_triggerMode == 2
        ? "GameDoubleJump"
        : (g_triggerMode == 1 ? "InputDoubleJump" : "ChordHold");
    LogFmt("Damiane/Oongka fallback: %s",superjump::palm::DoubleJumpMode(g_triggerMode)==1?"InputDoubleJump":"GameDoubleJump");
    LogFmt("input config: mode=%s keyboardVK=%d controllerMask=0x%04X diagnostics=%d",
           triggerModeName,
           g_jumpKey, (unsigned int)g_xiJumpBtn, g_inputDiagnostics ? 1 : 0);
    LogFmt("axiom guard: enabled=%d keyboardVK=%d controllerMask=0x%04X excludeMask=0x%04X holdMs=%d timeoutMs=%d firstJumpWindowMs=%d",
           g_axiomGuardEnabled ? 1 : 0,
           g_axiomKey,
           static_cast<unsigned int>(g_xiAxiomButton),
           static_cast<unsigned int>(g_xiAxiomExcludeButtons),
           g_axiomActivationHoldMs,
           g_axiomLatchTimeoutMs,
           g_axiomFirstJumpWindowMs);

    bool  prevKbDown    = false;
    bool  prevXiDown    = false;
    bool  xiConsumed    = false;
    DWORD xiDownSince   = 0;
    DWORD xiLockoutUntil = 0;
    DWORD xiIdleSince    = GetTickCount();
    uint16_t prevXiButtons = 0;
    uint8_t prevXiConnectedMask = 0;
    uint8_t prevXiProviderMask = 0;
    bool prevJumpDown = false;
    superjump::PendingActivation activation{};
    auto& lastJumpPressMs=activation.lastPress;
    auto& doubleJumpArmed=activation.armed;
    auto& doubleJumpPendingAt=activation.pendingAt;
    DWORD doubleJumpCooldownUntil = 0;
    auto& doubleJumpPending=activation.pending;
    DWORD airborneSinceMs = 0;
    DWORD lastUiSuppressLogMs = 0;
    DWORD lastAirborneSuppressLogMs = 0;
    DWORD lastGameImpulseLogMs = 0;
    auto& gameDoubleJumpState=activation.game;
    superjump::AxiomGuardState axiomGuardState{};
    DWORD lastIni       = GetTickCount();
    LONG seenCancellation=InterlockedCompareExchange(&g_cancelGeneration,0,0);

    bool loggedMenuMode=initialMode!=nullptr;
    bool inputRequiresNeutral=true;
    while (true) {
        PollFlightOwner();
        Sleep(16);
        DrainPalmDeliveries();
            if(!g_palmCollector.Poll()){CancelPalmRequest(GetTickCount64());InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_boostRemaining),0);LogFmt("ERROR: native collector stopped: %s; restart required",g_palmCollector.ErrorText());LogPalmCollectorFailure();StopThirdVariant();return;}
        if(!loggedMenuMode){
            if(const auto* verifiedMode=g_palmCollector.ModeContractName()){
                LogFmt("palm mode contract verified: %s",verifiedMode);
                loggedMenuMode=true;
            }
        }
        const auto sampleTick=GetTickCount64();
        const DWORD now=static_cast<DWORD>(sampleTick);
        if (now - lastIni > 1000) {
            lastIni=now;
            LoadConfig();
        }
        if (!g_enabled || !GameHasForeground()) {
            inputRequiresNeutral=true;
            CancelPalmRequest(GetTickCount64());
            InterlockedExchange(&g_epochInvalidateRequested,1);
            InterlockedIncrement(&g_cancelGeneration);
            InterlockedExchange(&g_contextBlocked,1);
            InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_boostRemaining),0);
            prevKbDown = false;
            prevXiDown = false;
            xiConsumed = false;
            xiDownSince = 0;
            xiLockoutUntil = 0;
            xiIdleSince = now;
            prevXiButtons = 0;
            prevXiConnectedMask = 0;
            prevXiProviderMask = 0;
            prevJumpDown = false;
            lastJumpPressMs = 0;
            doubleJumpArmed = false;
            doubleJumpPendingAt = 0;
            doubleJumpCooldownUntil = 0;
            doubleJumpPending = false;
            gameDoubleJumpState = {};
            superjump::ResetAxiomGuard(axiomGuardState);
            continue;
        }

        GuardDecision routeGuard{};const bool routeBlocked=RefreshBoostContext(false,&routeGuard);
        const auto route=superjump::palm::Route(g_currentHero.load());
        if(route==superjump::palm::Mode::Palm){
            superjump::CancelPendingActivation(activation);
            ProcessPalmPackets(ReadPalmCurrent(routeGuard));
            continue;
        }
        CancelPalmRequest(GetTickCount64());
        motiontrace::TraceEvent discarded{};
        while(g_palmCollector.Pop(discarded)){
            if(g_inputDiagnostics&&g_palmPacketLogs<128&&discarded.snapshot.hero_key==1){
                ++g_palmPacketLogs;
                LogPalmDiagnostic(discarded,GetTickCount64(),"route-discard",g_palmPolicy.CurrentPhase(),g_palmPolicy.Ready(),false,0);
            }
        }
        if(route==superjump::palm::Mode::None||routeBlocked){superjump::CancelPendingActivation(activation);continue;}
        const int fallbackMode=superjump::palm::DoubleJumpMode(g_triggerMode);
        // SuperJump uses TWO keyboard modifiers (Shift + C) + key (Space).
        // cdinput::Hotkey only models one kb modifier, so we hand-compose
        // the kb leg and OR with cdinput's XInput leg.
        bool kbLeg = cdinput::KbDown(g_hotkeyKey)
                     && (g_hotkeyMod  == 0 || cdinput::KbDown(g_hotkeyMod))
                     && (g_hotkeyMod2 == 0 || cdinput::KbDown(g_hotkeyMod2));
        uint8_t xiConnectedMask = 0;
        uint8_t xiProviderMask = 0;
        uint16_t xiButtons = cdinput::XInputButtons(
            &xiConnectedMask, &xiProviderMask);
        uint16_t xiComboMask = (uint16_t)(g_xiBtn | g_xiMod);
        uint16_t xiMenuMask = (uint16_t)(0x000F | 0x0030 | 0x00C0 | 0xE000 | 0x0200);
        bool xiLeg = g_xiBtn != 0 && xiComboMask != 0 &&
                     (xiButtons & xiComboMask) == xiComboMask;
        if (inputRequiresNeutral) {
            const bool jumpHeld=cdinput::KbDown(g_jumpKey) ||
                (g_xiJumpBtn && (xiButtons&g_xiJumpBtn)==g_xiJumpBtn);
            if (kbLeg || xiLeg || jumpHeld || !GameHasForeground()) continue;
            inputRequiresNeutral=false;
            prevXiButtons=xiButtons;
            prevXiConnectedMask=xiConnectedMask;
            prevXiProviderMask=xiProviderMask;
        }
        const bool prohibitedContext=RefreshBoostContext();
        const LONG cancellation=InterlockedCompareExchange(&g_cancelGeneration,0,0);
        if(prohibitedContext||cancellation!=seenCancellation){
            superjump::CancelPendingActivation(activation);
            prevKbDown=kbLeg;prevXiDown=xiLeg;prevJumpDown=cdinput::KbDown(g_jumpKey)||(g_xiJumpBtn!=0&&(xiButtons&g_xiJumpBtn)==g_xiJumpBtn);
            xiConsumed=xiLeg;xiDownSince=now;superjump::ResetAxiomGuard(axiomGuardState);
            seenCancellation=cancellation;
        }
        const Vec4f playerDelta=ReadPlayerDelta(cancellation);
        bool interactionUi = prohibitedContext;
        bool controllerLockout = (int32_t)(xiLockoutUntil - now) > 0;
        bool otherControllerButtonPressed =
            (xiButtons & xiMenuMask & (uint16_t)~xiComboMask & (uint16_t)~prevXiButtons) != 0;
        bool controllerIdleReady = (int32_t)(now - xiIdleSince) >=
                                   (int32_t)(g_controllerIdleMs < 0 ? 0 : g_controllerIdleMs);
        bool triggered = false;
        bool triggerFromDoubleJump = false;
        bool controllerBlockedByUi = false;
        bool controllerBlockedBySession = false;

        if (g_inputDiagnostics && xiConnectedMask != prevXiConnectedMask) {
            LogFmt("input: XInput connected-slots mask changed 0x%02X -> 0x%02X",
                   (unsigned int)prevXiConnectedMask, (unsigned int)xiConnectedMask);
        }
        if (g_inputDiagnostics && xiProviderMask != prevXiProviderMask) {
            LogFmt("input: active XInput provider changed 0x%02X -> 0x%02X (bit0=xinput1_4 bit1=xinput1_3 bit2=xinput9_1_0)",
                   (unsigned int)prevXiProviderMask,
                   (unsigned int)xiProviderMask);
        }

        if (fallbackMode == 2) {
            const uint32_t minIntervalMs = static_cast<uint32_t>(
                g_gameDoubleJumpMinMs < 0 ? 0 : g_gameDoubleJumpMinMs);
            uint32_t maxIntervalMs = static_cast<uint32_t>(
                g_gameDoubleJumpMaxMs < 0 ? 0 : g_gameDoubleJumpMaxMs);
            if (maxIntervalMs < minIntervalMs) maxIntervalMs = minIntervalMs;
            const superjump::GameDoubleJumpConfig detectorConfig{
                g_gameJumpImpulseY,
                g_gameJumpResetY,
                minIntervalMs,
                maxIntervalMs,
                static_cast<uint32_t>(g_doubleJumpCooldownMs < 0 ? 0 : g_doubleJumpCooldownMs)
            };
            const float deltaY = playerDelta.y;
            const superjump::GameDoubleJumpEvent event =
                superjump::ObserveGameJumpDelta(
                    gameDoubleJumpState, detectorConfig, now, deltaY, interactionUi);

            switch (event) {
            case superjump::GameDoubleJumpEvent::FirstImpulse:
                if (g_inputDiagnostics) {
                    LogFmt("game double-jump: first accepted impulse dy=%.4f contextVisible=%d",
                           deltaY, interactionUi ? 1 : 0);
                }
                break;
            case superjump::GameDoubleJumpEvent::Triggered:
                triggered = true;
                triggerFromDoubleJump = true;
                LogFmt("game double-jump: second accepted impulse dy=%.4f contextVisible=%d - trigger",
                       deltaY, interactionUi ? 1 : 0);
                break;
            case superjump::GameDoubleJumpEvent::IgnoredTooEarly:
                if (g_inputDiagnostics && now - lastGameImpulseLogMs > 250) {
                    LogFmt("game double-jump: ignored duplicate impulse dy=%.4f", deltaY);
                    lastGameImpulseLogMs = now;
                }
                break;
            case superjump::GameDoubleJumpEvent::SuppressedCooldown:
                if (g_inputDiagnostics && now - lastGameImpulseLogMs > 500) {
                    Log("game double-jump: impulse suppressed by cooldown");
                    lastGameImpulseLogMs = now;
                }
                break;
            case superjump::GameDoubleJumpEvent::SuppressedContext:
                if (g_inputDiagnostics && now - lastUiSuppressLogMs > 10000) {
                    Log("suppressed game double-jump - UI/context visible");
                    lastUiSuppressLogMs = now;
                }
                break;
            case superjump::GameDoubleJumpEvent::None:
                break;
            }
        } else if (fallbackMode == 1) {
            float dyAbs = fabsf(playerDelta.y);
            bool airborneNow = dyAbs >= g_airborneDeltaYMin;
            if (airborneNow) {
                if (airborneSinceMs == 0) airborneSinceMs = now;
            } else {
                airborneSinceMs = 0;
            }
            DWORD airborneMs = airborneSinceMs ? (now - airborneSinceMs) : 0;
            bool keyboardJumpDown = g_jumpKey != 0 && cdinput::KbDown(g_jumpKey);
            bool controllerJumpDown = g_xiJumpBtn != 0 &&
                (xiButtons & g_xiJumpBtn) == g_xiJumpBtn;
            bool jumpDown = keyboardJumpDown || controllerJumpDown;
            bool jumpEdge = jumpDown && !prevJumpDown;
            bool axiomSuppressJumpEdge = false;
            if (g_axiomGuardEnabled) {
                const superjump::AxiomGuardConfig axiomGuardConfig{
                    static_cast<uint32_t>(
                        g_axiomActivationHoldMs < 0 ? 0 : g_axiomActivationHoldMs),
                    static_cast<uint32_t>(
                        g_axiomLatchTimeoutMs < 0 ? 0 : g_axiomLatchTimeoutMs),
                    static_cast<uint32_t>(
                        g_axiomFirstJumpWindowMs < 0 ? 0 : g_axiomFirstJumpWindowMs),
                    static_cast<uint32_t>(
                        g_doubleJumpWindowMs < 0 ? 0 : g_doubleJumpWindowMs),
                    g_xiAxiomButton,
                    g_xiAxiomExcludeButtons
                };
                const bool keyboardAxiomDown =
                    g_axiomKey != 0 && cdinput::KbDown(g_axiomKey);
                const superjump::AxiomGuardResult axiomResult =
                    superjump::ObserveAxiomGuard(
                        axiomGuardState,
                        axiomGuardConfig,
                        now,
                        keyboardAxiomDown,
                        xiButtons,
                        jumpEdge,
                        interactionUi);
                axiomSuppressJumpEdge = axiomResult.suppressJumpEdge;
                switch (axiomResult.event) {
                case superjump::AxiomGuardEvent::ActivationStarted:
                    lastJumpPressMs = 0;
                    doubleJumpArmed = false;
                    doubleJumpPending = false;
                    if (g_inputDiagnostics) {
                        Log("axiom guard: activation hold started");
                    }
                    break;
                case superjump::AxiomGuardEvent::ActivationInvalidated:
                    if (g_inputDiagnostics) {
                        Log("axiom guard: activation invalidated by excluded controller chord");
                    }
                    break;
                case superjump::AxiomGuardEvent::ActivationTooShort:
                    if (g_inputDiagnostics) {
                        Log("axiom guard: short activation ignored");
                    }
                    break;
                case superjump::AxiomGuardEvent::LatchArmed:
                    lastJumpPressMs = 0;
                    doubleJumpArmed = false;
                    doubleJumpPending = false;
                    Log("axiom guard: latch session armed");
                    break;
                case superjump::AxiomGuardEvent::FirstJumpSuppressed:
                    lastJumpPressMs = 0;
                    doubleJumpArmed = false;
                    doubleJumpPending = false;
                    Log("axiom guard: first Aerial Maneuver jump excluded from Super Jump");
                    break;
                case superjump::AxiomGuardEvent::AerialManeuverSuppressed:
                    lastJumpPressMs = 0;
                    doubleJumpArmed = false;
                    doubleJumpPending = false;
                    Log("axiom guard: Aerial Maneuver double-press excluded from Super Jump");
                    break;
                case superjump::AxiomGuardEvent::FirstJumpWindowExpired:
                    if (g_inputDiagnostics) {
                        Log("axiom guard: activation context expired before first jump");
                    }
                    break;
                case superjump::AxiomGuardEvent::JumpSequenceExpired:
                    if (g_inputDiagnostics) {
                        Log("axiom guard: Aerial Maneuver pair window expired; jump passed through");
                    }
                    break;
                case superjump::AxiomGuardEvent::LatchExpired:
                    if (g_inputDiagnostics) {
                        Log("axiom guard: abandoned latch session expired");
                    }
                    break;
                case superjump::AxiomGuardEvent::ContextReset:
                    lastJumpPressMs = 0;
                    doubleJumpArmed = false;
                    doubleJumpPending = false;
                    if (g_inputDiagnostics) {
                        Log("axiom guard: session cleared by UI/context");
                    }
                    break;
                case superjump::AxiomGuardEvent::None:
                    break;
                }
            } else {
                superjump::ResetAxiomGuard(axiomGuardState);
            }
            if (jumpEdge && g_inputDiagnostics) {
                DWORD armAge = lastJumpPressMs ? now - lastJumpPressMs : 0;
                LogFmt("input: jump edge source=%s%s slots=0x%02X buttons=0x%04X dy=%.4f airborneMs=%lu armed=%d armAgeMs=%lu ui=%d axiomSuppressed=%d",
                       keyboardJumpDown ? "keyboard" : "",
                       controllerJumpDown ? (keyboardJumpDown ? "+controller" : "controller") : "",
                       (unsigned int)xiConnectedMask, (unsigned int)xiButtons,
                       playerDelta.y, (unsigned long)airborneMs,
                       doubleJumpArmed ? 1 : 0, (unsigned long)armAge,
                       interactionUi ? 1 : 0,
                       axiomSuppressJumpEdge ? 1 : 0);
            }
            if (interactionUi && (jumpDown || doubleJumpArmed || doubleJumpPending)) {
                lastJumpPressMs = 0;
                doubleJumpArmed = false;
                doubleJumpPending = false;
                if (now - lastUiSuppressLogMs > 1000) {
                    Log("suppressed input double-jump - UI/context visible");
                    lastUiSuppressLogMs = now;
                }
            } else if (jumpEdge && !axiomSuppressJumpEdge) {
                int windowMs = g_doubleJumpWindowMs < 0 ? 0 : g_doubleJumpWindowMs;
                bool inCooldown = (int32_t)(doubleJumpCooldownUntil - now) > 0;
                bool secondPress = !inCooldown &&
                    doubleJumpArmed &&
                    lastJumpPressMs != 0 &&
                    (int32_t)(now - lastJumpPressMs) <= windowMs;
                if (secondPress) {
                    if (g_requireAirborneSecondPress && !airborneNow) {
                        lastJumpPressMs = 0;
                        doubleJumpArmed = false;
                        doubleJumpPending = false;
                        if (now - lastAirborneSuppressLogMs > 1000) {
                            LogFmt("suppressed input double-jump - second press not airborne/gameplay (dy=%.4f)", playerDelta.y);
                            lastAirborneSuppressLogMs = now;
                        }
                    } else {
                        doubleJumpPending = true;
                        const DWORD delayMs=static_cast<DWORD>(g_doubleJumpFireDelayMs < 0 ? 0 : g_doubleJumpFireDelayMs);
                        doubleJumpPendingAt = now + delayMs;
                        lastJumpPressMs = 0;
                        doubleJumpArmed = false;
                        LogFmt("input double-jump second press detected (dy=%.4f airborneMs=%lu) tick=%llu delayMs=%lu",
                               playerDelta.y, static_cast<unsigned long>(airborneMs),
                               static_cast<unsigned long long>(sampleTick), delayMs);
                    }
                } else {
                    DWORD grace = (DWORD)(g_firstPressAirborneGraceMs < 0 ? 0 : g_firstPressAirborneGraceMs);
                    bool alreadyAirborne = airborneNow && airborneMs > grace;
                    if (g_requireGroundedFirstPress && alreadyAirborne) {
                        lastJumpPressMs = 0;
                        doubleJumpArmed = false;
                        doubleJumpPending = false;
                        if (now - lastAirborneSuppressLogMs > 1000) {
                            LogFmt("suppressed input double-jump arm - first press already airborne/gliding (dy=%.4f airborneMs=%lu)",
                                   playerDelta.y, (unsigned long)airborneMs);
                            lastAirborneSuppressLogMs = now;
                        }
                    } else {
                        lastJumpPressMs = now;
                        doubleJumpArmed = true;
                        if (g_inputDiagnostics) {
                            LogFmt("input double-jump armed (dy=%.4f airborneMs=%lu)",
                                   playerDelta.y, (unsigned long)airborneMs);
                        }
                    }
                }
            }
            if (doubleJumpPending && (int32_t)(now - doubleJumpPendingAt) >= 0) {
                triggered = true;
                triggerFromDoubleJump = true;
                doubleJumpPending = false;
                doubleJumpCooldownUntil = now + (DWORD)(g_doubleJumpCooldownMs < 0 ? 0 : g_doubleJumpCooldownMs);
            }
            if (lastJumpPressMs != 0 &&
                (int32_t)(now - lastJumpPressMs) > (int32_t)(g_doubleJumpWindowMs < 0 ? 0 : g_doubleJumpWindowMs)) {
                if (g_inputDiagnostics) {
                    LogFmt("input double-jump arm expired after %lu ms",
                           (unsigned long)(now - lastJumpPressMs));
                }
                lastJumpPressMs = 0;
                doubleJumpArmed = false;
            }
            prevJumpDown = jumpDown;
        } else {
            triggered = kbLeg && !prevKbDown;
        }

        if (superjump::ShouldEvaluateChordTrigger(fallbackMode) &&
            (interactionUi || otherControllerButtonPressed) &&
            (xiLeg || controllerLockout || interactionUi)) {
            xiLockoutUntil = now + (DWORD)(g_controllerLockoutMs < 0 ? 0 : g_controllerLockoutMs);
            controllerLockout = true;
            if (xiLeg) xiConsumed = true;
        }

        if (superjump::ShouldEvaluateChordTrigger(fallbackMode) && xiLeg) {
            if (!prevXiDown) {
                xiDownSince = now;
                xiConsumed = false;
            }

            DWORD heldMs = now - xiDownSince;
            if (!xiConsumed && heldMs >= (DWORD)(g_controllerHoldMs < 0 ? 0 : g_controllerHoldMs)) {
                if (interactionUi) {
                    controllerBlockedByUi = true;
                    xiConsumed = true;
                    xiLockoutUntil = now + (DWORD)(g_controllerLockoutMs < 0 ? 0 : g_controllerLockoutMs);
                } else if (controllerLockout || !controllerIdleReady) {
                    controllerBlockedBySession = true;
                    xiConsumed = true;
                } else {
                    triggered = true;
                    xiConsumed = true;
                }
            }
        } else if (superjump::ShouldEvaluateChordTrigger(fallbackMode)) {
            if (prevXiDown && !xiConsumed) {
                xiLockoutUntil = now + (DWORD)(g_controllerLockoutMs < 0 ? 0 : g_controllerLockoutMs);
            }
            xiDownSince = 0;
            xiConsumed = false;
            if (xiButtons == 0) xiIdleSince = now;
        }

        prevKbDown = kbLeg;
        prevXiDown = xiLeg;
        prevXiButtons = xiButtons;
        prevXiConnectedMask = xiConnectedMask;
        prevXiProviderMask = xiProviderMask;
        if (controllerBlockedByUi) {
            Log("suppressed controller trigger ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â interaction UI visible");
        }
        if (controllerBlockedBySession) {
            Log("suppressed controller trigger - interaction session active");
        }
        if (prohibitedContext) {
            gameDoubleJumpState.hasFirstImpulse=false;gameDoubleJumpState.firstImpulseMs=0;
            doubleJumpArmed=false;doubleJumpPending=false;doubleJumpPendingAt=0;lastJumpPressMs=0;
            continue;
        }
        if (!triggered) continue;

        // v1.4 stationary gate: use g_lastDelta from the cave (the engine's
        // per-frame physics delta xyz, captured at the DELTA hook site).
        // v1.3 tried to read velocity from [*g_playerStaticVA + 0xC0] but
        // *g_playerStaticVA is the top-of-chain static, NOT the physics body
        // ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â the +0xC0 there is unrelated junk that reads ~1186.0f and
        // permanently suppresses the launch. Reverting to the cave delta,
        // which is what worked in v1.2 and earlier.
        if (g_requireStationary && !triggerFromDoubleJump) {
            float dx = playerDelta.x, dz = playerDelta.z;
            float mag = sqrtf(dx*dx + dz*dz);
            if (mag > g_stationaryThresh) {
                LogFmt("suppressed ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â moving (|ÃƒÅ½Ã¢â‚¬Â|=%.4f, thresh=%.4f)",
                       mag, g_stationaryThresh);
                continue;
            }
        }

        if (g_animCrouch) {
            std::thread(SimulateCrouch,static_cast<UINT>(g_crouchKey),
                        static_cast<DWORD>((std::max)(0,g_crouchDurationMs))).detach();
            Sleep(80);
        }

        if (RefreshBoostContext(true)) continue;
        const auto queueTick=GetTickCount64();
        if(!QueueBoost(cancellation,g_currentHero.load(),queueTick+250))continue;
        LogFmt("FIRE queued - boost=%.1f frames=%d trigger=%s requestTick=%llu",
               g_boostY, g_boostFrames,
               triggerFromDoubleJump ? "doublejump" : "chord",
               static_cast<unsigned long long>(queueTick));
    }
}

BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hMod);
        if (!IsCrimsonDesertProcess()) {
            return TRUE;
        }
        if(!AcquireVariantLease())return TRUE;
        g_ownModule=hMod;
        if(!EnsureOwnPinned()){
            if(g_variantMutex){CloseHandle(g_variantMutex);g_variantMutex=nullptr;}
            g_ownModule=nullptr;return TRUE;
        }
        g_startedInGame = true;
        char path[MAX_PATH]={};
        GetModuleFileNameA(hMod, path, MAX_PATH);
        std::string dir(path);
        dir = dir.substr(0, dir.find_last_of("\\/")+1);
        g_iniPath = dir + "SuperJump.ini";
        g_log.open(dir + "SuperJump_ThirdPalm.log");
        LogFmt("v1.6.2.21 third-palm-p22-safetyhook loaded; pid=%lu tick=%llu; exactly one SuperJump alternative required",
               GetCurrentProcessId(),GetTickCount64());
        LoadConfig();
        std::thread(ModThread).detach();
    }
    else if (reason == DLL_PROCESS_DETACH) {
        // Pinned callbacks, workers, emitted caves and the variant lease are process lifetime.
        // No hot unpatch, cave release, collector teardown, or lease reopening.
        if(!g_ownPinned.load(std::memory_order_acquire)&&g_variantMutex){CloseHandle(g_variantMutex);g_variantMutex=nullptr;}
    }

    return TRUE;
}





