#pragma once

#include <cmath>
#include <cstdint>
#include <optional>

namespace superjump {

enum class JumpObservation { None, Neutral, FirstImpulse, TooSoon, Cooldown, DoubleJump };
enum class NativeJumpCommit { GroundLaunch, AirborneDoubleJump };

constexpr bool IsNativeDoubleJumpSkill(std::uint32_t hero, std::uint32_t skill) noexcept {
    constexpr std::uint32_t damianeDoubleJump = 10340;
    constexpr std::uint32_t oongkaDoubleJump = 10235;
    // The shared action was also observed on Oongka's 3732 native publisher.
    constexpr std::uint32_t sharedDoubleJump = 10070;
    return (hero == 4 && skill == damianeDoubleJump) ||
           (hero == 6 && (skill == oongkaDoubleJump || skill == sharedDoubleJump));
}

constexpr bool QualifiesNativeAirLaunch(std::uint32_t hero, bool skillMatches,
                                       bool consumerVerified) noexcept {
    return (hero == 4 && (consumerVerified || skillMatches)) || (hero == 6 && skillMatches);
}

// Automatic behavior from the P26 GameDoubleJump path. The runtime passes
// movement samples only for the validated local Damiane or Oongka actor.
class NativeDoubleJump {
public:
    void ResetContext() {
        ResetMotion();
        nativeFirstImpulse_.reset();
    }

    void ResetMotion() {
        firstImpulse_.reset();
        armed_ = false;
    }

    JumpObservation ObserveCommit(std::uint64_t nowMs, NativeJumpCommit commit) {
        if (InCooldown(nowMs)) return JumpObservation::Cooldown;
        if (commit == NativeJumpCommit::GroundLaunch) {
            nativeFirstImpulse_ = nowMs;
            return JumpObservation::FirstImpulse;
        }
        if (!nativeFirstImpulse_) return JumpObservation::None;

        const auto first = *nativeFirstImpulse_;
        if (nowMs < first || nowMs - first > maxIntervalMs) {
            nativeFirstImpulse_.reset();
            return JumpObservation::None;
        }
        if (nowMs - first < minIntervalMs) return JumpObservation::TooSoon;
        return Trigger(nowMs);
    }

    JumpObservation Observe(std::uint64_t nowMs, float deltaY, bool allowed) {
        if (!allowed || !std::isfinite(deltaY)) {
            ResetContext();
            return JumpObservation::None;
        }
        if (firstImpulse_ && (nowMs < *firstImpulse_ || nowMs - *firstImpulse_ > maxIntervalMs)) {
            firstImpulse_.reset();
        }
        if (deltaY <= resetY) {
            armed_ = true;
            return JumpObservation::Neutral;
        }
        if (!armed_ || deltaY < impulseY) return JumpObservation::None;
        armed_ = false;

        if (InCooldown(nowMs)) return JumpObservation::Cooldown;
        if (!firstImpulse_) {
            firstImpulse_ = nowMs;
            return JumpObservation::FirstImpulse;
        }
        if (nowMs - *firstImpulse_ < minIntervalMs) return JumpObservation::TooSoon;

        return Trigger(nowMs);
    }

private:
    bool InCooldown(std::uint64_t nowMs) const {
        return lastBoost_ && (nowMs < *lastBoost_ || nowMs - *lastBoost_ < cooldownMs);
    }

    JumpObservation Trigger(std::uint64_t nowMs) {
        ResetContext();
        lastBoost_ = nowMs;
        return JumpObservation::DoubleJump;
    }

    static constexpr float resetY = 0.05f;
    static constexpr float impulseY = 0.20f;
    static constexpr std::uint64_t minIntervalMs = 80;
    static constexpr std::uint64_t maxIntervalMs = 900;
    static constexpr std::uint64_t cooldownMs = 900;

    bool armed_ = true;
    std::optional<std::uint64_t> firstImpulse_;
    std::optional<std::uint64_t> nativeFirstImpulse_;
    std::optional<std::uint64_t> lastBoost_;
};

} // namespace superjump
