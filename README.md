# Super Jump for Crimson Desert

## Current Third Palm candidate

[Third Palm 2.05.01.05rc1](variants/third-palm-2.05.01.05rc1/BUILD.md) adds an offline-verified coexistence path for the recognized FreedomFlyer writer. The classic double-jump and keypress source folders remain available. Their 3732 guards and FreedomFlyer behavior are still under verification.

Windows x64 ASI source for the Super Jump variants maintained by sreerajjay. Original Super Jump concept by dukusmaximus. [Nexus mod page](https://www.nexusmods.com/crimsondesert/mods/3041)

| Variant | Trigger | Configuration |
| --- | --- | --- |
| [Third Palm P22](variants/third-palm/) | Kliff's third successful downward Force Palm in the opening jump. Damiane and Oongka use double jump. | SuperJump.ini |
| [Double-jump d43](variants/double-jump/) | The configured double-jump trigger. | SuperJump.ini |
| [Keypress k31](variants/keypress/) | The configured manual hotkey or controller chord. | SuperJump_Keypress.ini |

The variants target the named CrimsonDesert.exe process, validate native instruction contracts, install hooks through SafetyHook, and poll keyboard and controller inputs only while the game owns the foreground. Activate one variant at a time.

Build and test instructions are in the individual folders. The [Third Palm build notes](variants/third-palm/BUILD.md) document the exact build for Nexus file SJ_ThirdPalmv205. [Legacy source](source/SuperJump/) is retained for older releases.
