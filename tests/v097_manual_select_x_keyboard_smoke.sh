#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
LAUNCHER="$ROOT/_dev/src/main.cpp"
EVENT="$ROOT/_dev/mkxp-z-upstream/src/eventthread.cpp"
BINDING="$ROOT/_dev/mkxp-z-upstream/binding/binding-mri.cpp"

# Start+Select is exit-only again.
! grep -Fq 'Start+Select-tap' "$LAUNCHER"
! grep -Fq 'GAME_KEYBOARD_START_SELECT_TAP_MIN_MS' "$LAUNCHER"
grep -Fq 'GAME_EXIT_COMBO_HOLD_MS = 1500' "$LAUNCHER"

# Manual SteamOS keyboard is Select+X only, with a normal short chord.
grep -Fq 'GAME_KEYBOARD_COMBO_HOLD_MS = 50' "$LAUNCHER"
grep -Fq 'combo=Select+X' "$LAUNCHER"
grep -Fq 'keyboardComboPressed = selectPressed && xPressed && !startPressed' "$LAUNCHER"
grep -Fq 'buttons.keyboardCombo = select && (north || west || keyX) && !start' "$LAUNCHER"

# Automatic game-side Steam keyboard popups are retired.
! grep -Fq 'requestSteamOnScreenKeyboard' "$EVENT"
! grep -Fq 'SteamOS keyboard requested for game text input' "$EVENT"
! grep -Fq 'steam_text_entry_compat.rb' "$BINDING"

# SDL text mode itself remains intact so games that use it still receive text.
grep -Fq 'SDL_StartTextInput();' "$EVENT"
grep -Fq 'SDL_StopTextInput();' "$EVENT"

echo "V097 manual Select+X keyboard / Start+Select exit-only / no auto-popup PASS"
