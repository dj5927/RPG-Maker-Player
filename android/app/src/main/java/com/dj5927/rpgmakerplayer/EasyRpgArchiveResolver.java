package com.dj5927.rpgmakerplayer;

import java.io.File;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Enumeration;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

final class EasyRpgArchiveResolver {
    private static final int HAS_LDB = 1;
    private static final int HAS_LMT = 2;
    private static final int HAS_EDB = 4;
    private static final int HAS_EMT = 8;

    private EasyRpgArchiveResolver() {}

    static String findProjectSubpath(File archive) {
        if (archive == null || !archive.isFile()) return "";
        Map<String, Integer> flags = new HashMap<>();
        try (ZipFile zip = new ZipFile(archive)) {
            Enumeration<? extends ZipEntry> entries = zip.entries();
            while (entries.hasMoreElements()) {
                ZipEntry entry = entries.nextElement();
                if (entry == null || entry.isDirectory()) continue;
                String name = normalize(entry.getName());
                if (name.isEmpty()) continue;
                int slash = name.lastIndexOf('/');
                String dir = slash < 0 ? "" : name.substring(0, slash);
                String leaf = (slash < 0 ? name : name.substring(slash + 1)).toLowerCase(Locale.ROOT);
                int bit = 0;
                if ("rpg_rt.ldb".equals(leaf)) bit = HAS_LDB;
                else if ("rpg_rt.lmt".equals(leaf)) bit = HAS_LMT;
                else if ("easy_rt.edb".equals(leaf)) bit = HAS_EDB;
                else if ("easy_rt.emt".equals(leaf)) bit = HAS_EMT;
                if (bit != 0) flags.put(dir, flags.getOrDefault(dir, 0) | bit);
            }
        } catch (Exception ignored) {
            return "";
        }

        ArrayList<String> candidates = new ArrayList<>();
        for (Map.Entry<String, Integer> entry : flags.entrySet()) {
            int value = entry.getValue();
            boolean classic = (value & (HAS_LDB | HAS_LMT)) == (HAS_LDB | HAS_LMT);
            boolean easyrpg = (value & (HAS_EDB | HAS_EMT)) == (HAS_EDB | HAS_EMT);
            if (classic || easyrpg) candidates.add(entry.getKey());
        }
        if (candidates.isEmpty()) return "";

        Collections.sort(candidates, (a, b) -> {
            int da = depth(a);
            int db = depth(b);
            if (da != db) return Integer.compare(da, db);
            if (a.length() != b.length()) return Integer.compare(a.length(), b.length());
            return a.compareToIgnoreCase(b);
        });
        return candidates.get(0);
    }

    static String projectPath(File archive) {
        String subpath = findProjectSubpath(archive);
        if (subpath.isEmpty()) return archive.getAbsolutePath();
        return archive.getAbsolutePath() + "/" + subpath;
    }

    private static int depth(String path) {
        if (path == null || path.isEmpty()) return 0;
        int depth = 1;
        for (int i = 0; i < path.length(); i++) if (path.charAt(i) == '/') depth++;
        return depth;
    }

    private static String normalize(String path) {
        if (path == null) return "";
        String out = path.replace('\\', '/');
        while (out.startsWith("/")) out = out.substring(1);
        while (out.endsWith("/") && !out.isEmpty()) out = out.substring(0, out.length() - 1);
        return out;
    }
}
