#!/usr/bin/env bash
set -euo pipefail

ROOT="${1:-/mnt/d/GPT/MKXP}"
LAUNCHER="$ROOT/_dev/src/main.cpp"
EVENT="$ROOT/_dev/mkxp-z-upstream/src/eventthread.cpp"
URL='steam://open/keyboard?XPosition=0&YPosition=0&Width=0&Height=0&Mode=0'

# Parent-launcher manual keyboard shortcut uses the Steam Deck-style deeplink.
grep -Fq "$URL" "$LAUNCHER"
grep -Fq 'execlp("steam", "steam", "-ifrunning", kSteamDeckKeyboardUrl' "$LAUNCHER"

# The old parameterless deeplink was unreliable in Gaming Mode and must not be
# used anymore.
! grep -Fq '"steam://open/keyboard"' "$LAUNCHER"

# V097 retired automatic game-side Steam keyboard requests completely.
! grep -Fq "$URL" "$EVENT"
! grep -Fq 'requestSteamOnScreenKeyboard' "$EVENT"

echo 'V092 parameterized Steam Deck keyboard deeplink PASS'
