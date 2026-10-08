#!/usr/bin/env bash
set -euo pipefail

ROOT=/opt/rpgmp-mkxpz-build/app/libs/arm64-v8a
READELF=/opt/rpgmp-android-ndk-r25c/android-ndk-r25c/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf

for name in \
  libmkxp-z-187.so \
  libmkxp-z-193.so \
  libruby187.so \
  libruby193.so \
  libSDL2.so \
  libSDL2_image.so \
  libSDL2_sound.so \
  libSDL2_ttf.so \
  libopenal.so \
  libc++_shared.so
do
  file="$ROOT/$name"
  echo "===== $name ====="
  if [ ! -f "$file" ]; then
    echo MISSING
    continue
  fi
  ls -lh "$file"
  "$READELF" -d "$file" | grep -E 'SONAME|NEEDED' || true
done
