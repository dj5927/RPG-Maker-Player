package com.dj5927.rpgmakerplayer;

import android.content.ContentResolver;
import android.content.Context;
import android.content.SharedPreferences;
import android.net.Uri;
import android.provider.DocumentsContract;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

final class PersistentSaveManager {
    private static final String PREFS = "persistent_save_session";
    private static final String KEY_TYPE = "type";
    private static final String KEY_TREE = "tree";
    private static final String KEY_REMOTE = "remote";
    private static final String KEY_LOCAL = "local";
    private static final String KEY_BASELINE = "baseline";
    private static final long MAX_SYNC_FILE = 256L * 1024L * 1024L;

    private PersistentSaveManager() {}

    static final class WebSaveCache {
        final String parentDocumentId;
        final String fileName;
        String fileDocumentId;
        final HashMap<String, String> values = new HashMap<>();

        WebSaveCache(String parentDocumentId, String fileName) {
            this.parentDocumentId = parentDocumentId;
            this.fileName = fileName;
        }
    }

    static void prepareEasyRpg(Context context, Uri treeUri, GameEntry game, File localSaves) throws Exception {
        String remote = game.saveRootDocumentId;
        if (!localSaves.exists() && !localSaves.mkdirs()) {
            throw new IllegalStateException("Cannot create EasyRPG save directory: " + localSaves);
        }
        clearEasyRpgSaveFiles(localSaves);
        copyEasyRpgSavesSafToLocal(context, treeUri, remote, localSaves);
        markPending(context, "EASYRPG_GAME_ROOT", treeUri, remote, localSaves, null);
    }

    static void prepareEasyRpgLibretro(Context context, Uri treeUri, GameEntry game,
                                       File runtimeSaveDir, File legacyStaging) throws Exception {
        String remote = game.saveRootDocumentId;
        if (!runtimeSaveDir.exists() && !runtimeSaveDir.mkdirs()) {
            throw new IllegalStateException("Cannot create EasyRPG core save directory: " + runtimeSaveDir);
        }
        clearEasyRpgSaveFiles(runtimeSaveDir);
        copyEasyRpgSavesSafToLocal(context, treeUri, remote, runtimeSaveDir);
        markPending(context, "EASYRPG_GAME_ROOT", treeUri, remote, runtimeSaveDir, null);
    }

    static void prepareRgss(Context context, Uri treeUri, GameEntry game, File mirrorRoot) throws Exception {
        String remote = game.saveRootDocumentId;
        List<String> remoteFiles = listRelativeFiles(context, treeUri, remote);
        clearStaleRgssSaveFiles(mirrorRoot, new HashSet<>(remoteFiles));

        JSONObject baseline = new JSONObject();
        baseline.put("files", snapshotLocalTree(mirrorRoot));
        JSONArray tracked = new JSONArray();
        for (String path : remoteFiles) {
            if (!isLauncherManagedLegacyPath(path) && looksLikeRgssSave(path)) tracked.put(path);
        }
        baseline.put("remote", tracked);
        File baselineFile = baselineFile(context, game.stableId());
        writeLocalText(baselineFile, baseline.toString());
        markPending(context, "RGSS", treeUri, remote, mirrorRoot, baselineFile);
    }

    static boolean hasPending(Context context) {
        return !context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .getString(KEY_TYPE, "").isEmpty();
    }

    static void finishPending(Context context) throws Exception {
        SharedPreferences prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        String type = prefs.getString(KEY_TYPE, "");
        if (type == null || type.isEmpty()) return;
        Uri treeUri = Uri.parse(prefs.getString(KEY_TREE, ""));
        String remote = prefs.getString(KEY_REMOTE, "");
        File local = new File(prefs.getString(KEY_LOCAL, ""));
        if ("DIR".equals(type)) {
            syncLocalDirectoryExact(context, treeUri, remote, local);
        } else if ("EASYRPG_CORE".equals(type)) {
            syncEasyRpgFilesExact(context, treeUri, remote, local);
        } else if ("EASYRPG_GAME_ROOT".equals(type)) {
            syncEasyRpgGameRoot(context, treeUri, remote, local);
        } else if ("RGSS".equals(type)) {
            String baselinePath = prefs.getString(KEY_BASELINE, "");
            syncRgssChanges(context, treeUri, remote, local,
                    baselinePath == null || baselinePath.isEmpty() ? null : new File(baselinePath));
        }
        prefs.edit().clear().apply();
    }

