package com.dj5927.rpgmakerplayer;

import java.io.File;
import java.io.FileWriter;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.List;

final class GameLog {
    private static final long MAX_BYTES = 4L * 1024L * 1024L;
    private static final Object LOCK = new Object();

    private GameLog() {}

    static final class LogEntry {
        final long timestampMs;
        final String tag;
        final String message;
        LogEntry(long timestampMs, String tag, String message) {
            this.timestampMs = timestampMs;
            this.tag = tag;
            this.message = message;
        }
    }

    static void appendBatch(String path, List<LogEntry> entries) {
        if (path == null || path.isEmpty() || entries == null || entries.isEmpty()) return;
        synchronized (LOCK) {
            try (FileWriter out = new FileWriter(path, true)) {
                SimpleDateFormat format = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.US);
                for (LogEntry entry : entries) {
                    out.write(format.format(new Date(entry.timestampMs)) +
                            " [" + entry.tag + "] " + sanitize(entry.message) + "\n");
                }
            } catch (Throwable ignored) {}
        }
    }

    static String prepare(File gameDir, String title, String engine) {
        if (gameDir == null || !gameDir.isDirectory()) return null;
        try {
            File root = gameDir.getParentFile();
            if (root == null || !root.isDirectory()) root = gameDir;
            File dir = new File(root, "_log");
            if (!dir.isDirectory() && !dir.mkdirs()) return null;
            File log = new File(dir, safeName(title) + ".log");
            rotateIfNeeded(log);
            append(log.getAbsolutePath(), "START",
                    "engine=" + engine + " title=" + title +
                            " gameDir=" + gameDir.getAbsolutePath() +
                            " logRoot=" + dir.getAbsolutePath());
            return log.getAbsolutePath();
        } catch (Throwable ignored) {
            return null;
        }
    }

    static void append(String path, String tag, String message) {
        if (path == null || path.isEmpty()) return;
        synchronized (LOCK) {
            try (FileWriter out = new FileWriter(path, true)) {
                String time = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS",
                        Locale.US).format(new Date());
                out.write(time + " [" + tag + "] " + sanitize(message) + "\n");
            } catch (Throwable ignored) {}
        }
    }

    private static void rotateIfNeeded(File log) {
        if (!log.isFile() || log.length() < MAX_BYTES) return;
        File old = new File(log.getParentFile(), log.getName() + ".old");
        if (old.exists()) old.delete();
        log.renameTo(old);
    }

    private static String safeName(String value) {
        String s = value == null || value.trim().isEmpty() ? "game" : value.trim();
        s = s.replaceAll("[\\\\/:*?\"<>|]", "_");
        return s.length() > 80 ? s.substring(0, 80) : s;
    }

    private static String sanitize(String value) {
        if (value == null) return "";
        return value.replace("\r", "\\r").replace("\n", "\\n");
    }
}
