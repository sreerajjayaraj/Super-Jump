// SuperJump.cpp â€” Crimson Desert ASI mod  v1.3
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
//   * The cave hook is unchanged â€” it still publishes g_lastDelta and
//     handles the boost write. Only the trigger-side check moved.
//
// v1.2 (Nexus release):
//   * Adds HotkeyMod2 (default C, 0x43) â€” a second modifier key that must be
//     held alongside HotkeyMod for the chord to fire. Set HotkeyMod2=0 in the
//     INI to disable the second modifier and fall back to v1.1 behaviour.
//
// v1.1: hook chaining
//   If another mod (FreedomFlyer etc.) already hooked the DELTA site when SuperJump
//   wakes up, SuperJump installs a 5-byte rel32 JMP (so the other mod's return
//   address stays valid), does NOT emit the original instruction (the other mod's
//   cave does that), and jumps to the other mod's cave at the end of our cave.
//   The yield window gives other mods up to 5 s to hook first.
//   If SuperJump hooks first (no other mod present), behaviour is unchanged:
//   14-byte abs JMP, original instructions emitted in cave, return past patch.
//
// Build: x64 DLL, rename to SuperJump.asi, drop in bin64/
// Targets CrimsonDesert.exe v1.0.0.1670

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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

namespace cdmod {
static uintptr_t ScanExecForBytes(uint8_t* base, const uint8_t* sig, size_t sigLen,
                                  int maxHits, int* hitsOut) {
    if (hitsOut) *hitsOut = 0;
    if (!base || !sig || !sigLen) return 0;

    auto* dos = (PIMAGE_DOS_HEADER)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    auto* nt = (PIMAGE_NT_HEADERS)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;

    uintptr_t first = 0;
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD s = 0; s < nt->FileHeader.NumberOfSections; ++s) {
        if (!(sec[s].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        uint8_t* start = base + sec[s].VirtualAddress;
        size_t size = sec[s].Misc.VirtualSize ? sec[s].Misc.VirtualSize : sec[s].SizeOfRawData;
        if (size < sigLen) continue;

        for (size_t i = 0; i + sigLen <= size; ++i) {
            if (!memcmp(start + i, sig, sigLen)) {
                if (!first) first = (uintptr_t)(start + i);
                if (hitsOut) ++*hitsOut;
                if (hitsOut && *hitsOut >= maxHits) return first;
            }
        }
    }
    return first;
}

static uintptr_t ResolvePlayerStaticVA(uint8_t*) {
    return 0;
}
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
    auto getAsyncKeyState = ResolveGetAsyncKeyState();
    return vk != 0 && getAsyncKeyState && (getAsyncKeyState(vk) & 0x8000) != 0;
}

static uint16_t XInputButtons() {
    using XInputGetStateFn = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);
    static HMODULE mod = []() -> HMODULE {
        if (HMODULE h = LoadLibraryA("xinput1_4.dll")) return h;
        if (HMODULE h = LoadLibraryA("xinput1_3.dll")) return h;
        return LoadLibraryA("xinput9_1_0.dll");
    }();
    static XInputGetStateFn getState =
        mod ? (XInputGetStateFn)GetProcAddress(mod, "XInputGetState") : nullptr;
    if (!getState) return 0;

    for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
        XINPUT_STATE st = {};
        if (getState(i, &st) == ERROR_SUCCESS) {
            return st.Gamepad.wButtons;
        }
    }
    return 0;
}

static bool XInputAllHeld(uint16_t mask) {
    if (!mask) return false;
    uint16_t buttons = XInputButtons();
    return (buttons & mask) == mask;
}
}

using SendInputFn = UINT (WINAPI*)(UINT, LPINPUT, int);
static SendInputFn ResolveSendInput() {
    static HMODULE user32 = LoadLibraryA("user32.dll");
    static SendInputFn fn = user32
        ? (SendInputFn)GetProcAddress(user32, "SendInput")
        : nullptr;
    return fn;
}

using GetCursorInfoFn = BOOL (WINAPI*)(PCURSORINFO);
static GetCursorInfoFn ResolveGetCursorInfo() {
    static HMODULE user32 = LoadLibraryA("user32.dll");
    static GetCursorInfoFn fn = user32
        ? (GetCursorInfoFn)GetProcAddress(user32, "GetCursorInfo")
        : nullptr;
    return fn;
}


// â”€â”€â”€ Config â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
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
static int      g_chainYieldMs      = 45000;
static int      g_chainProbeMs      = 100;
static bool     g_requireFreedomFlyerChain = true;
static bool     g_blockCursorUi     = true;
static bool     g_enabled           = true;
static bool     g_animCrouch        = true;
static int      g_crouchKey         = VK_CONTROL;
static int      g_crouchDurationMs  = 120;

// â”€â”€â”€ Game addresses (v1.06.00) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// AOB-resolved at install time. Default is v1.06.00; overwritten on first
// successful CD_Player AOB hit (see InstallHooks / FindHookSite).
static uintptr_t g_playerStaticVA = 0x145F27F60ULL;