    private static void syncEasyRpgFilesExact(Context context, Uri treeUri, String remoteRoot,
                                              File localRoot) throws Exception {
        Set<String> localPaths = new HashSet<>();
        collectEasyRpgSaveFiles(localRoot, localRoot, localPaths);
        Set<String> remotePaths = new HashSet<>(listRelativeFiles(context, treeUri, remoteRoot));
        for (String remotePath : remotePaths) {
            if (!localPaths.contains(remotePath)) {
                deleteSafRelative(context, treeUri, remoteRoot, remotePath);
            }
        }
        for (String path : localPaths) {
            File file = new File(localRoot, path.replace('/', File.separatorChar));
            if (file.isFile() && file.length() <= MAX_SYNC_FILE) {
                writeLocalFileToSaf(context, treeUri, remoteRoot, path, file);
            }
        }
    }

    private static void collectEasyRpgSaveFiles(File root, File dir, Set<String> out) {
        if (dir == null || !dir.exists()) return;
        File[] children = dir.listFiles();
        if (children == null) return;
        for (File child : children) {
            if (child.isDirectory()) {
                collectEasyRpgSaveFiles(root, child, out);
            } else if (child.getName().toLowerCase(Locale.ROOT).endsWith(".lsd")) {
                out.add(relative(root, child));
            }
        }
    }

    private static boolean hasEasyRpgSaveFiles(File dir) {
        if (dir == null || !dir.exists()) return false;
        File[] children = dir.listFiles();
        if (children == null) return false;
        for (File child : children) {
            if (child.isDirectory() && hasEasyRpgSaveFiles(child)) return true;
            if (child.isFile() &&
                    child.getName().toLowerCase(Locale.ROOT).endsWith(".lsd")) return true;
        }
        return false;
    }

    private static void clearEasyRpgSaveFiles(File dir) {
        if (dir == null || !dir.exists()) return;
        File[] children = dir.listFiles();
        if (children == null) return;
        for (File child : children) {
            if (child.isDirectory()) {
                clearEasyRpgSaveFiles(child);
            } else if (child.getName().toLowerCase(Locale.ROOT).endsWith(".lsd")) {
                child.delete();
            }
        }
    }

    private static void copyEasyRpgSavesSafToLocal(Context context, Uri treeUri, String remoteRoot,
                                                   File localRoot) throws Exception {
        if (!localRoot.exists() && !localRoot.mkdirs()) {
            throw new IllegalStateException("Cannot create EasyRPG save directory: " + localRoot);
        }
        copyEasyRpgSavesSafToLocalRecursive(context, treeUri, remoteRoot, localRoot, "");
    }

    private static void copyEasyRpgSavesSafToLocalRecursive(Context context, Uri treeUri,
                                                            String parentId, File localDir,
                                                            String prefix) throws Exception {
        for (GameScanner.Child child : GameScanner.listChildren(context, treeUri, parentId)) {
            String path = prefix.isEmpty() ? child.name : prefix + "/" + child.name;
            if (isLauncherManagedLegacyPath(path)) continue;
            File target = new File(localDir, safeLocalName(child.name));
            if (child.directory) {
                copyEasyRpgSavesSafToLocalRecursive(context, treeUri, child.documentId, target, path);
                continue;
            }
            if (!child.name.toLowerCase(Locale.ROOT).endsWith(".lsd")) continue;
            File parent = target.getParentFile();
            if (parent != null && !parent.exists() && !parent.mkdirs()) {
                throw new IllegalStateException("Cannot create EasyRPG save parent: " + parent);
            }
            try (InputStream in = context.getContentResolver().openInputStream(
                         GameScanner.documentUri(treeUri, child.documentId));
                 OutputStream out = new FileOutputStream(target, false)) {
                if (in == null) throw new IllegalStateException("Cannot read EasyRPG save: " + child.name);
                copy(in, out);
            }
            if (child.lastModified > 0) target.setLastModified(child.lastModified);
        }
    }

