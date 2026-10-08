package com.dj5927.rpgmakerplayer;

import android.content.Context;
import android.net.Uri;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.security.MessageDigest;
import java.util.ArrayDeque;
import java.util.List;

public final class GameMaterializer {
    public interface Progress {
        void onProgress(String message);
    }

    private static final int MAX_FILES = 40000;

    private GameMaterializer() {}

    public static File mirror(Context context, Uri treeUri, GameEntry game, Progress progress) throws Exception {
        File base = context.getExternalFilesDir("game_mirror");
        if (base == null) base = new File(context.getFilesDir(), "game_mirror");
        File targetRoot = new File(base, shortHash(game.stableId()));
        if (!targetRoot.exists() && !targetRoot.mkdirs()) {
            throw new IllegalStateException("Cannot create mirror: " + targetRoot);
        }

        ArrayDeque<DirPair> queue = new ArrayDeque<>();
        queue.add(new DirPair(game.documentId, targetRoot));
        int visitedFiles = 0;
        while (!queue.isEmpty()) {
            DirPair pair = queue.removeFirst();
            List<GameScanner.Child> children = GameScanner.listChildren(context, treeUri, pair.documentId);
            for (GameScanner.Child child : children) {
                if (++visitedFiles > MAX_FILES) throw new IllegalStateException("Too many files in game folder");
                String safeName = safeName(child.name);
                File target = new File(pair.target, safeName);
                if (child.directory) {
                    if (!target.exists() && !target.mkdirs()) throw new IllegalStateException("mkdir failed: " + target);
                    queue.addLast(new DirPair(child.documentId, target));
                    continue;
                }
                if (isCurrent(target, child)) continue;
                if (progress != null && (visitedFiles % 64 == 1)) progress.onProgress("게임 준비 중… " + safeName);
                copy(context, treeUri, child, target);
            }
        }
        return targetRoot;
    }

    public static File materializeEasyRpg(Context context, Uri treeUri, GameEntry game,
                                          Progress progress) throws Exception {
        if (!game.archive) return mirror(context, treeUri, game, progress);

        File base = context.getExternalFilesDir("easyrpg_project_mirror");
        if (base == null) base = new File(context.getFilesDir(), "easyrpg_project_mirror");
        File targetDir = new File(base, shortHash(game.stableId()));
        if (!targetDir.exists() && !targetDir.mkdirs()) {
            throw new IllegalStateException("Cannot create EasyRPG mirror: " + targetDir);
        }

        GameScanner.Child archive = null;
        for (GameScanner.Child child : GameScanner.listChildren(context, treeUri, game.saveRootDocumentId)) {
            if (!child.directory && game.documentId.equals(child.documentId)) {
                archive = child;
                break;
            }
        }
        if (archive == null) {
            throw new IllegalStateException("EasyRPG archive document not found");
        }

        File target = new File(targetDir, safeName(archive.name));
        if (!isCurrent(target, archive)) {
            if (progress != null) progress.onProgress("EasyRPG 게임 준비 중… " + archive.name);
            copy(context, treeUri, archive, target);
        }
        return target;
    }

    public static File mirrorSharedRtp(Context context, Uri treeUri, String rtpDocumentId,
                                       GameEntry.Engine engine, Progress progress) throws Exception {
        if (rtpDocumentId == null || rtpDocumentId.isEmpty()) return null;
        String engineFolder = rtpEngineFolder(engine);
        if (engineFolder == null) return null;

        File base = context.getExternalFilesDir("rtp_mirror");
        if (base == null) base = new File(context.getFilesDir(), "rtp_mirror");
        File targetRoot = new File(base,
                shortHash(treeUri.toString() + ":" + rtpDocumentId + ":" + engineFolder));
        if (!targetRoot.exists() && !targetRoot.mkdirs()) {
            throw new IllegalStateException("Cannot create RTP mirror: " + targetRoot);
        }

        List<GameScanner.Child> roots = GameScanner.listChildren(context, treeUri, rtpDocumentId);
        GameScanner.Child engineDir = findDirectoryIgnoreCase(roots, engineFolder);
        GameScanner.Child fontsDir = findDirectoryIgnoreCase(roots, "fonts");
        int[] visited = new int[]{0};
        if (engineDir != null) {
            mirrorDirectory(context, treeUri, engineDir.documentId,
                    new File(targetRoot, engineFolder), visited, progress, "RTP " + engineFolder);
        }
        if (fontsDir != null) {
            mirrorDirectory(context, treeUri, fontsDir.documentId,
                    new File(targetRoot, "fonts"), visited, progress, "RTP fonts");
        }
        return targetRoot;
    }

