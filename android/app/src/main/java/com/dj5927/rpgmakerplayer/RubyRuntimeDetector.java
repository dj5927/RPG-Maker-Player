package com.dj5927.rpgmakerplayer;

import android.app.Activity;
import android.content.SharedPreferences;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Locale;
import java.util.concurrent.TimeUnit;

public final class RubyRuntimeDetector {
    public interface Progress {
        void onProgress(String message);
    }

    public static final class Result {
        public final String runtime;
        public final String diagnostic;
        Result(String runtime, String diagnostic) {
            this.runtime = runtime;
            this.diagnostic = diagnostic;
        }
    }

    private static final String PREFS = "game_settings";
    private static final int DETECTOR_REV = 3;
    private static final String[] MODES = {"1.8", "1.9", "3.1"};
    private static final String[] PROBES = {
            "libprobe-ruby18.so", "libprobe-ruby19.so", "libprobe-ruby31.so"
    };

    private RubyRuntimeDetector() {}

    public static Result resolveAutomatic(Activity activity, GameEntry game, File local,
                                          boolean forceDeep, Progress progress) throws Exception {
        File archive = findScriptArchive(local);
        String signature = strongRuntimeSignature(local);
        if (archive == null) {
            RubyRuntimeHeuristics.Plan plan = RubyRuntimeHeuristics.inspect(activity, game, local);
            String fallback = signature != null ? signature :
                    (plan.candidates.isEmpty() ? engineDefault(game) : plan.candidates.get(0));
            return new Result(fallback, "no-archive:" + fallback + "+" + plan.diagnostic);
        }

        String hash = sha256(archive);
        SharedPreferences prefs = activity.getSharedPreferences(PREFS, Activity.MODE_PRIVATE);
        String prefix = "ruby_auto_" + Integer.toHexString(game.stableId().hashCode()) + "_";
        if (!forceDeep && prefs.getInt(prefix + "rev", 0) == DETECTOR_REV &&
                hash.equals(prefs.getString(prefix + "hash", ""))) {
            String cached = prefs.getString(prefix + "runtime", "");
            if (isRuntime(cached)) return new Result(cached, "cache:" + cached);
        }

        if (progress != null) progress.onProgress("Ruby 정밀 스캔 · Scripts 아카이브 해제 중…");
        File sections = extractSections(activity, archive);
        int[] codes = new int[]{127, 127, 127};
        try {
            for (int i = 0; i < MODES.length; i++) {
                if (progress != null) {
                    progress.onProgress("Ruby 정밀 스캔 · Ruby " + MODES[i] + " 전체 섹션 검사 중…");
                }
                codes[i] = scanCandidate(activity, PROBES[i], sections);
            }
        } finally {
            deleteTree(sections);
        }

        for (int code : codes) {
            if (code == 124 || code == 127) {
                throw new IllegalStateException("Ruby probe 실행 실패/시간초과: ruby18=" +
                        codes[0] + ", ruby19=" + codes[1] + ", ruby31=" + codes[2]);
            }
        }

        String selected = select(activity, local, codes, signature, game);
        RubyRuntimeHeuristics.Plan heuristic = RubyRuntimeHeuristics.inspect(activity, game, local);
        String diagnostic = "deep[ruby18=" + codeLabel(codes[0]) +
                ",ruby19=" + codeLabel(codes[1]) +
                ",ruby31=" + codeLabel(codes[2]) + "]->" + selected;
        if (signature != null) diagnostic += "+signature:" + signature;
        diagnostic += "+" + heuristic.diagnostic;

        prefs.edit()
                .putInt(prefix + "rev", DETECTOR_REV)
                .putString(prefix + "hash", hash)
                .putString(prefix + "runtime", selected)
                .putString(prefix + "diagnostic", diagnostic)
                .apply();
        appendLog(activity, game, diagnostic);
        return new Result(selected, diagnostic);
    }

    public static String cachedDiagnostic(Activity activity, GameEntry game) {
        String prefix = "ruby_auto_" + Integer.toHexString(game.stableId().hashCode()) + "_";
        SharedPreferences prefs = activity.getSharedPreferences(PREFS, Activity.MODE_PRIVATE);
        if (prefs.getInt(prefix + "rev", 0) != DETECTOR_REV) {
            return "재검사 필요";
        }
        return prefs.getString(prefix + "diagnostic", "아직 정밀 스캔하지 않음");
    }

    public static String cachedRuntime(Activity activity, GameEntry game) {
        String prefix = "ruby_auto_" + Integer.toHexString(game.stableId().hashCode()) + "_";
        SharedPreferences prefs = activity.getSharedPreferences(PREFS, Activity.MODE_PRIVATE);
        if (prefs.getInt(prefix + "rev", 0) != DETECTOR_REV) return null;
        String value = prefs.getString(prefix + "runtime", "");
        return isRuntime(value) ? value : null;
    }

    private static File extractSections(Activity activity, File archive) throws Exception {
        File root = new File(activity.getCacheDir(), "ruby_probe_sections");
        if (!root.exists()) root.mkdirs();
        File out = new File(root, Long.toHexString(System.nanoTime()));
        if (!out.mkdirs()) throw new IllegalStateException("Ruby probe temp 생성 실패");

        int count;
        try {
            count = RubyScriptsArchive.extract(archive, out);
        } catch (Exception e) {
            deleteTree(out);
            throw new IllegalStateException("Scripts 아카이브 해제 실패: " + e.getMessage(), e);
        }
        if (count <= 0) throw new IllegalStateException("Scripts 섹션 없음");
        return out;
    }