    private static void syncEasyRpgGameRoot(Context context, Uri treeUri, String remoteRoot,
                                            File localRoot) throws Exception {
        Set<String> localPaths = new HashSet<>();
        collectEasyRpgSaveFiles(localRoot, localRoot, localPaths);
        Set<String> remoteSavePaths = new HashSet<>();
        for (String path : listRelativeFiles(context, treeUri, remoteRoot)) {
            if (!isLauncherManagedLegacyPath(path) &&
                    path.toLowerCase(Locale.ROOT).endsWith(".lsd")) remoteSavePaths.add(path);
        }
        for (String remotePath : remoteSavePaths) {
            if (!localPaths.contains(remotePath)) {
                deleteSafRelative(context, treeUri, remoteRoot, remotePath);
            }
        }
        for (String path : localPaths) {
            File file = new File(localRoot, path.replace('/', File.separatorChar));
            if (file.isFile() && file.length() <= MAX_SYNC_FILE) {
                writeLocalFileToSaf(context, treeUri, remoteRoot, path, file);
            }
        }
    }

    private static void clearStaleRgssSaveFiles(File mirrorRoot, Set<String> originalPaths) {
        Set<String> localPaths = new HashSet<>();
        collectLocalFiles(mirrorRoot, mirrorRoot, localPaths);
        for (String path : localPaths) {
            if (isLauncherManagedLegacyPath(path) || !looksLikeRgssSave(path) || originalPaths.contains(path)) continue;
            File stale = new File(mirrorRoot, path.replace('/', File.separatorChar));
            if (stale.isFile()) stale.delete();
        }
    }

    static String ensureEngineSaveDir(Context context, Uri treeUri, String gameDocumentId,
                                      String engineFolder) throws Exception {
        String saves = ensureDirectory(context, treeUri, gameDocumentId, "saves");
        return ensureDirectory(context, treeUri, saves, engineFolder);
    }

    static String ensureCentralEngineSaveDir(Context context, Uri treeUri, String gameDocumentId,
                                             String engineFolder) throws Exception {
        String libraryRoot = DocumentsContract.getTreeDocumentId(treeUri);
        String saves = ensureDirectory(context, treeUri, libraryRoot, "saves");
        String engine = ensureDirectory(context, treeUri, saves, engineFolder);
        return ensureDirectory(context, treeUri, engine, centralGameKey(gameDocumentId));
    }

    private static boolean migrateLegacyEngineSaveDir(Context context, Uri treeUri,
                                                      String gameDocumentId, String engineFolder,
                                                      String centralRoot) throws Exception {
        GameScanner.Child oldSaves = findChild(context, treeUri, gameDocumentId, "saves", true);
        if (oldSaves == null) return false;
        GameScanner.Child oldEngine = findChild(context, treeUri, oldSaves.documentId, engineFolder, true);
        if (oldEngine == null || oldEngine.documentId.equals(centralRoot)) return false;
        copySafTreeToSaf(context, treeUri, oldEngine.documentId, centralRoot);
        return true;
    }

    static WebSaveCache prepareCentralWebSaveCache(Context context, Uri treeUri,
                                                   String gameDocumentId) throws Exception {
        String libraryRoot = DocumentsContract.getTreeDocumentId(treeUri);
        String saves = ensureDirectory(context, treeUri, libraryRoot, "saves");
        String mvmz = ensureDirectory(context, treeUri, saves, "mvmz");
        String fileName = centralWebSaveName(gameDocumentId);
        WebSaveCache cache = new WebSaveCache(mvmz, fileName);

        GameScanner.Child central = findChild(context, treeUri, mvmz, fileName, false);
        if (central != null) {
            cache.fileDocumentId = central.documentId;
            String text = readSafText(context, treeUri, central.documentId);
            if (text != null && !text.isEmpty()) {
                JSONObject root = new JSONObject(text);
                JSONObject values = root.optJSONObject("values");
                if (values != null) {
                    java.util.Iterator<String> it = values.keys();
                    while (it.hasNext()) {
                        String key = it.next();
                        if (!values.isNull(key)) cache.values.put(key, values.optString(key, ""));
                    }
                }
            }
        }

        boolean migrated = false;
        GameScanner.Child oldSaves = findChild(context, treeUri, gameDocumentId, "saves", true);
        if (oldSaves != null) {
            GameScanner.Child oldMvMz = findChild(context, treeUri, oldSaves.documentId, "mvmz", true);
            if (oldMvMz != null) {
                for (GameScanner.Child child : GameScanner.listChildren(context, treeUri, oldMvMz.documentId)) {
                    if (child.directory || child.name == null || !child.name.endsWith(".rpgmpsave")) continue;
                    String key = child.name.substring(0, child.name.length() - ".rpgmpsave".length());
                    if (cache.values.containsKey(key)) continue;
                    String value = readSafText(context, treeUri, child.documentId);
                    if (value != null) {
                        cache.values.put(key, value);
                        migrated = true;
                    }
                }
            }
        }
        if (migrated) persistCentralWebSaveCache(context, treeUri, cache);
        return cache;
    }

