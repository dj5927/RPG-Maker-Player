#!/bin/bash
set -euo pipefail

DEV_ROOT="$(cd -- "$(dirname -- "$0")" && pwd)"
APP_ROOT="$(cd -- "$DEV_ROOT/.." && pwd)"
DIST_ROOT="$APP_ROOT/DIST"
VERSION="V005"
ARCHIVE="$DIST_ROOT/MKXP_SteamOS_Launcher_${VERSION}.tar.gz"

mkdir -p "$DIST_ROOT"
if [ -e "$ARCHIVE" ]; then
  echo "$ARCHIVE already exists; move it aside before rebuilding." >&2
  exit 2
fi

required=(
  "$APP_ROOT/MKXP_Launcher"
  "$APP_ROOT/run.sh"
  "$APP_ROOT/runtime/ruby_probe.rb"
  "$APP_ROOT/runtime/ruby18/mkxp-z"
  "$APP_ROOT/runtime/ruby18/libruby.so.1.8"
  "$APP_ROOT/runtime/ruby18/ruby/bin/ruby"
  "$APP_ROOT/runtime/ruby19/mkxp-z"
  "$APP_ROOT/runtime/ruby19/libruby.so.1.9"
  "$APP_ROOT/runtime/ruby19/ruby/bin/ruby"
  "$APP_ROOT/runtime/ruby31/mkxp-z"
  "$APP_ROOT/runtime/ruby31/ruby/bin/ruby"
  "$APP_ROOT/lib/libphysfs.so.1"
)
for path in "${required[@]}"; do
  if [ ! -f "$path" ]; then
    echo "Missing required runtime file: $path" >&2
    exit 3
  fi
done

chmod 0755 \
  "$APP_ROOT/run.sh" \
  "$APP_ROOT/MKXP_Launcher" \
  "$APP_ROOT/runtime/ruby18/mkxp-z" \
  "$APP_ROOT/runtime/ruby18/ruby/bin/ruby" \
  "$APP_ROOT/runtime/ruby19/mkxp-z" \
  "$APP_ROOT/runtime/ruby19/ruby/bin/ruby" \
  "$APP_ROOT/runtime/ruby31/mkxp-z" \
  "$APP_ROOT/runtime/ruby31/ruby/bin/ruby"

# Package only end-user files. Real game directories and all _dev/DIST data
# are intentionally excluded. The transform creates one clean top-level dir.
tar -C "$APP_ROOT" \
  --exclude='runtime/*/mkxp-z.x86_64' \
  --exclude='runtime/*/lib/*.a' \
  --exclude='runtime/*/.keep' \
  --transform="s,^,MKXP_SteamOS_Launcher_${VERSION}/," \
  -czf "$ARCHIVE" \
  MKXP_Launcher run.sh README_KR.md assets game/gamelist.json game/_image lib runtime

echo "Package: $ARCHIVE"
sha256sum "$ARCHIVE"
echo "Permissions:"
tar -tvzf "$ARCHIVE" | grep -E 'run.sh$|MKXP_Launcher$|runtime/ruby(18|19|31)/mkxp-z$|runtime/ruby(18|19|31)/ruby/bin/ruby$'
