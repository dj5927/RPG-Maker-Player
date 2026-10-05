#!/usr/bin/env bash
set -euo pipefail

ROOT="${1:-/mnt/d/GPT/MKXP}"
SRC="$ROOT/_dev/src/main.cpp"

# WOLF/Proton keyboard bridge must live in the parent launcher process so it
# retains Steam's real non-Steam-game environment even though the Proton child
# deliberately uses SteamAppId/SteamGameId 0.
grep -Fq 'constexpr Uint64 GAME_KEYBOARD_COMBO_HOLD_MS = 50;' "$SRC"
grep -Fq 'bool allowSteamKeyboardShortcut = false' "$SRC"
grep -Fq 'SDL_CONTROLLER_BUTTON_X' "$SRC"
grep -Fq 'BTN_NORTH' "$SRC"
grep -Fq 'keyboardComboPressed = selectPressed && xPressed && !startPressed;' "$SRC"
grep -Fq 'buttons.keyboardCombo = select && (north || west || keyX) && !start;' "$SRC"
grep -Fq 'requestSteamOnScreenKeyboard();' "$SRC"
grep -Fq 'game keyboard | requested source=parent-input combo=Select+X holdMs=' "$SRC"

# V087 requires at least the WOLF and generic Proton compatibility launch
# paths to opt in. Later versions may extend the same explicit shortcut to
# native engines (V090 does this for RGSS), so do not require an exact count.
COUNT="$(grep -Fc 'waitForGameChild(pid, window, renderer, fonts, controller, exitRequestFile, true);' "$SRC")"
test "$COUNT" -ge "2"

# Start+Select direct exit remains independent and longer-held.
grep -Fq 'constexpr Uint64 GAME_EXIT_COMBO_HOLD_MS = 1500;' "$SRC"
grep -Fq 'comboPressed = startPressed && selectPressed;' "$SRC"

echo 'V087 WOLF/Proton Select+X SteamOS keyboard bridge PASS'
