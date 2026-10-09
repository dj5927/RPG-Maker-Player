package com.dj5927.rpgmakerplayer;

import android.content.Context;
import android.net.Uri;
import android.provider.DocumentsContract;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import org.json.JSONObject;

/** Creates the user-editable shared resource layout inside the selected game library. */
final class LibraryFolderLayout {
    private LibraryFolderLayout() {}

    static void ensure(Context context, Uri treeUri) throws Exception {
        if (treeUri == null) return;
        String rootId = DocumentsContract.getTreeDocumentId(treeUri);

        ensureDirectory(context, treeUri, rootId, "_image");

        String rtpId = ensureDirectory(context, treeUri, rootId, "rtp");
        ensureDirectory(context, treeUri, rtpId, "xp");
        ensureDirectory(context, treeUri, rtpId, "vx");
        ensureDirectory(context, treeUri, rtpId, "vxace");
        ensureDirectory(context, treeUri, rtpId, "fonts");

        String easyRpgId = ensureDirectory(context, treeUri, rtpId, "easyrpg-player");
        ensureDirectory(context, treeUri, easyRpgId, "Soundfont");
        ensureDirectory(context, treeUri, easyRpgId, "Font");

        // Central compatibility profiles live in the SELECTED library.
        // Only validated, exact-hash profiles are installed; retain any
        // existing user profiles or explicitly disabled entries.
        String compatId = ensureDirectory(context, treeUri, rootId, "_compat");
        ensurePatchManifest(context, treeUri, compatId);
    }

    private static void ensurePatchManifest(Context context, Uri treeUri,
                                            String compatId) throws Exception {
        String verified = verifiedRegistryJson(context);
        JSONObject verifiedRoot = new JSONObject(verified);
        JSONObject verifiedGames = verifiedRoot.getJSONObject("games");
        for (GameScanner.Child child : GameScanner.listChildren(context, treeUri, compatId)) {
            if ("patches.json".equalsIgnoreCase(child.name)) {
                if (child.directory) {
                    throw new IllegalStateException("_compat/patches.json is a directory");
                }
                // A164 mistakenly shipped a blank games:{} registry. Restore
                // ONLY the already verified Curse profile into that file on
                // upgrade. Never reactivate an explicitly disabled entry or
                // overwrite an unrelated game profile.
                Uri fileUri = GameScanner.documentUri(treeUri, child.documentId);
                byte[] original = readPatchFile(context, fileUri);
                JSONObject existing;
                try {
                    existing = new JSONObject(
                            new String(original, StandardCharsets.UTF_8).replace("\ufeff", ""));
                } catch (org.json.JSONException invalidUserManifest) {
                    // Preserve a malformed user-authored registry as-is.
                    // Compatibility recovery must never hide the game library.
                    android.util.Log.w("RPGMP-COMPAT",
                            "Existing patches.json invalid; left untouched",
                            invalidUserManifest);
                    return;
                }
                if (existing.optInt("schema", 0) != 2) return;
                JSONObject currentGames = existing.optJSONObject("games");
                if (currentGames == null) return;
                boolean changed = false;
                java.util.Iterator<String> names = verifiedGames.keys();
                while (names.hasNext()) {
                    String id = names.next();
                    if (!currentGames.has(id)) {
                        currentGames.put(id, verifiedGames.getJSONObject(id));
                        changed = true;
                    }
                }
                if (!changed) return;
                // Preserve the user's existing JSON before any in-place
                // rewrite, including custom profiles from older builds.
                String backupName = "patches_before_verified_restore.json";
                boolean backedUp = false;
                for (GameScanner.Child sibling :
                        GameScanner.listChildren(context, treeUri, compatId)) {
                    if (backupName.equalsIgnoreCase(sibling.name) && !sibling.directory) {
                        backedUp = true;
                        break;
                    }
                }
                if (!backedUp) {
                    Uri backupUri = DocumentsContract.createDocument(
                            context.getContentResolver(),
                            GameScanner.documentUri(treeUri, compatId),
                            "application/json", backupName);
                    if (backupUri == null)
                        throw new IllegalStateException("Cannot back up existing compat patches");
                    writePatchFile(context, backupUri, original);
                }
                byte[] updated = (existing.toString(2) + "\n")
                        .getBytes(StandardCharsets.UTF_8);
                try {
                    writePatchFile(context, fileUri, updated);
                } catch (Exception error) {
                    try { writePatchFile(context, fileUri, original); }
                    catch (Exception ignored) { }
                    throw error;
                }
                return;
            }
        }
        Uri file = DocumentsContract.createDocument(
                context.getContentResolver(), GameScanner.documentUri(treeUri, compatId),
                "application/json", "patches.json");
        if (file == null) throw new IllegalStateException("Cannot create _compat/patches.json");
        writePatchFile(context, file, verified.getBytes(StandardCharsets.UTF_8));
    }

    private static String verifiedRegistryJson(Context context) throws Exception {
        try (InputStream in = context.getAssets()
                .open("rgss_compat/verified_profiles.json")) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] bytes = new byte[8192];
            int size;
            while ((size = in.read(bytes)) != -1) {
                if (out.size() + size > 262144)
                    throw new IllegalArgumentException("Built-in patches exceed limit");
                out.write(bytes, 0, size);
            }
            return out.toString("UTF-8");
        }
    }

    private static byte[] readPatchFile(Context context, Uri uri) throws Exception {
        try (InputStream in = context.getContentResolver().openInputStream(uri)) {
            if (in == null) throw new IllegalStateException("Cannot read existing compat patches");
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] bytes = new byte[8192];
            int size;
            while ((size = in.read(bytes)) != -1) {
                if (out.size() + size > 4194304)
                    throw new IllegalArgumentException("Existing patch manifest too large");
                out.write(bytes, 0, size);
            }
            return out.toByteArray();
        }
    }

    private static void writePatchFile(Context context, Uri uri, byte[] contents)
            throws Exception {
        try (OutputStream out = context.getContentResolver().openOutputStream(uri, "wt")) {
            if (out == null) throw new IllegalStateException("Cannot write compat patches");
            out.write(contents);
        }
    }

    private static String ensureDirectory(Context context, Uri treeUri, String parentId,
                                          String name) throws Exception {
        for (GameScanner.Child child : GameScanner.listChildren(context, treeUri, parentId)) {
            if (child.directory && name.equalsIgnoreCase(child.name)) return child.documentId;
        }

        Uri parent = GameScanner.documentUri(treeUri, parentId);
        Uri created = DocumentsContract.createDocument(context.getContentResolver(), parent,
                DocumentsContract.Document.MIME_TYPE_DIR, name);
        if (created == null) {
            throw new IllegalStateException("Cannot create library directory: " + name);
        }
        return DocumentsContract.getDocumentId(created);
    }
}