    private static void mirrorDirectory(Context context, Uri treeUri, String documentId, File targetRoot,
                                        int[] visited, Progress progress, String label) throws Exception {
        if (!targetRoot.exists() && !targetRoot.mkdirs()) {
            throw new IllegalStateException("mkdir failed: " + targetRoot);
        }
        ArrayDeque<DirPair> queue = new ArrayDeque<>();
        queue.add(new DirPair(documentId, targetRoot));
        while (!queue.isEmpty()) {
            DirPair pair = queue.removeFirst();
            List<GameScanner.Child> children = GameScanner.listChildren(context, treeUri, pair.documentId);
            for (GameScanner.Child child : children) {
                if (++visited[0] > MAX_FILES) {
                    throw new IllegalStateException("Too many files in shared RTP folder");
                }
                String safeName = safeName(child.name);
                File target = new File(pair.target, safeName);
                if (child.directory) {
                    if (!target.exists() && !target.mkdirs()) {
                        throw new IllegalStateException("mkdir failed: " + target);
                    }
                    queue.addLast(new DirPair(child.documentId, target));
                    continue;
                }
                if (isCurrent(target, child)) continue;
                if (progress != null && (visited[0] % 64 == 1)) {
                    progress.onProgress(label + " 준비 중… " + safeName);
                }
                copy(context, treeUri, child, target);
            }
        }
    }

    private static GameScanner.Child findDirectoryIgnoreCase(List<GameScanner.Child> children, String wanted) {
        for (GameScanner.Child child : children) {
            if (child.directory && wanted.equalsIgnoreCase(child.name)) return child;
        }
        return null;
    }

    private static String rtpEngineFolder(GameEntry.Engine engine) {
        if (engine == GameEntry.Engine.XP) return "xp";
        if (engine == GameEntry.Engine.VX) return "vx";
        if (engine == GameEntry.Engine.VXACE) return "vxace";
        return null;
    }

    private static boolean isCurrent(File target, GameScanner.Child child) {
        if (!target.isFile()) return false;
        if (child.size >= 0 && target.length() != child.size) return false;
        if (child.lastModified > 0 && target.lastModified() != child.lastModified) return false;
        return child.size >= 0 || child.lastModified > 0;
    }

    private static void copy(Context context, Uri treeUri, GameScanner.Child child, File target) throws Exception {
        File parent = target.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs()) throw new IllegalStateException("mkdir failed: " + parent);
        File temp = new File(parent, target.getName() + ".part");
        try (InputStream in = context.getContentResolver().openInputStream(GameScanner.documentUri(treeUri, child.documentId));
             FileOutputStream out = new FileOutputStream(temp)) {
            if (in == null) throw new IllegalStateException("open failed: " + child.name);
            byte[] buffer = new byte[256 * 1024];
            int read;
            while ((read = in.read(buffer)) >= 0) out.write(buffer, 0, read);
        }
        if (target.exists() && !target.delete()) throw new IllegalStateException("replace failed: " + target);
        if (!temp.renameTo(target)) throw new IllegalStateException("rename failed: " + target);
        if (child.lastModified > 0) target.setLastModified(child.lastModified);
    }

    private static String safeName(String name) {
        String out = name == null ? "unnamed" : name.replace('/', '_').replace('\\', '_');
        if (out.equals(".") || out.equals("..") || out.isEmpty()) out = "unnamed";
        return out;
    }

    private static String shortHash(String text) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        byte[] bytes = digest.digest(text.getBytes(java.nio.charset.StandardCharsets.UTF_8));
        StringBuilder out = new StringBuilder();
        for (int i = 0; i < 12; i++) out.append(String.format(java.util.Locale.ROOT, "%02x", bytes[i]));
        return out.toString();
    }

    private static final class DirPair {
        final String documentId;
        final File target;
        DirPair(String documentId, File target) {
            this.documentId = documentId;
            this.target = target;
        }
    }
}
