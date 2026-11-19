# Keypress k31

This folder contains the variant's C++ and resource source, with shared native-hook and input code in the neighbouring shared folder. The configuration filename is `SuperJump_Keypress.ini`.

Open the x64 Native Tools Command Prompt for Visual Studio 2026 with CMake, Ninja and Node.js on PATH. Run `Build.cmd` to build the ASI using the repository's bundled dependency source, and `Test.cmd` to run the authored test fixtures. Build output goes to `release/`. The tests do not need an installed game, DesertLink or a user's INI.

[Toolchain and dependency details](../third-palm/BUILD.md)
