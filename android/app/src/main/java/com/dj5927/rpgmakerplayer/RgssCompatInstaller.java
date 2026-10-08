package com.dj5927.rpgmakerplayer;

import android.content.Context;
import android.content.res.AssetManager;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

public final class RgssCompatInstaller {
    private static final String ASSET_ROOT = "rgss_compat";
    private static final String RUBY31_STDLIB_REV = "a095";

    private RgssCompatInstaller() {}

    public static File prepareRuntime(Context context, File gameDir) throws Exception {
        String key;
        try {
            key = Integer.toHexString(gameDir.getCanonicalPath().hashCode());
        } catch (Exception e) {
            key = Integer.toHexString(gameDir.getAbsolutePath().hashCode());
        }
        File base = new File(context.getFilesDir(), "mkxp_compat/" + key);
        install(context, base);
        cleanupGeneratedGameCompat(gameDir);
        return base;
    }

    public static void install(Context context, File gameMirror) throws Exception {
        File compat = new File(gameMirror, "compat");
        if (!compat.exists() && !compat.mkdirs()) {
            throw new IllegalStateException("compat mkdir failed: " + compat);
        }
        copyTree(context.getAssets(), ASSET_ROOT + "/common", new File(compat, "common"));
        copyTree(context.getAssets(), ASSET_ROOT + "/cicpoffs", new File(compat, "cicpoffs"));
        File ruby31 = new File(compat, "ruby31lib");
        File ruby31Marker = new File(ruby31, ".rpgmp_" + RUBY31_STDLIB_REV);
        if (!ruby31Marker.isFile()) {
            deleteTree(ruby31);
            copyTree(context.getAssets(), ASSET_ROOT + "/ruby31lib", ruby31);
            try (FileOutputStream out = new FileOutputStream(ruby31Marker, false)) {
                out.write((RUBY31_STDLIB_REV + "\n").getBytes(StandardCharsets.US_ASCII));
            }
        }
        copyOne(context.getAssets(), ASSET_ROOT + "/cicpoffs_LICENSE.txt",
                new File(compat, "cicpoffs_LICENSE.txt"));
    }

    public static void writeMouseMode(File gameMirror, String mode) throws Exception {
        File compat = new File(gameMirror, "compat");
        if (!compat.exists() && !compat.mkdirs()) {
            throw new IllegalStateException("compat mkdir failed: " + compat);
        }
        String safe = "PATCH".equals(mode) || "PASS".equals(mode) || "ORIGINAL".equals(mode)
                ? mode : "AUTO";
        try (FileOutputStream out = new FileOutputStream(new File(compat, "mouse_mode.txt"), false)) {
            out.write((safe + "\n").getBytes(StandardCharsets.US_ASCII));
        }
    }

    private static void cleanupGeneratedGameCompat(File gameDir) {
        File compat = new File(gameDir, "compat");
        File markerA = new File(compat, "cicpoffs_LICENSE.txt");
        File markerB = new File(compat, "common/cicpoffs_compat.rb");
        if (!markerA.isFile() || !markerB.isFile()) return;
        deleteTree(compat);
    }

    private static void deleteTree(File file) {
        if (file == null || !file.exists()) return;
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) {
                for (File child : children) deleteTree(child);
            }
        }
        file.delete();
    }

    private static void copyTree(AssetManager assets, String assetPath, File target) throws Exception {
        String[] children = assets.list(assetPath);
        if (children == null || children.length == 0) {
            copyOne(assets, assetPath, target);
            return;
        }
        if (!target.exists() && !target.mkdirs()) {
            throw new IllegalStateException("mkdir failed: " + target);
        }
        for (String child : children) {
            copyTree(assets, assetPath + "/" + child, new File(target, child));
        }
    }

    private static void copyOne(AssetManager assets, String assetPath, File target) throws Exception {
        File parent = target.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs()) {
            throw new IllegalStateException("mkdir failed: " + parent);
        }
        try (InputStream in = assets.open(assetPath);
             FileOutputStream out = new FileOutputStream(target, false)) {
            byte[] buffer = new byte[64 * 1024];
            int read;
            while ((read = in.read(buffer)) >= 0) out.write(buffer, 0, read);
        }
    }
}
