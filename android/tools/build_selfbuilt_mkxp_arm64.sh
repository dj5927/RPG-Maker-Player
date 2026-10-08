#!/usr/bin/env bash
set -euo pipefail

NDK=/opt/rpgmp-android-ndk-r25c/android-ndk-r25c
APP=/opt/rpgmp-mkxpz-build/app
LEGACY_MK="$APP/jni/Android-legacy.mk"
PROJECT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

cp "$PROJECT/patches/mkxp-z-android-mtool/Android-legacy.mk" "$LEGACY_MK"
cp "$PROJECT/native/mkxp-z-legacy/mkxp-z-187.mk" "$APP/jni/mkxp-z-187.mk"
cp "$PROJECT/native/mkxp-z-legacy/mkxp-z-193.mk" "$APP/jni/mkxp-z-193.mk"
cp "$PROJECT/native/mkxp-z-legacy/mkxp-z.mk" "$APP/jni/mkxp-z.mk"
cp "$PROJECT/native/mkxp-z-legacy/src/config.cpp" "$APP/jni/mkxp-z/src/config.cpp"
cp "$PROJECT/native/mkxp-z-legacy/src/main.cpp" "$APP/jni/mkxp-z/src/main.cpp"
cp "$PROJECT/native/mkxp-z-legacy/src/sharedstate.cpp" "$APP/jni/mkxp-z/src/sharedstate.cpp"
cp "$PROJECT/native/mkxp-z-legacy/src/display/font.cpp" "$APP/jni/mkxp-z/src/display/font.cpp"
cp "$PROJECT/native/mkxp-z-legacy/src/display/bitmap.cpp" "$APP/jni/mkxp-z/src/display/bitmap.cpp"
cp "$PROJECT/native/mkxp-z-legacy/src/util/debugwriter.h" "$APP/jni/mkxp-z/src/util/debugwriter.h"
cp "$PROJECT/native/mkxp-z-legacy/binding/binding-mri.cpp" "$APP/jni/mkxp-z/binding/binding-mri.cpp"
cp "$PROJECT/native/mkxp-z-legacy/src/util/encoding.h" "$APP/jni/mkxp-z/src/util/encoding.h"
cp "$PROJECT/native/mkxp-z-legacy/binding/audio-binding.cpp" "$APP/jni/mkxp-z/binding/audio-binding.cpp"
cp "$PROJECT/native/mkxp-z-legacy/binding/bitmap-binding.cpp" "$APP/jni/mkxp-z/binding/bitmap-binding.cpp"
cp "$PROJECT/native/mkxp-z-legacy/binding/graphics-binding.cpp" "$APP/jni/mkxp-z/binding/graphics-binding.cpp"
python3 "$PROJECT/tools/patch_mkxpz_android_bridge.py" "$APP/jni/mkxp-z"
python3 "$PROJECT/tools/patch_mkxpz_cicpoffs_pre_eval.py" "$APP/jni/mkxp-z/binding/binding-mri.cpp"
python3 "$PROJECT/tools/patch_mkxp193_include.py" "$APP/jni/mkxp-z-193.mk"

exec "$NDK/ndk-build" \
  -C "$APP" \
  NDK_PROJECT_PATH="$APP" \
  APP_BUILD_SCRIPT="$LEGACY_MK" \
  NDK_APPLICATION_MK="$APP/jni/Application.mk" \
  APP_ABI=arm64-v8a \
  APP_PLATFORM=android-21 \
  APP_MODULES="mkxp-z-187 mkxp-z-193 mkxp-z-31" \
  -j16
