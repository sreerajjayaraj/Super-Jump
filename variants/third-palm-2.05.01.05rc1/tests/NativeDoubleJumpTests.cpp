#include "../src/NativeDoubleJump.h"
#include "../src/JumpFrameQueue.h"

#include <iostream>
#include <limits>

int main() {
    using superjump::NativeDoubleJump;
    using superjump::JumpObservation;
    unsigned failures = 0;
    const auto check = [&](bool value, const char* description) {
        if (value) return;
        ++failures;
        std::cerr << description << '\n';
    };

    NativeDoubleJump detector;
    check(detector.Observe(0, .20f, true) == JumpObservation::FirstImpulse, "First impulse arms only.");
    check(detector.Observe(100, .25f, true) == JumpObservation::None, "Held impulse does not repeat.");
    check(detector.Observe(101, .05f, true) == JumpObservation::Neutral, "Neutral sample rearms.");
    check(detector.Observe(102, .20f, true) == JumpObservation::DoubleJump, "Second impulse triggers.");
    check(detector.Observe(200, .04f, true) == JumpObservation::Neutral &&
          detector.Observe(201, .20f, true) == JumpObservation::Cooldown,
          "Cooldown blocks a new sequence.");
    check(detector.Observe(1001, 0, true) == JumpObservation::Neutral &&
          detector.Observe(1002, .20f, true) == JumpObservation::FirstImpulse &&
          detector.Observe(1010, .03f, true) == JumpObservation::Neutral &&
          detector.Observe(1082, .20f, true) == JumpObservation::DoubleJump,
          "Cooldown and minimum interval endpoints are inclusive.");

    NativeDoubleJump interrupted;
    check(interrupted.Observe(20, .2f, true) == JumpObservation::FirstImpulse &&
          interrupted.Observe(100, .04f, false) == JumpObservation::None &&
          interrupted.Observe(110, .2f, true) == JumpObservation::None,
          "Blocked context cancels and requires a neutral sample.");
    check(interrupted.Observe(120, 0, true) == JumpObservation::Neutral &&
          interrupted.Observe(121, .2f, true) == JumpObservation::FirstImpulse &&
          interrupted.Observe(129, 0, true) == JumpObservation::Neutral &&
          interrupted.Observe(130, .2f, true) == JumpObservation::TooSoon &&
          interrupted.Observe(1021, 0, true) == JumpObservation::Neutral &&
          interrupted.Observe(1022, .2f, true) == JumpObservation::FirstImpulse,
          "Early duplicate does not trigger, and late impulse starts a fresh sequence.");
    check(interrupted.Observe(1023, std::numeric_limits<float>::quiet_NaN(), true) ==
          JumpObservation::None && interrupted.Observe(1110, .2f, true) == JumpObservation::None,
          "Non-finite movement cancels pending activation.");

    using superjump::IsNativeDoubleJumpSkill;
    check(IsNativeDoubleJumpSkill(4, 10340) && IsNativeDoubleJumpSkill(6, 10235) &&
          IsNativeDoubleJumpSkill(6, 10070), "Current per-character and observed shared keys qualify.");
    check(!IsNativeDoubleJumpSkill(4, 10235) && !IsNativeDoubleJumpSkill(6, 10340) &&
          !IsNativeDoubleJumpSkill(1, 10070) && !IsNativeDoubleJumpSkill(4, 10070) &&
          !IsNativeDoubleJumpSkill(6, 10162) && !IsNativeDoubleJumpSkill(0, 10235),
          "Other heroes, step actions and cross-character skills cannot qualify.");
    using superjump::QualifiesNativeAirLaunch;
    check(QualifiesNativeAirLaunch(4, false, true) &&
          !QualifiesNativeAirLaunch(4, false, false),
          "Damiane can use a proven native launch without an effective skill slot.");
    check(QualifiesNativeAirLaunch(6, true, true) &&
          !QualifiesNativeAirLaunch(6, false, true) &&
          !QualifiesNativeAirLaunch(1, false, true),
          "Oongka retains skill verification and Kliff uses the palm trigger.");
    using superjump::NativeJumpCommit;
    NativeDoubleJump native;
    native.ResetContext();
    check(native.ObserveCommit(100, NativeJumpCommit::GroundLaunch) == JumpObservation::FirstImpulse,
          "Accepted grounded launch arms the native sequence.");
    check(native.Observe(200, .08f, true) == JumpObservation::None &&
          native.Observe(209, .09f, true) == JumpObservation::None &&
          native.ObserveCommit(210, NativeJumpCommit::AirborneDoubleJump) == JumpObservation::DoubleJump,
          "Accepted airborne double jump triggers during a continuous subthreshold ascent.");
    check(native.ObserveCommit(220, NativeJumpCommit::AirborneDoubleJump) != JumpObservation::DoubleJump,
          "Repeated native publications cannot deliver another boost.");
    check(native.ObserveCommit(1000, NativeJumpCommit::GroundLaunch) == JumpObservation::Cooldown &&
          native.ObserveCommit(1110, NativeJumpCommit::GroundLaunch) == JumpObservation::FirstImpulse &&
          native.ObserveCommit(1189, NativeJumpCommit::AirborneDoubleJump) == JumpObservation::TooSoon &&
          native.ObserveCommit(1190, NativeJumpCommit::AirborneDoubleJump) == JumpObservation::DoubleJump,
          "Native events preserve the existing 80 ms interval and 900 ms cooldown.");

    NativeDoubleJump unpaired;
    check(unpaired.ObserveCommit(100, NativeJumpCommit::AirborneDoubleJump) == JumpObservation::None,
          "Airborne skill requires a verified opening jump in the same context.");
    unpaired.ObserveCommit(110, NativeJumpCommit::GroundLaunch);
    unpaired.ResetContext();
    check(unpaired.ObserveCommit(220, NativeJumpCommit::AirborneDoubleJump) == JumpObservation::None,
          "Context loss cancels the native opening jump.");
    unpaired.ObserveCommit(300, NativeJumpCommit::GroundLaunch);
    check(unpaired.ObserveCommit(1201, NativeJumpCommit::AirborneDoubleJump) == JumpObservation::None,
          "Stale openings cannot qualify an airborne jump.");
    NativeDoubleJump hybrid;
    hybrid.ObserveCommit(0, NativeJumpCommit::GroundLaunch);
    hybrid.Observe(2, .21f, true);
    hybrid.Observe(100, .02f, true);
    check(hybrid.Observe(102, .21f, true) == JumpObservation::DoubleJump &&
          hybrid.ObserveCommit(103, NativeJumpCommit::AirborneDoubleJump) != JumpObservation::DoubleJump,
          "Motion and native routes share one cooldown and delivery.");
    NativeDoubleJump motionLost;
    motionLost.ObserveCommit(50, NativeJumpCommit::GroundLaunch);
    motionLost.ResetMotion();
    check(motionLost.ObserveCommit(205, NativeJumpCommit::AirborneDoubleJump) == JumpObservation::DoubleJump,
          "Independent accepted events survive loss of a generic movement frame.");

    superjump::JumpFrameQueue queue;
    queue.SetTarget(10, 100);
    queue.Capture(20, {101, .8f});
    queue.Capture(10, {100, .9f});
    queue.Capture(10, {110, .21f});
    queue.Capture(10, {165, .03f});
    queue.Capture(10, {200, .22f});
    const auto backlog = queue.Drain();
    NativeDoubleJump fromFrames;
    unsigned recognized{};
    for (const auto frame : backlog.Samples())
        if (fromFrames.Observe(frame.tick, frame.deltaY, true) == JumpObservation::DoubleJump)
            ++recognized;
    check(backlog.count == 3 && !backlog.missed && recognized == 1,
          "Frames captured between worker passes retain both impulses and discard foreign or old frames.");
    queue.Capture(10, {215, .3f});
    queue.SetTarget(20, 220);
    check(queue.Drain().count == 0, "Actor change discards the previous actor's pending samples.");
    for (unsigned i = 0; i < 40; ++i) queue.Capture(20, {221 + i, 0});
    const auto saturated = queue.Drain();
    check(saturated.count == 32 && saturated.missed == 8 && queue.Drain().missed == 0,
          "The bounded queue reports and clears lost samples.");

    std::cout << "Native double-jump checks: " << failures << " failure(s).\n";
    return failures ? 1 : 0;
}
