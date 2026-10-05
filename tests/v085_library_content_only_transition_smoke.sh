#!/usr/bin/env bash
set -euo pipefail

ROOT="${1:-/mnt/d/GPT/MKXP}"
SRC="$ROOT/_dev/src/main.cpp"

# Header/filter/sort/page counter have their own static renderer.
grep -Fq 'auto drawLibraryHeader = [&]()' "$SRC"
grep -Fq 'drawTopTitle(uiWord("내 라이브러리"' "$SRC"
grep -Fq 'drawEnginePills(108, 82);' "$SRC"

# Only the game rows/cards accept the transition Y offset.
grep -Fq 'auto drawLibraryContent = [&](int yOffset)' "$SRC"
grep -Fq 'const int y = 126 + yOffset + i * 56;' "$SRC"
grep -Fq 'const int y = 126 + yOffset + rowIndex * rowGap;' "$SRC"

# During page animation the header is rendered once and only content moves in
# the clipped game-list region.
grep -Fq 'const SDL_Rect libraryContentClip{92, 118, WIDTH - 92, 532};' "$SRC"
grep -Fq 'drawLibraryContent(-direction * slideY);' "$SRC"
grep -Fq 'drawLibraryContent(direction * (HEIGHT - slideY));' "$SRC"
! grep -Fq 'displayLayout.uiOffsetY - direction * slideY' "$SRC"
! grep -Fq 'displayLayout.uiOffsetY + direction * (HEIGHT - slideY)' "$SRC"

echo 'V085 static Library header + content-only page slide PASS'
