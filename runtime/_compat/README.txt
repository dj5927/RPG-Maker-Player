SteamOS RPG Maker Player - central compatibility registry

runtime/_compat/verified_profiles.json - bundled Android + verified SteamOS profiles
runtime/_compat/patches.json          - generated user-editable active database
runtime/_compat/merge_registry.py     - merges missing verified + legacy profiles

The launcher initializes patches.json automatically. On existing installations,
it imports custom entries from the previously used <game library>/_compat/patches.json
without modifying or deleting the old file. Existing global disabled states,
per-profile disabled states, custom profiles, and invalid files are preserved.

Never copy an unverified game rule to multiple games. Exact SHA-256 fingerprints
must match original Scripts + System data (RGSS) or System.json + plugins.js
(MV/MZ). No game archives or save files are modified.

SteamOS 1.6 uses the built-in case-insensitive RGSS resource reader.
Only the four Android-verified Curse rules remain. Existing installations
automatically retire the unchanged stock Avatar rule and keep any user-edited
rules, custom profiles and disabled settings. The pre-cleanup registry is
backed up once to patches_before_v16_cleanup.json.
