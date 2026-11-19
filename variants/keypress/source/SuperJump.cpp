// Super Jump for Crimson Desert.
// Publishes a pre-collision producer, preserving the Menu's downstream DELTA.

#define WIN32_LEAN_AND_MEAN
#include "SuperJumpBossNativeProfile.h"
#include "SuperJumpContextGuard.h"
#include <windows.h>
#include <array>
#include <xinput.h>
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
#include "../../shared/MenuFlight.h"
#include "safetyhook/mid_hook.hpp"
#include <xmmintrin.h>
#include <bit>
#include <new>
static bool g_nativeMovementChecksReady=false;
#include "SuperJumpHostPolicy.h"
#include "SuperJumpActivationPolicy.h"

#include "../../shared/ForegroundInput.inc"

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
static int      g_triggerMode       = 0;      // Dedicated keypress edition: ChordHold only
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
static uint8_t* g_hookSite = nullptr;
static safetyhook::MidHook* g_motionHook = nullptr;
static superjump::menu60::PublishedProvider g_menuOwner{};
static bool     g_startedInGame = false;
// Every emitted callback and worker remains valid for the process lifetime.
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
// The keypress alternative owns this matching-stem INI exclusively. If it is
// absent, the existing reader uses built-in defaults; no shared file is read.
static void SetKeypressIniDirectory(const std::string& dir) {
    g_iniPath = dir + "SuperJump_Keypress.ini";
}
static float IniFloat(const char* k, float d) {
    char b[64]={}; GetPrivateProfileStringA("SuperJump",k,"",b,64,g_iniPath.c_str());
    return b[0]?(float)atof(b):d;
}
static int IniInt(const char* k, int d) {
    return GetPrivateProfileIntA("SuperJump",k,d,g_iniPath.c_str());
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
    g_blockCursorUi     = !!IniInt("BlockContextUi",
                                   IniInt("ControllerBlockCursorUi", 1));
    g_enabled           = !!IniInt("Enabled",          1);
    g_animCrouch        = !!IniInt("AnimCrouch",       1);
    g_crouchKey         = IniInt  ("CrouchKey",        VK_CONTROL);
    g_crouchDurationMs  = IniInt  ("CrouchDurationMs", 120);
    // Keep this parallel build mode-isolated. Double-jump settings from an
    // older INI must never change the keypress edition's trigger contract.
    g_triggerMode = 0;
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
#include "../../shared/MotionProducer.inc"

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
static bool ReadBossMemory(void*,uintptr_t address,void* out,size_t size) {
    __try { memcpy(out,reinterpret_cast<const void*>(address),size);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Loader and file checks run only on the worker. The callback reads verified
// Menu state under its existing nonblocking guard.
static void PollMenuOwner() {
    if (g_menuOwner.Load().module) return;
    HMODULE owner=nullptr;
    if (!GetModuleHandleExW(0,L"DesertLinkModMenu.asi",&owner)) {
        if (GetLastError()!=ERROR_MOD_NOT_FOUND)
            g_menuOwner.Publish({1,nullptr});
        return;
    }
    g_menuOwner.Publish({1,nullptr});
    const superjump::boss::Reader reader{nullptr,ReadBossMemory};
    if (const auto menu=superjump::menu60::IdentifyRetainedMenu(reader,owner)) {
        g_menuOwner.Publish(menu);
        Log("DesertLink: verified movement provider; standalone triggers share ground gameplay; flight owns flight frames");
    } else {
        FreeLibrary(owner);
        Log("Menu movement contract unavailable; using verified native movement sites");
    }
}

static bool MenuAllowsBoost(const superjump::boss::Reader& reader) {
    using namespace superjump::menu60;
    const auto menu=g_menuOwner.Load();
    if (!menu.module) return true;
    if (!menu) return LocomotionReady(reader,g_bossProfile.base,menu) &&
                      DeltaReady(reader,g_bossProfile.base,menu,true);
    if (!FlightAllowsBoost(reader,menu)) return false;
    // The DELTA producer writes the configured absolute delta only for a
    // standalone request. Ordinary embedded jump remains in Menu locomotion.
    uint32_t enabled=0, repeated=0;
    const auto slot=menu.module+menu.profile->jumpEnabled;
    return reader.Get(slot,enabled) && enabled<=1 &&
           reader.Get(slot,repeated) && enabled==repeated;
}

struct GuardDecision {
 superjump::boss::Result encounter{};
 superjump::context::UiResult ui{};
 superjump::traversal::Snapshot native{};
 ForegroundSnapshot foreground{};bool focused=false,cursor=false,blocked=true;
};
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
 d.foreground=ReadForegroundWindow();
 d.focused=d.foreground.focused;
 d.cursor=CursorVisibleDiagnostic();
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
 const bool context_allowed=g_enabled&&motionAllowed&&MenuAllowsBoost(reader)&&!context::BlocksBoost(d.ui,d.encounter,d.focused,d.cursor);
 bool traversalValid=g_traversalProfileValid;
 if(g_nativeMovementChecksReady){
  menu60::TraversalView view{reader,reader,g_bossProfile.base+menu60::kStepper,menuOwner};
  traversalValid=traversalValid &&
      traversal::native::Validate({&view,menu60::TraversalView::Read},g_bossProfile.base);
 }
 d.native=traversal::Sample(reader,g_bossProfile,traversalValid);
 const bool coherent=d.native.valid&&d.native.root==d.encounter.world_root&&d.native.context==d.encounter.context&&d.native.actor==d.encounter.player&&d.native.uid==d.encounter.player_uid;
 const uintptr_t currentBody=d.native.valid&&coherent?d.native.frame_body:0;
 if(g_currentFrameBody.exchange(currentBody)!=currentBody){memset((void*)&g_lastDelta,0,sizeof(g_lastDelta));g_deltaBody=0;g_deltaGeneration=0;}
 // A valid newly grounded actor still cancels every request from the old
 // actor/context. Fresh eligibility and request ownership are separate.
 if(d.native.valid&&g_traversalEpoch.actor&&!traversal::SameIdentity(g_traversalEpoch,d.native)){
  InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_boostRemaining),0);InterlockedIncrement(&g_cancelGeneration);g_traversalEpoch={};memset((void*)&g_lastDelta,0,sizeof(g_lastDelta));g_deltaBody=0;g_deltaGeneration=0;
 }
 d.blocked=!traversal::Allows(g_traversalEpoch,d.native,context_allowed&&coherent);
 if(InterlockedCompareExchange(&g_epochInvalidateRequested,0,0)||generation!=InterlockedCompareExchange(&g_cancelGeneration,0,0)){d.blocked=true;g_traversalEpoch={};}
 InterlockedExchange(&g_contextBlocked,d.blocked?1:0);
 if(d.blocked){InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_boostRemaining),0);InterlockedIncrement(&g_cancelGeneration);}
 return d;
}
static void ConsumeBoostFrame(float* delta,uintptr_t nativeBody,uintptr_t nativeDestination) {
 const DWORD savedLastError=GetLastError();
 // The worker owns actor acquisition. Foreign physics bodies return
 // before touching the potentially changing client roster.
 if(!nativeBody||nativeBody!=g_currentFrameBody.load(std::memory_order_acquire)||
    nativeDestination!=nativeBody+0x90){SetLastError(savedLastError);return;}
 // Physics never waits for worker sampling. Contention cancels the queued
 // request and asks the lock owner to invalidate its epoch before reuse.
 if(!TryAcquireSRWLockExclusive(&g_eligibilityLock)){
  if(nativeBody!=g_currentFrameBody.load()||nativeDestination!=nativeBody+0x90){SetLastError(savedLastError);return;}
  InterlockedExchange(&g_epochInvalidateRequested,1);
  InterlockedExchange(&g_contextBlocked,1);
  InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_boostRemaining),0);
  InterlockedIncrement(&g_cancelGeneration);SetLastError(savedLastError);return;
 }
 const auto decision=EvaluateBoostEligibility();
 if(!decision.native.valid||nativeBody!=decision.native.frame_body||nativeDestination!=nativeBody+0x90){ReleaseSRWLockExclusive(&g_eligibilityLock);SetLastError(savedLastError);return;}
 // Publish only the authoritative player's original (pre-boost) frame.
 memcpy((void*)&g_lastDelta,delta,sizeof(g_lastDelta));g_deltaBody=nativeBody;g_deltaGeneration=InterlockedCompareExchange(&g_cancelGeneration,0,0);
 const bool owned=g_queuedGeneration==InterlockedCompareExchange(&g_cancelGeneration,0,0)&&superjump::traversal::SameIdentity(g_queuedEpoch,decision.native);
 if(!decision.blocked&&g_boostRemaining>0&&!owned){InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_boostRemaining),0);InterlockedIncrement(&g_cancelGeneration);}
 if(!decision.blocked&&owned&&g_boostRemaining>0){
  superjump::boss::Reader reader{nullptr,ReadBossMemory};
#ifdef SUPERJUMP_TESTING
  if(g_testGuardReader.read)reader=g_testGuardReader;
#endif
#ifdef SUPERJUMP_TESTING
  const bool verifyWriter=g_nativeMovementChecksReady;
#else
  constexpr bool verifyWriter=true;
#endif
  if (!MenuAllowsBoost(reader) || (verifyWriter &&
        !superjump::menu60::DeltaReady(reader,g_bossProfile.base,g_menuOwner.Load(),true))) {
   InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_boostRemaining),0);
   InterlockedIncrement(&g_cancelGeneration);
   g_traversalEpoch={};
   ReleaseSRWLockExclusive(&g_eligibilityLock);
   SetLastError(savedLastError);
   return;
  }
  // Atomic claim orders this frame against a concurrent fail-closed cancel.
  if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(&g_boostRemaining))>=0){const uint32_t bits=g_boostYBits;memcpy(delta+1,&bits,sizeof(bits));}
  else InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_boostRemaining),0);
 }
 ReleaseSRWLockExclusive(&g_eligibilityLock);
 SetLastError(savedLastError);
}
static Vec4f ReadPlayerDelta(LONG generation) {
 Vec4f result{};AcquireSRWLockExclusive(&g_eligibilityLock);
 if(g_deltaBody&&g_deltaBody==g_currentFrameBody.load()&&g_deltaGeneration==generation)memcpy(&result,(const void*)&g_lastDelta,sizeof(result));
 ReleaseSRWLockExclusive(&g_eligibilityLock);return result;
}
static bool QueueBoost(LONG expectedGeneration=InterlockedCompareExchange(&g_cancelGeneration,0,0)) {
 AcquireSRWLockExclusive(&g_eligibilityLock);
 const auto decision=EvaluateBoostEligibility();
 bool queued=!decision.blocked&&expectedGeneration==InterlockedCompareExchange(&g_cancelGeneration,0,0);
 if(queued){
  g_queuedEpoch=g_traversalEpoch;g_queuedGeneration=expectedGeneration;
  uint32_t strengthBits=0;memcpy(&strengthBits,&g_boostY,sizeof(g_boostY));g_boostYBits.store(strengthBits);InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_boostRemaining),g_boostFrames);
  if(InterlockedCompareExchange(&g_epochInvalidateRequested,0,0)||expectedGeneration!=InterlockedCompareExchange(&g_cancelGeneration,0,0)){InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_boostRemaining),0);queued=false;}
 }
 ReleaseSRWLockExclusive(&g_eligibilityLock);return queued;
}

