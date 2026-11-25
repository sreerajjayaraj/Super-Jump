#pragma once

#include "SiteResolver.h"
#include <Windows.h>
#include <array>
#include <vector>

namespace superjump {

bool ReadGameMemory(void*, std::uintptr_t address, void* output, std::size_t size) noexcept;
bool GameRegion(std::uintptr_t base, std::uintptr_t address, std::size_t size, bool executable) noexcept;

struct Witness {
    std::uintptr_t address{};
    std::array<std::uint8_t, 48> bytes{};
    std::size_t length{};
    bool Matches() const noexcept;
};

struct LoadedSites {
    NativeSites sites;
    std::array<Witness, 2> original;
    std::array<Witness, 5> dependencies;
    std::array<Witness, 4> jumpCommitDependencies;
    bool DependenciesMatch() const noexcept;
};

struct GameResolution {
    ResolveError error{ResolveError::InvalidImage};
    LoadedSites live;
};

GameResolution ResolveGameImage(HMODULE module);

} // namespace superjump
