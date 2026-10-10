MKXP Launcher legacy shared font folder (automatic migration source)

SteamOS v1.5 uses runtime/rtp/fonts as the single shared fallback folder.
Place NEW fallback fonts under runtime/rtp/fonts, NOT here.
Any existing user-added font files found here are copied to the new folder
at launcher startup without overwriting existing new-folder fonts or
deleting the originals. Game-local fonts always take priority.

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