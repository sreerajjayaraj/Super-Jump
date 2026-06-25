# SuperJump for Crimson Desert  —  v1.4

Hold **Shift + C**, press **Space** while standing still — Kliff launches straight up. Fully INI-configurable: chord keys, launch power, crouch fake, all live-reload (no game restart to retune).

Targets **`CrimsonDesert.exe v1.07.00`**.

---

## What's new in v1.4 (2026-05-17)

- **v1.07-ready cave allocator.** Previous versions could fail with `cave too far for rel32` in chain mode when other mods loaded first and fragmented nearby memory. v1.4 walks the address space with VirtualQuery and only allocates into confirmed-free regions within ±1.5 GB of the hook site, so the relative jump always fits.
- **Stationary gate reverted to the working v1.2 cave-delta check.** v1.3 experimented with reading the actor's velocity vector directly, but the offset was on the wrong struct on v1.07 and produced bogus values (1186 m/s readings). The cave-delta path is back — same as the original Nexus release that worked.
- AOB-resolved DELTA hook site for v1.07 (`+0x384909F` RVA). Falls back gracefully if scan misses.

---

## Requirements

- **Crimson Desert** patched to **v1.07.00**.
- **Ultimate ASI Loader** (or any equivalent) installed in `bin64/`. See <https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases>.

---

## Install

1. Copy `bin64/SuperJump.asi` and `bin64/SuperJump.ini` into your `<Crimson Desert>/bin64/` folder.
2. Launch the game. SuperJump waits ~5 s after attach (yield window for chain detection), then logs to `bin64/SuperJump.log`.
3. Default chord: **hold Shift + C, then press Space** while standing still.

---

## Usage

Default chord: **hold Shift + C, then press Space**. Must be standing still — the mod blocks the launch if the physics-delta indicates movement. A brief crouch animation plays as a wind-up, then a vertical launch.

---

## Configuration

Edit `bin64/SuperJump.ini`. Changes are picked up live (~1 s re-read interval).

| Key | Default | Notes |
|---|---|---|
| `Enabled` | 1 | Master switch. |
| `HotkeyMod` | `0x10` (Shift) | First modifier. `0` to disable. |
| `HotkeyMod2` | `0x43` (C) | Second modifier. `0` to fall back to single-modifier mode. |
| `HotkeyKey` | `0x20` (Space) | Trigger key. |
| `BoostY` | 40 | Vertical velocity per boosted frame. Higher = higher. |
| `BoostFrames` | 5 | How many frames the boost is applied. Higher = floatier. |
| `RequireStationary` | 1 | Block launch while moving. `0` to allow mid-sprint. |
| `StationaryThresh` | 0.04 | Max horizontal physics-delta allowed at fire. Lower = stricter. |
| `VelThresh` | 0.3 | (Legacy v1.3 knob — not used by v1.4, leave at default.) |
| `AnimCrouch` | 1 | Send a brief crouch keypress as a visual wind-up. |
| `CrouchKey` | `0x11` (Ctrl) | Your in-game crouch keybind. |
| `CrouchDurationMs` | 120 | How long the fake crouch is held. |

VK code reference: <https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes>

---

## Compatibility — hook chaining

SuperJump shares its hook site (`+0x384909F` v1.07 RVA) with several other movement mods (FreedomFlyer being the common one). On load it yields for ~5 s to let other mods hook first, then either:

- **Standalone install (no prior hook):** 14-byte abs JMP, original instructions re-emitted in cave, return past patch.
- **Chain install (other mod hooked first):** 5-byte rel32 JMP (so the prior mod's return is untouched), our cave does its boost work then JMPs to the prior mod's cave.

If you load SuperJump after FreedomFlyer, both work. If you load before, FreedomFlyer detects our hook and chains onto us (same protocol).

The v1.4 VirtualQuery-based cave allocator ensures the rel32 always fits, even when nearby memory is heavily fragmented by other mods.

---

## Troubleshooting

- **`ERROR: hook install failed` / `cave too far for rel32`:** v1.4 should have eliminated this. If it still appears, your `bin64/` has a LOT of other mods (`.asi` files) loaded and the address space near `0x140000000..0x180000000` is exhausted. Unload other mods temporarily to test.
- **Launch doesn't fire:** check `SuperJump.log`. If it shows `suppressed — moving`, your stationary check is rejecting — either you're actually moving, or `StationaryThresh` is too tight (raise it slightly).
- **Crouch animation doesn't play:** `CrouchKey` doesn't match your in-game crouch bind. Set it to your actual crouch key's VK code, or `AnimCrouch=0` to disable the visual wind-up entirely.
- **Game runs unmodded after install:** ASI Loader isn't loading. Verify `winmm.dll` / `dxgi.dll` / similar loader DLL is in `bin64/`.

---

## Building from source

```
cd <this folder>/source
cl /O2 /LD /EHsc SuperJump.cpp /link /OUT:SuperJump.dll
ren SuperJump.dll SuperJump.asi
```

Requires MSVC's x64 build tools. Drop the resulting `SuperJump.asi` next to `SuperJump.ini` in your game's `bin64/`.

Source depends on `../../player_base.hpp` (shared with Phase and Miner).
