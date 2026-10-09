package com.dj5927.rpgmakerplayer;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.SequenceInputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.function.Consumer;

/**
 * Per-game, opt-in MV/MZ compatibility patches. No game files are rewritten;
 * patching happens in memory before a WebView response is returned.
 *
 * Schema is shared with SteamOS runtime/mvmz/compat_patch_loader.py.
 */
final class MvMzGamePatches {
    private static final int MAX_MANIFEST_BYTES = 256 * 1024;
    private static final int MAX_SCRIPT_BYTES = 12 * 1024 * 1024;
    private final Map<String, List<Patch>> byFile;

    private static final class Patch {
        final String id;
        final String find;
        final String replace;
        final String sha256;

        Patch(String id, String find, String replace, String sha256) {
            this.id = id;
            this.find = find;
            this.replace = replace;
            this.sha256 = sha256;
        }
    }

    private MvMzGamePatches(Map<String, List<Patch>> patches) {
        this.byFile = patches;
    }

    static MvMzGamePatches empty() {
        return new MvMzGamePatches(new HashMap<>());
    }

    boolean has(String requestPath) {
        return byFile.containsKey(normalizeRequestPath(requestPath));
    }

    static MvMzGamePatches load(InputStream manifest, String engine,
                                Consumer<String> report) {
        if (manifest == null || (!"MV".equals(engine) && !"MZ".equals(engine))) {
            return empty();
        }
        try (InputStream input = manifest) {
            byte[] bytes = readUpTo(input, MAX_MANIFEST_BYTES);
            if (bytes == null) throw new IllegalArgumentException("manifest too large");
            String json = new String(bytes, StandardCharsets.UTF_8);
            if (json.startsWith("\ufeff")) json = json.substring(1);
            JSONObject root = new JSONObject(json);
            if (root.optInt("schema", -1) != 1) {
                throw new IllegalArgumentException("requires schema=1");
            }
            if (!(root.opt("enabled") instanceof Boolean) ||
                    !root.optBoolean("enabled", false)) {
                report.accept("manifest disabled");
                return empty();
            }
            JSONArray entries = root.optJSONArray("patches");
            if (entries == null || entries.length() > 100) {
                throw new IllegalArgumentException("patches array missing or too large");
            }
            Map<String, List<Patch>> byFile = new HashMap<>();
            Set<String> ids = new HashSet<>();
            for (int i = 0; i < entries.length(); ++i) {
                JSONObject row = entries.getJSONObject(i);
                String id = row.getString("id");
                String path = row.getString("file");
                String targetEngine = row.getString("engine");
                if (!id.matches("[a-zA-Z0-9._-]{1,70}") || !ids.add(id)) {
                    throw new IllegalArgumentException("invalid or duplicate id: " + id);
                }
                if (!validFile(path) || !("MV".equals(targetEngine) || "MZ".equals(targetEngine))) {
                    throw new IllegalArgumentException("invalid engine or file: " + id);
                }
                if (row.has("enabled") && !(row.opt("enabled") instanceof Boolean)) {
                    throw new IllegalArgumentException("enabled must be boolean: " + id);
                }
                if (!row.optBoolean("enabled", true)) continue;
                if (!engine.equals(targetEngine)) {
                    report.accept("engine-skip id=" + id);
                    continue;
                }
                String find = row.getString("find");
                String replacement = row.getString("replace");
                int count = row.optInt("expected_matches", 1);
                if (find.isEmpty() || find.length() > 8192 ||
                        replacement.length() > 32768 || count != 1 ||
                        (row.has("expected_matches") && !(row.opt("expected_matches") instanceof Integer))) {
                    throw new IllegalArgumentException("unsafe replacement: " + id);
                }
                String hash = row.optString("sha256_before", "");
                if (!hash.isEmpty() && !hash.matches("[0-9a-fA-F]{64}")) {
                    throw new IllegalArgumentException("invalid sha256_before: " + id);
                }
                String key = "/" + path;
                if (!byFile.containsKey(key)) byFile.put(key, new ArrayList<>());
                byFile.get(key).add(new Patch(id, find, replacement, hash));
            }
            report.accept("loaded rules=" + ids.size() + " applicableFiles=" + byFile.size());
            return new MvMzGamePatches(byFile);
        } catch (Exception error) {
            report.accept("rejected: " + error.getMessage());
            return empty(); // Fails closed: a bad manifest never breaks game startup.
        }
    }

    InputStream apply(String requestPath, InputStream source, Consumer<String> report)
            throws Exception {
        List<Patch> patches = byFile.get(normalizeRequestPath(requestPath));
        if (patches == null || patches.isEmpty()) return source;
        // Preserve the original bytes, even for an over-limit script. The
        // remainder is streamed directly; the WebView can still load it.
        ByteArrayOutputStream buffer = new ByteArrayOutputStream();
        byte[] part = new byte[16384];
        int read;
        while ((read = source.read(part)) >= 0) {
            buffer.write(part, 0, read);
            if (buffer.size() > MAX_SCRIPT_BYTES) {
                report.accept("oversize-skip file=" + requestPath);
                return new SequenceInputStream(
                        new ByteArrayInputStream(buffer.toByteArray()), source);
            }
        }
        byte[] original = buffer.toByteArray();
        source.close();
        String input = new String(original, StandardCharsets.UTF_8);
        if (!Arrays.equals(input.getBytes(StandardCharsets.UTF_8), original)) {
            report.accept("encoding-skip file=" + requestPath);
            return new ByteArrayInputStream(original);
        }
        String output = input;
        String originalSha = null;
        for (Patch patch : patches) {
            if (!patch.sha256.isEmpty()) {
                if (originalSha == null) originalSha = sha256(original);
                if (!originalSha.equalsIgnoreCase(patch.sha256)) {
                    report.accept("hash-mismatch id=" + patch.id);
                    continue;
                }
            }
            int first = output.indexOf(patch.find);
            if (first < 0 || output.indexOf(patch.find, first + patch.find.length()) >= 0) {
                report.accept("match-skip id=" + patch.id);
                continue;
            }
            output = output.substring(0, first) + patch.replace +
                    output.substring(first + patch.find.length());
            report.accept("applied id=" + patch.id + " file=" + requestPath);
        }
        return new ByteArrayInputStream(
                (output.equals(input) ? original : output.getBytes(StandardCharsets.UTF_8)));
    }

    private static String normalizeRequestPath(String path) {
        return path == null ? "" : path;
    }

    private static boolean validFile(String path) {
        if (path == null || path.length() > 250 || path.startsWith("/") ||
                path.contains("\\") || path.contains("//")) return false;
        for (String part : path.split("/", -1)) {
            if (part.isEmpty() || part.equals(".") || part.equals("..")) return false;
        }
        return path.equals("index.html") || (path.startsWith("js/") && path.endsWith(".js"));
    }

    private static byte[] readUpTo(InputStream input, int limit) throws Exception {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buf = new byte[16384];
        int n;
        while ((n = input.read(buf)) >= 0) {
            if (out.size() + n > limit) return null;
            out.write(buf, 0, n);
        }
        return out.toByteArray();
    }

    private static String sha256(byte[] bytes) throws Exception {
        MessageDigest md = MessageDigest.getInstance("SHA-256");
        byte[] hash = md.digest(bytes);
        StringBuilder sb = new StringBuilder(64);
        for (byte b : hash) sb.append(String.format(Locale.ROOT, "%02x", b & 0xff));
        return sb.toString();
    }
}
