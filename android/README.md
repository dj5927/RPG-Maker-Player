# RPG Maker Player for Android

Android port/source tree for RPG Maker Player.

This directory is intended to live under `android/` in the existing public repository:
`https://github.com/dj5927/RPG-Maker-Player`

## Supported engines

- RPG Maker 2000 / 2003: EasyRPG libretro runtime
- RPG Maker XP / VX / VX Ace: mkxp-z with Ruby 1.8 / 1.9 / 3.1 compatibility paths
- RPG Maker MV / MZ: Android WebView compatibility runtime

## UI languages

The launcher follows the Android system language automatically:

- English
- 한국어 (Korean)
- 日本語 (Japanese)

Any other Android system language falls back to English.

## Current pre-release candidate

RC6 / Android app version `0.16.5-a165`.

Selected libraries automatically create an empty _compat/patches.json file
without overwriting user patches. Static MV/MZ NW.js file lookups reuse the
complete resource index regardless of JSON/CSV/RCSV; saves use live storage.

## Source layout

- `app/` Android launcher/player application source
- `easyrpg_runtime/` EasyRPG Android bridge source
- `native/mkxp-z-modern/` modified modern mkxp-z source snapshot
- `native/mkxp-z-legacy/` modified legacy mkxp-z source snapshot
- `native/ruby31-android-patches/` Ruby 3.1 Android filesystem/locale source and patch helpers
- `patches/` compatibility patches
- `tools/` build/reproducibility helpers
- `tests/` project smoke tests

## Binary policy

Large/generated binaries are intentionally not tracked in the Git source tree. The release APK and required redistributable runtime payloads are published as GitHub Release assets. Android SDK/NDK, Gradle caches, build outputs, personal logs and game files are never committed.

See `BUILDING_ANDROID.md`, `THIRD_PARTY_ANDROID.md` and `PUBLISH_CHECKLIST.md`.

RC6 restores four previously verified exact-hash XP fixes for The Curse of Pleasure that were accidentally omitted in RC5. Existing user profiles are preserved and backed up before migration. Abaddon test rules remain excluded.
