#!/usr/bin/env bash
set -euo pipefail

ROOT="${1:-/mnt/d/GPT/MKXP}"
SRC="$ROOT/_dev/src/main.cpp"

# Linux kernel canonical positional aliases:
#   BTN_NORTH == BTN_X
#   BTN_WEST  == BTN_Y
# The WOLF/Proton background-input path therefore must use NORTH for the
# physical X button, matching SDL_CONTROLLER_BUTTON_X.
grep -Fq 'SDL_CONTROLLER_BUTTON_X' "$SRC"
grep -Fq 'north = north || down(BTN_NORTH);' "$SRC"
grep -Fq 'buttons.keyboardCombo = select && (north || west || keyX) && !start;' "$SRC"
grep -Fq 'north = north || down(BTN_NORTH);' "$SRC"

if [ -r /usr/include/linux/input-event-codes.h ]; then
  grep -Eq '^#define[[:space:]]+BTN_X[[:space:]]+BTN_NORTH' /usr/include/linux/input-event-codes.h
  grep -Eq '^#define[[:space:]]+BTN_Y[[:space:]]+BTN_WEST' /usr/include/linux/input-event-codes.h
fi

# Existing behavior remains: Select+X is independent from Start+Select 1.5s.
grep -Fq 'constexpr Uint64 GAME_KEYBOARD_COMBO_HOLD_MS = 50;' "$SRC"
grep -Fq 'constexpr Uint64 GAME_EXIT_COMBO_HOLD_MS = 1500;' "$SRC"
grep -Fq 'game keyboard | requested source=parent-input combo=Select+X holdMs=' "$SRC"

echo 'V089 WOLF/Proton evdev physical-X mapping (BTN_NORTH) PASS'