    static synchronized void putCentralWebSave(Context context, Uri treeUri,
                                               WebSaveCache cache, String key,
                                               String value) throws Exception {
        cache.values.put(key, value == null ? "" : value);
        persistCentralWebSaveCache(context, treeUri, cache);
    }

    static synchronized void removeCentralWebSave(Context context, Uri treeUri,
                                                  WebSaveCache cache, String key) throws Exception {
        if (cache.values.remove(key) != null) persistCentralWebSaveCache(context, treeUri, cache);
    }

    static String centralWebSaveGet(WebSaveCache cache, String key) {
        return cache == null ? null : cache.values.get(key);
    }

    static boolean centralWebSaveExists(WebSaveCache cache, String key) {
        return cache != null && cache.values.containsKey(key);
    }

    static String centralWebSaveKeys(WebSaveCache cache, String prefix) {
        JSONArray out = new JSONArray();
        if (cache == null) return out.toString();
        for (String key : cache.values.keySet()) {
            if (prefix == null || prefix.isEmpty() || key.startsWith(prefix)) out.put(key);
        }
        return out.toString();
    }

    private static void persistCentralWebSaveCache(Context context, Uri treeUri,
                                                   WebSaveCache cache) throws Exception {
        JSONObject values = new JSONObject();
        for (Map.Entry<String, String> entry : cache.values.entrySet()) {
            values.put(entry.getKey(), entry.getValue());
        }
        JSONObject root = new JSONObject();
        root.put("version", 1);
        root.put("values", values);
        writeSafText(context, treeUri, cache.parentDocumentId, cache.fileName, root.toString());
        if (cache.fileDocumentId == null) {
            GameScanner.Child created = findChild(context, treeUri, cache.parentDocumentId,
                    cache.fileName, false);
            if (created != null) cache.fileDocumentId = created.documentId;
        }
    }

    private static String centralWebSaveName(String gameDocumentId) throws Exception {
        return centralGameKey(gameDocumentId) + ".json";
    }

    private static String centralGameKey(String gameDocumentId) throws Exception {
        String raw = gameDocumentId == null ? "game" : gameDocumentId.replace('\\', '/');
        int slash = raw.lastIndexOf('/');
        int colon = raw.lastIndexOf(':');
        int cut = Math.max(slash, colon);
        String leaf = cut >= 0 && cut + 1 < raw.length() ? raw.substring(cut + 1) : raw;
        leaf = safeLocalName(leaf).replaceAll("[^A-Za-z0-9._-]", "_");
        if (leaf.length() > 40) leaf = leaf.substring(0, 40);
        if (leaf.isEmpty()) leaf = "game";
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        byte[] hash = digest.digest(raw.getBytes(StandardCharsets.UTF_8));
        StringBuilder hex = new StringBuilder();
        for (int i = 0; i < 6; i++) hex.append(String.format(Locale.ROOT, "%02x", hash[i] & 0xff));
        return leaf + "_" + hex;
    }

    static void putWebSave(Context context, Uri treeUri, String gameDocumentId,
                           String logicalKey, String value) throws Exception {
        String root = ensureEngineSaveDir(context, treeUri, gameDocumentId, "mvmz");
        String name = safeWebKey(logicalKey) + ".rpgmpsave";
        writeSafText(context, treeUri, root, name, value == null ? "" : value);
    }

    static String getWebSave(Context context, Uri treeUri, String gameDocumentId,
                             String logicalKey) throws Exception {
        String root = ensureEngineSaveDir(context, treeUri, gameDocumentId, "mvmz");
        GameScanner.Child child = findChild(context, treeUri, root,
                safeWebKey(logicalKey) + ".rpgmpsave", false);
        return child == null ? null : readSafText(context, treeUri, child.documentId);
    }

