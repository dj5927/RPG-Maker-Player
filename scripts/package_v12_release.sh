#!/bin/bash
set -euo pipefail

BASE=/mnt/d/GPT/MKXP
SRC="$BASE/_dev/GITHUB_V1_SOURCE/release-assets/RPG_Maker_Player_SteamOS_WINDOWS_SAFE.tar.gz"
ST=/tmp/rpgmp_v12_stage
OUT="$BASE/DIST/RPG_Maker_Player_SteamOS_WINDOWS_SAFE_v1.2.tar.gz"

rm -rf "$ST"
mkdir -p "$ST"
tar -xzf "$SRC" -C "$ST"
ROOT="$ST/rpg maker player"

cp "$BASE/MKXP_Launcher" "$ROOT/MKXP_Launcher"
cp "$BASE/runtime/ruby18/mkxp-z" "$ROOT/runtime/ruby18/mkxp-z"
cp "$BASE/runtime/ruby19/mkxp-z" "$ROOT/runtime/ruby19/mkxp-z"
cp "$BASE/runtime/ruby31/mkxp-z" "$ROOT/runtime/ruby31/mkxp-z"
cp "$BASE/runtime/mvmz/jspatches/start-select-exit.js" "$ROOT/runtime/mvmz/jspatches/start-select-exit.js"
cp "$BASE/README_KR.md" "$ROOT/README_KR.md"
cp "$BASE/CURRENT_MKXP_AUTHORITY.json" "$ROOT/CURRENT_MKXP_AUTHORITY.json"
cp "$BASE/FULL_PACKAGE_V082.txt" "$ROOT/FULL_PACKAGE_V082.txt"

rm -f "$ROOT/FULL_PACKAGE_V068.txt" "$ROOT/config/launcher.json" "$ROOT/game/gamelist.json"
rm -rf "$ROOT/config/catalogs"
find "$ROOT/cache" -mindepth 1 -maxdepth 1 -exec rm -rf {} + 2>/dev/null || true
find "$ROOT/logs" -mindepth 1 -maxdepth 1 -exec rm -rf {} + 2>/dev/null || true
mkdir -p "$ROOT/cache" "$ROOT/logs" "$ROOT/config"

chmod +x "$ROOT/MKXP_Launcher" \
  "$ROOT/runtime/ruby18/mkxp-z" \
  "$ROOT/runtime/ruby19/mkxp-z" \
  "$ROOT/runtime/ruby31/mkxp-z"

rm -f "$OUT"
tar -C "$ST" -czf "$OUT" 'rpg maker player'
gzip -t "$OUT"

echo PACKAGE_OK
sha256sum "$OUT"
stat -c '%s' "$OUT"
echo USER_STATE_CHECK
if tar -tzf "$OUT" | grep -E 'config/launcher.json|config/catalogs/|cache/.+|logs/.+|game/gamelist.json'; then
  echo 'ERROR: user-state file found in package' >&2
  exit 9
fi
echo USER_STATE_CLEAN
