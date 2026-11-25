# Super Jump Third Palm technical contract

## Identity and player-facing behavior

- Maintainer and author: sreerajjay (SrJ). Original Super Jump concept: dukusmaximus. The public version is `2.05.01.02` and the plugin is `SuperJump_ThirdPalm.asi`. Use one Super Jump variant per game session with a compatible x64 ASI loader.
- Kliff jumps once from the ground, then performs three successful downward Force Palms in a single airborne sequence. The third Palm triggers the boost. The sequence starts with one Jump press and fresh progress after landing or invalidation. Count accepted native actions only.
- Damiane and Oongka boost on the game's native double jump. Preserve the recognition interval, cooldown and delivery window. The configured vertical impulse defaults to 40.0. The scoped native frame queue preserves frame order and resets on missed samples or a changed actor or context.
- The build uses automatic native inputs. The optional matching-stem INI provides only BoostY. The existing worker reads it at most once per second. Every accepted boost captures its value before delivery, and malformed or unavailable input retains the last valid value. The reference P26 source is historical context.
- Preserve actor ownership, traversal, UI, boss context, and flight restrictions. Reset pending boosts on lost focus or invalid context. Require a fresh activation after returning to gameplay. Keep the DesertLink True Flight gate. Recheck identity and eligibility before applying an impulse.

## Active native intervention

| Role | Public release contract |
| --- | --- |
| Movement | One five-byte window starts with a conditional branch and includes the full `movaps` instruction. Preserve flags and both continuations. Validate the local actor, body, and destination before applying the vertical change. |
| Shared Force Palm publisher | One seven-byte entry window covers two complete instructions. Accept only the two proven call sites. The return address identifies the source, RCX contains the native state pointer, and the zero-extended result is in R9D. The two separate P26 collector locations serve as resolution evidence. |

The live group installs both hooks after checking layout, semantics and ownership. Recheck the dependency spans during operation. Partial installation leaves the group inactive and rolls back owned modifications where safe. The two windows total 12 bytes. RC3 and RC4 used three hooks historically. Public maintenance starts from the two-hook RC5 implementation in this archive.

## Movement compatibility

An accepted movement entry and an independently validated native continuation are two supported paths. If an unrelated mod intercepts the entry, Super Jump may continue only when the independent stepper, downstream writer, and owned observation hook are valid and execution reaches that observation point with the expected live actor and destination. Super Jump leaves the foreign detour in place. If the writer, stepper or owned observation path becomes invalid, block the boost. Preserve recognized provider and flight rules before allowing intervention.

`MOVEMENT_PATH mode=NATIVE_CONTINUATION` reports availability of the fallback. `BOOST_APPLIED` records actual boost delivery. `DOUBLE_JUMP_STATUS` exposes recognition and queuing counters. Runtime diagnostics use fixed INFO in release builds, with session rotation and an inventory scoped to the ASI directory. The release includes exact-name cleanup for the old logs during migration.

## Update and distribution checks

The offline references are Crimson Desert 2.05.00 (`1.0.0.3663`) and 2.05.01 (`1.0.0.3732`). Resolve targets from executable structure and semantic checks. The validated native object profiles may require revalidation after a game update. Keep the version identifiers for the game and the plugin distinct. Check normal acceptance, an entry-only modification, and refusal of changes at the actual writer or stepper.

Use [BUILD.md](BUILD.md) to reproduce Release x64. Keep the pinned static dependencies, author metadata, licenses, a single ASI, its default matching INI, and a SHA-512 manifest in the runtime ZIP. Keep runtime and editable sources as separate packages, with the runtime README and license notices in the ZIP. Use artifact hashes to identify the exact version any external scanner examines.
