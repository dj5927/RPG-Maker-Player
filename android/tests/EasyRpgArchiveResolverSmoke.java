package com.dj5927.rpgmakerplayer;

import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

public final class EasyRpgArchiveResolverSmoke {
    public static void main(String[] args) throws Exception {
        File dir = Files.createTempDirectory("rpgmp-easyrpg-resolver").toFile();
        try {
            File nested = new File(dir, "nested.easyrpg");
            try (ZipOutputStream out = new ZipOutputStream(new FileOutputStream(nested))) {
                put(out, "RelicsWalker-CrimsonLady-/RPG_RT.ldb");
                put(out, "RelicsWalker-CrimsonLady-/RPG_RT.lmt");
                put(out, "RelicsWalker-CrimsonLady-/Map0001.lmu");
                put(out, "rwmanual/readme.txt");
            }
            String sub = EasyRpgArchiveResolver.findProjectSubpath(nested);
            if (!"RelicsWalker-CrimsonLady-".equals(sub)) {
                throw new AssertionError("nested project root mismatch: " + sub);
            }
            String projectPath = EasyRpgArchiveResolver.projectPath(nested).replace('\\', '/');
            if (!projectPath.endsWith("nested.easyrpg/RelicsWalker-CrimsonLady-")) {
                throw new AssertionError("nested launch path mismatch: " + projectPath);
            }

            File root = new File(dir, "root.zip");
            try (ZipOutputStream out = new ZipOutputStream(new FileOutputStream(root))) {
                put(out, "RPG_RT.ldb");
                put(out, "RPG_RT.lmt");
            }
            if (!EasyRpgArchiveResolver.findProjectSubpath(root).isEmpty()) {
                throw new AssertionError("root project should not append a subpath");
            }

            File easy = new File(dir, "easy.easyrpg");
            try (ZipOutputStream out = new ZipOutputStream(new FileOutputStream(easy))) {
                put(out, "game/EASY_RT.edb");
                put(out, "game/EASY_RT.emt");
                put(out, "manual/info.txt");
            }
            if (!"game".equals(EasyRpgArchiveResolver.findProjectSubpath(easy))) {
                throw new AssertionError("EasyRPG native project pair not detected");
            }
            System.out.println("EASYRPG_ARCHIVE_RESOLVER_SMOKE_PASS");
        } finally {
            delete(dir);
        }
    }

    private static void put(ZipOutputStream out, String name) throws Exception {
        out.putNextEntry(new ZipEntry(name));
        out.write("x".getBytes(StandardCharsets.UTF_8));
        out.closeEntry();
    }

    private static void delete(File file) {
        if (file == null || !file.exists()) return;
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) for (File child : children) delete(child);
        }
        file.delete();
    }
}
