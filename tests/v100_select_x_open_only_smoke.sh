#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/_dev/src/main.cpp"

grep -Fq 'GAME_KEYBOARD_COMBO_HOLD_MS = 50' "$SRC"
grep -Fq 'combo=Select+X' "$SRC"
grep -Fq 'action=open' "$SRC"
grep -Fq 'GAME_EXIT_COMBO_HOLD_MS = 1500' "$SRC"
! grep -Fq 'requestSteamOnScreenKeyboardClose' "$SRC"
! grep -Fq 'steamKeyboardOpenByShortcut' "$SRC"
! grep -Fq 'steam://close/keyboard' "$SRC"
! grep -Fq 'action=close' "$SRC"
! grep -Fq 'cleanup action=close' "$SRC"
! grep -Fq 'Start+Select-tap' "$SRC"

echo 'V100 Select+X open-only / Start+Select exit-only PASS'