    static boolean webSaveExists(Context context, Uri treeUri, String gameDocumentId,
                                 String logicalKey) throws Exception {
        String root = ensureEngineSaveDir(context, treeUri, gameDocumentId, "mvmz");
        return findChild(context, treeUri, root, safeWebKey(logicalKey) + ".rpgmpsave", false) != null;
    }

    static void removeWebSave(Context context, Uri treeUri, String gameDocumentId,
                              String logicalKey) throws Exception {
        String root = ensureEngineSaveDir(context, treeUri, gameDocumentId, "mvmz");
        GameScanner.Child child = findChild(context, treeUri, root,
                safeWebKey(logicalKey) + ".rpgmpsave", false);
        if (child != null) DocumentsContract.deleteDocument(context.getContentResolver(),
                GameScanner.documentUri(treeUri, child.documentId));
    }

    static String webSaveKeys(Context context, Uri treeUri, String gameDocumentId,
                              String prefix) throws Exception {
        String root = ensureEngineSaveDir(context, treeUri, gameDocumentId, "mvmz");
        JSONArray out = new JSONArray();
        for (GameScanner.Child child : GameScanner.listChildren(context, treeUri, root)) {
            if (child.directory || !child.name.endsWith(".rpgmpsave")) continue;
            String key = child.name.substring(0, child.name.length() - ".rpgmpsave".length());
            if (prefix == null || prefix.isEmpty() || key.startsWith(prefix)) out.put(key);
        }
        return out.toString();
    }

    private static void markPending(Context context, String type, Uri treeUri, String remote,
                                    File local, File baseline) {
        SharedPreferences.Editor e = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                .putString(KEY_TYPE, type)
                .putString(KEY_TREE, treeUri.toString())
                .putString(KEY_REMOTE, remote)
                .putString(KEY_LOCAL, local.getAbsolutePath());
        if (baseline != null) e.putString(KEY_BASELINE, baseline.getAbsolutePath());
        else e.remove(KEY_BASELINE);
        e.apply();
    }

    private static String ensureDirectory(Context context, Uri treeUri, String parentId,
                                          String name) throws Exception {
        GameScanner.Child existing = findChild(context, treeUri, parentId, name, true);
        if (existing != null) return existing.documentId;
        Uri parent = GameScanner.documentUri(treeUri, parentId);
        Uri created = DocumentsContract.createDocument(context.getContentResolver(), parent,
                DocumentsContract.Document.MIME_TYPE_DIR, name);
        if (created == null) throw new IllegalStateException("Cannot create save directory: " + name);
        return DocumentsContract.getDocumentId(created);
    }

    private static GameScanner.Child findChild(Context context, Uri treeUri, String parentId,
                                               String name, boolean directory) throws Exception {
        for (GameScanner.Child child : GameScanner.listChildren(context, treeUri, parentId)) {
            if (child.directory == directory && name.equalsIgnoreCase(child.name)) return child;
        }
        return null;
    }

    private static void copySafTreeToLocal(Context context, Uri treeUri, String remoteRoot,
                                           File localRoot) throws Exception {
        if (!localRoot.exists() && !localRoot.mkdirs()) throw new IllegalStateException("mkdir failed: " + localRoot);
        copySafTreeToLocalRecursive(context, treeUri, remoteRoot, localRoot);
    }

    private static void copySafTreeToSaf(Context context, Uri treeUri, String sourceRoot,
                                         String targetRoot) throws Exception {
        copySafTreeToSafRecursive(context, treeUri, sourceRoot, targetRoot);
    }

    private static void copySafTreeToSafRecursive(Context context, Uri treeUri, String sourceParent,
                                                  String targetParent) throws Exception {
        for (GameScanner.Child child : GameScanner.listChildren(context, treeUri, sourceParent)) {
            if (child.directory) {
                String targetDir = ensureDirectory(context, treeUri, targetParent, child.name);
                copySafTreeToSafRecursive(context, treeUri, child.documentId, targetDir);
                continue;
            }
            if (child.size > MAX_SYNC_FILE) continue;
            GameScanner.Child existing = findChild(context, treeUri, targetParent, child.name, false);
            Uri targetUri = existing != null
                    ? GameScanner.documentUri(treeUri, existing.documentId)
                    : DocumentsContract.createDocument(context.getContentResolver(),
                            GameScanner.documentUri(treeUri, targetParent),
                            "application/octet-stream", child.name);
            if (targetUri == null) throw new IllegalStateException("Cannot migrate save file: " + child.name);
            try (InputStream in = context.getContentResolver().openInputStream(
                         GameScanner.documentUri(treeUri, child.documentId));
                 OutputStream out = context.getContentResolver().openOutputStream(targetUri, "wt")) {
                if (in == null || out == null) throw new IllegalStateException("Cannot migrate save file: " + child.name);
                copy(in, out);
            }
        }
    }

