#pragma once

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
using XInputGetStateFn = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);
#ifdef SUPERJUMP_TESTING
static GetAsyncKeyStateFn g_testKeyboardPoll=nullptr;
static XInputGetStateFn g_testControllerPoll=nullptr;
#endif
static GetAsyncKeyStateFn ResolveGetAsyncKeyState() {
#ifdef SUPERJUMP_TESTING
    if (g_testKeyboardPoll) return g_testKeyboardPoll;
#endif
    static HMODULE user32 = LoadLibraryA("user32.dll");
    static GetAsyncKeyStateFn fn = user32
        ? (GetAsyncKeyStateFn)GetProcAddress(user32, "GetAsyncKeyState")
        : nullptr;
    return fn;
}

static bool KbDown(int vk) {
    if (!vk || !GameHasForeground()) return false;
    const auto getAsyncKeyState = ResolveGetAsyncKeyState();
    if (!getAsyncKeyState || !GameHasForeground()) return false;
    const SHORT state=getAsyncKeyState(vk);
    return GameHasForeground() && (state & 0x8000) != 0;
}

static uint16_t XInputButtons(
    uint8_t* connectedMaskOut = nullptr,
    uint8_t* providerMaskOut = nullptr) {
    if (connectedMaskOut) *connectedMaskOut = 0;
    if (providerMaskOut) *providerMaskOut = 0;
    if (!GameHasForeground()) return 0;
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

        auto getState = providers[providerIndex];
#ifdef SUPERJUMP_TESTING
        if (g_testControllerPoll) getState=g_testControllerPoll;
#endif
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
    return 0;
}

[[maybe_unused]] static bool XInputAllHeld(uint16_t mask) {
    if (!mask) return false;
    uint16_t buttons = XInputButtons();
    return (buttons & mask) == mask;
}
}
