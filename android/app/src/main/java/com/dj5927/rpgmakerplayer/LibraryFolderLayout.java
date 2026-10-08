package com.dj5927.rpgmakerplayer;

import android.content.Context;
import android.net.Uri;
import android.provider.DocumentsContract;

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
