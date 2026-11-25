#pragma once

#include "GameImage.h"
#include <safetyhook.hpp>

namespace superjump {

enum class HookOutcome { Installed, PreparationFailed, Changed, EnableFailed, RetainedInert };

class HookGroup {
public:
    HookOutcome Install(const LoadedSites& sites, std::uintptr_t base,
                        const std::shared_ptr<safetyhook::Allocator>& allocator,
                        std::array<safetyhook::MidHookFn, 2> callbacks);
    bool StillOwned() const noexcept;

private:
    bool CapturePatch(std::size_t index) noexcept;
    HookOutcome Rollback(HookOutcome cause) noexcept;

    std::uintptr_t base_{};
    std::array<safetyhook::MidHook, 2> hooks_;
    std::array<Witness, 2> pristine_;
    std::array<Witness, 2> installed_;
};

} // namespace superjump