    private static int scanCandidate(Activity activity, String probeName, File sections) throws Exception {
        File probe = new File(activity.getApplicationInfo().nativeLibraryDir, probeName);
        if (!probe.isFile()) return 127;
        String shell = "ruby=\"$1\"; dir=\"$2\"; count=0; " +
                "for f in \"$dir\"/*.rb; do count=$((count+1)); \"$ruby\" -c \"$f\" >/dev/null 2>&1 || { echo FAIL:$f; exit 2; }; done; " +
                "echo PASS:$count; exit 0";
        ProcessBuilder pb = new ProcessBuilder("/system/bin/sh", "-c", shell, "rpgmp-scan",
                probe.getAbsolutePath(), sections.getAbsolutePath());
        pb.redirectErrorStream(true);
        pb.environment().put("TMPDIR", activity.getCacheDir().getAbsolutePath());
        Process process = pb.start();
        if (!process.waitFor(120, TimeUnit.SECONDS)) {
            process.destroyForcibly();
            return 124;
        }
        String output = readOutput(process);
        int code = process.exitValue();
        if (code != 0 && !output.isEmpty()) android.util.Log.w("RPGMP-RUBY", probeName + " " + output);
        return code;
    }

    private static String select(Activity activity, File local, int[] codes,
                                 String signature, GameEntry game) {
        if (signature != null) {
            int si = "1.8".equals(signature) ? 0 : "1.9".equals(signature) ? 1 : 2;
            if (codes[si] == 0) return signature;
        }
        ArrayList<String> ranked = RubyRuntimeHeuristics.rankForDeep(activity, game, local);
        for (String runtime : ranked) {
            int i = "1.8".equals(runtime) ? 0 : "1.9".equals(runtime) ? 1 : 2;
            if (codes[i] == 0) return runtime;
        }
        for (int i = 0; i < MODES.length; i++) {
            if (codes[i] == 0) return MODES[i];
        }
        return engineDefault(game);
    }

    private static String engineDefault(GameEntry game) {
        return game.engine == GameEntry.Engine.VXACE ? "1.9" : "1.8";
    }

    private static String strongRuntimeSignature(File root) {
        File[] files = root.listFiles();
        if (files == null) return null;
        for (File file : files) {
            if (!file.isFile()) continue;
            String name = file.getName().toLowerCase(Locale.ROOT);
            if (name.contains("ruby310.dll") || name.contains("ruby31.dll") || name.contains("ruby3.1")) return "3.1";
            if (name.contains("ruby193.dll") || name.contains("ruby191.dll") || name.contains("ruby19.dll") || name.contains("ruby1.9")) return "1.9";
            if (name.contains("ruby187.dll") || name.contains("ruby18.dll") || name.contains("ruby1.8")) return "1.8";
        }
        return null;
    }

    private static File findScriptArchive(File root) {
        ArrayList<File> queue = new ArrayList<>();
        queue.add(root);
        int index = 0;
        while (index < queue.size() && index < 64) {
            File dir = queue.get(index++);
            File[] children = dir.listFiles();
            if (children == null) continue;
            for (File child : children) {
                if (child.isDirectory() && depth(root, child) <= 3) queue.add(child);
                else if (child.isFile()) {
                    String n = child.getName().toLowerCase(Locale.ROOT);
                    if (n.equals("scripts.rxdata") || n.equals("scripts.rvdata") || n.equals("scripts.rvdata2")) return child;
                }
            }
        }
        return null;
    }

    private static int depth(File root, File file) {
        int depth = 0;
        File p = file;
        while (p != null && !p.equals(root)) { depth++; p = p.getParentFile(); }
        return depth;
    }

    private static boolean isRuntime(String value) {
        return "1.8".equals(value) || "1.9".equals(value) || "3.1".equals(value);
    }

    private static String codeLabel(int code) { return code == 0 ? "ok" : String.valueOf(code); }

    private static String readOutput(Process process) throws Exception {
        StringBuilder out = new StringBuilder();
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(
                process.getInputStream(), StandardCharsets.UTF_8))) {
            String line;
            while ((line = reader.readLine()) != null) {
                if (out.length() < 4096) out.append(line).append('\n');
            }
        }
        return out.toString().trim();
    }

    private static String sha256(File file) throws Exception {
        MessageDigest md = MessageDigest.getInstance("SHA-256");
        try (FileInputStream in = new FileInputStream(file)) {
            byte[] buffer = new byte[64 * 1024];
            int read;
            while ((read = in.read(buffer)) >= 0) md.update(buffer, 0, read);
        }
        StringBuilder out = new StringBuilder();
        for (byte b : md.digest()) out.append(String.format(Locale.ROOT, "%02x", b & 0xff));
        return out.toString();
    }

    private static void appendLog(Activity activity, GameEntry game, String text) {
        try {
            File dir = new File(activity.getFilesDir(), "logs");
            if (!dir.exists()) dir.mkdirs();
            File file = new File(dir, "ruby_probe.log");
            try (FileOutputStream out = new FileOutputStream(file, true)) {
                out.write((game.stableId() + " " + text + "\n").getBytes(StandardCharsets.UTF_8));
            }
        } catch (Exception ignored) {}
    }

    private static void deleteTree(File file) {
        if (file == null || !file.exists()) return;
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) for (File child : children) deleteTree(child);
        }
        file.delete();
    }
}