static bool RefreshBoostContext(bool forceDiagnostic=false) {
    using namespace superjump;
    AcquireSRWLockExclusive(&g_eligibilityLock);
    const auto decision=EvaluateBoostEligibility();
    ReleaseSRWLockExclusive(&g_eligibilityLock);
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
        LogFmt("input context: nativeUi=%s uiReason=%u mode=%u subtype=%u dirty=%u queued=%u modeRequests=%08X subtypeRequests=%08X worldState=%u gameplayEnabled=%u foreground=%d foregroundReason=%u foregroundHwnd=0x%llX foregroundOwner=%lu foregroundThread=%lu selfPid=%lu osCursor=%d boss=%s bossReason=%u player=0x%llX vtable=0x%llX context=0x%llX uid=%08X bossUid=%08X blocked=%d motionBlocked=%ld",
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

static void ModThread() {
    Sleep(static_cast<DWORD>((std::max)(g_initDelayMs,0)));
    const superjump::boss::Reader reader{nullptr,ReadBossMemory};
    const auto base=reinterpret_cast<uintptr_t>(GetGameBase());
    g_bossProfile=superjump::boss::ValidateProfile(reader,base);
    g_traversalProfileValid=g_bossProfile.valid;
    if (!g_bossProfile.valid || IsFreedomFlyerLoaded()) {
        Log("ERROR: native profile or Freedom Flyer compatibility check refused motion setup");
        return;
    }
    g_nativeMovementChecksReady=true;
    PollMenuOwner();
    // A stock pair is valid when the Menu's movement module is unavailable.
    // An in-progress Menu publication may settle to an owned pair or stock.
    const auto deadline=GetTickCount64()+15000;
    while (!superjump::menu60::LocomotionReady(reader,base,g_menuOwner.Load())) {
        if (g_menuOwner.Load().module==1 || GetTickCount64()>=deadline) {
            Log("ERROR: Menu locomotion detour did not verify; motion producer untouched");
            return;
        }
        Sleep(25);
    }
    g_hookSite=reinterpret_cast<uint8_t*>(g_bossProfile.base+MOTION_PRODUCER_RVA+sizeof(MOTION_PRODUCER));
    if (!InstallHook()) {
        Log("ERROR: native motion contract or SafetyHook setup refused");
        return;
    }
    bool inputRequiresNeutral=true;
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

    while (true) {
        Sleep(16);
        PollMenuOwner();
        DWORD now = GetTickCount();
        if (now - lastIni > 1000) { lastIni = now; LoadConfig(); }
        if (!g_enabled || !GameHasForeground()) {
            inputRequiresNeutral=true;
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

        // SuperJump uses TWO keyboard modifiers (Shift + C) + key (Space).
        // cdinput::Hotkey only models one kb modifier, so we hand-compose
        // the kb leg and OR with cdinput's XInput leg.
        bool kbKeyDown = cdinput::KbDown(g_hotkeyKey);
        bool kbModsDown =
            (g_hotkeyMod  == 0 || cdinput::KbDown(g_hotkeyMod)) &&
            (g_hotkeyMod2 == 0 || cdinput::KbDown(g_hotkeyMod2));
        uint8_t xiConnectedMask = 0;
        uint8_t xiProviderMask = 0;
        uint16_t xiButtons = cdinput::XInputButtons(
            &xiConnectedMask, &xiProviderMask);
        uint16_t xiComboMask = (uint16_t)(g_xiBtn | g_xiMod);
        uint16_t xiMenuMask = (uint16_t)(0x000F | 0x0030 | 0x00C0 | 0xE000 | 0x0200);
        bool xiLeg = g_xiBtn != 0 && xiComboMask != 0 &&
                     (xiButtons & xiComboMask) == xiComboMask;
        if (!GameHasForeground()) {
            inputRequiresNeutral=true;
            continue;
        }
        if (inputRequiresNeutral) {
            const bool jumpHeld=cdinput::KbDown(g_jumpKey) ||
                (g_xiJumpBtn && (xiButtons&g_xiJumpBtn)==g_xiJumpBtn);
            if (kbKeyDown || xiLeg || jumpHeld || !GameHasForeground()) continue;
            inputRequiresNeutral=false;
            prevXiButtons=xiButtons;
            prevXiConnectedMask=xiConnectedMask;
            prevXiProviderMask=xiProviderMask;
        }
        const bool prohibitedContext=RefreshBoostContext();
        const LONG cancellation=InterlockedCompareExchange(&g_cancelGeneration,0,0);
        if(prohibitedContext||cancellation!=seenCancellation){
            superjump::CancelPendingActivation(activation);
            prevKbDown=kbKeyDown;prevXiDown=xiLeg;prevJumpDown=cdinput::KbDown(g_jumpKey)||(g_xiJumpBtn!=0&&(xiButtons&g_xiJumpBtn)==g_xiJumpBtn);
            xiConsumed=xiLeg;xiDownSince=now;superjump::ResetAxiomGuard(axiomGuardState);
            seenCancellation=cancellation;
        }
        const Vec4f playerDelta=ReadPlayerDelta(cancellation);
        bool interactionUi = prohibitedContext;
        bool controllerLockout = (int32_t)(xiLockoutUntil - now) > 0;
        const superjump::KeyboardKeypressDecision keyboardDecision =
            superjump::EvaluateKeyboardKeypress(
                g_triggerMode, kbKeyDown, prevKbDown, kbModsDown,
                interactionUi, controllerLockout);
        bool otherControllerButtonPressed =
            (xiButtons & xiMenuMask & (uint16_t)~xiComboMask & (uint16_t)~prevXiButtons) != 0;
        bool controllerIdleReady = (int32_t)(now - xiIdleSince) >=
                                   (int32_t)(g_controllerIdleMs < 0 ? 0 : g_controllerIdleMs);
        bool triggered = false;
        bool triggerFromDoubleJump = false;
        bool triggeredByKeyboard = false;
        bool keyboardBlockedByUi = false;
        bool keyboardBlockedBySession = false;
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

        if (g_triggerMode == 2) {
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
        } else if (g_triggerMode == 1) {
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
                        doubleJumpPendingAt = now + (DWORD)(g_doubleJumpFireDelayMs < 0 ? 0 : g_doubleJumpFireDelayMs);
                        lastJumpPressMs = 0;
                        doubleJumpArmed = false;
                        LogFmt("input double-jump second press detected (dy=%.4f airborneMs=%lu)",
                               playerDelta.y, (unsigned long)airborneMs);
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
            switch (keyboardDecision) {
            case superjump::KeyboardKeypressDecision::Trigger:
                triggered = true;
                triggeredByKeyboard = true;
                break;
            case superjump::KeyboardKeypressDecision::SuppressedUi:
                keyboardBlockedByUi = true;
                break;
            case superjump::KeyboardKeypressDecision::SuppressedSession:
                keyboardBlockedBySession = true;
                break;
            case superjump::KeyboardKeypressDecision::None:
                break;
            }
        }

        if (superjump::ShouldEvaluateChordTrigger(g_triggerMode) &&
            (interactionUi || otherControllerButtonPressed) &&
            (xiLeg || controllerLockout || interactionUi)) {
            xiLockoutUntil = now + (DWORD)(g_controllerLockoutMs < 0 ? 0 : g_controllerLockoutMs);
            controllerLockout = true;
            if (xiLeg) xiConsumed = true;
        }

        if (superjump::ShouldEvaluateChordTrigger(g_triggerMode) && xiLeg) {
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
        } else if (superjump::ShouldEvaluateChordTrigger(g_triggerMode)) {
            if (prevXiDown && !xiConsumed) {
                xiLockoutUntil = now + (DWORD)(g_controllerLockoutMs < 0 ? 0 : g_controllerLockoutMs);
            }
            xiDownSince = 0;
            xiConsumed = false;
            if (xiButtons == 0) xiIdleSince = now;
        }

        prevKbDown = kbKeyDown;
        prevXiDown = xiLeg;
        prevXiButtons = xiButtons;
        prevXiConnectedMask = xiConnectedMask;
        prevXiProviderMask = xiProviderMask;
        if (keyboardBlockedByUi) {
            Log("suppressed keyboard trigger - interaction UI visible");
        }
        if (keyboardBlockedBySession) {
            Log("suppressed keyboard trigger - controller interaction session active");
        }
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

        // v1.4 stationary gate: use g_lastDelta from the motion hook (the engine's
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
                        static_cast<DWORD>((std::max)(g_crouchDurationMs,0))).detach();
            Sleep(80);
        }

        if (RefreshBoostContext(true)) continue;
        if(!QueueBoost(cancellation))continue;
        LogFmt("FIRE queued - boost=%.1f frames=%d producer=4603323 trigger=%s",
               g_boostY, g_boostFrames, 
               triggerFromDoubleJump ? "doublejump" :
               (triggeredByKeyboard ? "keyboard-chord" : "controller-chord"));
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
        SetKeypressIniDirectory(dir);
        g_log.open(dir + "SuperJump.log");
        Log("v1.5.91 normal-keypress-k31-safetyhook loaded");
        LogFmt("settings path=%s standaloneKeypressINI=1 sharedINIRead=0", g_iniPath.c_str());
        LoadConfig();
        std::thread(ModThread).detach();
    }
    else if (reason == DLL_PROCESS_DETACH) {
        // Pinned workers/callbacks and their variant lease stay process lifetime.
        // The OS releases resources at process exit; hot detach never unpatches.
        if(!g_ownPinned.load(std::memory_order_acquire)&&g_variantMutex){CloseHandle(g_variantMutex);g_variantMutex=nullptr;}
    }
    return TRUE;
}





