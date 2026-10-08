package com.dj5927.rpgmakerplayer;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

public final class GameScanner {
    private static final int MAX_DEPTH = 10;
    private static final int MAX_DIRECTORIES = 12000;

    private GameScanner() {}

    public static List<GameEntry> scan(Context context, Uri treeUri) throws Exception {
        String rootId = DocumentsContract.getTreeDocumentId(treeUri);
        List<Child> rootChildren = listChildren(context, treeUri, rootId);
        Map<String, String> thumbnailCatalog = buildThumbnailCatalog(context, treeUri, rootChildren);
        String sharedRtpDocumentId = findRootDirectoryDocumentId(rootChildren, "rtp");
        ArrayDeque<Node> queue = new ArrayDeque<>();
        queue.add(new Node(rootId, displayName(context, treeUri, rootId), 0));
        ArrayList<GameEntry> games = new ArrayList<>();
        int visited = 0;

        while (!queue.isEmpty() && visited < MAX_DIRECTORIES) {
            Node node = queue.removeFirst();
            visited++;
            List<Child> children = listChildren(context, treeUri, node.documentId);
            Detection detection = detect(context, treeUri, node, children);
            if (detection != null) {
                String thumbnailDocumentId = findThumbnailDocumentId(node, thumbnailCatalog);
                games.add(new GameEntry(
                        node.name == null || node.name.isEmpty() ? "RPG Maker Game" : node.name,
                        detection.engine,
                        detection.documentId,
                        detection.webRootDocumentId,
                        detection.archive,
                        thumbnailDocumentId,
                        sharedRtpDocumentId,
                        node.documentId));
                continue;
            }
            if (node.depth >= MAX_DEPTH) continue;
            for (Child child : children) {
                if (child.directory && !child.name.startsWith(".") &&
                        !"_image".equalsIgnoreCase(child.name) && !"rtp".equalsIgnoreCase(child.name) &&
                        !"saves".equalsIgnoreCase(child.name)) {
                    queue.addLast(new Node(child.documentId, child.name, node.depth + 1));
                }
            }
        }
        games.sort((a, b) -> a.title.compareToIgnoreCase(b.title));
        return games;
    }

    private static Map<String, String> buildThumbnailCatalog(Context context, Uri treeUri,
                                                             List<Child> rootChildren) {
        HashMap<String, String> out = new HashMap<>();
        Child imageDir = null;
        for (Child child : rootChildren) {
            if (child.directory && "_image".equalsIgnoreCase(child.name)) {
                imageDir = child;
                break;
            }
        }
        if (imageDir == null) return out;

        for (Child child : listChildrenQuiet(context, treeUri, imageDir.documentId)) {
            if (child.directory || child.name == null) continue;
            String lower = child.name.toLowerCase(Locale.ROOT);
            if (!lower.endsWith(".png")) continue;
            out.put(lower, child.documentId);
        }
        return out;
    }

    private static String findThumbnailDocumentId(Node node, Map<String, String> thumbnailCatalog) {
        if (node.name == null || node.name.isEmpty()) return null;
        return thumbnailCatalog.get((node.name + ".png").toLowerCase(Locale.ROOT));
    }

    private static String findRootDirectoryDocumentId(List<Child> rootChildren, String wanted) {
        for (Child child : rootChildren) {
            if (child.directory && wanted.equalsIgnoreCase(child.name)) return child.documentId;
        }
        return null;
    }

    private static Detection detect(Context context, Uri treeUri, Node node, List<Child> children) {
        Map<String, Child> byName = mapByName(children);
        ArrayList<Child> payloadFiles = new ArrayList<>();
        for (Child child : children) {
            if (!child.directory && !child.name.startsWith(".")) payloadFiles.add(child);
        }

        Child gameIni = byName.get("game.ini");
        if (gameIni != null) {
            String ini = readSmallText(context, treeUri, gameIni.documentId).toLowerCase(Locale.ROOT);
            if (ini.contains("rgss301")) return new Detection(GameEntry.Engine.VXACE, node.documentId, null, false);
            if (ini.contains("rgss202") || ini.contains("rgss200")) return new Detection(GameEntry.Engine.VX, node.documentId, null, false);
            if (ini.contains("rgss102") || ini.contains("rgss100")) return new Detection(GameEntry.Engine.XP, node.documentId, null, false);
        }

        Child data = byName.get("data");
        if (data != null && data.directory) {
            Map<String, Child> dataFiles = mapByName(listChildrenQuiet(context, treeUri, data.documentId));
            if (dataFiles.containsKey("scripts.rvdata2")) return new Detection(GameEntry.Engine.VXACE, node.documentId, null, false);
            if (dataFiles.containsKey("scripts.rvdata")) return new Detection(GameEntry.Engine.VX, node.documentId, null, false);
            if (dataFiles.containsKey("scripts.rxdata")) return new Detection(GameEntry.Engine.XP, node.documentId, null, false);
        }

        if (byName.containsKey("rpg_rt.ldb") && byName.containsKey("rpg_rt.lmt")) {
            return new Detection(GameEntry.Engine.EASYRPG, node.documentId, null, false);
        }

        Child www = byName.get("www");
        if (www != null && www.directory) {
            GameEntry.Engine webEngine = detectWebEngine(context, treeUri, www.documentId);
            if (webEngine != null) return new Detection(webEngine, node.documentId, www.documentId, false);
        }
        GameEntry.Engine rootWeb = detectWebEngineFromChildren(context, treeUri, node.documentId, children);
        if (rootWeb != null) return new Detection(rootWeb, node.documentId, node.documentId, false);

        if (payloadFiles.size() == 1) {
            Child only = payloadFiles.get(0);
            String lower = only.name.toLowerCase(Locale.ROOT);
            if (lower.endsWith(".easyrpg") || lower.endsWith(".zip")) {
                return new Detection(GameEntry.Engine.EASYRPG, only.documentId, null, true);
            }
        }
        return null;
    }

