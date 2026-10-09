# Central patch registry: Android A160 and SteamOS V110

One central JSON holds game-specific compatibility rules. Game folder names
and display names are NEVER used for matching. Identification is based on
the game data content and SHA-256, with strict engine/version matching.

Central file locations (created automatically after selecting a library):

    Android: <selected mkxp games library>/_compat/patches.json
    SteamOS: <selected games library>/_compat/patches.json

New files start with {"schema":2,"enabled":true,"games":{}} and
contain no unverified example patches. Existing user-edited files are
never overwritten when the library changes or is scanned again.
A single
central file can hold many game profiles, not one file per game.
The file is reloaded at every game start. The new loader is installed once,
then new JSON rules do not require more APK or runtime builds.
The example registry has top-level enabled=false and each example game
enabled=false for safety. Only enable after confirming the target version.

Schema 2 uses an arbitrary administrative ID in the games map (NOT folder
names); each game specifies engine, enabled, fingerprints and patches.

XP fingerprint: Data/Scripts.rxdata AND Data/System.rxdata SHA-256.
VX fingerprint: Data/Scripts.rvdata AND Data/System.rvdata SHA-256.
VXACE fingerprint: Data/Scripts.rvdata2 AND Data/System.rvdata2 SHA-256.
MV/MZ content fingerprint: exact combination of data/System.json and
js/plugins.js SHA-256, optionally other explicit file hashes.

Each game can list several fingerprints for additional verified versions.
Changed or translated scripts can change hashes; add the specific additional
fingerprint after verification. No match means NO central patch. Conflicting
duplicate matches are rejected. Each source patch still requires the exact
target source occurrence and optional matching section hash.

Example RGSS structure, with fake placeholder SHA (DO NOT enable as-is):

    {
      "schema": 2,
      "enabled": true,
      "games": {
        "sample_xp_game_version_a": {
          "engine": "XP",
          "enabled": false,
          "fingerprints": [{
            "Data/Scripts.rxdata": "SCRIPT_64_HEX_SHA256",
            "Data/System.rxdata": "SYSTEM_64_HEX_SHA256"
          }],
          "patches": []
        }
      }
    }

For MV/MZ fingerprints, use these two paths instead:

    {"data/System.json": "64_HEX_SHA256",
     "js/plugins.js": "64_HEX_SHA256"}

Patches use the previous schema-1 entries (id, engine, section / file,
find, replace, expected_matches=1). The older game-root
rpgmp-patches.json format remains supported, with a matching central
profile taking priority.

The included Abaddon and The Curse of Pleasure examples are DISABLED until
device-tested. Do not auto-activate experimental patches globally. Original
game files are not edited. EasyRPG and WOLF are out of scope.

Android A160 and SteamOS V110 real-device acceptance testing is pending.
