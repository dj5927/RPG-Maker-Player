package com.dj5927.rpgmakerplayer;

import android.app.Activity;

import java.io.File;
import java.io.FileInputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;
import java.util.regex.Pattern;

final class RubyRuntimeHeuristics {
    static final class Plan {
        final ArrayList<String> candidates;
        final String diagnostic;
        Plan(ArrayList<String> candidates, String diagnostic) {
            this.candidates = candidates;
            this.diagnostic = diagnostic;
        }
    }

    private static final String[] MODES = {"1.8", "1.9", "3.1"};
    private static final Pattern KWARGS = Pattern.compile("(?m)\\bdef\\s+[A-Za-z_][A-Za-z0-9_!?=]*\\s*\\([^\\n)]*\\*\\*[A-Za-z_]");
    private static final Pattern SAFE_NAV = Pattern.compile("&\\.[A-Za-z_]");
    private static final Pattern KEYWORD_PARAMS = Pattern.compile(
            "(?m)^\\s*def\\s+[^\\n(]+\\([^\\n)]*\\b[A-Za-z_][A-Za-z0-9_]*:\\s*[^:]");

    private RubyRuntimeHeuristics() {}

    static Plan inspect(Activity activity, GameEntry game, File root) {
        Evidence e = new Evidence();
        String engineDefault = game.engine == GameEntry.Engine.VXACE ? "1.9" : "1.8";
        e.add(engineDefault, 12, "engine=" + game.engine.name());

        File archive = findScriptArchive(root);
        if (archive != null) {
            String n = archive.getName().toLowerCase(Locale.ROOT);
            if (n.endsWith(".rvdata2")) e.add("1.9", 45, "archive=rvdata2");
            else if (n.endsWith(".rvdata")) e.add("1.8", 36, "archive=rvdata");
            else if (n.endsWith(".rxdata")) e.add("1.8", 45, "archive=rxdata");
        }

        scanRoot(root, e);
        File scripts = new File(root, "Scripts");
        if (scripts.isDirectory()) {
            e.add("3.1", 8, "external-Scripts");
            scanRubyTree(scripts, e, 256, 4 * 1024 * 1024);
        }
        if (new File(root, "stdlib").isDirectory()) e.add("3.1", 6, "stdlib-dir");

        String cached = RubyRuntimeDetector.cachedRuntime(activity, game);
        if (cached != null) e.add(cached, 70, "deep-cache=" + cached);

        ArrayList<String> order = e.order(engineDefault);
        return new Plan(order, "quick[" + e.summary() + "]->" + join(order));
    }

    static ArrayList<String> rankForDeep(Activity activity, GameEntry game, File root) {
        return inspect(activity, game, root).candidates;
    }

    private static void scanRoot(File root, Evidence e) {
        ArrayList<File> queue = new ArrayList<>();
        queue.add(root);
        int index = 0;
        int visited = 0;
        while (index < queue.size() && visited < 192) {
            File dir = queue.get(index++);
            File[] children = dir.listFiles();
            if (children == null) continue;
            for (File child : children) {
                if (++visited > 192) break;
                if (child.isDirectory()) {
                    if (depth(root, child) <= 2) queue.add(child);
                    continue;
                }
                String n = child.getName().toLowerCase(Locale.ROOT);
                detectName(n, e);
                if (n.equals("game.ini")) inspectText(child, e, "Game.ini", 128 * 1024);
                else if ((n.endsWith(".exe") || n.endsWith(".dll")) && child.length() <= 16L * 1024 * 1024) {
                    inspectText(child, e, child.getName(), 6 * 1024 * 1024);
                }
            }
        }
    }

    private static void detectName(String n, Evidence e) {
        if (n.contains("ruby310") || n.contains("ruby31.") || n.contains("ruby3.1")) e.add("3.1", 150, "ruby31-signature");
        else if (n.contains("ruby193") || n.contains("ruby191") || n.contains("ruby19.") || n.contains("ruby1.9")) e.add("1.9", 150, "ruby19-signature");
        else if (n.contains("ruby187") || n.contains("ruby18.") || n.contains("ruby1.8")) e.add("1.8", 150, "ruby18-signature");
        if (n.matches(".*rgss3\\d*.*\\.dll")) e.add("1.9", 110, "RGSS3-dll");
        else if (n.matches(".*rgss[12]\\d*.*\\.dll")) e.add("1.8", 110, "RGSS1/2-dll");
    }

    private static void inspectText(File f, Evidence e, String source, int maxBytes) {
        try {
            String text = readText(f, maxBytes).toLowerCase(Locale.ROOT);
            if (text.contains("ruby310") || text.contains("ruby31.dll") || text.contains("ruby 3.1")) e.add("3.1", 140, source + ":ruby31");
            if (text.contains("ruby193") || text.contains("ruby191") || text.contains("ruby19.dll")) e.add("1.9", 140, source + ":ruby19");
            if (text.contains("ruby187") || text.contains("ruby18.dll")) e.add("1.8", 140, source + ":ruby18");
            if (text.contains("rgss3")) e.add("1.9", 90, source + ":RGSS3");
            if (text.contains("rgss2") || text.contains("rgss1")) e.add("1.8", 90, source + ":RGSS1/2");
        } catch (Exception ignored) {}
    }

