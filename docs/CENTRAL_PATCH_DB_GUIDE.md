# Central content-fingerprint game patches

SteamOS central location: <selected-game-library>/_compat/patches.json
Android central location: <selected-game-library>/_compat/patches.json

Both launchers automatically create this file after choosing a new
game-library folder, and on later re-scans if it is missing. An existing
file is always retained unchanged. New registries start as
{"schema":2,"enabled":true,"games":{}}; no game patches are active until
you add a verified content-fingerprint profile.

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

See the Android CENTRAL_PATCH_DB_GUIDE.md for optional game profiles.
Unused Abaddon test patches and other unverified example entries are
not included in the newly created default registry.
The original per-game rpgmp-patches.json still works as a fallback.