    private static GameEntry.Engine detectWebEngine(Context context, Uri treeUri, String rootId) {
        return detectWebEngineFromChildren(context, treeUri, rootId, listChildrenQuiet(context, treeUri, rootId));
    }

    private static GameEntry.Engine detectWebEngineFromChildren(Context context, Uri treeUri,
                                                                 String rootId, List<Child> children) {
        Map<String, Child> byName = mapByName(children);
        if (!byName.containsKey("index.html")) return null;
        Child js = byName.get("js");
        if (js == null || !js.directory) return null;
        Map<String, Child> jsFiles = mapByName(listChildrenQuiet(context, treeUri, js.documentId));
        if (jsFiles.containsKey("rmmz_core.js")) return GameEntry.Engine.MZ;
        if (jsFiles.containsKey("rpg_core.js")) return GameEntry.Engine.MV;
        return null;
    }

    private static Map<String, Child> mapByName(List<Child> children) {
        HashMap<String, Child> out = new HashMap<>();
        for (Child child : children) out.put(child.name.toLowerCase(Locale.ROOT), child);
        return out;
    }

    static List<Child> listChildrenQuiet(Context context, Uri treeUri, String parentId) {
        try { return listChildren(context, treeUri, parentId); }
        catch (Exception ignored) { return new ArrayList<>(); }
    }

    static List<Child> listChildren(Context context, Uri treeUri, String parentId) throws Exception {
        ContentResolver resolver = context.getContentResolver();
        Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, parentId);
        String[] projection = {
                DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE,
                DocumentsContract.Document.COLUMN_SIZE,
                DocumentsContract.Document.COLUMN_LAST_MODIFIED
        };
        ArrayList<Child> children = new ArrayList<>();
        try (Cursor cursor = resolver.query(childrenUri, projection, null, null, null)) {
            if (cursor == null) return children;
            while (cursor.moveToNext()) {
                String id = cursor.getString(0);
                String name = cursor.getString(1);
                String mime = cursor.getString(2);
                long size = cursor.isNull(3) ? -1L : cursor.getLong(3);
                long lastModified = cursor.isNull(4) ? 0L : cursor.getLong(4);
                children.add(new Child(id, name == null ? "" : name,
                        DocumentsContract.Document.MIME_TYPE_DIR.equals(mime), size, lastModified));
            }
        }
        return children;
    }

    static Uri documentUri(Uri treeUri, String documentId) {
        return DocumentsContract.buildDocumentUriUsingTree(treeUri, documentId);
    }

    private static String readSmallText(Context context, Uri treeUri, String documentId) {
        Uri uri = documentUri(treeUri, documentId);
        try (InputStream input = context.getContentResolver().openInputStream(uri)) {
            if (input == null) return "";
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buffer = new byte[8192];
            int total = 0;
            while (total < 65536) {
                int read = input.read(buffer, 0, Math.min(buffer.length, 65536 - total));
                if (read < 0) break;
                if (read > 0) {
                    out.write(buffer, 0, read);
                    total += read;
                }
            }
            return new String(out.toByteArray(), StandardCharsets.UTF_8);
        } catch (Exception ignored) {}
        return "";
    }

    private static String displayName(Context context, Uri treeUri, String documentId) {
        Uri uri = documentUri(treeUri, documentId);
        try (Cursor cursor = context.getContentResolver().query(uri,
                new String[]{DocumentsContract.Document.COLUMN_DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) return cursor.getString(0);
        } catch (Exception ignored) {}
        return "Games";
    }

    static final class Child {
        final String documentId;
        final String name;
        final boolean directory;
        final long size;
        final long lastModified;
        Child(String documentId, String name, boolean directory, long size, long lastModified) {
            this.documentId = documentId;
            this.name = name;
            this.directory = directory;
            this.size = size;
            this.lastModified = lastModified;
        }
    }

    private static final class Node {
        final String documentId;
        final String name;
        final int depth;
        Node(String documentId, String name, int depth) {
            this.documentId = documentId;
            this.name = name;
            this.depth = depth;
        }
    }

    private static final class Detection {
        final GameEntry.Engine engine;
        final String documentId;
        final String webRootDocumentId;
        final boolean archive;
        Detection(GameEntry.Engine engine, String documentId, String webRootDocumentId, boolean archive) {
            this.engine = engine;
            this.documentId = documentId;
            this.webRootDocumentId = webRootDocumentId;
            this.archive = archive;
        }
    }
}