// DELTA hook bytes: MOVAPS xmm0,xmm6 / SUBSS xmm9,xmm8 / ADDPS xmm0,[r13] / MOVUPS [r13],xmm0
static const uint8_t DELTA_SIG[] = {
    0x0F,0x28,0xC6, 0xF3,0x45,0x0F,0x5C,0xC8,
    0x41,0x0F,0x58,0x45,0x00, 0x41,0x0F,0x11,0x45,0x00
};
static constexpr size_t DELTA_SIG_LEN      = 18;
static constexpr size_t HOOK_PATCH_LEN_ABS = 14; // FF 25 00 00 00 00 + 8B addr
static constexpr size_t HOOK_PATCH_LEN_REL = 5;  // E9 + 4B rel32

static constexpr uint32_t DELTA_RVA = 0x384781FU;

// â”€â”€â”€ Shared state (cave <-> main thread) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
struct alignas(16) Vec4f { float x, y, z, w; };
static volatile Vec4f    g_lastDelta      = {};
static volatile int32_t  g_boostRemaining = 0;
static volatile uint32_t g_boostYBits    = 0;

// â”€â”€â”€ Hook state â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static uint8_t* g_hookSite  = nullptr;
static uint8_t  g_origBytes[DELTA_SIG_LEN] = {};
static uint8_t* g_cave      = nullptr;
// Chain mode normally patches hookSite -> g_cave with a 5-byte rel32 JMP.
// If the real cave is outside rel32 reach, this points to a tiny nearby
// absolute-JMP stub, and the hook patches hookSite -> g_entryStub instead.
static uint8_t* g_entryStub = nullptr;
// Non-null when chaining: address of the prior mod's cave entry.
static uint8_t* g_chainNext = nullptr;
static bool     g_startedInGame = false;

// â”€â”€â”€ Log â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static std::ofstream g_log;
static void Log(const char* msg) {
    if (g_log.is_open()) { g_log << "SuperJump: " << msg << "\n"; g_log.flush(); }
}
static void LogFmt(const char* fmt, ...) {
    char buf[512]; va_list ap; va_start(ap,fmt); vsnprintf(buf,512,fmt,ap); va_end(ap); Log(buf);
}

static bool EndsWithNoCase(const char* text, const char* suffix) {
    if (!text || !suffix) return false;
    size_t textLen = strlen(text);
    size_t suffixLen = strlen(suffix);
    if (suffixLen > textLen) return false;
    return _stricmp(text + textLen - suffixLen, suffix) == 0;
}

static bool IsCrimsonDesertProcess() {
    char exePath[MAX_PATH] = {};
    if (!GetModuleFileNameA(nullptr, exePath, MAX_PATH)) return false;
    return EndsWithNoCase(exePath, "\\CrimsonDesert.exe") ||
           _stricmp(exePath, "CrimsonDesert.exe") == 0;
}

// â”€â”€â”€ INI â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
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
    g_chainYieldMs      = IniInt  ("ChainYieldMs",     45000);
    g_chainProbeMs      = IniInt  ("ChainProbeMs",     100);
    g_requireFreedomFlyerChain = !!IniInt("RequireFreedomFlyerChain", 1);
    g_blockCursorUi     = !!IniInt("ControllerBlockCursorUi", 1);
    g_enabled           = !!IniInt("Enabled",          1);
    g_animCrouch        = !!IniInt("AnimCrouch",       1);
    g_crouchKey         = IniInt  ("CrouchKey",        VK_CONTROL);
    g_crouchDurationMs  = IniInt  ("CrouchDurationMs", 120);
    g_boostYBits        = *reinterpret_cast<const uint32_t*>(&g_boostY);
}

// â”€â”€â”€ SEH-wrapped scalar reads â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// Kept as separate leaf functions because MSVC /EHsc rejects __try in any
// function that needs C++ object unwinding â€” ModThread spawns std::thread,
// so the SEH-deref has to live in a function without C++ destructors.
static uintptr_t SafeReadPtr(uintptr_t addr, bool* okOut) {
    __try {
        uintptr_t v = *reinterpret_cast<volatile uintptr_t*>(addr);
        if (okOut) *okOut = true;
        return v;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if (okOut) *okOut = false;
        return 0;
    }
}
static float SafeReadF32(uintptr_t addr, bool* okOut) {
    __try {
        float v = *reinterpret_cast<volatile float*>(addr);
        if (okOut) *okOut = true;
        return v;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if (okOut) *okOut = false;
        return 0.0f;
    }
}

