package com.dj5927.rpgmakerplayer;

import android.net.Uri;
import android.os.Build;
import android.os.Environment;

import java.io.File;

final class DirectStorageResolver {
    private static final String EXTERNAL_STORAGE_AUTHORITY =
            "com.android.externalstorage.documents";

    private DirectStorageResolver() {}

    static boolean canUseDirectStorage() {
        return Build.VERSION.SDK_INT >= 30 && Environment.isExternalStorageManager();
    }

    static File resolveGameDirectory(Uri treeUri, GameEntry game) {
        if (game == null) return null;
        return resolveDocumentDirectory(treeUri, game.documentId);
    }

    static File resolveSharedRtpDirectory(Uri treeUri, GameEntry game) {
        if (game == null || game.sharedRtpDocumentId == null ||
                game.sharedRtpDocumentId.isEmpty()) return null;
        return resolveDocumentDirectory(treeUri, game.sharedRtpDocumentId);
    }

    static File resolveDocumentDirectory(Uri treeUri, String documentId) {
        if (!canUseDirectStorage() || treeUri == null || documentId == null ||
                documentId.isEmpty()) return null;
        if (!EXTERNAL_STORAGE_AUTHORITY.equals(treeUri.getAuthority())) return null;

        int colon = documentId.indexOf(':');
        String volume = colon >= 0 ? documentId.substring(0, colon) : documentId;
        String relative = colon >= 0 ? documentId.substring(colon + 1) : "";
        File base = "primary".equalsIgnoreCase(volume)
                ? Environment.getExternalStorageDirectory()
                : new File("/storage", volume);
        File resolved = relative.isEmpty()
                ? base : new File(base, relative.replace('/', File.separatorChar));
        try {
            resolved = resolved.getCanonicalFile();
        } catch (Exception ignored) {
            resolved = resolved.getAbsoluteFile();
        }
        return resolved.isDirectory() && resolved.canRead() ? resolved : null;
    }
}
