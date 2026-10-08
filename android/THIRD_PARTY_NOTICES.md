# Android third-party notices

The repository-level `LICENSE` applies to original RPG Maker Player code. Third-party components retain their upstream copyrights and licenses.

## mkxp-z
- Upstream: https://github.com/mkxp-z/mkxp-z
- License: GPL-2.0-or-later, subject to upstream dependency/license requirements
- Modified Android source snapshots: `native/mkxp-z-modern/`, `native/mkxp-z-legacy/`
- Upstream license text is preserved as `COPYING` in each snapshot where available.

## Ruby
- Upstream: https://github.com/ruby/ruby
- Runtime families used: Ruby 1.8, 1.9 and 3.1
- Ruby License / 2-clause BSD option as documented upstream
- Project Android filesystem/locale changes: `native/ruby31-android-patches/`

## EasyRPG Player / libretro integration
- Upstream: https://github.com/EasyRPG/Player
- License: GPL-3.0
- Android bridge source: `easyrpg_runtime/`

## SDL family
- SDL: https://github.com/libsdl-org/SDL
- SDL_image: https://github.com/libsdl-org/SDL_image
- SDL_ttf: https://github.com/libsdl-org/SDL_ttf
- Each component retains its upstream license.

## GeneralUser GS SoundFont
The Android binary package may bundle GeneralUser GS for MIDI playback. The SoundFont is treated as a release/runtime asset rather than source and must be distributed with its original license/attribution terms.

## Other runtime dependencies
The APK/native runtimes also use libraries such as OpenAL, Ogg/Vorbis, FluidSynth, FreeType and Android system libraries. Their upstream licenses remain applicable. This project does not claim ownership of RPG Maker, EasyRPG, mkxp-z, Ruby, game assets or third-party runtime components.
