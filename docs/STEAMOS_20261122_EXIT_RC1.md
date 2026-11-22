# SteamOS v1.6 shutdown / upstream compatibility candidate

This branch is based on upstream `dj5927/RPG-Maker-Player` `3e4d7fc`.
It is a testing candidate, **not** an official release or a completed Steam Deck hardware validation.

## SteamOS shutdown changes

- `scripts/run.sh` now `exec`s `MKXP_Launcher` instead of detaching via `setsid`.
- The launcher handles TERM/INT/HUP and SDL window-close events without destroying
  the Gamescope frontend surface until its active game has been terminated.
- Each forked game process uses a separate process group, resets parent signal
  handlers, and receives `PR_SET_PDEATHSIG` to avoid an orphan after launcher death.
- Game cleanup sends TERM then escalates to KILL after 2.5 seconds; remaining
  processes in the launched game group are cleaned without signaling Steam itself.
- The existing Start+Select game-exit and Select+X keyboard controls are preserved.
- Launcher log identifier: `V161-STEAM-EXIT-RC1`.

## Source-level upstream candidates

- mkxp-z `src/util/encoding.h`: unknown `iconv_open` charset guard
  (upstream PR #374, merged 2026-11-22).
- mkxp-z `src/display/sprite.cpp`: allow bilinear sprite scaling with shader
  effects (upstream PR #184, merged 2026-11-21).
- Both mkxp-z changes are **source only** until the SteamOS RGSS 1.8/1.9/3.1
  native runtime binaries are separately rebuilt and regression-tested.
- EasyRPG/NW.js runtime binary upgrades are not included in this candidate.
- Proton runtime acquisition/selection remains unchanged.

## Verification

Build with CMake/SDL2 on Linux and run `tests/steam_exit_smoke.py` against the
new `MKXP_Launcher`. This tests TERM, INT, HUP, wrapper PID tracking, and the
SIGKILL timeout. Also run scanner, Ruby detector, config, game view, and mouse bridge
smoke tests. The missing `steam_mouse_bridge_smoke.cpp` is restored to the source tree.

## SteamOS hardware acceptance still required

1. On Gaming Mode launch the launcher, Steam menu > Exit must return to Steam.
2. Launch an RGSS, EasyRPG, MV/MZ, and WOLF game, then Steam menu > Exit.
3. Check `logs/run.log` and `logs/MKXP_Launcher.log` for `steam exit` lines.
4. Repeat after suspend/resume and verify Steam Input and the Gamepad work.
5. Ensure the device remains responsive; this cannot be established from WSL.
