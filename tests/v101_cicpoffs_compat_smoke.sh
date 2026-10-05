#!/usr/bin/env bash
set -euo pipefail

ROOT="${1:-/mnt/d/GPT/MKXP}"
TMP="$(mktemp -d /tmp/mkxp_v101_cicpoffs.XXXXXX)"
trap 'rc=$?; if [ $rc -ne 0 ]; then echo "V101 smoke failed rc=$rc"; for f in "$TMP"/*.log; do [ -f "$f" ] && { echo "--- $f ---"; tail -80 "$f"; }; done; fi; rm -rf "$TMP"' EXIT

grep -Fq '"cicpoffs_compat.rb"' "$ROOT/_dev/mkxp-z-upstream/binding/binding-mri.cpp"
grep -Fq 'Preload.apply_cicpoffs_compat' "$ROOT/preload/common/cicpoffs_compat.rb"
grep -Fq 'GetPrivateProfileInt' "$ROOT/preload/common/win32_wrap.rb"
grep -Fq 'WideCharToMultiByte' "$ROOT/preload/common/win32_wrap.rb"
grep -Fq 'MultiByteToWideChar' "$ROOT/preload/common/win32_wrap.rb"
grep -Fq 'ToUnicodeEx' "$ROOT/preload/common/win32_wrap.rb"
grep -Fq 'XInputGetState' "$ROOT/preload/common/win32_wrap.rb"
grep -Fq 'SteamAPI_Init' "$ROOT/preload/common/win32_wrap.rb"

for rb in "$ROOT/runtime/ruby18/ruby/bin/ruby" "$ROOT/runtime/ruby19/nothreaded_ruby/bin/ruby" "$ROOT/runtime/ruby31/ruby/bin/ruby"; do
  if [ -x "$rb" ]; then
    "$rb" -c "$ROOT/preload/common/cicpoffs_compat.rb" >/dev/null
    "$rb" -c "$ROOT/preload/common/win32_wrap.rb" >/dev/null
    "$rb" -c "$ROOT/preload/cicpoffs/patches.rb" >/dev/null
  fi
done

make_game() {
  local game="$1" library="$2" scripts="$3"
  mkdir -p "$game/Data"
  cat >"$game/Game.ini" <<EOF
[Game]
Library=$library
Scripts=Data\\$scripts
Title=V101 cicpoffs smoke
EOF
  GAME_DIR="$game" SCRIPT_FILE="$scripts" ruby <<'RUBY'
require 'zlib'
dir = ENV.fetch('GAME_DIR')
probe = <<'CODE'
$imported ||= {}
$imported[:mog_anti_lag] = true
# self.visible = @character.can_update
CODE
main = <<'CODE'
begin
  patched = false
  $RGSS_SCRIPTS.each { |s| patched = true if s[3].to_s.include?('self.visible = !!@character.can_update') }
  raise 'cicpoffs inline patch missing' unless patched
  raise 'GetPrivateProfileInt missing' unless Win32API.new('kernel32', 'GetPrivateProfileInt', 'ppip', 'i').call('A','B',7,'missing.ini') == 7
  b = "xxxx"
  Win32API.new('kernel32', 'RtlZeroMemory', 'pl', 'v').call(b, 4)
  raise 'RtlZeroMemory missing' unless b == "\0\0\0\0"
  raise 'XInput shim missing' unless Win32API.new('xinput1_3', 'XInputGetState', 'ip', 'i').call(0, "\0" * 16) == 0
  raise 'SteamAPI shim missing' unless Win32API.new('steam_api', 'SteamAPI_Init', '', 'i').call == 1
  File.open('v101.ok', 'wb') { |f| f.write(RUBY_VERSION) }
rescue Exception => e
  File.open('v101.fail', 'wb') { |f| f.write(e.class.to_s + ': ' + e.to_s) }
  raise
end
exit
CODE
payload = [[1, 'CompatProbe', Zlib::Deflate.deflate(probe)], [2, 'Main', Zlib::Deflate.deflate(main)]]
File.open(File.join(dir, 'Data', ENV.fetch('SCRIPT_FILE')), 'wb') { |f| Marshal.dump(payload, f) }
RUBY
}

make_game "$TMP/game18" RGSS102E.dll Scripts.rxdata
make_game "$TMP/game19" RGSS202E.dll Scripts.rvdata
make_game "$TMP/game31" RGSS301.dll Scripts.rvdata2

r18="$ROOT/runtime/ruby18"
( cd "$TMP/game18"; xvfb-run -a env LD_LIBRARY_PATH="$r18:$r18/ruby/lib:$r18/ruby/openssl/lib:$ROOT/lib" MKXP_PORTABLE_RUBY_LOADPATH="$r18/ruby/lib/ruby/1.8:$r18/ruby/lib/ruby/1.8/x86_64-linux:$r18/ruby/lib/ruby/site_ruby/1.8:$r18/ruby/lib/ruby/site_ruby/1.8/x86_64-linux" MKXP_PORTABLE_PRELOAD_DIR="$ROOT/preload/common" "$r18/mkxp-z" >"$TMP/r18.log" 2>&1 )

r19="$ROOT/runtime/ruby19"
( cd "$TMP/game19"; xvfb-run -a env SDL_VIDEODRIVER=x11 RUBY_HEAP_MIN_SLOTS=1000000 RUBY_GC_MALLOC_LIMIT=1000000000 RUBY_FREE_MIN=100000 LD_PRELOAD="$r19/libpthread_retry_einval.so" LD_LIBRARY_PATH="$r19:$r19/nothreaded_ruby/lib:$r19/nothreaded_ruby/openssl/lib:$r19/ruby/lib:$ROOT/lib" MKXP_PORTABLE_RUBY_LOADPATH="$r19/nothreaded_ruby/lib/ruby/1.9.1:$r19/nothreaded_ruby/lib/ruby/1.9.1/x86_64-linux:$r19/nothreaded_ruby/lib/ruby/site_ruby/1.9.1:$r19/nothreaded_ruby/lib/ruby/site_ruby/1.9.1/x86_64-linux" MKXP_PORTABLE_PRELOAD_DIR="$ROOT/preload/common" "$r19/mkxp-z" >"$TMP/r19.log" 2>&1 )

r31="$ROOT/runtime/ruby31"
( cd "$TMP/game31"; xvfb-run -a env LD_LIBRARY_PATH="$r31:$r31/ruby/lib:$r31/ruby/openssl/lib:$ROOT/lib" MKXP_PORTABLE_RUBY_LOADPATH="$r31/stdlib:$r31/stdlib/x86_64-linux" MKXP_PORTABLE_PRELOAD_DIR="$ROOT/preload/common" "$r31/mkxp-z" >"$TMP/r31.log" 2>&1 )

test -f "$TMP/game18/v101.ok"; test ! -f "$TMP/game18/v101.fail"
test -f "$TMP/game19/v101.ok"; test ! -f "$TMP/game19/v101.fail"
test -f "$TMP/game31/v101.ok"; test ! -f "$TMP/game31/v101.fail"
grep -Fq '[cicpoffs] compat active' "$TMP/r18.log"
grep -Fq '[cicpoffs] compat active' "$TMP/r19.log"
grep -Fq '[cicpoffs] compat active' "$TMP/r31.log"

echo 'V101 cicpoffs patch layer + Win32API stubs PASS on Ruby 1.8/1.9/3.1'