// â”€â”€â”€ Find game module base â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// Primary: PEB->ImageBaseAddress (kernel-tracked, name-independent). See
// Phase.cpp's matching comment for full reasoning. Fallbacks below.
static uint8_t* GetGameBase()
{
    HMODULE h = GetModuleHandleW(L"CrimsonDesert.exe");
    if (h) { LogFmt("base via name: 0x%llX", (uint64_t)h); return (uint8_t*)h; }

    PEB* peb = (PEB*)__readgsqword(0x60);
    if (peb && peb->Reserved3[1]) {
        uint8_t* base = (uint8_t*)peb->Reserved3[1];
        LogFmt("base via PEB->ImageBaseAddress: 0x%llX", (uint64_t)base);
        return base;
    }
    Log("WARN: PEB ImageBaseAddress unreadable â€” falling back to name lookup");

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me = {}; me.dwSize = sizeof(me);
        uint8_t* bestBase = nullptr; DWORD bestSize = 0;
        if (Module32FirstW(snap, &me)) {
            do {
                if (me.modBaseSize > bestSize) { bestSize = me.modBaseSize; bestBase = me.modBaseAddr; }
            } while (Module32NextW(snap, &me));
        }
        CloseHandle(snap);
        if (bestBase) { LogFmt("base via toolhelp (size=0x%X): 0x%llX", bestSize, (uint64_t)bestBase); return bestBase; }
    }

    Log("WARN: toolhelp failed â€” VirtualQuery fallback");
    SYSTEM_INFO si; GetSystemInfo(&si);
    uint8_t* addr = (uint8_t*)si.lpMinimumApplicationAddress;
    uint8_t* maxA = (uint8_t*)si.lpMaximumApplicationAddress;
    uint8_t* bestBase = nullptr; size_t bestSz = 0;
    while (addr < maxA) {
        MEMORY_BASIC_INFORMATION mbi={};
        if (!VirtualQuery(addr,&mbi,sizeof(mbi))) break;
        if (mbi.State==MEM_COMMIT && mbi.Type==MEM_IMAGE &&
            mbi.BaseAddress==mbi.AllocationBase &&
            (mbi.Protect&(PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE))) {
            uint8_t* imgBase = (uint8_t*)mbi.BaseAddress;
            uint8_t* imgEnd  = imgBase;
            MEMORY_BASIC_INFORMATION m2={};
            uint8_t* scan = imgBase;
            while (VirtualQuery(scan,&m2,sizeof(m2)) && m2.Type==MEM_IMAGE && m2.AllocationBase==imgBase) {
                imgEnd = (uint8_t*)m2.BaseAddress + m2.RegionSize; scan = imgEnd;
            }
            size_t sz = (size_t)(imgEnd - imgBase);
            if (sz > bestSz) { bestSz=sz; bestBase=imgBase; }
        }
        addr = (uint8_t*)mbi.BaseAddress + mbi.RegionSize;
    }
    if (bestBase) LogFmt("base via VQ (size=0x%zX): 0x%llX", bestSz, (uint64_t)bestBase);
    return bestBase;
}

// â”€â”€â”€ Detect a prior hook at a site â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static uint8_t* DetectPriorHook(uint8_t* site) {
    if (site[0]==0xFF && site[1]==0x25 &&
        site[2]==0 && site[3]==0 && site[4]==0 && site[5]==0) {
        uint64_t t; memcpy(&t, site+6, 8);
        LogFmt("prior abs-JMP at hook site, chain target=0x%llX", (uint64_t)t);
        return (uint8_t*)t;
    }
    if (site[0]==0xE9) {
        int32_t r; memcpy(&r, site+1, 4);
        uint8_t* t = site + 5 + r;
        LogFmt("prior rel32-JMP at hook site, chain target=0x%llX", (uint64_t)t);
        return t;
    }
    return nullptr;
}

static uint8_t* ResolveDeltaSiteByAob(uint8_t* base) {
    int hits = 0;
    const uintptr_t found = cdmod::ScanExecForBytes(base, DELTA_SIG, DELTA_SIG_LEN, 2, &hits);
    if (hits == 1 && found) {
        LogFmt("DELTA: unique AOB match RVA 0x%llX", (uint64_t)(found - (uintptr_t)base));
        return (uint8_t*)found;
    }
    if (hits > 1) {
        LogFmt("WARN: DELTA AOB ambiguous (%d hits); trying known validated RVAs", hits);
    } else {
        Log("DELTA: AOB miss; trying known validated RVAs");
    }
    return nullptr;
}

static uint8_t* ResolveDeltaSiteByKnownRva(uint8_t* base, bool allowHooked) {
    static constexpr uint32_t kKnownDeltaRvas[] = {
        0x3A2393FU, // CrimsonDesert.exe 1.0.0.1855 / 1.12.02
        0x3A2407FU, // CrimsonDesert.exe 1.0.0.1819 / 1.12
        0x392F22FU, // CrimsonDesert.exe 1.0.0.1744 / 1.11
        DELTA_RVA,  // legacy v1.06 fallback, byte-validated before use
    };

    for (const uint32_t rva : kKnownDeltaRvas) {
        uint8_t* site = base + rva;
        LogFmt("DELTA: known RVA candidate 0x%X bytes: %02X %02X %02X %02X",
               rva, site[0], site[1], site[2], site[3]);
        if (memcmp(site, DELTA_SIG, DELTA_SIG_LEN) == 0) {
            LogFmt("DELTA: known RVA 0x%X byte-validated", rva);
            return site;
        }
        if (allowHooked) {
            uint8_t* chained = DetectPriorHook(site);
            if (chained) {
                g_chainNext = chained;
                LogFmt("DELTA: known RVA 0x%X already patched; chaining", rva);
                return site;
            }
        }
    }
    return nullptr;
}

