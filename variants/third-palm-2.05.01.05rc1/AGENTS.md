# Super Jump Third Palm

Read [HANDOFF.md](HANDOFF.md), [SUPER_JUMP_CONTRACT.md](SUPER_JUMP_CONTRACT.md), [BUILD.md](BUILD.md), and [TEST_HISTORY.json](TEST_HISTORY.json) before changing the project. Version `2.05.01.02` is the public baseline promoted from gameplay-approved RC5.

- Preserve the automatic, input-independent behavior for Kliff, Damiane, and Oongka. Preserve the two-hook design and existing boost tuning unless the maintainer asks for a gameplay change.
- Use x64 MSVC, C++23, the static CRT, and the pinned static SafetyHook/Zydis/Zycore sources in `reference/P26`. Treat the P26 snapshot as reference material.
- Verify native identity, ABI, ownership, instruction boundaries, and both continuation branches. Prove an independent safe movement path when the entry is occupied. Disable intervention on a critical mismatch and restore only bytes owned by this plugin.
- Use fixed INFO logging in RC and public builds. Keep logs in the ASI's `logs` directory and inventory only the ASI directory. Keep addresses, offsets, instruction bytes, signatures, and sensitive internal labels out of runtime logs.
- Use read-only executable inspection and isolated fixtures for automated checks. The maintainer owns actual gameplay and release publication. Record the exact artifact, who tested it, and the outcome in `TEST_HISTORY.json`.
- Keep the ASI and source distribution separate. Preserve author credits and licenses, update version identity consistently, and verify each archive against its SHA-512 manifest.
