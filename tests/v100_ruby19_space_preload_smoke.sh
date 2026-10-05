#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/_dev/src/main.cpp"
TMP="$(mktemp -d /tmp/mkxp_v100_space.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

grep -Fq 'mkxp-ruby19-' "$SRC"
grep -Fq 'fs::create_symlink(pthreadCompat, preloadAlias' "$SRC"
grep -Fq 'fs::copy_file(pthreadCompat, preloadAlias' "$SRC"
grep -Fq 'find_first_of(" \t\r\n")' "$SRC"

mkdir -p "$TMP/root with spaces/runtime/ruby19" "$TMP/alias"
cp "$ROOT/runtime/ruby19/libpthread_retry_einval.so" \
  "$TMP/root with spaces/runtime/ruby19/libpthread_retry_einval.so"
ln -s "$TMP/root with spaces/runtime/ruby19/libpthread_retry_einval.so" \
  "$TMP/alias/libpthread_retry_einval.so"

LD_PRELOAD="$TMP/alias/libpthread_retry_einval.so" /bin/true 2>"$TMP/preload.log"
! grep -Fq 'cannot be preloaded' "$TMP/preload.log"

echo 'V100 Ruby19 whitespace-safe LD_PRELOAD alias PASS'
