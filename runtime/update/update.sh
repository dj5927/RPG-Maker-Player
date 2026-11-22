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
PROGRESS="${OUT}.progress"
PROGRESS_PERCENT=0
DOWNLOAD_BYTES=0
DOWNLOAD_TOTAL=0

write_progress() {
  local step="$1" pct="$2" bytes="${3:-$DOWNLOAD_BYTES}" total="${4:-$DOWNLOAD_TOTAL}"
  PROGRESS_PERCENT="$pct"
  DOWNLOAD_BYTES="$bytes"
  DOWNLOAD_TOTAL="$total"
  local temp="${PROGRESS}.tmp.$$"
  mkdir -p "$(dirname "$PROGRESS")"
  {
    printf 'STAGE=%s\n' "$step"
    printf 'PERCENT=%s\n' "$pct"
    printf 'BYTES=%s\n' "$bytes"
    printf 'TOTAL=%s\n' "$total"
  } > "$temp"
  mv -f -- "$temp" "$PROGRESS"
}

fail_install() {
  local reason="$1" code="$2"
  write_progress failed "$PROGRESS_PERCENT"
  write_out "STATUS=error" "ERROR=$reason"
  exit "$code"
}

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
  write_progress prepare 1 0 0
  ASSET="RPG_Maker_Player_SteamOS_Update_V${LATEST}.tar.gz"
  URL="${RELEASE_ROOT}/v${LATEST}/${ASSET}"
  PKG="$CACHE/$ASSET"
  SHA="$CACHE/${ASSET}.sha256"
  : > "$PKG"
  # HEAD reports the final release asset size without downloading it.
  # file:// test fixtures also report Content-Length through curl.
  HEADERS="$(curl -fsSLI --retry 1 --connect-timeout 8 --max-time 30 "$URL" 2>/dev/null || true)"
  DOWNLOAD_TOTAL="$(printf '%s\n' "$HEADERS" | tr -d '\r' |
    awk 'tolower($1)=="content-length:" { value=$2 } END { print value+0 }')"
  write_progress download 3 0 "$DOWNLOAD_TOTAL"
  # Run curl in the background so that real downloaded bytes can be reported
  # repeatedly while the HTTP transfer is still active.
  curl -fsSL --retry 2 --connect-timeout 8 --max-time 900 "$URL" -o "$PKG" &
  download_pid=$!
  while kill -0 "$download_pid" 2>/dev/null; do
    bytes="$(stat -c %s "$PKG" 2>/dev/null || printf '0')"
    pct=3
    if [ "$DOWNLOAD_TOTAL" -gt 0 ]; then
      pct=$((3 + bytes * 69 / DOWNLOAD_TOTAL))
      [ "$pct" -le 72 ] || pct=72
    fi
    write_progress download "$pct" "$bytes" "$DOWNLOAD_TOTAL"
    sleep 0.25
  done
  wait "$download_pid" || fail_install download 7
  bytes="$(stat -c %s "$PKG" 2>/dev/null || printf '0')"
  write_progress checksum 74 "$bytes" "$DOWNLOAD_TOTAL"
  curl -fsSL --retry 2 --connect-timeout 8 --max-time 30 "${URL}.sha256" -o "$SHA" || fail_install sha_download 8
  EXPECTED="$(awk 'NR==1 {print $1}' "$SHA")"
  write_progress verify 78
  ACTUAL="$(sha256sum "$PKG" | awk '{print $1}')"
  [ -n "$EXPECTED" ] && [ "$EXPECTED" = "$ACTUAL" ] || fail_install sha_mismatch 9
  write_progress validate 82
  # Reject path traversal, special files and user-owned data BEFORE untarring.
  ENTRY_TOTAL="$(python3 - "$PKG" <<'PY'
import pathlib, sys, tarfile
count = 0
with tarfile.open(sys.argv[1], 'r:gz') as archive:
    for member in archive:
        name = member.name.rstrip('/')
        parts = name.split('/')
        if (not name or name.startswith('/') or '\\' in name or
                any(part in ('', '.', '..') for part in parts) or
                name in ('config/launcher.json', 'game/gamelist.json',
                         'runtime/_compat/patches.json') or
                any(name.startswith(path) for path in
                    ('cache/', 'logs/', 'config/catalogs/', 'game/',
                     'runtime/_compat/patches_before'))):
            raise SystemExit('Unsafe OTA path: ' + member.name)
        if member.issym():
            link = pathlib.PurePosixPath(member.linkname)
            if link.is_absolute() or '..' in link.parts:
                raise SystemExit('Unsafe OTA symlink: ' + name)
        elif not (member.isfile() or member.isdir()):
            raise SystemExit('Unsafe OTA entry type: ' + name)
        if member.isfile() or member.issym():
            count += 1
print(count)
PY
)" || fail_install unsafe_payload 12
  [ "$ENTRY_TOTAL" -gt 0 ] || fail_install empty_payload 12
  STAGE="$(mktemp -d "$CACHE/stage.XXXXXX")" || fail_install stage 10
  cleanup() { rm -rf "$STAGE"; }
  trap cleanup EXIT
  write_progress extract 84
  tar -xzf "$PKG" -C "$STAGE" &
  extract_pid=$!
  while kill -0 "$extract_pid" 2>/dev/null; do
    count="$(find "$STAGE" \( -type f -o -type l \) -print | wc -l)"
    pct=$((84 + count * 9 / ENTRY_TOTAL))
    [ "$pct" -le 93 ] || pct=93
    write_progress extract "$pct"
    sleep 0.25
  done
  wait "$extract_pid" || fail_install extract 11
  # A temp file and rename keep each replaced executable complete, even if
  # power or Steam shutdown interrupts a transfer during installation.
  write_progress install 94
  applied=0
  while IFS= read -r -d '' source; do
    relative="${source#"$STAGE"/}"
    target="$ROOT/$relative"
    mkdir -p "$(dirname "$target")" || fail_install apply 13
    temp="${target}.rpgmp-new.$$"
    cp -a -- "$source" "$temp" || fail_install apply 13
    mv -fT -- "$temp" "$target" || fail_install apply 13
    applied=$((applied + 1))
    pct=$((94 + applied * 5 / ENTRY_TOTAL))
    [ "$pct" -le 99 ] || pct=99
    write_progress install "$pct"
  done < <(find "$STAGE" \( -type f -o -type l \) -print0)
  [ "$applied" -eq "$ENTRY_TOTAL" ] || fail_install apply_incomplete 13
  chmod +x "$ROOT/MKXP_Launcher" "$ROOT/run.sh" 2>/dev/null || true
  write_progress done 100
  write_out "STATUS=installed" "LATEST=$LATEST"
  exit 0
fi

write_out "STATUS=error" "ERROR=bad_mode"
exit 11
