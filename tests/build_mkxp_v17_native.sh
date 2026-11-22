#!/usr/bin/env bash
set -euo pipefail
PROJECT=/mnt/d/GPT/MKXP
SRC=/root/mkxp-build/src
PATCH="$PROJECT/work/STEAMOS_20261122/third_party/mkxp-z"
DEST="$PROJECT/work/v17_runtime_stage/runtime"
BACKUP=/root/rpgmp-v17/mkxp-source-backup
mkdir -p "$BACKUP"
for relative in src/display/bitmap.cpp src/display/sprite.cpp src/util/encoding.h; do
  test -f "$SRC/$relative"
  cmp "$SRC/$relative" "$PROJECT/_dev/mkxp-z-upstream/$relative" >/dev/null || {
    echo "SOURCE_DRIFT_REFUSING=$relative" >&2
    exit 4
  }
  mkdir -p "$BACKUP/$(dirname "$relative")"
  if [[ ! -f "$BACKUP/$relative" ]]; then cp -p "$SRC/$relative" "$BACKUP/$relative"; fi
  cp "$PATCH/$relative" "$SRC/$relative"
done
for pair in ruby18:build18release ruby19:build19nothreaded ruby31:build31release; do
  runtime=${pair%%:*}
  build=${pair#*:}
  echo "MKXP_V17_BUILD_START=$runtime"
  ninja -C "$SRC/$build" -j4
  binary="$SRC/$build/mkxp-z.x86_64"
  test -x "$binary"
  if ldd "$binary" | grep -q 'not found'; then
    echo "MKXP_V17_LINK_FAILED=$runtime" >&2
    exit 5
  fi
  test "$(file -b "$binary")" != ''
  for marker in rpgmp_resource_file rpgmp_resource_disk_directory rpgmp_resource_casefold.rb; do
    grep -aq "$marker" "$binary" || { echo "MISSING_MARKER=$runtime:$marker" >&2; exit 6; }
  done
  mkdir -p "$DEST/$runtime"
  cp "$binary" "$DEST/$runtime/mkxp-z"
  chmod +x "$DEST/$runtime/mkxp-z"
  sha256sum "$DEST/$runtime/mkxp-z"
  echo "MKXP_V17_NATIVE_BUILT=$runtime"
done
echo MKXP_V17_THREE_RUBY_NATIVE_BUILDS_PASS