static uint8_t* ResolveDeltaSite(uint8_t* base, bool allowHooked) {
    if (!base) return nullptr;
    if (uint8_t* site = ResolveDeltaSiteByAob(base)) {
        return site;
    }
    return ResolveDeltaSiteByKnownRva(base, allowHooked);
}

static uint8_t* FindHookSite()
{
    uint8_t* base = GetGameBase();
    if (!base) { Log("ERROR: could not locate game module"); return nullptr; }

    // One-time current player-static-VA AOB resolve.
    if (g_playerStaticVA == 0x145F27F60ULL) {
        if (uintptr_t resolved = cdmod::ResolvePlayerStaticVA(base)) {
            g_playerStaticVA = resolved;
            LogFmt("playerStaticVA: 0x%llX  (AOB-resolved)", (uint64_t)resolved);
        } else {
            LogFmt("playerStaticVA: 0x%llX  (v1.06 fallback - AOB miss)",
                   (uint64_t)g_playerStaticVA);
        }
    }

    uint8_t* site = ResolveDeltaSite(base, true);
    if (!site) {
        Log("ERROR: DELTA site unresolved; failing closed");
        return nullptr;
    }

    LogFmt("hook site candidate: 0x%llX  bytes: %02X %02X %02X %02X",
           (uint64_t)site, site[0],site[1],site[2],site[3]);

    if (memcmp(site, DELTA_SIG, DELTA_SIG_LEN) == 0) {
        if (g_requireFreedomFlyerChain && IsFreedomFlyerLoaded()) {
            Log("FreedomFlyer is loaded but DELTA is still original; installing direct hook (no chain target present)");
        }
        return site;
    }

    g_chainNext = DetectPriorHook(site);
    if (g_chainNext) {
        Log("hook site already patched; chaining");
        return site;
    }

    Log("ERROR: resolved DELTA bytes mismatch; failing closed");
    return nullptr;
}

static uint8_t* AllocCave(size_t size) {
    if (!g_chainNext) {
        void* p = VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (p) {
            LogFmt("AllocCave[ABS]: %lld bytes at 0x%llX (absolute hook; near cave not required)",
                   (long long)size, (uint64_t)p);
            return (uint8_t*)p;
        }
        Log("ERROR: AllocCave[ABS] failed");
        return nullptr;
    }
    SYSTEM_INFO si; GetSystemInfo(&si);
    DWORD gran = si.dwAllocationGranularity ? si.dwAllocationGranularity
                                             : 0x10000;
    uintptr_t hookAddr = (uintptr_t)g_hookSite;

    // â”€â”€â”€â”€â”€ Strategy A: brute-force VirtualAlloc-at-hint â”€â”€â”€â”€â”€
    if (hookAddr) {
        for (intptr_t d = 0x10000; d < 0x70000000LL; d += 0x10000) {
            for (int s : {-1, 1}) {
                uintptr_t c = hookAddr + s * d;
                if (c < (uintptr_t)si.lpMinimumApplicationAddress) continue;
                if (c > (uintptr_t)si.lpMaximumApplicationAddress) continue;
                void* p = VirtualAlloc((LPVOID)c, size,
                                       MEM_COMMIT | MEM_RESERVE,
                                       PAGE_EXECUTE_READWRITE);
                if (p) {
                    LogFmt("AllocCave[A]: %lld bytes at 0x%llX (%+lld bytes from hook)",
                           (long long)size, (uint64_t)p,
                           (long long)((intptr_t)((uintptr_t)p - hookAddr)));
                    return (uint8_t*)p;
                }
            }
        }
        Log("AllocCave[A]: 64KB-stride hint sweep exhausted, trying VirtualQuery walk");
    }

    // â”€â”€â”€â”€â”€ Strategy B: VirtualQuery-walk for MEM_FREE gaps â”€â”€â”€â”€â”€
    if (hookAddr) {
        constexpr intptr_t MAX_DIST = 0x7E000000LL; // ~2GB, slightly inside rel32
        auto scanDirection = [&](int sign) -> uint8_t* {
            uintptr_t cursor = hookAddr;
            intptr_t walked = 0;
            int regionsSeen = 0, freeRegionsSeen = 0;
            while (walked < MAX_DIST) {
                if (cursor < (uintptr_t)si.lpMinimumApplicationAddress) break;
                if (cursor > (uintptr_t)si.lpMaximumApplicationAddress) break;
                MEMORY_BASIC_INFORMATION mbi = {};
                if (VirtualQuery((LPCVOID)cursor, &mbi, sizeof(mbi)) == 0) break;
                regionsSeen++;
                if (mbi.State == MEM_FREE) {
                    freeRegionsSeen++;
                    // Try multiple aligned offsets inside this free region.
                    // Most allocations will land at the first aligned offset, but
                    // sometimes the OS rejects the exact start; step through.
                    uintptr_t regionStart = (uintptr_t)mbi.BaseAddress;
                    uintptr_t regionEnd   = regionStart + mbi.RegionSize;
                    uintptr_t aligned     = (regionStart + gran - 1) & ~(uintptr_t)(gran - 1);
                    while (aligned + size <= regionEnd) {
                        intptr_t dist = (intptr_t)aligned - (intptr_t)hookAddr;
                        if (dist < -MAX_DIST || dist > MAX_DIST) break;
                        void* p = VirtualAlloc((LPVOID)aligned, size,
                                               MEM_COMMIT | MEM_RESERVE,
                                               PAGE_EXECUTE_READWRITE);
                        if (p) {
                            LogFmt("AllocCave[B]: %lld bytes at 0x%llX "
                                   "(%+lld bytes from hook; %d regions walked, %d free)",
                                   (long long)size, (uint64_t)p,
                                   (long long)dist, regionsSeen, freeRegionsSeen);
                            return (uint8_t*)p;
                        }
                        aligned += gran;
                    }
                }
                // Advance past this region.
                uintptr_t regionEnd2 = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
                uintptr_t step = (sign > 0)
                                 ? (regionEnd2 - cursor)
                                 : (cursor - (uintptr_t)mbi.BaseAddress + 1);
                if (step == 0) step = gran;
                walked += (intptr_t)step;
                if (sign > 0) {
                    cursor = regionEnd2;
                } else {
                    if ((uintptr_t)mbi.BaseAddress == 0) break;
                    cursor = (uintptr_t)mbi.BaseAddress - 1;
                }
            }
            return nullptr;
        };

        if (uint8_t* p = scanDirection(+1)) return p;
        if (uint8_t* p = scanDirection(-1)) return p;
    }

    // â”€â”€â”€â”€â”€ Final fallback â€” anywhere â”€â”€â”€â”€â”€
    Log("AllocCave: WARN â€” both strategies exhausted; OS-chosen address "
        "(rel32 will overflow in chain mode and install will refuse)");
    return (uint8_t*)VirtualAlloc(nullptr, size,
                                  MEM_COMMIT | MEM_RESERVE,
                                  PAGE_EXECUTE_READWRITE);
}

