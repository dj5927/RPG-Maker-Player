#!/usr/bin/env bash
set -euo pipefail
ROOT="${1:-/mnt/d/GPT/MKXP}"
SRC="$ROOT/_dev/src/main.cpp"
JS="$ROOT/runtime/mvmz/jspatches/start-select-exit.js"
RGSS="$ROOT/_dev/mkxp-z-upstream/src/eventthread.cpp"

# Static cleanup/contract checks.
grep -Fq 'constexpr Uint64 GAME_EXIT_COMBO_HOLD_MS = 1500;' "$SRC"
grep -Fq 'var HOLD_MS = 1500;' "$JS"
grep -Fq 'SDL_AddTimer(1500, pushExitComboTimerEvent' "$RGSS"
! grep -Fq 'GAMESCOPECTRL_BASELAYER_WINDOW' "$SRC"
! grep -Fq 'GAMESCOPE_FOCUSED_WINDOW' "$SRC"
! grep -Fq 'RPG Maker Player - Exit' "$SRC"
! grep -Fq 'ExitConfirmOverlay' "$SRC"
! grep -Fq 'SIGSTOP' "$SRC"
! grep -Fq 'SDL_syswm' "$SRC"
! grep -Fq 'X11/Xlib.h' "$SRC"

TMP="$(mktemp -d /tmp/mkxp_v082_hold.XXXXXX)"
cp -a "$ROOT/_dev/tests/mvmz/mv" "$TMP/MV"
printf '%s\n' 'v0.29.0' > "$TMP/MV/mkxp-nwjs.txt"
READY="$TMP/ready.flag"
FLAG="$TMP/exit.flag"
: > "$FLAG"

cat > "$TMP/MV/www/index.html" <<'EOF'
<!doctype html><html><body>V082 LONG HOLD TEST<script>
try {
  var fs = require('fs');
  if (process.env.MKXP_V082_READY_FILE) fs.writeFileSync(process.env.MKXP_V082_READY_FILE, '1\n');
  Object.defineProperty(navigator, 'getGamepads', {
    configurable: true,
    value: function() {
      var a = [];
      for (var i = 0; i < 10; i++) a.push({pressed:false,value:0});
      a[8] = {pressed:true,value:1};
      a[9] = {pressed:true,value:1};
      return [{buttons:a,mapping:'standard',index:0,id:'V082 Hold Test'}];
    }
  });
} catch(e) {}
setTimeout(function(){ try { nw.App.quit(); } catch(e){} }, 3500);
</script></body></html>
EOF

xvfb-run -a env MKXP_EXIT_REQUEST_FILE="$FLAG" MKXP_V082_READY_FILE="$READY" \
  "$ROOT/runtime/mvmz/launch_mvmz.sh" \
  "$ROOT" "$TMP/MV" "$TMP/MV/www" MV v0.29.0 \
  >"$TMP/run.log" 2>&1 &
RUNPID=$!
cleanup(){ kill -KILL "$RUNPID" 2>/dev/null || true; }
trap cleanup EXIT

for _ in $(seq 1 120); do [ -s "$READY" ] && break; sleep 0.05; done
test -s "$READY"
# It must definitely NOT behave like the old 300 ms path.
sleep 0.90
if [ -s "$FLAG" ]; then
  echo 'V082 exit request arrived before 0.9s; long-hold guard failed' >&2
  exit 1
fi
# After the 1.5 s hold threshold it must fire.
for _ in $(seq 1 40); do [ -s "$FLAG" ] && break; sleep 0.05; done
test -s "$FLAG"
wait "$RUNPID" 2>/dev/null || true
trap - EXIT

grep -Fq '[MKXP] Start+Select 1.5s direct-exit hook loaded' "$TMP/run.log"
grep -Fq '[MKXP] Start+Select 1.5s direct exit requested' "$TMP/run.log"
for bin in "$ROOT/runtime/ruby18/mkxp-z" "$ROOT/runtime/ruby19/mkxp-z" "$ROOT/runtime/ruby31/mkxp-z"; do
  strings "$bin" | grep -F 'Start+Select 1.5s direct exit requested' >/dev/null
done

echo 'V082 1.5s Start+Select direct-exit hold + no-popup cleanup PASS'
