# Android MV/MZ game-specific JSON patches

The same schema is used on SteamOS and Android. Put a file named
rpgmp-patches.json in the web root (alongside index.html; MV normally www/).
The file is reloaded at each game start; no APK rebuild is needed for changes.

Example (disabled for safety):

    {
      "schema": 1,
      "enabled": false,
      "patches": [{
        "id": "example-fix",
        "engine": "MV",
        "file": "js/plugins/Example.js",
        "find": "const value=1;",
        "replace": "const value=2;",
        "expected_matches": 1
      }]
    }

Optional: sha256_before = 64 hex characters of the original source file.
Each enabled patch requires a matching MV/MZ engine, safe explicit source path,
and exactly one matching source text occurrence. Bad entries fail closed.
Only the WebView response bytes change; game files remain untouched.

RGSS (XP/VX/VX Ace) is supported from Android A159 in the same named manifest
but in the GAME ROOT (next to Game.ini, not next to web index.html). Specify
engine=XP/VX/VXACE, section=Ruby script section name, optionally a
section_index to disambiguate, and find/replace/expected_matches.
The preloaded Ruby adapter changes $RGSS_SCRIPTS section 3 only in memory.
Optional fields: sha256_before of the decompressed section AFTER existing
built-in compatibility passes, and ruby=1.8/1.9/3.1
to match the selected runtime. A dedicated portable JSON parser is bundled
for the older Ruby versions.

This is not an importer for JoiPlay patches.json: its duplicate root keys and
generic replacements are not blindly enabled. EasyRPG/WOLF are not targeted.
See SteamOS PATCHES_JSON_GUIDE.md for full RGSS examples and cautions.
See the SteamOS PATCHES_JSON_GUIDE.md for mirror/save-path details.
