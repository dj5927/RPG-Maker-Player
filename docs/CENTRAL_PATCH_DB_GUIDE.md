# Central content-fingerprint game patches

SteamOS central location: <RPG Maker Player installation>/runtime/_compat/patches.json
Android central location: <selected-game-library>/_compat/patches.json

SteamOS 1.5 and newer automatically creates the runtime registry at launcher startup
after selecting the game library. It installs only the content-verified
profiles distributed in runtime/_compat/verified_profiles.json (currently
the four Android-verified The_Curse_of_Pleasure XP fixes). Existing custom
profiles and explicitly disabled game entries are retained. Existing
registries from the older selected-library/_compat/patches.json or old
installation-root/_compat/patches.json are imported without modifying the
original files. The existing global enabled=false setting is preserved.
Before changing an existing runtime registry, the first original contents
are saved as runtime/_compat/patches_before_verified_restore.json.

Android continues to manage its own selected-library registry. SteamOS
does not write a new _compat directory into a game folder or game library.
The runtime/_compat/patches.json file is deliberately EXCLUDED from
SteamOS installer/OTA archives to protect user customizations. The
verified_profiles.json bundle and merge script can be updated separately.
SteamOS 1.6 uses common case-insensitive resource lookup instead of the
old Curse-specific Avatar casefold patch. On existing v1.5.1/v1.5.2 installs,
only the untouched stock Avatar rule is retired. The previous registry is
backed up to runtime/_compat/patches_before_v16_cleanup.json. Custom entries,
modified rules, global disabled flags and game data remain unchanged.

One central schema-2 JSON file holds any number of profiles. Each profile
is keyed by an arbitrary management ID, NEVER the user's game folder name.
On every launch, the loader hashes original game content and selects only a
matching profile for the correct engine. Changes to JSON don't need a
launcher rebuild once the new loader has been installed.

RGSS XP/VX/VXACE hash both original Scripts.rxdata/rvdata/rvdata2 AND
System.rxdata/rvdata/rvdata2, so games sharing stock engine scripts will
not be incorrectly treated as the same game.
MV/MZ compare BOTH webroot/data/System.json and webroot/js/plugins.js.
Multiple registered hashes per game support tested game versions. Unknown
versions are deliberately skipped to prevent misapplying patches.

See runtime/_compat/README.txt for layout. Unused Abaddon test patches and
other unverified example entries are not included by default.
The original per-game rpgmp-patches.json still works as a fallback.