static bool IsRel32Reachable(uint8_t* from, uint8_t* to) {
    intptr_t rel = (intptr_t)to - (intptr_t)from - 5;
    return rel >= INT32_MIN && rel <= INT32_MAX;
}

static bool IsReadableExecProtect(DWORD protect) {
    if (protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    DWORD p = protect & 0xFF;
    return p == PAGE_EXECUTE_READ ||
           p == PAGE_EXECUTE_READWRITE ||
           p == PAGE_EXECUTE_WRITECOPY;
}

static uint8_t* FindNearImageCodeCave(size_t size) {
    if (!g_hookSite) return nullptr;

    SYSTEM_INFO si; GetSystemInfo(&si);
    constexpr intptr_t MAX_DIST = 0x7E000000LL;
    uintptr_t hookAddr = (uintptr_t)g_hookSite;
    uintptr_t minA = (uintptr_t)si.lpMinimumApplicationAddress;
    uintptr_t maxA = (uintptr_t)si.lpMaximumApplicationAddress;
    uintptr_t lo = hookAddr > (uintptr_t)MAX_DIST ? hookAddr - MAX_DIST : minA;
    uintptr_t hi = hookAddr + MAX_DIST;
    if (lo < minA) lo = minA;
    if (hi > maxA || hi < hookAddr) hi = maxA;

    for (uintptr_t cursor = lo; cursor < hi; ) {
        MEMORY_BASIC_INFORMATION mbi = {};
        if (!VirtualQuery((LPCVOID)cursor, &mbi, sizeof(mbi))) break;

        uintptr_t regionStart = (uintptr_t)mbi.BaseAddress;
        uintptr_t regionEnd = regionStart + mbi.RegionSize;
        if (regionEnd <= cursor) {
            cursor += 0x1000;
            continue;
        }

        if (mbi.State == MEM_COMMIT &&
            mbi.Type == MEM_IMAGE &&
            IsReadableExecProtect(mbi.Protect)) {
            uintptr_t scanStart = regionStart > lo ? regionStart : lo;
            uintptr_t scanEnd = regionEnd < hi ? regionEnd : hi;

            for (uint8_t fill : { (uint8_t)0xCC, (uint8_t)0x00 }) {
                uint8_t* p = (uint8_t*)scanStart;
                uint8_t* end = (uint8_t*)scanEnd;
                while (p + size <= end) {
                    if (!IsRel32Reachable(g_hookSite, p)) {
                        ++p;
                        continue;
                    }
                    size_t n = 0;
                    while (n < size && p[n] == fill) ++n;
                    if (n == size) {
                        LogFmt("EntryStub: image cave at 0x%llX (%zu bytes of %02X)",
                               (uint64_t)p, size, fill);
                        return p;
                    }
                    p += n ? n : 1;
                }
            }
        }

        cursor = regionEnd;
    }

    Log("EntryStub: no nearby executable padding cave found");
    return nullptr;
}

static bool BuildEntryStub() {
    if (!g_entryStub) return true;

    DWORD old = 0;
    if (!VirtualProtect(g_entryStub, HOOK_PATCH_LEN_ABS, PAGE_EXECUTE_READWRITE, &old)) {
        Log("ERROR: EntryStub VirtualProtect failed");
        return false;
    }

    memset(g_entryStub, 0x90, HOOK_PATCH_LEN_ABS);
    g_entryStub[0] = 0xFF;
    g_entryStub[1] = 0x25;
    uint32_t zero = 0;
    memcpy(g_entryStub + 2, &zero, 4);
    uint64_t target = (uint64_t)g_cave;
    memcpy(g_entryStub + 6, &target, 8);

    VirtualProtect(g_entryStub, HOOK_PATCH_LEN_ABS, old, &old);
    FlushInstructionCache(GetCurrentProcess(), g_entryStub, HOOK_PATCH_LEN_ABS);
    LogFmt("EntryStub: installed at 0x%llX -> cave 0x%llX",
           (uint64_t)g_entryStub, (uint64_t)g_cave);
    return true;
}

// â”€â”€â”€ Build shellcode cave â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// Chaining (g_chainNext != null):
//   boost logic -> jump to prior mod's cave (which emits original instruction itself)
//   We use a 5-byte rel32 patch so the prior mod's hookSite+N return is untouched.
// Normal (g_chainNext == null):
//   boost logic -> original instructions -> return to hookSite+14
//   We use a 14-byte abs JMP patch.
static void BuildCave() {
    uint8_t* c = g_cave;
    uintptr_t returnAddr = g_chainNext
        ? (uintptr_t)g_chainNext
        : (uintptr_t)g_hookSite + HOOK_PATCH_LEN_ABS;

    auto B  = [&](std::initializer_list<uint8_t> bs) { for (auto b:bs) *c++=b; };
    auto B8 = [&](uint64_t v) { memcpy(c,&v,8); c+=8; };

    // v1.5a: Save RCX + EFLAGS too. FreedomFlyer's trampoline runs its
    // own state-machine logic after the original 18 DELTA bytes; if our
    // boost path leaves ECX = boost-counter and EFLAGS = whatever-test-
    // ecx-ecx-set, FF's code reads garbage and faults. Cheap to save:
    // 2 bytes prologue (pushfq + push rcx), 2 bytes epilogue.
    B({0x9C});                           // pushfq          (flags)
    B({0x50});                           // push rax
    B({0x51});                           // push rcx        (NEW: preserve)
    B({0x48,0x83,0xEC,0x28});            // sub rsp,0x28
    B({0x0F,0x11,0x3C,0x24});            // movups [rsp],    xmm7
    B({0x0F,0x11,0x74,0x24,0x10});       // movups [rsp+16], xmm6
    B({0x48,0xB8}); B8((uint64_t)&g_lastDelta);
    B({0x0F,0x11,0x30});                 // movups [rax], xmm6   (capture delta)
    B({0x48,0xB8}); B8((uint64_t)&g_boostRemaining);
    B({0x8B,0x08});                      // mov ecx,[rax]
    B({0x85,0xC9});                      // test ecx,ecx
    B({0x74,0x19});                      // jz .no_boost
    B({0xFF,0xC9});                      // dec ecx
    B({0x89,0x08});                      // mov [rax],ecx
    B({0x48,0xB8}); B8((uint64_t)&g_boostYBits);
    B({0x8B,0x00});                      // mov eax,[rax]
    B({0x89,0x44,0x24,0x14});            // mov [rsp+0x14],eax  (Y float into xmm6 slot)
    B({0x0F,0x10,0x74,0x24,0x10});       // movups xmm6,[rsp+16]
    // .no_boost:
    B({0x0F,0x10,0x3C,0x24});            // movups xmm7,[rsp]
    B({0x48,0x83,0xC4,0x28});            // add rsp,0x28
    B({0x59});                           // pop rcx         (NEW: restore)
    B({0x58});                           // pop rax
    B({0x9D});                           // popfq           (flags)

    if (!g_chainNext) {
        // No chain: emit original stomped instructions then return
        memcpy(c, g_origBytes, DELTA_SIG_LEN); c += DELTA_SIG_LEN;
    }
    // Jump to prior mod's cave (chain) or back past our patch (normal)
    B({0xFF,0x25,0x00,0x00,0x00,0x00});
    B8(returnAddr);
}

// â”€â”€â”€ Install / remove â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static bool InstallHook() {
    DWORD old;
    if (g_chainNext) {
        // 5-byte rel32 â€” keeps prior mod's return address valid
        uint8_t* patchTarget = g_entryStub ? g_entryStub : g_cave;
        intptr_t rel = (intptr_t)patchTarget - (intptr_t)g_hookSite - 5;
        if (rel < INT32_MIN || rel > INT32_MAX) {
            Log("ERROR: patch target too far for rel32 in chain mode");
            return false;
        }
        if (!VirtualProtect(g_hookSite, HOOK_PATCH_LEN_REL, PAGE_EXECUTE_READWRITE, &old)) return false;
        g_hookSite[0] = 0xE9;
        int32_t r32 = (int32_t)rel;
        memcpy(g_hookSite+1, &r32, 4);
        VirtualProtect(g_hookSite, HOOK_PATCH_LEN_REL, old, &old);
        FlushInstructionCache(GetCurrentProcess(), g_hookSite, HOOK_PATCH_LEN_REL);
    } else {
        // 14-byte abs JMP
        uint8_t patch[HOOK_PATCH_LEN_ABS] = {0xFF,0x25,0x00,0x00,0x00,0x00};
        uint64_t addr = (uint64_t)g_cave;
        memcpy(patch+6, &addr, 8);
        if (!VirtualProtect(g_hookSite, HOOK_PATCH_LEN_ABS, PAGE_EXECUTE_READWRITE, &old)) return false;
        memcpy(g_hookSite, patch, HOOK_PATCH_LEN_ABS);
        VirtualProtect(g_hookSite, HOOK_PATCH_LEN_ABS, old, &old);
        FlushInstructionCache(GetCurrentProcess(), g_hookSite, HOOK_PATCH_LEN_ABS);
    }
    return true;
}

