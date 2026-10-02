#!/usr/bin/env bash
set -euo pipefail

RUBY_BUILD=/root/mkxp-oldruby/ruby-build/bin/ruby-build
BASE_DEF=/root/mkxp-oldruby/ruby-dev/1.9.3-p551
DEF_DIR=/root/mkxp-nothreaded-defs
DEF="$DEF_DIR/1.9.3-p551-nothreaded"
PREFIX=/root/mkxp-rubies/1.9.3-p551-nothreaded

mkdir -p "$DEF_DIR"
cp "$BASE_DEF" "$DEF"
sed -i 's/RUBY_CFLAGS="-O3 /RUBY_CFLAGS="-O0 /' "$DEF"

python3 - "$DEF" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1])
s = p.read_text()
needle = 'build_package_standard_serial() {\n  local package_name="$1"\n  {'
repl = '''build_package_standard_serial() {
  local package_name="$1"
  if [ "$package_name" = "ruby-1.9.3-p551" ]; then
    sed -i 's/#define OPT_DIRECT_THREADED_CODE     1/#define OPT_DIRECT_THREADED_CODE     0/' vm_opts.h
  fi
  {'''
if needle not in s:
    raise SystemExit('definition patch anchor not found')
p.write_text(s.replace(needle, repl, 1))
PY

RUBY_BUILD_DEFINITIONS="$DEF_DIR" "$RUBY_BUILD" -k 1.9.3-p551-nothreaded "$PREFIX"
"$PREFIX/bin/ruby" -v
