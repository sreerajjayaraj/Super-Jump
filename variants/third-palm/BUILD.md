# Build and test Third Palm P22

## Tools

Use the x64 Native Tools Command Prompt for Visual Studio 2026 with the Desktop development with C++ workload. The reviewed build used the MSVC 14.52.37025 toolset, compiler 19.52.37029, and Windows SDK 10.0.26100.0. CMake 3.28 or newer, Ninja, and Node.js must be on PATH. The verification used CMake 4.3.1, Ninja 1.13.2, and Node.js 26.12.1.

SafetyHook, Zydis 4.1.0, and its Zycore source are present under `deps/` with their licenses. The bundled SafetyHook headers expose read-only trampoline and original instruction accessors used to check the native hooks. The project does not need the game executable, an installed mod loader, or any active INI to build or test.

## Build from source

From the directory containing these files, run:

```bat
BuildThirdPalm.cmd
TestThirdPalm.cmd
```

The first command builds the bundled dependencies, compiles `source/SuperJump.cpp` and its version resource, and links `release/SuperJump_ThirdPalm.asi`. It then checks the intact static TLS template and empty callback list. The second runs the 44 policy checks and 569 integration checks using authored fixtures.

A source build recompiles the dependencies. It may have a different hash from the uploaded ASI, which was linked against the archived library builds listed below.

## Reproduce the uploaded file byte for byte

The separate `SuperJump-ThirdPalm-P22-link-libraries.zip` preserves the three exact static libraries used for the upload. Extract its `deps/lib/` subtree into the source root. With the toolchain above, run:

```bat
BuildThirdPalm.cmd exact
certutil -hashfile release\SuperJump_ThirdPalm.asi SHA256
```

The expected release ASI is 743,936 bytes with SHA-256 `342A4AED32561C382818F86C98368B300657C7E0B63EE24769B0E560F97215B6`.

| Static library | SHA-256 |
| --- | --- |
| `safetyhook.lib` | `62F19981489A73A1BA04D9E306F8DDDC41265D3A2C92CF533B7614EDEE631D9D` |
| `Zydis.lib` | `6479619FF7E4584D81191D5993421DEB267010AC59CD5235EB5ACAF43AE2433F` |
| `Zycore.lib` | `3F24DA02C087C1A107591096F2F98E71837A40B8671C55DB71E0E449C4DE24D3` |

These hashes identify the reviewed artifact and link inputs. Runtime admission checks the named game process and relevant native instruction contracts without requiring an executable hash.
