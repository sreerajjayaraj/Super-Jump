# Super Jump Third Palm P22

Source for the Windows x64 Crimson Desert mod published as `SJ_ThirdPalmv205` (Nexus version `205TP`, internal version `1.6.2.21`).

Kliff receives a boost after the third successful downward Force Palm in his opening jump combo. Damiane and Oongka retain the double-jump fallback. Original Super Jump concept by dukusmaximus. [Nexus mod page](https://www.nexusmods.com/crimsondesert/mods/3041)

All three native hooks use SafetyHook inside the main `CrimsonDesert.exe` executable mapping. The mod checks the executable basename, executable-page ownership, and surrounding native instructions. Updates that change a required instruction or data layout can cause the affected feature to refuse initialization. Keyboard and XInput polling require the game to own the foreground window, and a focus change cancels pending boosts. DesertLink compatibility checks read its movement and flight state. Third Palm yields while True Flight is engaged.

[Build and test steps](BUILD.md) · [Changelog](CHANGELOG.md)
