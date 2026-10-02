MKXP Launcher shared game-font fallback folder

Place fallback fonts here. Game-local fonts always have priority.

Used by:
- RPG Maker XP / VX / VX Ace (Fontconfig fallback)
- RPG Maker MV / MZ (Fontconfig + filename/FontFace fallback)
- WOLF RPG Editor via Proton (Fontconfig + per-game Windows Fonts hardlinks)
- EasyRPG 2000/2003 merged font path

Supported shared-file types:
.ttf .ttc .otf .fon .fnt .bdf .woff .woff2

WOLF/Proton Windows-font injection uses:
.ttf .ttc .otf .fon .fnt

Do not remove a game's own font files. The shared folder is used only as a fallback layer.