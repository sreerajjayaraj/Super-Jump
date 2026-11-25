#include "HookGroup.h"

#include <algorithm>
#include <bit>
#include <cstring>

namespace superjump {
namespace {

std::uintptr_t JumpDestination(const std::uint8_t* operand, std::uintptr_t next) noexcept {
    std::int32_t relative{};
    std::memcpy(&relative, operand, sizeof(relative));
    return next + relative;
}

bool CheckContinuation(const safetyhook::MidHook& hook, const Witness& pristine,
                       std::size_t index) noexcept {
    const auto trampoline = hook.owned_trampoline_address();
    std::array<std::uint8_t, 14> code{};
    if (!ReadGameMemory(nullptr, trampoline, code.data(), code.size())) return false;
    if (index == 0) {
        const auto target = pristine.address + 2 + std::bit_cast<std::int8_t>(pristine.bytes[1]);
        return code[0] == 0x0F && code[1] == 0x84 &&
            JumpDestination(code.data() + 2, trampoline + 6) == target &&
            std::equal(pristine.bytes.begin() + 2, pristine.bytes.begin() + 5, code.begin() + 6) &&
            code[9] == 0xE9 && JumpDestination(code.data() + 10, trampoline + 14) == pristine.address + 5;
    }
    return std::equal(pristine.bytes.begin(), pristine.bytes.begin() + 7, code.begin()) &&
        code[7] == 0xE9 && JumpDestination(code.data() + 8, trampoline + 12) == pristine.address + 7;
}

} // namespace

HookOutcome HookGroup::Install(const LoadedSites& sites, std::uintptr_t base,
                               const std::shared_ptr<safetyhook::Allocator>& allocator,
                               std::array<safetyhook::MidHookFn, 2> callbacks) {
    base_ = base;
    pristine_ = sites.original;
    if (!allocator || !sites.DependenciesMatch()) return HookOutcome::PreparationFailed;
    for (std::size_t i = 0; i < hooks_.size(); ++i) {
        const auto& original = pristine_[i];
        if (!GameRegion(base_, original.address, original.length, true) || !original.Matches())
            return HookOutcome::Changed;
        auto hook = safetyhook::MidHook::create(allocator, reinterpret_cast<void*>(original.address),
                                               callbacks[i], safetyhook::MidHook::StartDisabled);
        if (!hook || !std::ranges::equal(hook->original_bytes(),
            std::span(original.bytes).first(original.length)) || !CheckContinuation(*hook, original, i))
            return HookOutcome::PreparationFailed;
        hooks_[i] = std::move(*hook);
    }
    for (std::size_t i = 0; i < hooks_.size(); ++i) {
        if (!sites.DependenciesMatch() || !pristine_[i].Matches()) return Rollback(HookOutcome::Changed);
        if (!hooks_[i].enable()) return Rollback(HookOutcome::EnableFailed);
        if (!CapturePatch(i)) return Rollback(HookOutcome::Changed);
    }
    if (!sites.DependenciesMatch() || !StillOwned()) return Rollback(HookOutcome::Changed);
    return HookOutcome::Installed;
}

bool HookGroup::CapturePatch(std::size_t index) noexcept {
    const auto& original = pristine_[index];
    Witness patch{original.address, {}, original.length};
    if (!ReadGameMemory(nullptr, patch.address, patch.bytes.data(), patch.length) || patch.bytes[0] != 0xE9)
        return false;
    const auto& hook = hooks_[index];
    const auto relay = JumpDestination(patch.bytes.data() + 1, patch.address + 5);
    const auto expectedRelay = hook.owned_trampoline_address() + (index == 0 ? 14 : 12);
    if (relay != expectedRelay) return false;
    std::array<std::uint8_t, 14> hop{};
    std::uintptr_t destination{};
    if (!ReadGameMemory(nullptr, relay, hop.data(), hop.size()) || hop[0] != 0xFF || hop[1] != 0x25 ||
        std::any_of(hop.begin() + 2, hop.begin() + 6, [](auto b) { return b != 0; })) return false;
    std::memcpy(&destination, hop.data() + 6, sizeof(destination));
    if (destination != hook.owned_stub_address()) return false;
    installed_[index] = patch;
    return true;
}

bool HookGroup::StillOwned() const noexcept {
    return std::ranges::all_of(installed_, &Witness::Matches);
}

HookOutcome HookGroup::Rollback(HookOutcome cause) noexcept {
    bool retained = false;
    for (std::size_t i = hooks_.size(); i-- > 0;) {
        if (!hooks_[i].enabled()) continue;
        // On lost ownership, keep callback storage pinned and inert.
        if (!installed_[i].Matches()) {
            retained = true;
            continue;
        }
        if (!hooks_[i].disable() || !pristine_[i].Matches()) retained = true;
    }
    return retained ? HookOutcome::RetainedInert : cause;
}

} // namespace superjump
