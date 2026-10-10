# RPG Maker Player - JSON game patches (MV/MZ)

MV/MZ game folder: put rpgmp-patches.json next to index.html (for MV usually
www/) or in the game root on SteamOS. Android uses web root only.
The manifest is read every game launch, so changing JSON does NOT require
another launcher/APK build after this patch loader has been installed.

SteamOS v1.5 central profile database: installation runtime/_compat/patches.json
(auto-created, user-editable). Android central database: selected game
library/_compat/patches.json. Both use content-based SHA-256 fingerprints;
currently verified Android game profiles are bundled in
runtime/_compat/verified_profiles.json on SteamOS. Existing per-game
rpgmp-patches.json is still an optional fallback when the central DB does
not contain a matching game.

Example JSON (disabled intentionally):

    {
      "schema": 1,
      "enabled": false,
      "patches": [
        {
          "id": "example-fix",
          "engine": "MV",
          "file": "js/plugins/Example.js",
          "find": "const value=1;",
          "replace": "const value=2;",
          "expected_matches": 1
        }
      ]
    }

Replace the example with a VERIFIED game-specific patch and set enabled=true.
An optional sha256_before field requires 64 hex digits of the original file
SHA-256; use it to restrict patches to an exact game/plugin build.

Rules:
* Each patch uses an explicit file path under js/ ending in .js or index.html.
* MV/MZ engine must match. Exactly one find occurrence is required.
* Bad JSON, missing files, mismatched code or hash never apply a patch.
* Other games without the file are unchanged.
* The original JoiPlay patches.json is NOT recognized by this importer.
  Its repeated top-level JSON keys and broad replacements are unsafe to
  import blindly. Do not enable the imported 17 fixes globally.
* XP/VX/VX Ace RGSS Ruby is supported by the **V109** Ruby script-section
  loader. Use engine XP / VX / VXACE plus section and/or section_index,
  instead of the MV/MZ file field. It edits decompressed script section [3]
  in memory after existing cicpoffs patches, before execution.
  RPG Maker RGSS 1/2/3 Ruby is not the same as user-selectable runtime Ruby
  1.8/1.9/3.1. Engine names select game format, not interpreter version.
* EasyRPG and WOLF are not supported and are out of scope.
* SteamOS creates a symlink-backed patch view in cache/mvmz/.../patchviews.
  Original JS remains unchanged. Existing save/ folder is linked; if absent,
  new save/ files go to persistent cache/mvmz/.../patch-saves instead.
  Custom scripts that write new files under the mirrored js/ folder may need
  an additional compatibility rule. Old views remain in cache until cleaned.
* Android patches WebView response content in memory, not original game files.
* Applying a syntactically valid JSON patch does NOT prove that its JavaScript
  code is safe or compatible. Test the game before sharing each new patch.

Logs: SteamOS [RPGMP-PATCH], Android [PATCH].
Rollback: set enabled=false (or remove rpgmp-patches.json), restart game.

## RGSS XP/VX/VX Ace example (disabled by default)

Place rpgmp-patches.json alongside Game.ini and Data/ in the GAME ROOT.
If Android uses an external folder mirror, the manifest needs to be copied
into that same game directory.

    {
      "schema": 1,
      "enabled": false,
      "patches": [
        {
          "id": "example-rgss-armor-fix",
          "engine": "VXACE",
          "section": "Window_Base",
          "find": "armors.map(&:atype_id).uniq",
          "replace": "armors.compact.map(&:atype_id).uniq",
          "expected_matches": 1
        }
      ]
    }

This example reuses an idea from JoiPlay but is NOT validated for any game.
Set enabled=true only after verifying the actual script and behavior.
For duplicate section names, add section_index (0-based). For extra safety,
include sha256_before of the Ruby section **as seen after the built-in
compatibility pre-pass** (the SteamOS/Android pre-passes may differ), or
ruby=1.8/1.9/3.1 to limit a compatibility patch to that interpreter.
The patch applies only if its selected section and exact text both match.
SteamOS and Android use the same embedded Ruby parser without needing a
Ruby JSON gem. The original Scripts.rxdata/rvdata/rvdata2 is untouched.
