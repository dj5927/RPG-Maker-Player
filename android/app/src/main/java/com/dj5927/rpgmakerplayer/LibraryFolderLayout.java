package com.dj5927.rpgmakerplayer;

import android.content.Context;
import android.net.Uri;
import android.provider.DocumentsContract;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;

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

        // Central compatibility profiles live in the SELECTED game library.
        // Never overwrite an existing user-edited manifest, even when the
        // library is reselected or scanned on application startup.
        String compatId = ensureDirectory(context, treeUri, rootId, "_compat");
        ensurePatchManifest(context, treeUri, compatId);
    }

    private static void ensurePatchManifest(Context context, Uri treeUri,
                                            String compatId) throws Exception {
        for (GameScanner.Child child : GameScanner.listChildren(context, treeUri, compatId)) {
            if ("patches.json".equalsIgnoreCase(child.name)) {
                if (child.directory) {
                    throw new IllegalStateException("_compat/patches.json is a directory");
                }
                return;
            }
        }
        Uri file = DocumentsContract.createDocument(
                context.getContentResolver(), GameScanner.documentUri(treeUri, compatId),
                "application/json", "patches.json");
        if (file == null) throw new IllegalStateException("Cannot create _compat/patches.json");
        // Empty-but-enabled central registry: compatibility fixes remain
        // opt-in per verified game content fingerprint, never global.
        try (OutputStream out = context.getContentResolver().openOutputStream(file, "wt")) {
            if (out == null) throw new IllegalStateException("Cannot write _compat/patches.json");
            out.write(("{\n  \"schema\": 2,\n  \"enabled\": true,\n" +
                    "  \"games\": {}\n}\n").getBytes(StandardCharsets.UTF_8));
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