static void RemoveHook() {
    if (!g_hookSite) return;
    size_t patchLen = g_chainNext ? HOOK_PATCH_LEN_REL : HOOK_PATCH_LEN_ABS;
    DWORD old;
    VirtualProtect(g_hookSite, patchLen, PAGE_EXECUTE_READWRITE, &old);
    memcpy(g_hookSite, g_origBytes, patchLen);
    VirtualProtect(g_hookSite, patchLen, old, &old);
    FlushInstructionCache(GetCurrentProcess(), g_hookSite, patchLen);
}

// â”€â”€â”€ Crouch simulation â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static void SimulateCrouch() {
    if (!g_animCrouch) return;
    auto sendInput = ResolveSendInput();
    if (!sendInput) return;
    INPUT inp = {}; inp.type = INPUT_KEYBOARD;
    inp.ki.wVk = (WORD)g_crouchKey;
    sendInput(1, &inp, sizeof(INPUT));
    Sleep(g_crouchDurationMs);
    inp.ki.dwFlags = KEYEVENTF_KEYUP;
    sendInput(1, &inp, sizeof(INPUT));
}

static bool InteractionUiVisible() {
    if (!g_blockCursorUi) return false;
    auto getCursorInfo = ResolveGetCursorInfo();
    if (!getCursorInfo) return false;

    CURSORINFO ci = {};
    ci.cbSize = sizeof(ci);
    return getCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING);
}

