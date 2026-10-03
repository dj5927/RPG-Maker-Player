#!/usr/bin/env bash
set -euo pipefail
ROOT="${1:-/mnt/d/GPT/MKXP}"
if [ -z "${MKXP_V073_IN_XVFB:-}" ] && command -v xvfb-run >/dev/null 2>&1; then
  exec xvfb-run -a env MKXP_V073_IN_XVFB=1 bash "$0" "$ROOT"
fi
SRC="$ROOT/_dev/src/main.cpp"
BIN="$ROOT/_dev/build/MKXP_Launcher"

grep -Fq 'GAME_EXIT_COMBO_HOLD_MS = 1500' "$SRC"
grep -Fq 'exit combo | direct exit ' "$SRC"
! grep -Fq 'drawInGameExitConfirm(' "$SRC"
! grep -Fq 'RPG Maker Player - Exit' "$SRC"

BASE="$(mktemp -d /tmp/mkxp_v073_exit.XXXXXX)"
GAMES="$(mktemp -d /tmp/mkxp_v073_exit_games.XXXXXX)"
GAME="$GAMES/Game2K"
mkdir -p "$BASE/config" "$BASE/runtime/easyrpg" "$GAME"
cp "$BIN" "$BASE/MKXP_Launcher"
chmod +x "$BASE/MKXP_Launcher"
cat > "$BASE/config/launcher.json" <<EOF
{"gameRoot":"$GAMES"}
EOF
: > "$GAME/RPG_RT.ldb"
: > "$GAME/RPG_RT.lmt"
cat > "$BASE/runtime/easyrpg/easyrpg-player" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$$" > "$MKXP_V073_CHILD_PID_FILE"
trap 'exit 0' TERM INT
while :; do sleep 1; done
EOF
chmod +x "$BASE/runtime/easyrpg/easyrpg-player"

env MKXP_LAUNCHER_WINDOWED=1 LANG=en_US.UTF-8 MKXP_V073_CHILD_PID_FILE="$BASE/child.pid" \
  "$BASE/MKXP_Launcher" >"$BASE/launcher.out" 2>"$BASE/launcher.err" &
PID=$!
trap 'kill -KILL "$PID" 2>/dev/null || true' EXIT
MAIN=""
for _ in $(seq 1 80); do
  MAIN="$(xdotool search --all --pid "$PID" --onlyvisible --name '^RPG Maker Player$' 2>/dev/null | head -1 || true)"
  [ -n "$MAIN" ] && break
  sleep 0.1
done
test -n "$MAIN"
xdotool windowfocus "$MAIN" 2>/dev/null || true
sleep 0.3
xdotool key --window "$MAIN" Return
for _ in $(seq 1 80); do [ -s "$BASE/child.pid" ] && break; sleep 0.1; done
test -s "$BASE/child.pid"
CHILD="$(cat "$BASE/child.pid")"
kill -0 "$CHILD"
MAPPED="$(xdotool search --all --pid "$PID" --onlyvisible --name '^RPG Maker Player$' 2>/dev/null | head -1 || true)"
[ "$MAPPED" = "$MAIN" ]

printf '1\n' > "$BASE/cache/game_exit_request.flag"
for _ in $(seq 1 120); do
  grep -Fq 'exit combo | direct exit request-file holdMs=1500 pid=' "$BASE/logs/MKXP_Launcher.log" 2>/dev/null && \
  grep -Fq 'frontend surface | child returned kind=game folder=Game2K' "$BASE/logs/MKXP_Launcher.log" 2>/dev/null && break
  sleep 0.05
done
grep -Fq 'exit combo | direct exit request-file holdMs=1500 pid=' "$BASE/logs/MKXP_Launcher.log"
grep -Fq 'frontend surface | child returned kind=game folder=Game2K' "$BASE/logs/MKXP_Launcher.log"
if kill -0 "$CHILD" 2>/dev/null; then
  echo 'child still alive after direct exit' >&2
  exit 1
fi
RETURNED="$(xdotool search --all --pid "$PID" --onlyvisible --name '^RPG Maker Player$' 2>/dev/null | head -1 || true)"
[ "$RETURNED" = "$MAIN" ]

kill -KILL "$PID" 2>/dev/null || true
wait "$PID" 2>/dev/null || true
trap - EXIT
echo 'V073 direct Start+Select request exit + same mapped frontend PASS'
