SteamOS RPG Maker Player - shared RPG Maker RTP resources

Place legally obtained RTP contents in these folders:
  runtime/rtp/xp/      - RPG Maker XP resources (Graphics/, Audio/, etc.)
  runtime/rtp/vx/      - RPG Maker VX resources
  runtime/rtp/vxace/   - RPG Maker VX Ace resources
  runtime/rtp/fonts/   - shared fallback fonts for RGSS, MV/MZ, WOLF, EasyRPG

The player always checks the game's own resources first, then the matching
RTP engine folder. Existing user-configured MKXP RTP paths keep priority over
the shared fallback. The fonts folder is registered with Fontconfig and the
legacy resource scanner. RPG Maker RTP game art/music is NOT bundled here.

SteamOS v1.6: the native MKXP-Z resource cache and RGSS Ruby FileTest,
File.exist?, File.file?, read-only File.open/read, Dir.entries and limited
single-directory Dir.glob also ignore ASCII filename case in Graphics,
Audio, Data, Fonts and Movies. This works for RPG Maker XP/VX/VX Ace games
without per-game _compat profiles. Exact-case files and local game files
retain precedence over case-folded shared RTP resources. All file writes,
saves, configuration files, absolute/system paths and traversal are excluded.
