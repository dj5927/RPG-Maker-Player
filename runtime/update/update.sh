#!/bin/bash
set -euo pipefail

MODE="${1:-}"
ROOT="${2:-}"
CURRENT="${3:-0}"
LATEST="${4:-}"
OUT="${5:-}"
REPO="dj5927/RPG-Maker-Player"
API="${RPGMP_UPDATE_API:-https://api.github.com/repos/${REPO}/releases/latest}"
RELEASE_ROOT="${RPGMP_UPDATE_RELEASE_ROOT:-https://github.com/${REPO}/releases/download}"

write_out() {
  mkdir -p "$(dirname "$OUT")"
  printf '%s\n' "$@" >"$OUT"
}

version_newer() {
  local current="$1" latest="$2"
  [ "$current" != "$latest" ] && [ "$(printf '%s\n%s\n' "$current" "$latest" | sort -V | tail -n1)" = "$latest" ]
}

[ -n "$ROOT" ] && [ -n "$OUT" ] || exit 2

if [ "$MODE" = "check" ]; then
  command -v curl >/dev/null 2>&1 || { write_out "STATUS=error" "ERROR=curl_missing"; exit 3; }
  JSON="$(curl -fsSL --connect-timeout 8 --max-time 20 -H 'Accept: application/vnd.github+json' "$API")" || {
    write_out "STATUS=error" "ERROR=network"; exit 4;
  }
  TAG="$(printf '%s' "$JSON" | sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"v\{0,1\}\([^"]*\)".*/\1/p' | head -n1)"
  [ -n "$TAG" ] || { write_out "STATUS=error" "ERROR=parse"; exit 5; }
  if version_newer "$CURRENT" "$TAG"; then
    write_out "STATUS=update" "LATEST=$TAG"
  else
    write_out "STATUS=current" "LATEST=$TAG"
  fi
  exit 0
fi

if [ "$MODE" = "install" ]; then
  [ -n "$LATEST" ] || { write_out "STATUS=error" "ERROR=missing_version"; exit 6; }
  CACHE="$ROOT/cache/update"
  mkdir -p "$CACHE"
  ASSET="RPG_Maker_Player_SteamOS_Update_V${LATEST}.tar.gz"
  URL="${RELEASE_ROOT}/v${LATEST}/${ASSET}"
  PKG="$CACHE/$ASSET"
  SHA="$CACHE/${ASSET}.sha256"
  curl -fL --retry 2 --connect-timeout 8 --max-time 900 "$URL" -o "$PKG" || {
    write_out "STATUS=error" "ERROR=download"; exit 7;
  }
  curl -fL --retry 2 --connect-timeout 8 --max-time 30 "${URL}.sha256" -o "$SHA" || {
    write_out "STATUS=error" "ERROR=sha_download"; exit 8;
  }
  EXPECTED="$(awk 'NR==1 {print $1}' "$SHA")"
  ACTUAL="$(sha256sum "$PKG" | awk '{print $1}')"
  [ -n "$EXPECTED" ] && [ "$EXPECTED" = "$ACTUAL" ] || {
    write_out "STATUS=error" "ERROR=sha_mismatch"; exit 9;
  }
  STAGE="$(mktemp -d "$CACHE/stage.XXXXXX")" || {
    write_out "STATUS=error" "ERROR=stage"; exit 10;
  }
  cleanup() { rm -rf "$STAGE"; }
  trap cleanup EXIT
  tar -xzf "$PKG" -C "$STAGE" || { write_out "STATUS=error" "ERROR=extract"; exit 11; }
  if [ -e "$STAGE/config/launcher.json" ] || [ -e "$STAGE/game/gamelist.json" ] || \
     [ -d "$STAGE/config/catalogs" ] || [ -d "$STAGE/cache" ] || [ -d "$STAGE/logs" ]; then
    write_out "STATUS=error" "ERROR=unsafe_payload"; exit 12
  fi
  cp -a --remove-destination "$STAGE"/. "$ROOT"/ || {
    write_out "STATUS=error" "ERROR=apply"; exit 13;
  }
  chmod +x "$ROOT/MKXP_Launcher" "$ROOT/run.sh" 2>/dev/null || true
  write_out "STATUS=installed" "LATEST=$LATEST"
  exit 0
fi

write_out "STATUS=error" "ERROR=bad_mode"
exit 11
