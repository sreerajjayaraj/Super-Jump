# Super Jump Third Palm 2.05.01.05rc1

For Crimson Desert 2.05.01, internal executable version 1.0.0.3732.

Maintainer: sreerajjay (SrJ). Original Super Jump concept: dukusmaximus.

## Install and test

- Close the game. Back up any previous Super Jump variant and leave only this
  ASI enabled. Use your existing compatible x64 ASI loader.
- Put SuperJump_ThirdPalm.asi in the loader's ASI folder (normally bin64) or
  use your existing DMM setup. The archive contains no loader.
- With Kliff, jump once from the ground, then perform
  three successful downward Force Palms in the same airborne sequence. Skip
  the second Jump press. The boost triggers on the third Force Palm.
- With Kliff, check that the first two Force Palms and a failed or interrupted
  sequence produce no boost. With Damiane and Oongka, check that one jump
  leaves movement intact and a double jump triggers the boost.
- Check loss of focus, menus and character switches. A pending boost should
  never cross a context change. If you use DesertLink, test True Flight
  separately and check that the boost yields when flight is active.
- Session logs are under logs/SuperJump_ThirdPalm.log next to the ASI. If it
  fails, share the active log and say which character or action was tested.

## Live boost height

The plugin reads `SuperJump_ThirdPalm.ini` beside the loaded ASI. A default
file is available under `distribution` in the source tree.

```ini
[SuperJump]
BoostY=40.0
```

`BoostY` is the vertical movement applied per boost frame. It accepts values
from 0.1 through 120.0 and applies to Kliff, Damiane and Oongka. Save the file
during gameplay and wait up to one second. The next boost uses the new value.
A boost in progress finishes at its original value. An invalid edit keeps
the current value. The default for a fresh session without this file is 40.0.

## Changes

- 2.05.01.05rc1 adds live boost height configuration to the gameplay-tested
  2.05.01.03rc3. The native actions and five-frame delivery are retained.

- Damiane can qualify from a verified native launch after a grounded opening.
  This covers Skystep when the effective action slot does not carry the
  double-jump skill. Oongka retains the previously verified skill route.
  Both use the existing timing, boost delivery and gameplay guards.

- The public release uses the gameplay and compatibility code tested in RC5.
  It uses two hooks and can work alongside third-party hooks at the locomotion
  entry when the downstream movement code is intact. The session log reports
  this path as `NATIVE_CONTINUATION`.

- RC2 collects unmodified Damiane and Oongka movement frames independently of
  the runtime validation lock, then evaluates them in order.
- Added compact log summaries (`DOUBLE_JUMP_STATUS`) for recognition,
  queueing and cancellation. The cooldown and timing thresholds are unchanged.

- Rebuilt the ASI runtime around native action and movement triggers. Removed
  keyboard/controller polling and synthetic crouch input.
- Resolves the movement and Force Palm hooks from their code and control flow.
- Added structured session logging, log rotation and exact-name migration for
  the previous log location.
