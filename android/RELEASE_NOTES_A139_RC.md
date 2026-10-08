# Android release candidate notes

## Candidate

- Internal build: A139
- Android version: 0.13.9-a139
- APK: `RPG_Maker_Player_Android_A139_I18N_INPUT_FIX.apk`
- SHA-256: `A3D7940DDCEFB98CC96A771648A523C70316DB7CE47ABBDAC965EABA9B79AFF3`

## Highlights

- Android-native launcher UI for RPG Maker 2K/2K3/XP/VX/VX Ace/MV/MZ.
- Ruby 1.8 / 1.9 / 3.1 automatic detection and fallback for RGSS games.
- Successful AUTO runtime is saved per game and reused on later launches.
- Visible Ruby loading/fallback status instead of a silent black screen.
- Android system UI language auto detection: Korean / Japanese / English; other languages fall back to English.
- Gamepad and touch launcher navigation.
- Per-game settings, controller mapping, locale and compatibility options.
- EasyRPG libretro integration and MV/MZ Android WebView compatibility path.
- Fixed stale launcher input/process state that could leave games selectable but not launchable after closing/saving game settings.
- Modern Ruby UTF-8 filesystem compatibility retained for non-ASCII Android game paths.

## Before public release

A139 is a release candidate, not the final v1.0 Android tag. Physical-device regression testing is still required for language switching, settings-save launch recovery and representative games on all engine families.
