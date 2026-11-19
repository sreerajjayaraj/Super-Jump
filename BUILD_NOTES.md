> Historical record for the older build. See the [current variants](README.md).

﻿# SuperJump v1.5.21 - Crimson Desert 1.0.0.1855 delta-r4

## Purpose
Fixes SuperJump still not responding after delta-r3 when Freedom Flyer is loaded but does not patch the DELTA site.

## Root cause
The r3 build correctly stopped refusing the unpatched Freedom Flyer state and resolved the current DELTA hook site:

```text
DELTA unique AOB match RVA 0x3A2393F
hook site: 0x143A2393F chain=no
```

However, it then entered the near-hook code-cave allocator:

```text
AllocCave[A]: 64KB-stride hint sweep exhausted, trying VirtualQuery walk
```

No `cave:` or `ready!` line appeared after that. Because `chain=no`, SuperJump installs a 14-byte absolute JMP, so the shellcode cave does not need to be rel32-near the hook. The near-cave search was unnecessary for this mode and prevented hook installation from completing.

## Change
For `chain=no`, `AllocCave()` now uses normal OS-chosen `VirtualAlloc` immediately and logs:

```text
AllocCave[ABS]: ... (absolute hook; near cave not required)
```

The existing near-hook allocation logic is retained for chain/rel32 mode only.

## Expected log after DMM redeploy

```text
v1.5.21 1.0.0.1855 delta-r4 loaded
hook site: 0x143A2393F chain=no
AllocCave[ABS]: ... absolute hook; near cave not required
cave: ...
ready!
```

## Build
- Source: `E:\CD_Mods_Master\work\SuperJump-1.0.0.1855-delta-r4-20260625\SuperJump`
- Output: `E:\CD_Mods_Master\builds\SuperJump\v1.5.21-1.0.0.1855-delta-r4-20260625\SuperJump.asi`
- SHA256: `2181F5395EE365AC6EE71576431D434279CA2A4619C1AF6A2397456596B7A6B8`
- Embedded marker: `v1.5.21 1.0.0.1855 delta-r4 loaded`

## Deployment
- Deployed ASI to DMM only: `E:\Downloads\Compressed\CD_Mods\DMM\mods\SuperJump.asi`
- `bin64` ASI was not directly replaced. DMM must redeploy it.
