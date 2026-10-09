package com.dj5927.rpgmakerplayer;

import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.security.MessageDigest;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Locale;
import java.util.Map;
import java.util.function.Consumer;

/** Content-based game selection for a single shared central patch registry. */
final class MvMzCentralPatches {
    interface GameAsset {
        InputStream open(String webRootRelativePath) throws Exception;
    }

    private MvMzCentralPatches() {}

    static InputStream findManifest(ContentResolver resolver, Uri treeUri,
                                    String directFile, GameAsset assets,
                                    String engine, Consumer<String> report) {
        try (InputStream stream = openCentralFile(resolver, treeUri, directFile)) {
            if (stream == null) return null;
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[16384];
            int n;
            while ((n = stream.read(buf)) != -1) {
                if (out.size() + n > 4 * 1024 * 1024)
                    throw new IllegalArgumentException("central registry too large");
                out.write(buf, 0, n);
            }
            JSONObject root = new JSONObject(out.toString("UTF-8").replace("\ufeff", ""));
            if (root.optInt("schema", 0) != 2)
                throw new IllegalArgumentException("central registry requires schema=2");
            if (!root.optBoolean("enabled", false)) return null;
            JSONObject games = root.optJSONObject("games");
            if (games == null) throw new IllegalArgumentException("games map missing");
            JSONObject chosen = null;
            String chosenId = null;
            Map<String,String> hashed = new HashMap<>();
            Iterator<String> names = games.keys();
            int inspected = 0;
            while (names.hasNext()) {
                if (++inspected > 1000) throw new IllegalArgumentException("too many game entries");
                String id = names.next();
                JSONObject entry = games.optJSONObject(id);
                if (entry == null || !engine.equals(entry.optString("engine"))) continue;
                JSONArray profiles = entry.optJSONArray("fingerprints");
                if (profiles == null) continue;
                for (int i = 0; i < profiles.length() && i < 32; ++i) {
                    JSONObject required = profiles.optJSONObject(i);
                    if (required == null || required.length() < 2) continue;
                    if (!required.has("data/System.json") ||
                            !required.has("js/plugins.js")) continue;
                    boolean matches = true;
                    Iterator<String> files = required.keys();
                    int checked = 0;
                    while (files.hasNext()) {
                        String path = files.next();
                        String expected = required.optString(path, "");
                        if (++checked > 8 || !validFingerprintPath(path) ||
                                !expected.matches("(?i)[0-9a-f]{64}")) {
                            matches = false; break;
                        }
                        String actual = hashed.get(path);
                        if (actual == null) {
                            actual = fileHash(assets.open(path));
                            hashed.put(path, actual);
                        }
                        if (!expected.equalsIgnoreCase(actual)) { matches = false; break; }
                    }
                    if (matches) {
                        if (chosen != null) {
                            report.accept("central ambiguous content hash: skipped");
                            return null;
                        }
                        chosen = entry;
                        chosenId = id;
                        break;
                    }
                }
            }
            if (chosen == null) {
                report.accept("central no matching game content");
                return null;
            }
            report.accept("central content match id=" + chosenId);
            // Preserve existing schema-1 exact JS source replacement rules.
            JSONObject selected = new JSONObject();
            selected.put("schema", 1);
            selected.put("enabled", chosen.optBoolean("enabled", false));
            selected.put("patches", chosen.optJSONArray("patches"));
            return new ByteArrayInputStream(selected.toString().getBytes("UTF-8"));
        } catch (Exception error) {
            report.accept("central skipped: " + error.getMessage());
            return null;
        }
    }

    private static String fileHash(InputStream input) throws Exception {
        if (input == null) return "";
        try (InputStream in = input) {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            byte[] buf = new byte[32768];
            int n;
            long total = 0;
            while ((n = in.read(buf)) != -1) {
                total += n;
                if (total > 32L * 1024 * 1024) return "";
                md.update(buf, 0, n);
            }
            StringBuilder hex = new StringBuilder(64);
            for (byte b : md.digest())
                hex.append(String.format(Locale.ROOT, "%02x", b & 0xff));
            return hex.toString();
        }
    }

    private static boolean validFingerprintPath(String path) {
        if (path == null || path.length() > 200 || path.contains("..") ||
                path.startsWith("/") || path.indexOf('\\') >= 0) return false;
        return path.startsWith("data/") && path.endsWith(".json") ||
               path.startsWith("js/") && path.endsWith(".js");
    }

    private static InputStream openCentralFile(ContentResolver resolver, Uri treeUri,
                                               String directFile) throws Exception {
        if (directFile != null) {
            File file = new File(directFile);
            if (file.isFile()) return new FileInputStream(file);
        }
        String rootId = DocumentsContract.getTreeDocumentId(treeUri);
        String compatId = findChild(resolver, treeUri, rootId, "_compat");
        if (compatId == null) return null;
        String fileId = findChild(resolver, treeUri, compatId, "patches.json");
        return fileId == null ? null : resolver.openInputStream(
                DocumentsContract.buildDocumentUriUsingTree(treeUri, fileId));
    }

    private static String findChild(ContentResolver resolver, Uri tree, String parentId,
                                    String childName) {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parentId);
        String[] projection = {DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                               DocumentsContract.Document.COLUMN_DISPLAY_NAME};
        try (Cursor cursor = resolver.query(children, projection, null, null, null)) {
            if (cursor == null) return null;
            while (cursor.moveToNext()) {
                if (childName.equalsIgnoreCase(cursor.getString(1)))
                    return cursor.getString(0);
            }
        } catch (Exception ignored) {}
        return null;
    }
}
