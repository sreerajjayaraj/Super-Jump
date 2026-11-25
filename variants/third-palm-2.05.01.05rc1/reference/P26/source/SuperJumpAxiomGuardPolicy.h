#pragma once

#include <cstdint>

namespace superjump {

struct AxiomGuardConfig {
    uint32_t activationHoldMs;
    uint32_t latchTimeoutMs;
    uint32_t firstJumpWindowMs;
    uint32_t jumpSequenceWindowMs;
    uint16_t controllerActivationButton;
    uint16_t controllerExcludedButtons;
};

struct AxiomGuardState {
    bool activationTracking = false;
    uint32_t activationStartMs = 0;
    bool latchActive = false;
    uint32_t latchUntilMs = 0;
    uint32_t firstJumpUntilMs = 0;
    bool firstJumpSuppressed = false;
    uint32_t firstJumpMs = 0;
};

enum class AxiomGuardEvent {
    None,
    ActivationStarted,
    ActivationInvalidated,
    ActivationTooShort,
    LatchArmed,
    FirstJumpSuppressed,
    AerialManeuverSuppressed,
    FirstJumpWindowExpired,
    JumpSequenceExpired,
    LatchExpired,
    ContextReset
};

struct AxiomGuardResult {
    AxiomGuardEvent event = AxiomGuardEvent::None;
    bool suppressJumpEdge = false;
};

inline void ResetAxiomLatch(AxiomGuardState& state) {
    state.latchActive = false;
    state.latchUntilMs = 0;
    state.firstJumpUntilMs = 0;
    state.firstJumpSuppressed = false;
    state.firstJumpMs = 0;
}

inline void ResetAxiomGuard(AxiomGuardState& state) {
    state.activationTracking = false;
    state.activationStartMs = 0;
    ResetAxiomLatch(state);
}

inline bool AxiomDeadlineReached(uint32_t nowMs, uint32_t deadlineMs) {
    return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
}

inline AxiomGuardResult ObserveAxiomGuard(
    AxiomGuardState& state,
    const AxiomGuardConfig& config,
    uint32_t nowMs,
    bool keyboardActivationDown,
    uint16_t controllerButtons,
    bool jumpEdge,
    bool contextVisible) {
    const bool hadState =
        state.activationTracking || state.latchActive ||
        state.firstJumpSuppressed;
    if (contextVisible) {
        ResetAxiomGuard(state);
        return {
            hadState ? AxiomGuardEvent::ContextReset
                     : AxiomGuardEvent::None,
            false
        };
    }

    if (state.latchActive &&
        AxiomDeadlineReached(nowMs, state.latchUntilMs)) {
        ResetAxiomLatch(state);
        return {AxiomGuardEvent::LatchExpired, false};
    }

    if (state.latchActive &&
        !state.firstJumpSuppressed &&
        AxiomDeadlineReached(nowMs, state.firstJumpUntilMs)) {
        ResetAxiomLatch(state);
        return {AxiomGuardEvent::FirstJumpWindowExpired, false};
    }

    const bool controllerActivationDown =
        config.controllerActivationButton != 0 &&
        (controllerButtons & config.controllerActivationButton) ==
            config.controllerActivationButton;
    const bool controllerActivationExcluded =
        controllerActivationDown &&
        config.controllerExcludedButtons != 0 &&
        (controllerButtons & config.controllerExcludedButtons) != 0;

    // Focus Mode uses both stick clicks. If right-stick appears while a
    // left-stick Axiom hold is being tracked, discard that activation instead
    // of treating its later release as an Axiom latch.
    if (!keyboardActivationDown && controllerActivationExcluded) {
        const bool wasTracking = state.activationTracking;
        state.activationTracking = false;
        state.activationStartMs = 0;
        return {
            wasTracking ? AxiomGuardEvent::ActivationInvalidated
                        : AxiomGuardEvent::None,
            false
        };
    }

    const bool activationDown =
        keyboardActivationDown ||
        (controllerActivationDown && !controllerActivationExcluded);
    if (activationDown && !state.activationTracking) {
        // Pressing the activation control again cancels any old latch before
        // beginning a fresh Axiom aim session.
        ResetAxiomLatch(state);
        state.activationTracking = true;
        state.activationStartMs = nowMs;
        return {AxiomGuardEvent::ActivationStarted, false};
    }

    if (!activationDown && state.activationTracking) {
        const uint32_t heldMs = nowMs - state.activationStartMs;
        state.activationTracking = false;
        state.activationStartMs = 0;
        if (heldMs < config.activationHoldMs) {
            return {AxiomGuardEvent::ActivationTooShort, false};
        }

        state.latchActive = true;
        state.latchUntilMs = nowMs + config.latchTimeoutMs;
        state.firstJumpUntilMs = nowMs + config.firstJumpWindowMs;
        state.firstJumpSuppressed = false;
        state.firstJumpMs = 0;
        return {AxiomGuardEvent::LatchArmed, false};
    }

    if (!jumpEdge || !state.latchActive) {
        return {};
    }

    if (!state.firstJumpSuppressed) {
        state.firstJumpSuppressed = true;
        state.firstJumpMs = nowMs;
        return {AxiomGuardEvent::FirstJumpSuppressed, true};
    }

    if (nowMs - state.firstJumpMs > config.jumpSequenceWindowMs) {
        ResetAxiomLatch(state);
        return {AxiomGuardEvent::JumpSequenceExpired, false};
    }

    ResetAxiomLatch(state);
    return {AxiomGuardEvent::AerialManeuverSuppressed, true};
}

} // namespace superjump