// â”€â”€â”€ Main thread â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static void ModThread() {
    LogFmt("waiting for game to load... InitDelayMs=%d ChainYieldMs=%d", g_initDelayMs, g_chainYieldMs);
    Sleep((DWORD)(g_initDelayMs > 0 ? g_initDelayMs : 0));

    // Yield window: give other mods up to 5 s to hook first so we can chain.
    // Resolve DELTA by unique AOB before yielding when possible; if another mod
    // has already patched the site, fall back to known byte-validated RVAs only.
    Log("yield window...");
    {
        uint8_t* base = GetGameBase();
        if (base) {
            uint8_t* site = ResolveDeltaSite(base, true);
            if (!site) {
                Log("yield window: DELTA site unresolved; install will fail closed");
            } else {
                int probeMs = g_chainProbeMs > 0 ? g_chainProbeMs : 100;
                int maxLoops = (g_chainYieldMs > 0 ? g_chainYieldMs : 0) / probeMs;
                LogFmt("yield window probing site 0x%llX for %d ms", (uint64_t)site,
                       maxLoops * probeMs);
                for (int i = 0; i < maxLoops; ++i) {
                    if (memcmp(site, DELTA_SIG, DELTA_SIG_LEN) != 0) {
                        Log("another mod hooked first - will chain");
                        break;
                    }
                    Sleep((DWORD)probeMs);
                }
                if (g_requireFreedomFlyerChain && IsFreedomFlyerLoaded() &&
                    memcmp(site, DELTA_SIG, DELTA_SIG_LEN) == 0) {
                    Log("FreedomFlyer loaded during yield but DELTA stayed original; proceeding with direct hook");
                }
            }
        }
    }

    Log("locating DELTA hook site...");
    g_hookSite = FindHookSite();
    if (!g_hookSite) { Log("ERROR: DELTA hook site not found"); return; }
    LogFmt("hook site: 0x%llX  chain=%s", (uint64_t)g_hookSite, g_chainNext ? "yes" : "no");

    memcpy(g_origBytes, g_hookSite, DELTA_SIG_LEN);

    g_cave = AllocCave(256);
    if (!g_cave) { Log("ERROR: cave alloc failed"); return; }
    LogFmt("cave: 0x%llX", (uint64_t)g_cave);

    BuildCave();

    if (g_chainNext && !IsRel32Reachable(g_hookSite, g_cave)) {
        Log("chain cave outside rel32; searching for nearby entry stub");
        g_entryStub = FindNearImageCodeCave(HOOK_PATCH_LEN_ABS);
        if (!g_entryStub) {
            Log("ERROR: no rel32-reachable entry stub available");
            return;
        }
        if (!BuildEntryStub()) return;
    }

    if (!InstallHook()) { Log("ERROR: hook install failed"); return; }
    Log("ready!");

    bool  prevKbDown    = false;
    bool  prevXiDown    = false;
    bool  xiConsumed    = false;
    DWORD xiDownSince   = 0;
    DWORD xiLockoutUntil = 0;
    DWORD xiIdleSince    = GetTickCount();
    uint16_t prevXiButtons = 0;
    DWORD lastIni       = GetTickCount();

    while (true) {
        Sleep(16);
        DWORD now = GetTickCount();
        if (now - lastIni > 1000) { lastIni = now; LoadConfig(); }
        if (!g_enabled) {
            prevKbDown = false;
            prevXiDown = false;
            xiConsumed = false;
            xiDownSince = 0;
            xiLockoutUntil = 0;
            xiIdleSince = now;
            prevXiButtons = 0;
            continue;
        }

        // SuperJump uses TWO keyboard modifiers (Shift + C) + key (Space).
        // cdinput::Hotkey only models one kb modifier, so we hand-compose
        // the kb leg and OR with cdinput's XInput leg.
        bool kbLeg = cdinput::KbDown(g_hotkeyKey)
                     && (g_hotkeyMod  == 0 || cdinput::KbDown(g_hotkeyMod))
                     && (g_hotkeyMod2 == 0 || cdinput::KbDown(g_hotkeyMod2));
        uint16_t xiButtons = cdinput::XInputButtons();
        uint16_t xiComboMask = (uint16_t)(g_xiBtn | g_xiMod);
        uint16_t xiMenuMask = (uint16_t)(0x000F | 0x0030 | 0x00C0 | 0xE000 | 0x0200);
        bool xiLeg = g_xiBtn != 0 && xiComboMask != 0 &&
                     (xiButtons & xiComboMask) == xiComboMask;
        bool interactionUi = InteractionUiVisible();
        bool controllerLockout = (int32_t)(xiLockoutUntil - now) > 0;
        bool otherControllerButtonPressed =
            (xiButtons & xiMenuMask & (uint16_t)~xiComboMask & (uint16_t)~prevXiButtons) != 0;
        bool controllerIdleReady = (int32_t)(now - xiIdleSince) >=
                                   (int32_t)(g_controllerIdleMs < 0 ? 0 : g_controllerIdleMs);
        bool triggered = kbLeg && !prevKbDown;
        bool controllerBlockedByUi = false;
        bool controllerBlockedBySession = false;

        if ((interactionUi || otherControllerButtonPressed) &&
            (xiLeg || controllerLockout || interactionUi)) {
            xiLockoutUntil = now + (DWORD)(g_controllerLockoutMs < 0 ? 0 : g_controllerLockoutMs);
            controllerLockout = true;
            if (xiLeg) xiConsumed = true;
        }

        if (xiLeg) {
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
        } else {
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
        if (controllerBlockedByUi) {
            Log("suppressed controller trigger â€” interaction UI visible");
        }
        if (controllerBlockedBySession) {
            Log("suppressed controller trigger - interaction session active");
        }
        if (!triggered) continue;

        // v1.4 stationary gate: use g_lastDelta from the cave (the engine's
        // per-frame physics delta xyz, captured at the DELTA hook site).
        // v1.3 tried to read velocity from [*g_playerStaticVA + 0xC0] but
        // *g_playerStaticVA is the top-of-chain static, NOT the physics body
        // â€” the +0xC0 there is unrelated junk that reads ~1186.0f and
        // permanently suppresses the launch. Reverting to the cave delta,
        // which is what worked in v1.2 and earlier.
        if (g_requireStationary) {
            float dx = g_lastDelta.x, dz = g_lastDelta.z;
            float mag = sqrtf(dx*dx + dz*dz);
            if (mag > g_stationaryThresh) {
                LogFmt("suppressed â€” moving (|Î”|=%.4f, thresh=%.4f)",
                       mag, g_stationaryThresh);
                continue;
            }
        }

        LogFmt("FIRE â€” boost=%.1f frames=%d chain=%s",
               g_boostY, g_boostFrames, g_chainNext ? "yes" : "no");

        if (g_animCrouch) {
            std::thread(SimulateCrouch).detach();
            Sleep(80);
        }

        g_boostYBits     = *reinterpret_cast<const uint32_t*>(&g_boostY);
        g_boostRemaining = g_boostFrames;
    }
}

BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hMod);
        if (!IsCrimsonDesertProcess()) {
            return TRUE;
        }
        g_startedInGame = true;
        char path[MAX_PATH]={};
        GetModuleFileNameA(hMod, path, MAX_PATH);
        std::string dir(path);
        dir = dir.substr(0, dir.find_last_of("\\/")+1);
        g_iniPath = dir + "SuperJump.ini";
        g_log.open(dir + "SuperJump.log");
    Log("v1.5.21 1.0.0.1855 delta-r4 loaded");
        LoadConfig();
        std::thread(ModThread).detach();
    }
    else if (reason == DLL_PROCESS_DETACH) {
        if (g_startedInGame) RemoveHook();
    }
    return TRUE;
}



