# Super Jump Third Palm maintainer handoff

This folder is the source root for the public release `2.05.01.02`, prepared for sreerajjay (SrJ). Extract the entire handoff archive, then open the extracted folder as a local project in Codex. The root contains `AGENTS.md` and `CMakeLists.txt`. Begin by asking Codex to read this handoff, the technical contract, and the test history before reproducing the build.

## Build and verify

Use an x64 Visual Studio 2026 developer prompt with C++, MASM, Windows SDK, CMake, Ninja, and Python. Follow [BUILD.md](BUILD.md). The bundled `reference/P26` snapshot contains the dependencies needed by the build. When running the executable inspection tools, substitute the path to your installed `CrimsonDesert.exe`.

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
.\build\publisher-fixture.exe
```

The build emits `build/SuperJump_ThirdPalm.asi`. The published-package ASI SHA-256 is `4813bb1bd004946fc51cb8506e3df1f406c0c9e2dbc6e5367855888933da429a`. The reference toolchain is MSVC 19.51.36263.0 with Windows SDK 10.0.26100.0. Other toolchains can affect the binary hash.

## Where to work

| Area | Files |
| --- | --- |
| Gameplay, actor ownership, activation and delivery | `src/Runtime.cpp`, `src/NativeDoubleJump.h`, `src/PalmPublishPolicy.h` |
| Layout and coexistence | `src/LayoutValidation.h`, `src/SiteResolver.cpp`, `src/GameImage.cpp` |
| Hook installation, continuation, ownership and rollback | `src/HookGroup.cpp`, `tests/PublisherFixture.cpp` |
| Runtime logging and version identity | `src/SessionLog.cpp`, `src/ModVersion.h` |
| Packaging and verification | `tools/package_rc.py`, `tools/inspect_sites.cpp`, `tools/verify_layout.cpp` |

## Release evidence

RC2 proved the two-hook design in gameplay on 2.05.00 for all three characters. RC3 and RC4 used separate Palm collectors while diagnosing Kliff. The original local Kliff test used a double jump before the three Force Palms. A single jump is the intended opener, and the RC2 retest confirmed it worked. RC5 retained RC2 gameplay and carried over the independently validated movement path introduced in RC3.

The tester approved RC5 and requested the public build. The reviewed 2.05.00 session shows `NATIVE_CONTINUATION` and boosts for Kliff and Damiane, with MasterLooter and CrimsonInvEditor in the ASI inventory. The log establishes availability of the fallback and Super Jump's boost delivery. The ASI inventory records file presence. Oongka's boost has a dedicated RC2 log entry. The public build changes version identity and release documentation. Both game executables passed offline layout validation, while the gameplay evidence cited here comes from 2.05.00.

The complete machine-readable history is in [TEST_HISTORY.json](TEST_HISTORY.json). The [technical contract](SUPER_JUMP_CONTRACT.md) is the reference for future changes. The maintainer handles Nexus publication. See the [mod page](https://www.nexusmods.com/crimsondesert/mods/3041).