    private static void copySafTreeToLocalRecursive(Context context, Uri treeUri, String parentId,
                                                    File localDir) throws Exception {
        for (GameScanner.Child child : GameScanner.listChildren(context, treeUri, parentId)) {
            File out = new File(localDir, safeLocalName(child.name));
            if (child.directory) {
                if (!out.exists() && !out.mkdirs()) throw new IllegalStateException("mkdir failed: " + out);
                copySafTreeToLocalRecursive(context, treeUri, child.documentId, out);
            } else {
                File parent = out.getParentFile();
                if (parent != null && !parent.exists()) parent.mkdirs();
                try (InputStream in = context.getContentResolver().openInputStream(
                        GameScanner.documentUri(treeUri, child.documentId));
                     OutputStream os = new FileOutputStream(out, false)) {
                    if (in == null) throw new IllegalStateException("open failed: " + child.name);
                    copy(in, os);
                }
                if (child.lastModified > 0) out.setLastModified(child.lastModified);
            }
        }
    }

    private static void syncLocalDirectoryExact(Context context, Uri treeUri, String remoteRoot,
                                                File localRoot) throws Exception {
        Set<String> localPaths = new HashSet<>();
        collectLocalFiles(localRoot, localRoot, localPaths);
        Set<String> remotePaths = new HashSet<>(listRelativeFiles(context, treeUri, remoteRoot));
        for (String remotePath : remotePaths) {
            if (!localPaths.contains(remotePath)) deleteSafRelative(context, treeUri, remoteRoot, remotePath);
        }
        for (String path : localPaths) {
            File file = new File(localRoot, path.replace('/', File.separatorChar));
            if (file.isFile() && file.length() <= MAX_SYNC_FILE) writeLocalFileToSaf(context, treeUri, remoteRoot, path, file);
        }
    }

    private static void syncRgssChanges(Context context, Uri treeUri, String remoteRoot,
                                        File localRoot, File baselineFile) throws Exception {
        JSONObject baseWrapper = baselineFile != null && baselineFile.isFile()
                ? new JSONObject(readLocalText(baselineFile)) : new JSONObject();
        JSONObject base = baseWrapper.optJSONObject("files");
        if (base == null) base = new JSONObject();
        JSONObject now = snapshotLocalTree(localRoot);
        JSONArray names = now.names();
        if (names != null) {
            for (int i = 0; i < names.length(); i++) {
                String path = names.getString(i);
                if (isLauncherManagedLegacyPath(path)) continue;
                JSONObject cur = now.optJSONObject(path);
                JSONObject old = base.optJSONObject(path);
                boolean changed = old == null || cur == null ||
                        cur.optLong("l", -1) != old.optLong("l", -2) ||
                        cur.optLong("m", -1) != old.optLong("m", -2);
                if (!changed) continue;
                File file = new File(localRoot, path.replace('/', File.separatorChar));
                if (file.isFile() && file.length() <= MAX_SYNC_FILE) {
                    writeLocalFileToSaf(context, treeUri, remoteRoot, path, file);
                }
            }
        }
        JSONArray tracked = baseWrapper.optJSONArray("remote");
        if (tracked != null) {
            for (int i = 0; i < tracked.length(); i++) {
                String path = tracked.optString(i, "");
                if (!path.isEmpty() && !isLauncherManagedLegacyPath(path) &&
                        !new File(localRoot, path.replace('/', File.separatorChar)).exists()) {
                    deleteSafRelative(context, treeUri, remoteRoot, path);
                }
            }
        }
        if (baselineFile != null) baselineFile.delete();
    }

    private static JSONObject snapshotLocalTree(File root) throws Exception {
        JSONObject out = new JSONObject();
        snapshotLocalRecursive(root, root, out);
        return out;
    }

