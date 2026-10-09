package com.dj5927.rpgmakerplayer;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

public final class GameLogBatchSmoke {
    public static void main(String[] args) throws Exception {
        Path file = Files.createTempFile("mkxp-console-batch-", ".log");
        try {
            ArrayList<GameLog.LogEntry> batch = new ArrayList<>();
            for (int i = 0; i < 120; i++) {
                batch.add(new GameLog.LogEntry(1791536340123L + i,
                        "JS/LOG", "loadWindowskin Window_Base: Window_basic #" + i));
            }
            GameLog.appendBatch(file.toString(), batch);
            GameLog.appendBatch(file.toString(), new ArrayList<>());
            GameLog.append(file.toString(), "JS/WARNING", "warn");
            List<String> lines = Files.readAllLines(file, StandardCharsets.UTF_8);
            if (lines.size() != 121) throw new AssertionError("count=" + lines.size());
            for (int i = 0; i < 120; i++) {
                if (!lines.get(i).endsWith("Window_basic #" + i)) {
                    throw new AssertionError("unordered #" + i + ": " + lines.get(i));
                }
            }
            if (!lines.get(120).contains("[JS/WARNING] warn")) {
                throw new AssertionError("warning lost");
            }
            System.out.println("GAME_LOG_BATCH_SMOKE_PASS events=121 order=OK warning=OK");
        } finally {
            Files.deleteIfExists(file);
        }
    }
}
