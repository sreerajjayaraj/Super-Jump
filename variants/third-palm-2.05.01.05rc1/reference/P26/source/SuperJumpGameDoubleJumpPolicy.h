#pragma once

#include <cstdint>

namespace superjump {

struct GameDoubleJumpConfig {
    float impulseY;
    float resetY;
    uint32_t minIntervalMs;
    uint32_t maxIntervalMs;
    uint32_t cooldownMs;
};

struct GameDoubleJumpState {
    bool impulseArmed = true;
    bool hasFirstImpulse = false;
    uint32_t firstImpulseMs = 0;
    uint32_t cooldownUntilMs = 0;
};

enum class GameDoubleJumpEvent {
    None,
    FirstImpulse,
    Triggered,
    IgnoredTooEarly,
    SuppressedCooldown,
    SuppressedContext
};

constexpr bool TimeBefore(uint32_t nowMs, uint32_t deadlineMs) {
    return static_cast<int32_t>(nowMs - deadlineMs) < 0;
}

inline GameDoubleJumpEvent ObserveGameJumpDelta(
    GameDoubleJumpState& state,
    const GameDoubleJumpConfig& config,
    uint32_t nowMs,
    float deltaY,
    bool contextVisible) {
    if (state.hasFirstImpulse &&
        nowMs - state.firstImpulseMs > config.maxIntervalMs) {
        state.hasFirstImpulse = false;
    }

    // Context now means verified native UI/boss/focus prohibition, rather
    // than an OS cursor hint. Cancel armed sessions even during real impulses.
    if (contextVisible) {
        state.hasFirstImpulse=false;state.firstImpulseMs=0;
        state.impulseArmed=deltaY<=config.resetY;
        return GameDoubleJumpEvent::SuppressedContext;
    }

    if (deltaY <= config.resetY) {
        state.impulseArmed = true;
        return GameDoubleJumpEvent::None;
    }

    if (!state.impulseArmed || deltaY < config.impulseY) {
        return GameDoubleJumpEvent::None;
    }

    state.impulseArmed = false;
    if (TimeBefore(nowMs, state.cooldownUntilMs)) {
        state.hasFirstImpulse = false;
        return GameDoubleJumpEvent::SuppressedCooldown;
    }

    if (!state.hasFirstImpulse) {
        state.hasFirstImpulse = true;
        state.firstImpulseMs = nowMs;
        return GameDoubleJumpEvent::FirstImpulse;
    }

    const uint32_t intervalMs = nowMs - state.firstImpulseMs;
    if (intervalMs < config.minIntervalMs) {
        return GameDoubleJumpEvent::IgnoredTooEarly;
    }

    if (intervalMs <= config.maxIntervalMs) {
        state.hasFirstImpulse = false;
        state.cooldownUntilMs = nowMs + config.cooldownMs;
        return GameDoubleJumpEvent::Triggered;
    }

    state.firstImpulseMs = nowMs;
    return GameDoubleJumpEvent::FirstImpulse;
}

} // namespace superjump