    private static void snapshotLocalRecursive(File root, File dir, JSONObject out) throws Exception {
        File[] children = dir.listFiles();
        if (children == null) return;
        for (File child : children) {
            if (child.isDirectory()) snapshotLocalRecursive(root, child, out);
            else {
                String rel = relative(root, child);
                JSONObject info = new JSONObject();
                info.put("l", child.length());
                info.put("m", child.lastModified());
                out.put(rel, info);
            }
        }
    }

    private static void migrateLegacyRgss(Context context, Uri treeUri, String remoteRoot,
                                          File mirrorRoot) throws Exception {
        Set<String> files = new HashSet<>();
        collectLocalFiles(mirrorRoot, mirrorRoot, files);
        for (String path : files) {
            if (!looksLikeRgssSave(path)) continue;
            File file = new File(mirrorRoot, path.replace('/', File.separatorChar));
            if (file.isFile() && file.length() <= MAX_SYNC_FILE) writeLocalFileToSaf(context, treeUri, remoteRoot, path, file);
        }
    }

    private static boolean looksLikeRgssSave(String path) {
        String p = path.replace('\\', '/').toLowerCase(Locale.ROOT);
        String name = p.substring(p.lastIndexOf('/') + 1);
        if (p.startsWith("save/") || p.startsWith("saves/") || p.startsWith("userdata/")) return true;
        if (name.startsWith("save") && (name.endsWith(".rxdata") || name.endsWith(".rvdata") ||
                name.endsWith(".rvdata2") || name.endsWith(".sav") || name.endsWith(".dat"))) return true;
        return name.equals("config.rvdata") || name.equals("config.rvdata2") || name.equals("global.rvdata2");
    }

    private static boolean isLauncherManagedLegacyPath(String path) {
        if (path == null) return false;
        String p = path.replace('\\', '/').toLowerCase(Locale.ROOT);
        return p.equals("saves/rgss") || p.startsWith("saves/rgss/") ||
                p.equals("saves/easyrpg") || p.startsWith("saves/easyrpg/") ||
                p.equals("saves/mvmz") || p.startsWith("saves/mvmz/");
    }

    private static List<String> listRelativeFiles(Context context, Uri treeUri, String remoteRoot) throws Exception {
        ArrayList<String> out = new ArrayList<>();
        listRelativeFilesRecursive(context, treeUri, remoteRoot, "", out);
        return out;
    }

    private static void listRelativeFilesRecursive(Context context, Uri treeUri, String parentId,
                                                   String prefix, List<String> out) throws Exception {
        for (GameScanner.Child child : GameScanner.listChildren(context, treeUri, parentId)) {
            String path = prefix.isEmpty() ? child.name : prefix + "/" + child.name;
            if (child.directory) listRelativeFilesRecursive(context, treeUri, child.documentId, path, out);
            else out.add(path);
        }
    }

    private static void writeLocalFileToSaf(Context context, Uri treeUri, String remoteRoot,
                                            String relativePath, File source) throws Exception {
        String[] parts = relativePath.replace('\\', '/').split("/");
        String parentId = remoteRoot;
        for (int i = 0; i < parts.length - 1; i++) {
            if (!parts[i].isEmpty()) parentId = ensureDirectory(context, treeUri, parentId, parts[i]);
        }
        String leaf = parts.length == 0 ? source.getName() : parts[parts.length - 1];
        GameScanner.Child existing = findChild(context, treeUri, parentId, leaf, false);
        Uri uri;
        if (existing != null) uri = GameScanner.documentUri(treeUri, existing.documentId);
        else {
            uri = DocumentsContract.createDocument(context.getContentResolver(),
                    GameScanner.documentUri(treeUri, parentId), "application/octet-stream", leaf);
            if (uri == null) throw new IllegalStateException("Cannot create save file: " + relativePath);
        }
        try (InputStream in = new FileInputStream(source);
             OutputStream out = context.getContentResolver().openOutputStream(uri, "wt")) {
            if (out == null) throw new IllegalStateException("Cannot write save file: " + relativePath);
            copy(in, out);
        }
    }

