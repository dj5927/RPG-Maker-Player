#!/usr/bin/env bash
set -euo pipefail

ROOT="${1:-/mnt/d/GPT/MKXP}"
SRC="$ROOT/_dev/src/main.cpp"

# Selection stops at the ends instead of wrapping last -> first.
! grep -Fq 'auto wrap = [&](long long value)' "$SRC"
grep -Fq 'if (dy < 0 && selected > 0) --selected;' "$SRC"
grep -Fq 'if (dy > 0 && selected + 1 < games.size()) ++selected;' "$SRC"

# Library page changes use the same 260 ms cubic vertical slide feel.
grep -Fq 'bool libraryPageTransitionActive = false;' "$SRC"
grep -Fq 'auto startLibraryPageTransition = [&](std::size_t fromSelected' "$SRC"
grep -Fq 'constexpr float LIBRARY_PAGE_TRANSITION_MS = 260.0f;' "$SRC"
grep -Fq 'drawLibraryContent(-direction * slideY);' "$SRC"
grep -Fq 'drawLibraryContent(direction * (HEIGHT - slideY));' "$SRC"

# Settings detail controls are centered inside the content panel.
grep -Fq 'const int optionStartX = content.x + (content.w - (optionW * 2 + optionGap)) / 2;' "$SRC"
grep -Fq 'const int chipStartX = content.x + (content.w - chipGroupW) / 2;' "$SRC"
grep -Fq 'content.x + content.w / 2, content.y + 28, WHITE, true' "$SRC"

# Short Start/F3 no longer open the redundant filter/sort modal.
! grep -Fq 'case SDLK_F3: if (!exitModal) openFilterSort(); break;' "$SRC"
! grep -Fq 'if (startTapCandidate && !selectHeld && !exitModal) openFilterSort();' "$SRC"
! grep -Fq 'Start 메뉴' "$SRC"
! grep -Fq 'Start Menu' "$SRC"
! grep -Fq 'Start メニュー' "$SRC"

echo 'V083 library smooth paging + bounded navigation + centered settings + Start-menu removal PASS'