    private static void scanRubyTree(File root, Evidence e, int maxFiles, int maxBytes) {
        ArrayList<File> queue = new ArrayList<>();
        queue.add(root);
        int index = 0, files = 0, bytes = 0;
        while (index < queue.size() && files < maxFiles && bytes < maxBytes) {
            File dir = queue.get(index++);
            File[] children = dir.listFiles();
            if (children == null) continue;
            for (File child : children) {
                if (child.isDirectory()) { queue.add(child); continue; }
                if (!child.getName().toLowerCase(Locale.ROOT).endsWith(".rb")) continue;
                int limit = (int)Math.min(256 * 1024L, Math.max(0, maxBytes - bytes));
                if (limit <= 0) return;
                try {
                    String src = readText(child, limit);
                    bytes += Math.min(limit, (int)child.length());
                    files++;
                    if (KWARGS.matcher(src).find()) e.add("3.1", 80, "syntax=kwargs");
                    if (SAFE_NAV.matcher(src).find()) e.add("3.1", 80, "syntax=safe-nav");
                    if (KEYWORD_PARAMS.matcher(src).find()) e.add("3.1", 120, "syntax=keyword-params");
                    if (src.contains("__dir__")) e.add("3.1", 40, "syntax=__dir__");
                    if (src.contains("frozen_string_literal")) e.add("3.1", 18, "frozen-string");
                    if (src.contains("require_relative") || src.contains(".force_encoding") || src.contains("__ENCODING__") || src.contains("Encoding::")) {
                        e.add("1.9", 28, "syntax=encoding");
                        e.add("3.1", 12, "syntax=encoding");
                    }
                } catch (Exception ignored) {}
            }
        }
    }

    private static String readText(File f, int maxBytes) throws Exception {
        int limit = (int)Math.min(f.length(), maxBytes);
        byte[] data = new byte[Math.max(0, limit)];
        int off = 0;
        try (FileInputStream in = new FileInputStream(f)) {
            while (off < data.length) {
                int n = in.read(data, off, data.length - off);
                if (n < 0) break;
                off += n;
            }
        }
        return new String(data, 0, off, StandardCharsets.ISO_8859_1);
    }

    private static File findScriptArchive(File root) {
        ArrayList<File> q = new ArrayList<>(); q.add(root);
        int i = 0;
        while (i < q.size() && i < 64) {
            File dir = q.get(i++); File[] c = dir.listFiles(); if (c == null) continue;
            for (File f : c) {
                if (f.isDirectory() && depth(root, f) <= 3) q.add(f);
                else if (f.isFile()) {
                    String n = f.getName().toLowerCase(Locale.ROOT);
                    if (n.equals("scripts.rxdata") || n.equals("scripts.rvdata") || n.equals("scripts.rvdata2")) return f;
                }
            }
        }
        return null;
    }

    private static int depth(File root, File file) {
        int d = 0; File p = file;
        while (p != null && !p.equals(root)) { d++; p = p.getParentFile(); }
        return d;
    }

    private static String join(ArrayList<String> values) {
        StringBuilder b = new StringBuilder();
        for (String v : values) { if (b.length() > 0) b.append('>'); b.append(v); }
        return b.toString();
    }

    private static final class Evidence {
        int r18, r19, r31;
        final Set<String> clues = new HashSet<>();
        void add(String r, int p, String clue) {
            if ("1.8".equals(r)) r18 += p; else if ("1.9".equals(r)) r19 += p; else if ("3.1".equals(r)) r31 += p;
            if (clue != null && clues.size() < 16) clues.add(clue);
        }
        int score(String r) { return "1.8".equals(r) ? r18 : "1.9".equals(r) ? r19 : r31; }
        ArrayList<String> order(final String preferred) {
            ArrayList<String> out = new ArrayList<>(); Collections.addAll(out, MODES);
            Collections.sort(out, new Comparator<String>() {
                @Override public int compare(String a, String b) {
                    int s = Integer.compare(score(b), score(a));
                    if (s != 0) return s;
                    if (a.equals(preferred) != b.equals(preferred)) return a.equals(preferred) ? -1 : 1;
                    return a.compareTo(b);
                }
            });
            return out;
        }
        String summary() {
            StringBuilder b = new StringBuilder("s18=").append(r18).append(",s19=").append(r19).append(",s31=").append(r31);
            if (!clues.isEmpty()) { b.append(",clues="); int i=0; for (String c: clues) { if(i++>0)b.append('|'); b.append(c); if(i>=8)break; } }
            return b.toString();
        }
    }
}
