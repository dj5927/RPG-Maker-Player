# Building the Android version

This document describes the source layout prepared for the public `android/` tree.

## Requirements

- JDK 17
- Android SDK matching compileSdk 36
- Android NDK r25c for the current mkxp-z native build path
- Gradle wrapper included in this tree
- Windows + WSL is the currently validated native build environment

Do not commit `local.properties`; create it locally and point `sdk.dir` to your Android SDK.

## Java / Android application

From the `android/` directory:

```text
gradlew.bat assembleDebug --no-daemon
```

The application source is in `app/src/main/` and the EasyRPG bridge is in `easyrpg_runtime/src/`.

## mkxp-z native runtimes

The project maintains separate compatibility runtimes for Ruby 1.8, Ruby 1.9 and modern Ruby 3.1.

Modified source snapshots are included under:

- `native/mkxp-z-legacy/`
- `native/mkxp-z-modern/`

The project build/reproducibility helpers are in `tools/` and compatibility patches are in `patches/`.

The modern Ruby 3.1 Android filesystem/locale changes are preserved in `native/ruby31-android-patches/`. The validated packaged modern `libruby.so` for the current candidate has SHA-256:

`B70601E654E02EE19E3DD764712AA9EEC5D262121115F7549F670AF6310245EC`

This hash is checked by the project smoke test to prevent the previous non-ASCII-path regression from returning.

## Prebuilt runtime files

Prebuilt `.so` libraries, SoundFonts and other large runtime payloads are not intended for normal Git history. Release builds should obtain/build them through the documented native build pipeline and package them into the APK/Release assets.

## Validation

The currently required project checks are:

```text
powershell -ExecutionPolicy Bypass -File tests/android_mkxp_runtime_smoke.ps1
powershell -ExecutionPolicy Bypass -File tests/android_storage_runner_smoke.ps1
```

A public release should only be tagged after both pass and the candidate is validated on a physical Android device.
