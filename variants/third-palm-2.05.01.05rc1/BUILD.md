# Build Super Jump Third Palm 2.05.01.05rc1

Use the x64 Native Tools Command Prompt for Visual Studio 2026 with the
Desktop development with C++ workload (including MASM), Windows SDK,
CMake 3.28 or later, Ninja, and Python 3.11 or later for packaging.

The rebuild uses a new ASI runtime. It retains the P26 action, actor, UI,
traversal, and compatibility contracts and the source snapshot of its
static SafetyHook/Zydis/Zycore dependencies. The P26 sources and original
build scripts are not used as the ASI entry point.

The source package includes the required snapshots under reference/P26.
For a source checkout without that folder, extract the original P26 source
archive to a separate folder and pass
`-DSUPERJUMP_P26_REFERENCE="<path to extracted P26 source>"` at configuration.
Its ZIP SHA-256 is
`5463946ce469be7537dc4c9d749786dd8854a6aa4e66bd5d64888577117ff247`.

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded
    cmake --build build
    ctest --test-dir build --output-on-failure
    build\boost-config-tests.exe build\boost-config-fixture
    build\publisher-fixture.exe
    build\inspect-sites.exe "C:\path\to\CrimsonDesert.exe" --verify
    build\verify-layout.exe "C:\path\to\CrimsonDesert.exe"
    build\logger-fixture.exe build "C:\path\to\CrimsonDesert.exe"

The verification utilities read the game executable as data. The hook
fixture uses its own test functions. The game and loader are required only
for gameplay tests. The output plugin is build/SuperJump_ThirdPalm.asi.

The 2.05.01.02 release used MSVC 19.51.36263.0 and Windows SDK 10.0.26100.0. This candidate is verified with the local MSVC 19.52 toolchain. The ASI
is C++23 x64 Release with static CRT and statically linked dependencies.
The toolchain can affect the final binary hash. The optional distribution INI
is at distribution/SuperJump_ThirdPalm.ini. The live reload test saves two
encodings, replaces the file, and verifies last-valid retention.

To assemble the runtime ZIP and the separate editable source ZIP:

    python tools\package_rc.py build\SuperJump_ThirdPalm.asi reference\P26 release

For a source checkout, use the same explicit P26 source path used for CMake.
The packager verifies the payload manifests after writing both archives.

## 2.05.01.05rc1 FreedomFlyer candidate

This revision keeps the two game-image SafetyHook sites. It verifies the recognized FreedomFlyer writer and continuation through a read-only view. Third Palm yields while the flight vectors are active or their ownership is unknown. The verifier keeps DLMM's adjacent writer visible. No foreign code or data is edited.

The stock 3732 layout and native action publisher fixtures passed. The isolated FreedomFlyer test passed nine checks. This is an offline-verified candidate. Manual testing must confirm idle-flight jumps and active-flight suppression, including the transition back to idle.
