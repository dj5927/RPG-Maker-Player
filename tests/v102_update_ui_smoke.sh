#!/bin/bash
set -euo pipefail

ROOT="${1:-/mnt/d/GPT/MKXP}"
SRC="$ROOT/_dev/src/main.cpp"
HELPER="$ROOT/runtime/update/update.sh"
TMP="$(mktemp -d /tmp/rpgmp_v102_update.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

grep -Fq 'APP_VERSION = "1.3"' "$SRC"
grep -Fq 'uiWord("업데이트", "Update"' "$SRC"
grep -Fq 'uiWord("업데이트 확인", "Check for updates"' "$SRC"
grep -Fq 'runUpdateHelper(root, "check")' "$SRC"
grep -Fq 'runUpdateHelper(root, "install", updateUi.latestVersion)' "$SRC"
grep -Fq 'launcher start | build=V102' "$SRC"
! grep -Fq 'runUpdateHelper(root, "check")' "$ROOT/_dev/src/launcher_config.cpp" 2>/dev/null || false

mkdir -p "$TMP/root/cache/update" "$TMP/releases/v1.4" "$TMP/payload"
printf '{"tag_name":"v1.4"}\n' >"$TMP/latest.json"
printf 'V102 OTA TEST\n' >"$TMP/payload/ota_test.txt"
tar -czf "$TMP/releases/v1.4/RPG_Maker_Player_SteamOS_Update_V1.4.tar.gz" -C "$TMP/payload" ota_test.txt
sha256sum "$TMP/releases/v1.4/RPG_Maker_Player_SteamOS_Update_V1.4.tar.gz" >"$TMP/releases/v1.4/RPG_Maker_Player_SteamOS_Update_V1.4.tar.gz.sha256"

RPGMP_UPDATE_API="file://$TMP/latest.json" \
  bash "$HELPER" check "$TMP/root" 1.3 '' "$TMP/check.txt"
grep -Fq 'STATUS=update' "$TMP/check.txt"
grep -Fq 'LATEST=1.4' "$TMP/check.txt"

RPGMP_UPDATE_RELEASE_ROOT="file://$TMP/releases" \
  bash "$HELPER" install "$TMP/root" 1.3 1.4 "$TMP/install.txt"
grep -Fq 'STATUS=installed' "$TMP/install.txt"
grep -Fq 'V102 OTA TEST' "$TMP/root/ota_test.txt"

echo 'V102 manual Settings update check + SHA verified OTA install PASS'
