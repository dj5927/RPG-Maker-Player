#!/usr/bin/env bash
set -euo pipefail

PROJECT=/mnt/d/GPT/MKXP
SOURCE=/root/mkxp-build/src
BACKUP="$PROJECT/DIST/STEAMOS_V1151_NATIVE_CASEFOLD_BACKUP"
mkdir -p "$BACKUP"

runtime=(ruby18 ruby19 ruby31)
build=(build18release build19nothreaded build31release)
baseline=(
  ED922FC681850DC52EDA05C4D5D24961DEBF41BB79120AD4E423116D612CFA84
  45C3869C90F968930CD8A9F341698FAE0A50E08A7C5E5D00583D0E1CB966B791
  3FB040FC2E2FD5F41073C26B14B6401C8D3C79E694401F648C025561611D774D
)

for i in 0 1 2; do
  game_binary="$PROJECT/runtime/${runtime[$i]}/mkxp-z"
  backup="$BACKUP/${runtime[$i]}_mkxp-z"
  if [[ ! -e "$backup" ]]; then
    current=$(sha256sum "$game_binary" | awk '{print toupper($1)}')
    [[ "$current" == "${baseline[$i]}" ]] || {
      echo "REFUSING native overwrite ${runtime[$i]}: expected ${baseline[$i]}, got $current" >&2
      exit 3
    }
    cp -p "$game_binary" "$backup"
  fi
  [[ $(sha256sum "$backup" | awk '{print toupper($1)}') == "${baseline[$i]}" ]] || {
    echo "ROLLBACK HASH MISMATCH ${runtime[$i]}" >&2
    exit 4
  }
done

mkdir -p "$SOURCE/src/filesystem" "$SOURCE/binding"
cp "$PROJECT/_dev/mkxp-z-upstream/src/filesystem/filesystem.cpp" "$SOURCE/src/filesystem/filesystem.cpp"
cp "$PROJECT/_dev/mkxp-z-upstream/src/filesystem/filesystem.h" "$SOURCE/src/filesystem/filesystem.h"
cp "$PROJECT/_dev/mkxp-z-upstream/binding/binding-mri.cpp" "$SOURCE/binding/binding-mri.cpp"

for i in 0 1 2; do
  dir="$SOURCE/${build[$i]}"
  test -f "$dir/build.ninja" || { echo "Missing ninja build $dir" >&2; exit 5; }
  echo "CASEFOLD_NATIVE_${runtime[$i]}_BUILD_START"
  ninja -C "$dir" -j2
  binary="$dir/mkxp-z.x86_64"
  test -f "$binary" || { echo "Native file missing: $binary" >&2; exit 6; }
  for marker in rpgmp_resource_file rpgmp_resource_disk_directory rpgmp_resource_casefold.rb; do
    grep -aq "$marker" "$binary" || {
      echo "Native resource API marker '$marker' missing from ${runtime[$i]}" >&2
      exit 7
    }
  done
  if ldd "$binary" | grep -q 'not found'; then
    echo "Missing native dependency ${runtime[$i]}" >&2
    exit 8
  fi
  echo "CASEFOLD_NATIVE_BUILT_${runtime[$i]} $(sha256sum "$binary")"
done

# Install only after every independent RGSS runtime has compiled and passed.
for i in 0 1 2; do
  binary="$SOURCE/${build[$i]}/mkxp-z.x86_64"
  cp "$binary" "$PROJECT/runtime/${runtime[$i]}/mkxp-z"
  chmod +x "$PROJECT/runtime/${runtime[$i]}/mkxp-z"
  echo "CASEFOLD_NATIVE_INSTALLED_${runtime[$i]} $(sha256sum "$PROJECT/runtime/${runtime[$i]}/mkxp-z")"
done
echo STEAMOS_V1152_CASEFOLD_THREE_NATIVE_RUBIES_PASS