    private static void writeSafText(Context context, Uri treeUri, String parentId,
                                     String name, String text) throws Exception {
        GameScanner.Child existing = findChild(context, treeUri, parentId, name, false);
        Uri uri = existing == null ? DocumentsContract.createDocument(context.getContentResolver(),
                GameScanner.documentUri(treeUri, parentId), "application/octet-stream", name)
                : GameScanner.documentUri(treeUri, existing.documentId);
        if (uri == null) throw new IllegalStateException("Cannot create save file: " + name);
        try (OutputStream out = context.getContentResolver().openOutputStream(uri, "wt")) {
            if (out == null) throw new IllegalStateException("Cannot write save file: " + name);
            out.write(text.getBytes(StandardCharsets.UTF_8));
        }
    }

    private static String readSafText(Context context, Uri treeUri, String documentId) throws Exception {
        try (InputStream in = context.getContentResolver().openInputStream(GameScanner.documentUri(treeUri, documentId));
             ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            if (in == null) return null;
            copy(in, out);
            return out.toString(StandardCharsets.UTF_8.name());
        }
    }

    private static void deleteSafRelative(Context context, Uri treeUri, String remoteRoot,
                                          String relativePath) throws Exception {
        String[] parts = relativePath.replace('\\', '/').split("/");
        String parentId = remoteRoot;
        for (int i = 0; i < parts.length - 1; i++) {
            GameScanner.Child dir = findChild(context, treeUri, parentId, parts[i], true);
            if (dir == null) return;
            parentId = dir.documentId;
        }
        if (parts.length == 0) return;
        GameScanner.Child child = findChild(context, treeUri, parentId, parts[parts.length - 1], false);
        if (child != null) DocumentsContract.deleteDocument(context.getContentResolver(),
                GameScanner.documentUri(treeUri, child.documentId));
    }

    private static void collectLocalFiles(File root, File dir, Set<String> out) {
        if (dir == null || !dir.exists()) return;
        File[] children = dir.listFiles();
        if (children == null) return;
        for (File child : children) {
            if (child.isDirectory()) collectLocalFiles(root, child, out);
            else out.add(relative(root, child));
        }
    }

    private static boolean hasAnyFile(File dir) {
        if (dir == null || !dir.exists()) return false;
        File[] children = dir.listFiles();
        if (children == null) return false;
        for (File child : children) {
            if (child.isFile() || (child.isDirectory() && hasAnyFile(child))) return true;
        }
        return false;
    }

    private static void clearLocal(File dir) {
        if (dir == null || !dir.exists()) return;
        File[] children = dir.listFiles();
        if (children == null) return;
        for (File child : children) deleteLocal(child);
    }

    private static void deleteLocal(File file) {
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) for (File child : children) deleteLocal(child);
        }
        file.delete();
    }

    private static File baselineFile(Context context, String stableId) {
        File dir = context.getExternalFilesDir("save_sessions");
        if (dir == null) dir = new File(context.getFilesDir(), "save_sessions");
        if (!dir.exists()) dir.mkdirs();
        return new File(dir, Integer.toHexString(stableId.hashCode()) + ".json");
    }

    private static void writeLocalText(File file, String text) throws Exception {
        File parent = file.getParentFile();
        if (parent != null && !parent.exists()) parent.mkdirs();
        try (OutputStream out = new FileOutputStream(file, false)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        }
    }

    private static String readLocalText(File file) throws Exception {
        try (InputStream in = new FileInputStream(file); ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            copy(in, out);
            return out.toString(StandardCharsets.UTF_8.name());
        }
    }

    private static String relative(File root, File file) {
        String base = root.getAbsolutePath();
        String path = file.getAbsolutePath();
        if (path.startsWith(base)) path = path.substring(base.length());
        while (path.startsWith(File.separator)) path = path.substring(1);
        return path.replace(File.separatorChar, '/');
    }

    private static String safeLocalName(String name) {
        if (name == null || name.isEmpty() || name.equals(".") || name.equals("..")) return "unnamed";
        return name.replace('/', '_').replace('\\', '_');
    }

    private static String safeWebKey(String key) {
        if (key == null || key.isEmpty()) return "empty";
        StringBuilder out = new StringBuilder();
        for (int i = 0; i < key.length(); i++) {
            char c = key.charAt(i);
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.') out.append(c);
            else out.append('_');
        }
        return out.toString();
    }

    private static void copy(InputStream in, OutputStream out) throws Exception {
        byte[] buffer = new byte[128 * 1024];
        int read;
        while ((read = in.read(buffer)) >= 0) out.write(buffer, 0, read);
    }
}
