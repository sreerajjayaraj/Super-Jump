#include <cassert>
#include <cstdint>

#include "../source/SuperJumpAxiomGuardPolicy.h"

using superjump::AxiomGuardConfig;
using superjump::AxiomGuardEvent;
using superjump::AxiomGuardResult;
using superjump::AxiomGuardState;
using superjump::ObserveAxiomGuard;

static AxiomGuardResult Observe(
    AxiomGuardState& state,
    const AxiomGuardConfig& config,
    uint32_t nowMs,
    bool keyboardAxiomDown,
    uint16_t controllerButtons,
    bool jumpEdge,
    bool contextVisible = false) {
    return ObserveAxiomGuard(
        state,
        config,
        nowMs,
        keyboardAxiomDown,
        controllerButtons,
        jumpEdge,
        contextVisible);
}

int main() {
    const AxiomGuardConfig config{
        120,    // activation hold required to distinguish Axiom aim from crouch
        12000,  // maximum latch-session lifetime
        1500,   // first jump must promptly follow a real Axiom activation
        900,    // Aerial Maneuver double-press window
        0x40, // Xbox left-stick click
        0x80  // Xbox right-stick click: Focus Mode exclusion
    };

    {
        // An ordinary jump pair must never be suppressed.
        AxiomGuardState state{};
        assert(!Observe(state, config, 100, false, 0, true).suppressJumpEdge);
        assert(!Observe(state, config, 300, false, 0, true).suppressJumpEdge);
        assert(!state.latchActive);
    }

    {
        // Keyboard Axiom activation: hold Tab, release after the game's aim
        // threshold, then let both Aerial Maneuver jump presses pass only to
        // the game. The second press consumes the guard session.
        AxiomGuardState state{};
        assert(Observe(state, config, 100, true, 0, false).event ==
               AxiomGuardEvent::ActivationStarted);
        assert(Observe(state, config, 250, false, 0, false).event ==
               AxiomGuardEvent::LatchArmed);
        AxiomGuardResult first =
            Observe(state, config, 400, false, 0, true);
        assert(first.suppressJumpEdge);
        assert(first.event == AxiomGuardEvent::FirstJumpSuppressed);
        AxiomGuardResult second =
            Observe(state, config, 650, false, 0, true);
        assert(second.suppressJumpEdge);
        assert(second.event == AxiomGuardEvent::AerialManeuverSuppressed);
        assert(!state.latchActive);
    }

    {
        // Controller Axiom activation uses left-stick click.
        AxiomGuardState state{};
        assert(Observe(state, config, 100, false, 0x40, false).event ==
               AxiomGuardEvent::ActivationStarted);
        assert(Observe(state, config, 260, false, 0, false).event ==
               AxiomGuardEvent::LatchArmed);
        assert(Observe(state, config, 400, false, 0, true).suppressJumpEdge);
        assert(Observe(state, config, 600, false, 0, true).suppressJumpEdge);
        assert(!state.latchActive);
    }

    {
        // Once the Aerial Maneuver pair window has elapsed, the next jump is
        // unrelated and must pass through instead of becoming another
        // suppressed "first" jump.
        AxiomGuardState state{};
        Observe(state, config, 100, true, 0, false);
        Observe(state, config, 250, false, 0, false);
        assert(Observe(state, config, 400, false, 0, true).suppressJumpEdge);
        const AxiomGuardResult lateSecond =
            Observe(state, config, 1301, false, 0, true);
        assert(!lateSecond.suppressJumpEdge);
        assert(lateSecond.event == AxiomGuardEvent::JumpSequenceExpired);
        assert(!state.latchActive);
    }

    {
        // L3+R3 is Focus Mode. It must invalidate, not arm, the Axiom guard.
        AxiomGuardState state{};
        assert(Observe(state, config, 100, false, 0x40, false).event ==
               AxiomGuardEvent::ActivationStarted);
        assert(Observe(state, config, 180, false, 0xc0, false).event ==
               AxiomGuardEvent::ActivationInvalidated);
        assert(Observe(state, config, 400, false, 0, false).event ==
               AxiomGuardEvent::None);
        assert(!state.latchActive);
        assert(!Observe(state, config, 500, false, 0, true).suppressJumpEdge);
    }

    {
        // A quick left-stick click is crouch, not Axiom Force.
        AxiomGuardState state{};
        Observe(state, config, 100, false, 0x40, false);
        assert(Observe(state, config, 180, false, 0, false).event ==
               AxiomGuardEvent::ActivationTooShort);
        assert(!state.latchActive);
    }

    {
        // Opening pause/storage UI clears all pending activation state.
        AxiomGuardState state{};
        Observe(state, config, 100, true, 0, false);
        assert(Observe(state, config, 180, true, 0, false, true).event ==
               AxiomGuardEvent::ContextReset);
        assert(!state.activationTracking);
        assert(!state.latchActive);
    }

    {
        // A stale activation must not consume an unrelated ordinary double
        // jump even though the wider safety timeout has not elapsed.
        AxiomGuardState state{};
        Observe(state, config, 100, true, 0, false);
        Observe(state, config, 250, false, 0, false);
        assert(state.latchActive);
        assert(Observe(state, config, 1751, false, 0, true).event ==
               AxiomGuardEvent::FirstJumpWindowExpired);
        assert(!state.latchActive);
        assert(!Observe(state, config, 1950, false, 0, true).suppressJumpEdge);
    }

    {
        // A stale abandoned latch must expire and stop suppressing jumps.
        AxiomGuardState state{};
        Observe(state, config, 100, true, 0, false);
        Observe(state, config, 250, false, 0, false);
        assert(state.latchActive);
        assert(Observe(state, config, 12251, false, 0, false).event ==
               AxiomGuardEvent::LatchExpired);
        assert(!Observe(state, config, 12300, false, 0, true).suppressJumpEdge);
    }

    return 0;
}
