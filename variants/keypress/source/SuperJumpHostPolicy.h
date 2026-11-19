#pragma once
#include <Windows.h>
#include <cwchar>
#include <iterator>
#include <cstdint>

namespace superjump::host {
inline bool IsGameName(const wchar_t* path) {
    if (!path || !*path) return false;
    const wchar_t* name = path;
    for (auto* p = path; *p; ++p)
        if (*p == L'\\' || *p == L'/') name = p + 1;
    return _wcsicmp(name, L"CrimsonDesert.exe") == 0;
}

inline HMODULE GameExecutable() {
    wchar_t path[1024]{};
    const auto size = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    return size && size < std::size(path) && IsGameName(path) ? GetModuleHandleW(nullptr) : nullptr;
}

inline bool IsGameCode(HMODULE game, uintptr_t address, size_t size) {
    if (!game || game != GameExecutable() || !size) return false;
    MEMORY_BASIC_INFORMATION region{};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &region, sizeof(region)) != sizeof(region)
        || region.AllocationBase != game || region.State != MEM_COMMIT || region.Type != MEM_IMAGE
        || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const auto base = reinterpret_cast<uintptr_t>(region.BaseAddress);
    const auto protection = region.Protect & 0xff;
    return address >= base && address - base < region.RegionSize
        && size <= region.RegionSize - (address - base)
        && (protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE
            || protection == PAGE_EXECUTE_WRITECOPY);
}
}
