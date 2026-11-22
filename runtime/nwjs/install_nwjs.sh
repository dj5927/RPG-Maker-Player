#!/bin/bash
set -eu

ROOT="$(cd -- "$(dirname -- "$0")/../.." && pwd)"
VERSION="${1:-v0.120.0}"
case "$VERSION" in
  v*) ;;
  *) VERSION="v$VERSION" ;;
esac

case "$(uname -m)" in
  x86_64) ARCH=x64 ;;
  i386|i686) ARCH=ia32 ;;
  *)
    echo "Unsupported NW.js architecture for this SteamOS build: $(uname -m)" >&2
    exit 2
    ;;
esac

DEST="$ROOT/runtime/nwjs/$VERSION"
if [ -x "$DEST/nw" ]; then
  echo "NW.js already installed: $VERSION"
  exit 0
fi

DOWNLOADS="$ROOT/runtime/nwjs/_downloads"
mkdir -p "$DEST" "$DOWNLOADS"
FILE="nwjs-$VERSION-linux-$ARCH.tar.gz"
ARCHIVE="$DOWNLOADS/$FILE"
URL="https://dl.nwjs.io/$VERSION/$FILE"

if [ ! -f "$ARCHIVE" ]; then
  echo "Downloading $URL"
  if command -v curl >/dev/null 2>&1; then
    curl -L --fail --retry 2 -o "$ARCHIVE" "$URL"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "$ARCHIVE" "$URL"
  else
    echo "curl or wget is required to install NW.js" >&2
    exit 3
  fi
fi

tar -xzf "$ARCHIVE" -C "$DEST" --strip-components=1
chmod +x "$DEST/nw" 2>/dev/null || true
echo "Installed NW.js $VERSION -> $DEST"

