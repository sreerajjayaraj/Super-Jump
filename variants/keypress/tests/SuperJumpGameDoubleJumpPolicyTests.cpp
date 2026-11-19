#include <cassert>

#include "../source/SuperJumpGameDoubleJumpPolicy.h"

using superjump::GameDoubleJumpConfig;
using superjump::GameDoubleJumpEvent;
using superjump::GameDoubleJumpState;
using superjump::ObserveGameJumpDelta;

static void ResetBelowThreshold(GameDoubleJumpState& state,
                                const GameDoubleJumpConfig& config,
                                unsigned long nowMs) {
    assert(ObserveGameJumpDelta(state, config, nowMs, 0.0f, false) ==
           GameDoubleJumpEvent::None);
}

int main() {
    const GameDoubleJumpConfig config{
        0.20f, // upward impulse threshold
        0.05f, // reset threshold
        80,    // minimum interval
        900,   // maximum interval
        900    // cooldown
    };

    {
        GameDoubleJumpState state{};
        ResetBelowThreshold(state, config, 10);
        assert(ObserveGameJumpDelta(state, config, 100, 0.30f, false) ==
               GameDoubleJumpEvent::FirstImpulse);
        ResetBelowThreshold(state, config, 150);
        assert(ObserveGameJumpDelta(state, config, 300, 0.35f, false) ==
               GameDoubleJumpEvent::Triggered);
    }

    {
        GameDoubleJumpState state{};
        ResetBelowThreshold(state, config, 10);
        assert(ObserveGameJumpDelta(state, config, 100, 0.30f, false) ==
               GameDoubleJumpEvent::FirstImpulse);
        ResetBelowThreshold(state, config, 120);
        assert(ObserveGameJumpDelta(state, config, 150, 0.35f, false) ==
               GameDoubleJumpEvent::IgnoredTooEarly);
        ResetBelowThreshold(state, config, 180);
        assert(ObserveGameJumpDelta(state, config, 300, 0.35f, false) ==
               GameDoubleJumpEvent::Triggered);
    }

    {
        GameDoubleJumpState state{};
        ResetBelowThreshold(state, config, 10);
        assert(ObserveGameJumpDelta(state, config, 100, 0.30f, false) ==
               GameDoubleJumpEvent::FirstImpulse);
        ResetBelowThreshold(state, config, 200);
        assert(ObserveGameJumpDelta(state, config, 1100, 0.35f, false) ==
               GameDoubleJumpEvent::FirstImpulse);
    }

    {
        GameDoubleJumpState state{};
        ResetBelowThreshold(state, config, 10);
        assert(ObserveGameJumpDelta(state, config, 100, 0.30f, false) ==
               GameDoubleJumpEvent::FirstImpulse);
        ResetBelowThreshold(state, config, 150);
        assert(ObserveGameJumpDelta(state, config, 300, 0.35f, false) ==
               GameDoubleJumpEvent::Triggered);
        ResetBelowThreshold(state, config, 350);
        assert(ObserveGameJumpDelta(state, config, 500, 0.40f, false) ==
               GameDoubleJumpEvent::SuppressedCooldown);
        ResetBelowThreshold(state, config, 1300);
        assert(ObserveGameJumpDelta(state, config, 1400, 0.40f, false) ==
               GameDoubleJumpEvent::FirstImpulse);
    }

    {
        GameDoubleJumpState state{};
        assert(ObserveGameJumpDelta(state, config, 10, 0.0f, true) ==
               GameDoubleJumpEvent::SuppressedContext);
        assert(ObserveGameJumpDelta(state, config, 100, 0.0f, true) ==
               GameDoubleJumpEvent::SuppressedContext);
        ResetBelowThreshold(state, config, 150);
        assert(ObserveGameJumpDelta(state, config, 300, 0.35f, false) ==
               GameDoubleJumpEvent::FirstImpulse);
    }

    {
        // The native context is authoritative; entering a menu after a real
        // first impulse cancels that session even if upward physics continues.
        GameDoubleJumpState state{};
        assert(ObserveGameJumpDelta(state, config, 100, 0.30f, false) == GameDoubleJumpEvent::FirstImpulse);
        assert(ObserveGameJumpDelta(state, config, 150, 0.0f, true) == GameDoubleJumpEvent::SuppressedContext);
        assert(!state.hasFirstImpulse);
        assert(ObserveGameJumpDelta(state, config, 200, 0.35f, true) == GameDoubleJumpEvent::SuppressedContext);
        assert(!state.hasFirstImpulse);
        ResetBelowThreshold(state, config, 250);
        assert(ObserveGameJumpDelta(state, config, 300, 0.35f, false) == GameDoubleJumpEvent::FirstImpulse);

    }

    {
        GameDoubleJumpState state{};
        ResetBelowThreshold(state, config, 10);
        assert(ObserveGameJumpDelta(state, config, 100, 0.30f, false) ==
               GameDoubleJumpEvent::FirstImpulse);
        // A sustained upward value cannot be counted as a second jump. The
        // game DELTA must fall below the reset threshold before another
        // accepted impulse can be recognized.
        assert(ObserveGameJumpDelta(state, config, 300, 0.35f, false) ==
               GameDoubleJumpEvent::None);
        assert(ObserveGameJumpDelta(state, config, 500, 0.35f, false) ==
               GameDoubleJumpEvent::None);
    }

    return 0;
}
