# Changelog

## 205TP / P22

- Converted the boost delivery path to SafetyHook and kept all native hook targets inside CrimsonDesert.exe.
- Removed game-version and whole-executable hash checks from runtime admission. The plugin still validates the native instructions and data layouts it uses.
- Restricted keyboard and controller polling to the game’s foreground window and required a fresh press after focus returns.
- Removed the hook inside DesertLink. DesertLink controls its own jump multiplier, and Third Palm treats flight and movement state as read-only.
- Preserved Kliff’s third downward palm and the double-jump fallback for Damiane and Oongka.
