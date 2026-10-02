#!/usr/bin/env bash
set -euo pipefail

SRC=/root/mkxp-build/src
BUILD="$SRC/build19nothreaded"
PCDIR=/mnt/d/GPT/MKXP/_dev/ruby_pc_nothreaded
MESON=/root/mkxp-pybuild/bin/meson

cp /mnt/d/GPT/MKXP/_dev/mkxp-z-upstream/binding/binding-mri.cpp "$SRC/binding/binding-mri.cpp"
cp /mnt/d/GPT/MKXP/_dev/mkxp-z-upstream/src/audio/audio.cpp "$SRC/src/audio/audio.cpp"
cp /mnt/d/GPT/MKXP/_dev/mkxp-z-upstream/src/audio/alstream.cpp "$SRC/src/audio/alstream.cpp"
cp /mnt/d/GPT/MKXP/_dev/mkxp-z-upstream/src/audio/vorbissource.cpp "$SRC/src/audio/vorbissource.cpp"

if [ ! -f "$BUILD/build.ninja" ]; then
  PKG_CONFIG_PATH="$PCDIR" "$MESON" setup "$BUILD" "$SRC" \
    -Dmri_version=1.9 \
    -Dworkdir_current=true \
    -Ddefault_system=auto \
    -Druby_system=enabled \
    -Dsdl_ttf_system=disabled \
    -Dbuildtype=release \
    -Dcjk_fallback_font=true
fi

PKG_CONFIG_PATH="$PCDIR" ninja -C "$BUILD" -j2
cp "$BUILD/mkxp-z.x86_64" /mnt/d/GPT/MKXP/runtime/ruby19/mkxp-z.nothreaded
ldd /mnt/d/GPT/MKXP/runtime/ruby19/mkxp-z.nothreaded | grep -E 'ruby|not found' || true
