# Android publication checklist

Target repository: `dj5927/RPG-Maker-Player`
Target source location: `android/`
Suggested release naming once device validation is complete: `RPG Maker Player Android v1.0` (tag to be chosen at release time).

## Source publication

- [x] Android application source staged
- [x] EasyRPG bridge source staged
- [x] modified mkxp-z modern source staged
- [x] modified mkxp-z legacy source staged
- [x] Ruby 3.1 Android patch source/helpers staged
- [x] build tools and smoke tests staged
- [x] Android README / build guide / third-party notices staged
- [x] SDK/NDK/build caches/logs/game files excluded
- [x] no source file over 50 MB in staging
- [ ] physical-device final regression pass
- [ ] final Android version/tag chosen
- [ ] release APK promoted from candidate to final
- [ ] screenshots for Android release page selected
- [ ] push `android/` source tree to `main` or review branch
- [ ] create Android-specific GitHub Release and attach APK/source archive

## Required final smoke

- `ANDROID_MKXP_RUNTIME_SMOKE_PASS`
- `ANDROID_STORAGE_RUNNER_SMOKE_PASS`
- `RGSS_COMPAT_ASSET_COUNT=1039`
- packaged modern `libruby.so` SHA-256 = `B70601E654E02EE19E3DD764712AA9EEC5D262121115F7549F670AF6310245EC`

## Device checks

- Save/close per-game settings, then launch multiple different games by touch.
- Korean Android system language -> Korean launcher.
- Japanese Android system language -> Japanese launcher.
- English or unsupported Android language -> English launcher.
- AUTO Ruby search shows current candidate and persists the successful runtime.
- Pokemon Reborn launches with modern Ruby 3.1 without transcoder recursion regression.
- One known Ruby 1.8 title and one Ruby 1.9 title launch.
- EasyRPG audio and per-game font/SoundFont settings.
- MV/MZ launch and back/exit handling.
