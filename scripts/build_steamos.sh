#!/usr/bin/env bash
set -euo pipefail

DEV_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_ROOT="$(cd "$DEV_ROOT/.." && pwd)"
BUILD="$DEV_ROOT/build"

cmake -S "$DEV_ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" -j"$(nproc)"
cp -f "$BUILD/MKXP_Launcher" "$APP_ROOT/MKXP_Launcher"
chmod +x "$APP_ROOT/MKXP_Launcher"

echo "Built: $APP_ROOT/MKXP_Launcher"
