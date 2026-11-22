#!/usr/bin/env bash
set -euo pipefail
ROOT=/mnt/d/GPT/MKXP
STAGE="$ROOT/work/v17_runtime_stage"
ARCHIVE="$ROOT/work/nwjs-v0.120.0-linux-x64.tar.gz"
DEST="$STAGE/runtime/nwjs/v0.120.0"
URL=https://dl.nwjs.io/v0.120.0/nwjs-v0.120.0-linux-x64.tar.gz
mkdir -p "$DEST"
if [[ ! -f "$ARCHIVE" || "$(stat -c %s "$ARCHIVE")" != 199473586 ]]; then
  curl --fail --location --retry 5 --retry-delay 3 --continue-at - \
       --output "$ARCHIVE" "$URL"
fi
test "$(stat -c %s "$ARCHIVE")" = 199473586
tar -tzf "$ARCHIVE" >/dev/null
tar -xzf "$ARCHIVE" -C "$DEST" --strip-components=1
chmod +x "$DEST/nw" "$DEST/chrome_crashpad_handler" 2>/dev/null || true
test -x "$DEST/nw"
sha256sum "$ARCHIVE" > "$ROOT/work/nwjs-v0.120.0.sha256.txt"
echo NWJS_0120_EXTRACTED_FILES="$(find "$DEST" -type f | wc -l)"
echo NWJS_0120_EXTRACTED_BYTES="$(du -sb "$DEST" | cut -f1)"
echo NWJS_0120_BINARY_FILE="$(file -b "$DEST/nw")"
echo NWJS_0120_PREPARE_PASS
