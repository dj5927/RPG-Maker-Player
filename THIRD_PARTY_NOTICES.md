# Third-Party Notices

RPG Maker Player combines original launcher code with multiple open-source runtimes and compatibility projects. Each component keeps its upstream copyright and license. The repository-level `LICENSE` applies to original RPG Maker Player code; it does not replace third-party licenses.

## mkxp-z
- Upstream: https://github.com/mkxp-z/mkxp-z
- Source revision used as base: `b47047e10211821634a6a61ec4ff298c9a480a8b`
- License: GPL-2.0-or-later. Upstream notes that builds with OpenSSL enabled must also comply with Apache-2.0/OpenSSL requirements and the resulting binary is effectively GPL-3.0-or-later.
- Local source: `third_party/mkxp-z/`
- License text: `third_party/mkxp-z/COPYING`

## rpgmakermlinux-cicpoffs
- Upstream: https://github.com/bakustarver/rpgmakermlinux-cicpoffs
- Source revision used as reference/base: `ced5b59a9e2e409197b5ebb4fda1847b82e07f21`
- License: GPL-3.0
- Local source material: `third_party/rpgmakermlinux-cicpoffs/`
- License text: `third_party/rpgmakermlinux-cicpoffs/LICENSE`

## cicpoffs
- Upstream: https://github.com/adlerosn/cicpoffs
- Source revision used: `ae6112ff6ceeb287149beaab759a1b17d4e27dc1`
- License: GPLv2 / GPL-2.0-or-later for the GPL launcher path; upstream also documents dual-licensing history for parts of the project.
- Local source: `third_party/cicpoffs/`
- License text: `third_party/cicpoffs/LICENSE`

## EasyRPG Player
- Upstream: https://github.com/EasyRPG/Player
- Release bundled by RPG Maker Player 1.0: EasyRPG Player 0.8.1.1
- License: GPL-3.0
- The binary distribution retains upstream notices and related assets.

## NW.js
- Upstream: https://github.com/nwjs/nw.js
- Runtime versions bundled by RPG Maker Player 1.0 include v0.29.0 and v0.117.0.
- NW.js license: MIT. Chromium and bundled third-party libraries carry additional licenses; upstream `credits.html`/notices are preserved in the binary runtime where provided.

## Ruby
- Upstream: https://github.com/ruby/ruby
- Runtime families used: Ruby 1.8, 1.9 and 3.1 compatibility paths.
- Ruby is distributed under the Ruby License / 2-clause BSD option as described by upstream.

## SDL family
- SDL: https://github.com/libsdl-org/SDL
- SDL_image: https://github.com/libsdl-org/SDL_image
- SDL_ttf: https://github.com/libsdl-org/SDL_ttf
- Used by the launcher for windowing, controller/input, image and text rendering.
- Each library retains its upstream license.

## Valve Proton
- Upstream: https://github.com/ValveSoftware/Proton
- Used externally through the user's Steam installation for WOLF RPG Editor titles.
- Proton is not relicensed by RPG Maker Player; see upstream for its component licenses.

## TimGM6mb SoundFont
- File: `assets/soundfonts/TimGM6mb.sf2`
- Copyright/license details: `assets/soundfonts/TimGM6mb.LICENSE.txt`
- License for the SoundFont package: GPL-2 as documented in the preserved Debian copyright notice.

## Additional dependencies
The binary release also includes or dynamically uses additional libraries required by the runtimes (for example OpenAL, Ogg/Vorbis, FluidSynth, FreeType and related multimedia/system libraries). Their original notices/licenses remain applicable. This repository does not claim ownership of third-party code, trademarks, game engines, or runtime components.