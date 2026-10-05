#!/usr/bin/env bash
set -euo pipefail

ROOT="${1:-/mnt/d/GPT/MKXP}"
WRAP="$ROOT/preload/common/win32_wrap.rb"
TMP="$(mktemp -d /tmp/mkxp_v088_win32.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

# Static checks: Windows path separators and .dll suffixes are normalized
# before Ruby constant lookup, and malformed names are guarded.
grep -Fq "s = s.tr('\\\\', '/')" "$WRAP"
grep -Fq "s = s.sub(/\\.dll\\z/i, '') if strip_dll" "$WRAP"
grep -Fq 'return nil unless s =~ /\A[A-Z][A-Za-z0-9_]*\z/' "$WRAP"
grep -Fq 'rescue NameError' "$WRAP"

make_game() {
  local game="$1"
  local library="$2"
  local scripts="$3"
  mkdir -p "$game/Data"
  cat > "$game/Game.ini" <<EOF
[Game]
RTP1=
RTP2=
RTP3=
Library=$library
Scripts=Data\\$scripts
Title=MKXP V088 Win32 DLL Path Smoke
EOF

  GAME_DIR="$game" SCRIPT_FILE="$scripts" ruby <<'RUBY'
require 'zlib'
dir = ENV.fetch('GAME_DIR')
source = <<~'CODE'
  # Exact shape used by WF-RGSS Exit-EX style plugins.  On Linux these DLL
  # paths are unsupported, but constructing the Win32API objects must not
  # throw merely because the DLL contains a directory separator.
  begin
    hook  = Win32API.new('System/WFExit', 'hookExit', 'v', 'l')
    exitf = Win32API.new('System\\WFExit.dll', 'getToExit', 'v', 'l')
    quitf = Win32API.new('System/WFExit.dll', 'Quit', 'v', 'v')
    raise 'hook fallback' unless hook.call == 0
    raise 'exit fallback' unless exitf.call == 0
    quitf.call
  rescue Exception => e
    File.open('v088.fail', 'wb') { |f| f.write(e.class.to_s + ': ' + e.to_s) }
    raise
  end

  # Built-in shim lookup must still work after path normalization changes.
  metrics = Win32API.new('user32.dll', 'GetSystemMetrics', 'i', 'i')
  raise 'user32 shim lost' unless metrics.call(0).to_i > 0
  File.open('v088.ok', 'wb') { |f| f.write(RUBY_VERSION) }
  exit
CODE
payload = [[1, 'Main', Zlib::Deflate.deflate(source)]]
File.open(File.join(dir, 'Data', ENV.fetch('SCRIPT_FILE')), 'wb') { |f| Marshal.dump(payload, f) }
RUBY
}

make_game "$TMP/game18" RGSS102E.dll Scripts.rxdata
make_game "$TMP/game19" RGSS202E.dll Scripts.rvdata
make_game "$TMP/game31" RGSS301.dll Scripts.rvdata2

run_runtime() {
  local runtime_name="$1"
  local game="$2"
  local runtime="$ROOT/runtime/$runtime_name"
  local log="$TMP/$runtime_name.log"
  rm -f "$game/v088.ok" "$game/v088.fail"

  case "$runtime_name" in
    ruby18)
      (
        cd "$game"
        xvfb-run -a env \
          LD_LIBRARY_PATH="$runtime:$runtime/ruby/lib:$runtime/ruby/openssl/lib:$ROOT/lib" \
          MKXP_PORTABLE_RUBY_LOADPATH="$runtime/ruby/lib/ruby/1.8:$runtime/ruby/lib/ruby/1.8/x86_64-linux:$runtime/ruby/lib/ruby/site_ruby/1.8:$runtime/ruby/lib/ruby/site_ruby/1.8/x86_64-linux" \
          MKXP_PORTABLE_PRELOAD_DIR="$ROOT/preload/common" \
          "$runtime/mkxp-z" >"$log" 2>&1
      )
      ;;
    ruby19)
      (
        cd "$game"
        xvfb-run -a env \
          SDL_VIDEODRIVER=x11 \
          RUBY_HEAP_MIN_SLOTS=1000000 RUBY_GC_MALLOC_LIMIT=1000000000 RUBY_FREE_MIN=100000 \
          LD_PRELOAD="$runtime/libpthread_retry_einval.so" \
          LD_LIBRARY_PATH="$runtime:$runtime/nothreaded_ruby/lib:$runtime/nothreaded_ruby/openssl/lib:$runtime/ruby/lib:$ROOT/lib" \
          MKXP_PORTABLE_RUBY_LOADPATH="$runtime/nothreaded_ruby/lib/ruby/1.9.1:$runtime/nothreaded_ruby/lib/ruby/1.9.1/x86_64-linux:$runtime/nothreaded_ruby/lib/ruby/site_ruby/1.9.1:$runtime/nothreaded_ruby/lib/ruby/site_ruby/1.9.1/x86_64-linux" \
          MKXP_PORTABLE_PRELOAD_DIR="$ROOT/preload/common" \
          "$runtime/mkxp-z" >"$log" 2>&1
      )
      ;;
    ruby31)
      (
        cd "$game"
        xvfb-run -a env \
          LD_LIBRARY_PATH="$runtime:$runtime/ruby/lib:$runtime/ruby/openssl/lib:$ROOT/lib" \
          MKXP_PORTABLE_RUBY_LOADPATH="$runtime/stdlib:$runtime/stdlib/x86_64-linux" \
          MKXP_PORTABLE_PRELOAD_DIR="$ROOT/preload/common" \
          "$runtime/mkxp-z" >"$log" 2>&1
      )
      ;;
  esac

  test -f "$game/v088.ok"
  test ! -f "$game/v088.fail"
  grep -Fq '[Win32API] [System/WFExit:hookExit]' "$log"
  grep -Fq '[Win32API] [System\WFExit.dll:getToExit]' "$log"
}

run_runtime ruby18 "$TMP/game18"
run_runtime ruby19 "$TMP/game19"
run_runtime ruby31 "$TMP/game31"

echo 'V088 Win32 DLL path normalization + tolerant custom DLL fallback PASS on Ruby 1.8/1.9/3.1'
