#!/bin/bash
set -u

ROOT="$(cd -- "$(dirname -- "$0")" && pwd)"
LOG_DIR="$ROOT/logs"
LOG="$LOG_DIR/run.log"
LAUNCHER="$ROOT/MKXP_Launcher"

show_error() {
  local message="$1"
  if command -v kdialog >/dev/null 2>&1; then
    kdialog --title "MKXP Player" --error "$message" >/dev/null 2>&1 || true
  elif command -v zenity >/dev/null 2>&1; then
    zenity --error --title="MKXP Player" --text="$message" >/dev/null 2>&1 || true
  fi
}

mkdir -p "$LOG_DIR" 2>/dev/null || true

exec >>"$LOG" 2>&1

echo "============================================================"
echo "MKXP run.sh start: $(date '+%Y-%m-%d %H:%M:%S %z' 2>/dev/null || date)"
echo "root=$ROOT"
echo "user=$(id -un 2>/dev/null || true) uid=$(id -u 2>/dev/null || true)"
echo "kernel=$(uname -a 2>/dev/null || true)"
echo "desktop=${XDG_CURRENT_DESKTOP:-unknown} session=${XDG_SESSION_TYPE:-unknown}"

if [ ! -f "$LAUNCHER" ]; then
  echo "FATAL: MKXP_Launcher not found: $LAUNCHER"
  show_error "MKXP_Launcher 파일을 찾을 수 없습니다.\n$LAUNCHER"
  exit 20
fi

# Files copied from Windows/FAT/NTFS may lose Unix executable mode.
chmod +x "$LAUNCHER" 2>/dev/null || true
for exe in \
  "$ROOT/runtime/ruby18/mkxp-z" \
  "$ROOT/runtime/ruby19/mkxp-z" \
  "$ROOT/runtime/ruby31/mkxp-z" \
  "$ROOT/runtime/ruby18/ruby/bin/ruby" \
  "$ROOT/runtime/ruby19/ruby/bin/ruby" \
  "$ROOT/runtime/ruby19/safe_ruby/bin/ruby" \
  "$ROOT/runtime/ruby19/nothreaded_ruby/bin/ruby" \
  "$ROOT/runtime/ruby31/ruby/bin/ruby" \
  "$ROOT/runtime/easyrpg/easyrpg-player" \
  "$ROOT/runtime/mvmz/launch_mvmz.sh" \
  "$ROOT/runtime/mvmz/lib/cicpoffs" \
  "$ROOT/runtime/nwjs/install_nwjs.sh"; do
  if [ -f "$exe" ]; then chmod +x "$exe" 2>/dev/null || true; fi
done

for nw in "$ROOT"/runtime/nwjs/*/nw; do
  if [ -f "$nw" ]; then chmod +x "$nw" 2>/dev/null || true; fi
done

for helper in \
  "$ROOT"/runtime/nwjs/*/chrome_crashpad_handler \
  "$ROOT"/runtime/nwjs/*/nacl_helper \
  "$ROOT"/runtime/nwjs/*/nacl_helper_bootstrap; do
  if [ -f "$helper" ]; then chmod +x "$helper" 2>/dev/null || true; fi
done

if [ ! -x "$LAUNCHER" ]; then
  echo "FATAL: MKXP_Launcher is not executable"
  ls -l "$LAUNCHER" 2>/dev/null || true
  show_error "MKXP_Launcher 실행 권한이 없습니다.\n자세한 내용: logs/run.log"
  exit 21
fi

if [ -d "$ROOT/lib" ]; then
  export LD_LIBRARY_PATH="$ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

if [ "${MKXP_DIAG:-0}" = "1" ] && command -v ldd >/dev/null 2>&1; then
  echo "--- launcher dependencies ---"
  ldd "$LAUNCHER" 2>&1 || true
  MISSING="$(ldd "$LAUNCHER" 2>/dev/null | grep 'not found' || true)"
  if [ -n "$MISSING" ]; then
    echo "FATAL: missing shared libraries"
    echo "$MISSING"
    show_error "필요한 Linux 라이브러리를 찾을 수 없습니다.\n자세한 내용: logs/run.log"
    exit 22
  fi
fi

echo "--- launching MKXP_Launcher ---"
LAUNCHER_PID=""
cleanup_launcher_tree() {
  if [ -n "$LAUNCHER_PID" ] && kill -0 "$LAUNCHER_PID" 2>/dev/null; then
    kill -TERM -- "-$LAUNCHER_PID" 2>/dev/null || kill -TERM "$LAUNCHER_PID" 2>/dev/null || true
    sleep 0.2
    kill -KILL -- "-$LAUNCHER_PID" 2>/dev/null || true
  fi
}
trap 'cleanup_launcher_tree; exit 143' TERM INT HUP

if command -v setsid >/dev/null 2>&1; then
  setsid "$LAUNCHER" &
else
  "$LAUNCHER" &
fi
LAUNCHER_PID=$!
wait "$LAUNCHER_PID"
RC=$?
LAUNCHER_PID=""
trap - TERM INT HUP
echo "MKXP_Launcher exit=$RC"
if [ "$RC" -ne 0 ]; then
  show_error "MKXP Launcher가 오류 코드 $RC 로 종료되었습니다.\n자세한 내용: logs/run.log"
fi
exit "$RC"
