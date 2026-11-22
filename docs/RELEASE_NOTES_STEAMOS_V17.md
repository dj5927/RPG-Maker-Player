# RPG Maker Player SteamOS v1.7

## Improvements
- Fix intermittent Steam menu Stop/Exit hang: use Steam-supervised launcher PID,
  handle TERM/INT/HUP and SDL window-close events, then stop active game
  process groups within a bounded timeout without terminating Steam.
- Update the three mkxp-z Linux/RGSS runtime binaries (Ruby 1.8 / 1.9 / 3.1)
  with selected upstream improvements for high-resolution Bitmap copies,
  bilinear sprite scaling and safe unknown-character-set conversion.
- Update EasyRPG Player/its liblcf parser from source, preserving SDL2
  compatibility, CP932/CP949 options and audio/font fallbacks.
- Add NW.js v0.120.0 (Chromium 157) as a separate runtime; keep
  v0.117.0 and v0.29.0 available for legacy MV games.
- Fix MV/MZ plugin root-file symlink detection when the plugin uses
  Node `fs.readFileSync` instead of `readdirSync`/`accessSync`.
- Restore missing source-tree Steam mouse bridge test and verify Linux
  launcher, native engines, and multi-signal Steam Stop behavior.

## Updates and preservation
- SteamOS built-in Settings > Update recognizes this version as v1.7.
- Full package is for fresh installations; the cumulative OTA supports
  existing SteamOS v1.3, v1.4, v1.5, v1.6 installations.
- SHA256 sidecar accompanies the OTA tar.gz.
- Game data, saved games, selected library, custom settings, fonts and
  runtime/_compat user patch database are not replaced by the OTA.
- Proton download/selection features have not been changed.

## Device testing
Linux automation is not a substitute for real SteamOS Gamescope testing.
Test Steam-menu Exit with the launcher idle and while an RPG Maker
2000/2003, XP/VX/VX Ace, MV/MZ and WOLF title is running.
