#!/usr/bin/env bash
set -euo pipefail
ROOT="${1:-/mnt/d/GPT/MKXP}"
if [ -z "${MKXP_V082_POWER_IN_XVFB:-}" ] && command -v xvfb-run >/dev/null 2>&1; then
  exec xvfb-run -a env MKXP_V082_POWER_IN_XVFB=1 bash "$0" "$ROOT"
fi
TMP="$(mktemp -d /tmp/mkxp_power_compare.XXXXXX)"
mkdir -p "$TMP/v072/config" "$TMP/v072/games" "$TMP/v082/config" "$TMP/v082/games"
tar -xOf "$ROOT/DIST/MKXP_V072_START_SELECT_SINGLE_WINDOW_OVERLAY.tar.gz" MKXP_Launcher > "$TMP/v072/MKXP_Launcher"
cp "$ROOT/_dev/build/MKXP_Launcher" "$TMP/v082/MKXP_Launcher"
chmod +x "$TMP/v072/MKXP_Launcher" "$TMP/v082/MKXP_Launcher"
printf '{"gameRoot":"%s"}\n' "$TMP/v072/games" > "$TMP/v072/config/launcher.json"
printf '{"gameRoot":"%s"}\n' "$TMP/v082/games" > "$TMP/v082/config/launcher.json"
bench() {
  local dir="$1" label="$2" pid u1 s1 u2 s2 ticks
  env MKXP_LAUNCHER_WINDOWED=1 LANG=en_US.UTF-8 "$dir/MKXP_Launcher" >/dev/null 2>/dev/null & pid=$!
  sleep 1
  read -r u1 s1 < <(awk '{print $14, $15}' "/proc/$pid/stat")
  sleep 3
  read -r u2 s2 < <(awk '{print $14, $15}' "/proc/$pid/stat")
  ticks=$(( (u2+s2)-(u1+s1) ))
  echo "$label CPU_TICKS_3S=$ticks"
  kill -KILL "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
}
bench "$TMP/v072" V072
bench "$TMP/v082" V082
