# Building from source

The v1.0 release is the recommended way to use RPG Maker Player on SteamOS. This repository also contains the current launcher source and the modified compatibility sources used to build the release.

## Launcher

The launcher is a C++17 SDL application. Its primary source is in `src/` and the root `CMakeLists.txt` mirrors the SteamOS launcher build configuration.

Typical native dependencies include:

- CMake / a C++17 compiler
- SDL2
- SDL2_image
- SDL2_ttf

The project build helper is `scripts/build_steamos.sh`.

## mkxp-z

The exact modified mkxp-z source used by this project is under `third_party/mkxp-z/`. It is based on upstream commit:

`b47047e10211821634a6a61ec4ff298c9a480a8b`

The release uses separate Ruby compatibility builds for Ruby 1.8, 1.9 and 3.1. Project-specific compatibility changes are present directly in the source tree.

The current Ruby 1.9 compatibility path uses a Ruby 1.9.3-p551 non-threaded VM build. The reproducibility helpers used for that path are included under `scripts/ruby19/`:

- `build_ruby19_nothreaded.sh` — creates the Ruby 1.9.3-p551 build with direct threaded-code dispatch disabled.
- `build_mkxp_ruby19_nothreaded.sh` — builds the matching mkxp-z runtime against that Ruby.
- `ruby-1.9.pc` — pkg-config metadata for the static non-threaded Ruby build.

`scripts/clear_elf_execstack.py` is also included for the ELF executable-stack compatibility fix used during runtime preparation.

## MV/MZ compatibility

Runtime launch and JavaScript compatibility helpers live under:

- `runtime/mvmz/`
- `third_party/rpgmakermlinux-cicpoffs/`
- `third_party/cicpoffs/`

Prebuilt NW.js/Ruby/EasyRPG binaries are intentionally not committed to the main Git repository. They are distributed in the v1.0 Release package. Their upstream projects and licenses are listed in `THIRD_PARTY_NOTICES.md`.