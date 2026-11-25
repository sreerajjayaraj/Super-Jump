#pragma once

#include <PalmMenuModeContract.h>
#include "FreedomFlyerContract.h"
#include <PalmNativeContracts.h>
#include <SuperJumpTraversalNativeProfile.h>
#include <AxiomDelayContract.h>
#include <native-trace/TraceVerificationDiagnostics.h>

namespace superjump {

enum class LayoutResult { Valid, MovementMismatch, TraversalMismatch, PalmMismatch };
enum class MovementPath { Unavailable, VerifiedEntry, NativeContinuation };

inline MovementPath ResolveMovementPath(const boss::Reader& reader, const boss::Profile& profile,
                                        menu60::Provider owner, bool forBoost, uintptr_t flyer = 0) {
    const auto base = profile.base;
    const auto shifted = profile.shifted_layout;
    if (forBoost && !menu60::FlightAllowsBoost(reader, owner)) return MovementPath::Unavailable;
    const auto delta = base + (shifted ? 0x4607B2F : menu60::kDeltaRva);
    freedom_flyer::View movement{reader,delta,{}};
    if (flyer) {
        movement.proof = freedom_flyer::Verify(reader,base,delta,flyer,forBoost,menu60::FlightCaveRegion);
        if (!movement.proof.verified) return MovementPath::Unavailable;
    }
    if (!menu60::DeltaReady(movement.Reader(), base, owner, forBoost, menu60::FlightCaveRegion, shifted))
        return MovementPath::Unavailable;
    if (menu60::LocomotionReady(reader, base, owner, menu60::PrivateExecutable, shifted))
        return MovementPath::VerifiedEntry;

    // No chain into an unknown entry hook. The independent observation point
    // is downstream of it. Require untouched stepper and writer semantics;
    // the callback still proves actor/body ownership when the frame arrives.
    if (owner.module) return MovementPath::Unavailable;
    std::array<std::uint8_t, menu60::kStepperPrefix.size()> first{}, second{};
    const auto stepper = base + menu60::ShiftedStepper(shifted);
    if (!reader.Get(stepper, first) || first != menu60::kStepperPrefix ||
        !reader.Get(stepper, second) || second != first) return MovementPath::Unavailable;
    return MovementPath::NativeContinuation;
}

inline LayoutResult ValidateGameLayout(const boss::Reader& reader, const boss::Profile& profile,
                                      palm::menu_mode::Provider provider,
                                      std::span<const std::uint32_t> callers, uintptr_t flyer = 0) {
    const auto base = profile.base;
    const auto shifted = profile.shifted_layout;
    const auto owner = provider.Movement();
    if (ResolveMovementPath(reader, profile, owner, false, flyer) == MovementPath::Unavailable)
        return LayoutResult::MovementMismatch;
    menu60::TraversalView traversalView{reader, reader, base + menu60::ShiftedStepper(shifted), owner};
    if (!traversal::native::Validate({&traversalView, menu60::TraversalView::Read}, base, shifted))
        return LayoutResult::TraversalMismatch;

    palm::menu_mode::View mode(reader, base, provider, motiontrace::runtime::PrivateExecutable,
        nullptr, shifted ? palm::menu_mode::kShiftedModeRva : palm::menu_mode::kModeRva);
    traversal::native::AxiomDelayView axiom{mode.Wrapped(), base, shifted};
    const auto normalized = axiom.Wrapped();
    motiontrace::diagnostics::Failure failure{};
    const auto extra = shifted ? std::span<const motiontrace::ContractBlock>{palm::native::shifted::kExtraContracts} :
        std::span<const motiontrace::ContractBlock>{palm::native::kExtraContracts};
    return motiontrace::diagnostics::Contracts(normalized, base, 0, {}, failure, shifted, callers) &&
           motiontrace::CheckContracts(normalized, base, extra, {}) ?
        LayoutResult::Valid : LayoutResult::PalmMismatch;
}

} // namespace superjump
