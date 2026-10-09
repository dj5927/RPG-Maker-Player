package com.dj5927.rpgmakerplayer;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Build;
import android.os.SystemClock;

import java.io.File;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;

public final class RunnerCoordinator {
    private static final String GAME_PREFS = "game_settings";
    private static final long RUBY_FALLBACK_POLL_MS = 120L;
    private static final long RUBY_FALLBACK_STARTUP_TIMEOUT_MS = 90000L;
    private static volatile RubyFallbackSession activeRubyFallback;
    public interface Callback {
        void onStatus(String message);
        void onError(String message);
        void onStarted();
    }

    public interface RubyScanCallback {
        void onStatus(String message);
        void onResult(String runtime, String diagnostic);
        void onError(String message);
    }

    public interface MvMzScanCallback {
        void onStatus(String message);
        void onResult(String diagnostic, String recommendedRenderer);
        void onError(String message);
    }

    private RunnerCoordinator() {}

    private static final class RubyFallbackSession {
        final GameEntry game;
        final File local;
        final File sharedRtp;
        final File compatRoot;
        final ArrayList<String> candidates;
        final String diagnostic;
        final String token;
        int index;

        RubyFallbackSession(GameEntry game, File local, File sharedRtp, File compatRoot,
                            ArrayList<String> candidates, String diagnostic) {
            this.game = game;
            this.local = local;
            this.sharedRtp = sharedRtp;
            this.compatRoot = compatRoot;
            this.candidates = candidates;
            this.diagnostic = diagnostic;
            this.token = Integer.toHexString(game.stableId().hashCode()) + "-" +
                    Long.toHexString(System.nanoTime());
        }
    }

    public static boolean hasActiveRubyFallback() {
        return activeRubyFallback != null;
    }

    public static void launch(Activity activity, ExecutorService worker, Uri treeUri,
                              GameEntry game, Callback callback) {
        if (game.engine == GameEntry.Engine.MV || game.engine == GameEntry.Engine.MZ) {
            File rawGameDir = DirectStorageResolver.resolveGameDirectory(treeUri, game);
            String gameLogPath = GameLog.prepare(rawGameDir, game.title, game.engine.name());
            Intent intent = new Intent(activity, MvMzPlayerActivity.class);
            intent.putExtra(MvMzPlayerActivity.EXTRA_TREE_URI, treeUri.toString());
            intent.putExtra(MvMzPlayerActivity.EXTRA_WEB_ROOT_ID, game.webRootDocumentId);
            intent.putExtra(MvMzPlayerActivity.EXTRA_SAVE_ROOT_ID, game.saveRootDocumentId);
            intent.putExtra(MvMzPlayerActivity.EXTRA_TITLE, game.title);
            File mkxpFolder = rawGameDir;
            while (mkxpFolder != null && !"mkxp".equalsIgnoreCase(mkxpFolder.getName())) {
                mkxpFolder = mkxpFolder.getParentFile();
            }
            if (mkxpFolder != null) {
                intent.putExtra(MvMzPlayerActivity.EXTRA_PATCH_DB_PATH,
                        new File(mkxpFolder, "_compat/patches.json").getAbsolutePath());
            }
            intent.putExtra(MvMzPlayerActivity.EXTRA_ENGINE, game.engine.name());
            intent.putExtra(MvMzPlayerActivity.EXTRA_PAD_ENABLED, padEnabled(activity, game));
            intent.putExtra(MvMzPlayerActivity.EXTRA_PAD_LARGE, padLarge(activity, game));
            intent.putExtra(MvMzPlayerActivity.EXTRA_KEYMAP, GameKeyMap.load(activity, game));
            intent.putExtra(MvMzPlayerActivity.EXTRA_RENDER_MODE, mvRenderMode(activity, game));
            intent.putExtra(MvMzPlayerActivity.EXTRA_MOUSE_MODE, mvMouseMode(activity, game));
            intent.putExtra(MvMzPlayerActivity.EXTRA_LOCALE, GameLocaleSettings.effective(activity, game));
            intent.putExtra(MvMzPlayerActivity.EXTRA_LOG_PATH, gameLogPath);
            activity.startActivity(intent);
            callback.onStarted();
            return;
        }

        if (game.engine == GameEntry.Engine.EASYRPG) {
            callback.onStatus("EasyRPG libretro core 준비 중…");
            worker.submit(() -> {
                try {
                    File project = GameMaterializer.materializeEasyRpg(activity, treeUri, game,
                            message -> activity.runOnUiThread(() -> callback.onStatus(message)));
                    String archiveSubpath = game.archive
                            ? EasyRpgArchiveResolver.findProjectSubpath(project) : "";
                    String projectPath = game.archive
                            ? EasyRpgArchiveResolver.projectPath(project)
                            : project.getAbsolutePath();

                    File config = new File(activity.getExternalFilesDir(null), "easyrpg");
                    File legacySaves = new File(config, "saves/" + safeId(game));
                    File runtimeSaveDir;
                    if (game.archive) {
                        File archiveSave = new File(project.getParentFile(), project.getName() + ".save");
                        runtimeSaveDir = archiveSubpath.isEmpty()
                                ? archiveSave
                                : new File(archiveSave,
                                        archiveSubpath.replace('/', File.separatorChar));
                    } else {
                        runtimeSaveDir = project;
                    }
                    PersistentSaveManager.prepareEasyRpgLibretro(
                            activity, treeUri, game, runtimeSaveDir, legacySaves);

                    File systemDir = EasyRpgCoreSettings.prepareSystemDirectory(
                            activity, treeUri, game);

                    Intent intent = new Intent(activity, LibretroEasyRpgActivity.class);
                    intent.putExtra(LibretroEasyRpgActivity.EXTRA_GAME_PATH, projectPath);
                    intent.putExtra(LibretroEasyRpgActivity.EXTRA_SAVE_DIR,
                            runtimeSaveDir.getAbsolutePath());
                    intent.putExtra(LibretroEasyRpgActivity.EXTRA_SYSTEM_DIR,
                            systemDir.getAbsolutePath());
                    intent.putExtra(LibretroEasyRpgActivity.EXTRA_PAD_ENABLED,
                            padEnabled(activity, game));
                    intent.putExtra(LibretroEasyRpgActivity.EXTRA_PAD_LARGE,
                            padLarge(activity, game));
                    intent.putExtra(LibretroEasyRpgActivity.EXTRA_KEYMAP,
                            GameKeyMap.load(activity, game));
                    intent.putExtra(LibretroEasyRpgActivity.EXTRA_LOCALE,
                            GameLocaleSettings.effective(activity, game));
                    activity.runOnUiThread(() -> {
                        activity.startActivity(intent);
                        activity.overridePendingTransition(0, 0);
                        callback.onStarted();
                    });
                } catch (Exception e) {
                    activity.runOnUiThread(() -> callback.onError(
                            "EasyRPG core 실행 준비 실패: " + e.getClass().getSimpleName() +
                                    ": " + String.valueOf(e.getMessage())));
                }
            });
            return;
        }

        if (!supportsArm64()) {
            callback.onError("XP/VX/VX Ace용 Android MKXP는 현재 ARM64(arm64-v8a) 기기에서만 지원합니다.");
            return;
        }

        callback.onStatus("Android MKXP 실행 경로 확인 중…");
        worker.submit(() -> {
            try {
                File local = DirectStorageResolver.resolveGameDirectory(treeUri, game);
                File sharedRtp = DirectStorageResolver.resolveSharedRtpDirectory(treeUri, game);
                boolean direct = local != null;
                if (direct) {
                    activity.runOnUiThread(() -> callback.onStatus("게임 실행 준비중"));
                } else {
                    activity.runOnUiThread(() -> callback.onStatus("게임 실행 준비중"));
                    local = GameMaterializer.mirror(activity, treeUri, game,
                            message -> activity.runOnUiThread(() -> callback.onStatus(message)));
                    sharedRtp = GameMaterializer.mirrorSharedRtp(activity, treeUri,
                            game.sharedRtpDocumentId, game.engine,
                            message -> activity.runOnUiThread(() -> callback.onStatus(message)));
                }
                File compatRoot = RgssCompatInstaller.prepareRuntime(activity, local);
                RgssCompatInstaller.writeMouseMode(compatRoot, rgssMouseMode(activity, game));
                if (!direct) PersistentSaveManager.prepareRgss(activity, treeUri, game, local);
                String configuredRuby = rubyMode(activity, game);
                if ("AUTO".equals(configuredRuby)) {
                    RubyRuntimeHeuristics.Plan plan =
                            RubyRuntimeHeuristics.inspect(activity, game, local);
                    RubyFallbackSession session = new RubyFallbackSession(
                            game, local, sharedRtp, compatRoot,
                            new ArrayList<>(plan.candidates), plan.diagnostic);
                    activeRubyFallback = session;
                    activity.runOnUiThread(() -> callback.onStatus(
                            "Ruby AUTO 판별 · " + session.candidates.get(0) +
                                    " · " + plan.diagnostic));
                    launchAutoRubyAttempt(activity, worker, session, callback);
                    return;
                }
                String launchRuby = configuredRuby;
                File launchLocal = local;
                File launchSharedRtp = sharedRtp;
                File launchCompatRoot = compatRoot;
                activity.runOnUiThread(() -> {
                    try {
                        launchMkxp(activity, game, launchLocal, launchSharedRtp,
                                launchCompatRoot, launchRuby);
                        callback.onStarted();
                    } catch (Exception e) {
                        callback.onError("MKXP 실행 준비 실패: " + e.getClass().getSimpleName());
                    }
                });
            } catch (Exception e) {
                activity.runOnUiThread(() -> callback.onError("MKXP 실행 경로 준비 실패: " + e.getMessage()));
            }
        });
    }

    private static void launchAutoRubyAttempt(Activity activity, ExecutorService worker,
                                              RubyFallbackSession session, Callback callback) {
        if (session.index < 0 || session.index >= session.candidates.size()) {
            activeRubyFallback = null;
            activity.runOnUiThread(() -> callback.onError(
                    "Ruby 1.8 / 1.9 / 3.1 모두 초기 구동에 실패했습니다.\n" +
                            session.diagnostic));
            return;
        }
        String ruby = session.candidates.get(session.index);
        int attemptNumber = session.index + 1;
        File failMarker = rubyFailureMarker(activity, session.token, session.index);
        File successMarker = rubySuccessMarker(activity, session.token, session.index);
        if (failMarker.isFile()) failMarker.delete();
        if (successMarker.isFile()) successMarker.delete();
        activity.runOnUiThread(() -> {
            try {
                callback.onStatus("Ruby AUTO 실행 " + attemptNumber + "/3 · Ruby " + ruby);
                launchMkxp(activity, session.game, session.local, session.sharedRtp,
                        session.compatRoot, ruby, true, session.token, session.index,
                        failMarker.getAbsolutePath(), successMarker.getAbsolutePath());
                callback.onStarted();
            } catch (Exception e) {
                session.index++;
                launchAutoRubyAttempt(activity, worker, session, callback);
            }
        });
        worker.submit(() -> monitorAutoRubyAttempt(activity, worker, session, callback, ruby));
    }

    private static void monitorAutoRubyAttempt(Activity activity, ExecutorService worker,
                                               RubyFallbackSession session, Callback callback,
                                               String ruby) {
        long started = SystemClock.elapsedRealtime();
        boolean seen = false;
        String processName = activity.getPackageName() + ":mkxp";
        File failMarker = rubyFailureMarker(activity, session.token, session.index);
        File successMarker = rubySuccessMarker(activity, session.token, session.index);
        try {
            while (SystemClock.elapsedRealtime() - started < RUBY_FALLBACK_STARTUP_TIMEOUT_MS) {
                if (failMarker.isFile()) break;
                if (successMarker.isFile()) break;
                int pid = findOwnProcessPid(activity, processName);
                if (pid > 0) seen = true;
                else if (seen) break;
                Thread.sleep(RUBY_FALLBACK_POLL_MS);
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            activeRubyFallback = null;
            return;
        }

        RubyFallbackSession current = activeRubyFallback;
        if (current != session) return;
        if (isUserExitMarker(activity, session.token)) {
            activeRubyFallback = null;
            return;
        }
        long elapsed = SystemClock.elapsedRealtime() - started;
        int activePid = findOwnProcessPid(activity, processName);
        boolean alive = activePid > 0;
        boolean failedMarker = failMarker.isFile();
        boolean successMarkerSeen = successMarker.isFile();
        if (!failedMarker && successMarkerSeen && alive) {
            saveRubyFallbackRuntime(activity, session.game, ruby);
            activity.runOnUiThread(() -> callback.onStatus(
                    "Ruby AUTO 확정 · 첫 화면 진입 확인 · Ruby " + ruby +
                            "를 이 게임 기본값으로 저장"));
            activeRubyFallback = null;
            return;
        }

        if (!failedMarker && alive &&
                elapsed >= RUBY_FALLBACK_STARTUP_TIMEOUT_MS) {
            failedMarker = true;
            try {
                java.io.FileOutputStream out = new java.io.FileOutputStream(failMarker, false);
                out.write("startup-timeout\n".getBytes(java.nio.charset.StandardCharsets.US_ASCII));
                out.close();
            } catch (Exception ignored) {}
        }

        if (failedMarker && activePid > 0) {
            try { android.os.Process.killProcess(activePid); } catch (Throwable ignored) {}
            long waitUntil = SystemClock.elapsedRealtime() + 1500L;
            while (SystemClock.elapsedRealtime() < waitUntil &&
                    findOwnProcessPid(activity, processName) > 0) {
                try {
                    Thread.sleep(RUBY_FALLBACK_POLL_MS);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    activeRubyFallback = null;
                    return;
                }
            }
        }

        String failed = ruby;
        session.index++;
        if (session.index >= session.candidates.size()) {
            activeRubyFallback = null;
            activity.runOnUiThread(() -> callback.onError(
                    "Ruby 1.8 / 1.9 / 3.1 모두 초기 구동에 실패했습니다.\n" +
                            "마지막 실패: Ruby " + failed + "\n" + session.diagnostic));
            return;
        }
        String next = session.candidates.get(session.index);
        activity.runOnUiThread(() -> callback.onStatus(
                "Ruby " + failed + " 초기 구동 실패 → Ruby " + next + " fallback"));
        launchAutoRubyAttempt(activity, worker, session, callback);
    }

    private static int findOwnProcessPid(Activity activity, String processName) {
        ActivityManager manager =
                (ActivityManager) activity.getSystemService(Activity.ACTIVITY_SERVICE);
        if (manager == null) return -1;
        List<ActivityManager.RunningAppProcessInfo> running = manager.getRunningAppProcesses();
        if (running == null) return -1;
        int uid = android.os.Process.myUid();
        for (ActivityManager.RunningAppProcessInfo info : running) {
            if (info != null && info.uid == uid && processName.equals(info.processName)) {
                return info.pid;
            }
        }
        return -1;
    }

    private static File userExitMarker(Activity activity, String token) {
        File dir = new File(activity.getFilesDir(), "ruby_fallback");
        if (!dir.exists()) dir.mkdirs();
        return new File(dir, token + ".user_exit");
    }

    private static File rubyFailureMarker(Activity activity, String token, int index) {
        File dir = new File(activity.getFilesDir(), "ruby_fallback");
        if (!dir.exists()) dir.mkdirs();
        return new File(dir, token + "." + index + ".fail");
    }

    private static File rubySuccessMarker(Activity activity, String token, int index) {
        File dir = new File(activity.getFilesDir(), "ruby_fallback");
        if (!dir.exists()) dir.mkdirs();
        return new File(dir, token + "." + index + ".success");
    }

    private static boolean isUserExitMarker(Activity activity, String token) {
        return userExitMarker(activity, token).isFile();
    }

    private static void saveRubyFallbackRuntime(Activity activity, GameEntry game, String ruby) {
        activity.getSharedPreferences(GAME_PREFS, Activity.MODE_PRIVATE).edit()
                .putString(prefKey("ruby", game), ruby)
                .apply();
    }

    private static void launchMkxp(Activity activity, GameEntry game, File local, File sharedRtp,
                                   File compatRoot, String rubyMode) throws Exception {
        launchMkxp(activity, game, local, sharedRtp, compatRoot, rubyMode,
                false, null, -1, null, null);
    }

    private static void launchMkxp(Activity activity, GameEntry game, File local, File sharedRtp,
                                   File compatRoot, String rubyMode, boolean autoFallback,
                                   String fallbackToken, int fallbackIndex,
                                   String fallbackFailMarker,
                                   String fallbackSuccessMarker) throws Exception {
        String gameLogPath = GameLog.prepare(local, game.title, game.engine.name());
        Intent intent = new Intent(activity, MkxpPlayerActivity.class);
        intent.putExtra(MkxpPlayerActivity.EXTRA_GAME_PATH, local.getAbsolutePath());
        if (sharedRtp != null) {
            intent.putExtra(MkxpPlayerActivity.EXTRA_RTP_ROOT_PATH, sharedRtp.getAbsolutePath());
        }
        if (compatRoot != null) {
            intent.putExtra(MkxpPlayerActivity.EXTRA_COMPAT_ROOT_PATH,
                    compatRoot.getAbsolutePath());
        }
        intent.putExtra(MkxpPlayerActivity.EXTRA_ENGINE, game.engine.name());
        intent.putExtra(MkxpPlayerActivity.EXTRA_TITLE, game.title);
        intent.putExtra(MkxpPlayerActivity.EXTRA_PAD_ENABLED, padEnabled(activity, game));
        intent.putExtra(MkxpPlayerActivity.EXTRA_PAD_LARGE, padLarge(activity, game));
        intent.putExtra(MkxpPlayerActivity.EXTRA_RUBY_MODE, rubyMode);
        intent.putExtra(MkxpPlayerActivity.EXTRA_AUTO_RUBY_FALLBACK, autoFallback);
        intent.putExtra(MkxpPlayerActivity.EXTRA_RUBY_FALLBACK_TOKEN, fallbackToken);
        intent.putExtra(MkxpPlayerActivity.EXTRA_RUBY_FALLBACK_INDEX, fallbackIndex);
        intent.putExtra(MkxpPlayerActivity.EXTRA_RUBY_FALLBACK_FAIL_MARKER,
                fallbackFailMarker);
        intent.putExtra(MkxpPlayerActivity.EXTRA_RUBY_FALLBACK_SUCCESS_MARKER,
                fallbackSuccessMarker);
        intent.putExtra(MkxpPlayerActivity.EXTRA_KEYMAP, GameKeyMap.load(activity, game));
        intent.putExtra(MkxpPlayerActivity.EXTRA_LOCALE, GameLocaleSettings.effective(activity, game));
        intent.putExtra(MkxpPlayerActivity.EXTRA_LOG_PATH, gameLogPath);
        activity.startActivity(intent);
    }

    public static void scanRubyRuntime(Activity activity, ExecutorService worker, Uri treeUri,
                                       GameEntry game, RubyScanCallback callback) {
        if (game.engine != GameEntry.Engine.XP && game.engine != GameEntry.Engine.VX &&
                game.engine != GameEntry.Engine.VXACE) {
            callback.onError("Ruby 정밀 스캔은 XP / VX / VX Ace 게임에서만 사용할 수 있습니다.");
            return;
        }
        if (!supportsArm64()) {
            callback.onError("Ruby 정밀 스캔은 현재 ARM64(arm64-v8a) 기기에서만 지원합니다.");
            return;
        }
        worker.submit(() -> {
            try {
                activity.runOnUiThread(() -> callback.onStatus("Ruby 정밀 스캔 준비 중…"));
                File local = DirectStorageResolver.resolveGameDirectory(treeUri, game);
                if (local == null) {
                    local = GameMaterializer.mirror(activity, treeUri, game,
                            message -> activity.runOnUiThread(() -> callback.onStatus(message)));
                } else {
                    activity.runOnUiThread(() -> callback.onStatus(
                            "Ruby 정밀 스캔 · 원본 게임 폴더 직접 사용"));
                }
                RgssCompatInstaller.prepareRuntime(activity, local);
                RubyRuntimeDetector.Result result = RubyRuntimeDetector.resolveAutomatic(
                        activity, game, local, true,
                        message -> activity.runOnUiThread(() -> callback.onStatus(message)));
                activity.runOnUiThread(() -> callback.onResult(result.runtime, result.diagnostic));
            } catch (Exception e) {
                activity.runOnUiThread(() -> callback.onError(
                        "Ruby 정밀 스캔 실패: " + e.getMessage()));
            }
        });
    }

    public static void scanMvMzCompatibility(Activity activity, ExecutorService worker, Uri treeUri,
                                             GameEntry game, MvMzScanCallback callback) {
        if (game.engine != GameEntry.Engine.MV && game.engine != GameEntry.Engine.MZ) {
            callback.onError("MV/MZ 호환성 검사는 MV / MZ 게임에서만 사용할 수 있습니다.");
            return;
        }
        worker.submit(() -> {
            try {
                activity.runOnUiThread(() -> callback.onStatus("MV/MZ 호환성 정밀 검사 중…"));
                ArrayDeque<ScanNode> queue = new ArrayDeque<>();
                queue.add(new ScanNode(game.webRootDocumentId, ""));
                ArrayList<String> paths = new ArrayList<>();
                int visited = 0;
                while (!queue.isEmpty() && visited < 30000) {
                    ScanNode node = queue.removeFirst();
                    visited++;
                    List<GameScanner.Child> children = GameScanner.listChildren(activity, treeUri, node.documentId);
                    for (GameScanner.Child child : children) {
                        if (node.path.isEmpty() && child.directory && "saves".equalsIgnoreCase(child.name)) continue;
                        String path = node.path.isEmpty() ? child.name : node.path + "/" + child.name;
                        if (child.directory) queue.addLast(new ScanNode(child.documentId, path));
                        else paths.add("/" + path);
                    }
                }
                MvMzCompatibility.Profile prof = MvMzCompatibility.inspect(paths, game.engine.name());
                String recommended = (prof.khas || prof.pixiFilters) ? "WEBGL" : "AUTO";
                String diagnostic = buildMvMzDiagnostic(prof, paths.size(), recommended);
                activity.getSharedPreferences(GAME_PREFS, Activity.MODE_PRIVATE).edit()
                        .putString(prefKey("mvmz_scan", game), diagnostic)
                        .putString(prefKey("mvmz_recommended_renderer", game), recommended)
                        .apply();
                activity.runOnUiThread(() -> callback.onResult(diagnostic, recommended));
            } catch (Exception e) {
                activity.runOnUiThread(() -> callback.onError("MV/MZ 호환성 검사 실패: " + e.getMessage()));
            }
        });
    }

    private static String buildMvMzDiagnostic(MvMzCompatibility.Profile prof, int fileCount, String renderer) {
        ArrayList<String> flags = new ArrayList<>();
        if (prof.khas) flags.add("Khas");
        if (prof.yep) flags.add("YEP");
        if (prof.visuStella) flags.add("VisuStella");
        if (prof.pixiFilters) flags.add("Pixi 필터");
        if (prof.communityBasic) flags.add("Community Basic");
        if (prof.bundledFont) flags.add("자체 폰트");
        if (prof.oggAudioAvailable && !prof.m4aAudioAvailable) flags.add("OGG 전용");
        if (prof.m4aAudioAvailable && !prof.oggAudioAvailable) flags.add("M4A 전용");
        if (prof.encryptedOggAudio || prof.encryptedM4aAudio) flags.add("암호화 오디오");
        if (prof.plusPath) flags.add("+ 파일명");
        if (prof.nonAsciiPath) flags.add("비ASCII 경로");
        String detail = flags.isEmpty() ? "특이사항 없음" : android.text.TextUtils.join(", ", flags);
        return "파일 " + fileCount + "개 · " + detail + " · 권장 " +
                ("WEBGL".equals(renderer) ? "WebGL" : "자동");
    }

    private static final class ScanNode {
        final String documentId;
        final String path;
        ScanNode(String documentId, String path) {
            this.documentId = documentId;
            this.path = path;
        }
    }

    public static boolean padEnabled(Activity activity, GameEntry game) {
        SharedPreferences prefs = activity.getSharedPreferences(GAME_PREFS, Activity.MODE_PRIVATE);
        String key = prefKey("pad_enabled", game);
        if (prefs.contains(key)) return prefs.getBoolean(key, true);
        if (game.engine == GameEntry.Engine.EASYRPG) return EngineSettings.easyPadEnabled(activity);
        return true;
    }

    public static boolean padLarge(Activity activity, GameEntry game) {
        SharedPreferences prefs = activity.getSharedPreferences(GAME_PREFS, Activity.MODE_PRIVATE);
        String key = prefKey("pad_large", game);
        if (prefs.contains(key)) return prefs.getBoolean(key, false);
        if (game.engine == GameEntry.Engine.EASYRPG) return EngineSettings.easyPadLarge(activity);
        return false;
    }

    public static boolean hasGamePadSettings(Activity activity, GameEntry game) {
        SharedPreferences prefs = activity.getSharedPreferences(GAME_PREFS, Activity.MODE_PRIVATE);
        return prefs.contains(prefKey("pad_enabled", game)) || prefs.contains(prefKey("pad_large", game));
    }

    public static void savePadSettings(Activity activity, GameEntry game, boolean enabled, boolean large) {
        SharedPreferences.Editor edit = activity.getSharedPreferences(GAME_PREFS, Activity.MODE_PRIVATE).edit();
        edit.putBoolean(prefKey("pad_enabled", game), enabled);
        edit.putBoolean(prefKey("pad_large", game), large);
        edit.apply();
    }

    public static String customTitle(Activity activity, GameEntry game) {
        return activity.getSharedPreferences(GAME_PREFS, Activity.MODE_PRIVATE)
                .getString(prefKey("title", game), "");
    }

    public static String rubyMode(Activity activity, GameEntry game) {
        SharedPreferences prefs = activity.getSharedPreferences(GAME_PREFS, Activity.MODE_PRIVATE);
        String key = prefKey("ruby", game);
        return prefs.contains(key) ? prefs.getString(key, "AUTO") : EngineSettings.rubyMode(activity, game.engine);
    }

    public static String gameRubyMode(Activity activity, GameEntry game) {
        SharedPreferences prefs = activity.getSharedPreferences(GAME_PREFS, Activity.MODE_PRIVATE);
        String key = prefKey("ruby", game);
        return prefs.contains(key) ? prefs.getString(key, "AUTO") : null;
    }

    public static String rgssMouseMode(Activity activity, GameEntry game) {
        return activity.getSharedPreferences(GAME_PREFS, Activity.MODE_PRIVATE)
                .getString(prefKey("rgss_mouse", game), "AUTO");
    }

    public static void saveRgssMouseMode(Activity activity, GameEntry game, String mode) {
        String safe = "PATCH".equals(mode) || "PASS".equals(mode) || "ORIGINAL".equals(mode)
                ? mode : "AUTO";
        activity.getSharedPreferences(GAME_PREFS, Activity.MODE_PRIVATE).edit()
                .putString(prefKey("rgss_mouse", game), safe)
                .apply();
    }

    public static String mvRenderMode(Activity activity, GameEntry game) {
        SharedPreferences prefs = activity.getSharedPreferences(GAME_PREFS, Activity.MODE_PRIVATE);
        String key = prefKey("mvmz_renderer", game);
        return prefs.contains(key) ? prefs.getString(key, "AUTO") : EngineSettings.renderMode(activity, game.engine);
    }

    public static String gameMvRenderMode(Activity activity, GameEntry game) {
        SharedPreferences prefs = activity.getSharedPreferences(GAME_PREFS, Activity.MODE_PRIVATE);
        String key = prefKey("mvmz_renderer", game);
        return prefs.contains(key) ? prefs.getString(key, "AUTO") : null;
    }

    public static String mvScanDiagnostic(Activity activity, GameEntry game) {
        return activity.getSharedPreferences(GAME_PREFS, Activity.MODE_PRIVATE)
                .getString(prefKey("mvmz_scan", game), "아직 검사하지 않음");
    }

    public static boolean mvMouseMode(Activity activity, GameEntry game) {
        return activity.getSharedPreferences(GAME_PREFS, Activity.MODE_PRIVATE)
                .getBoolean(prefKey("mvmz_mouse_mode", game), false);
    }

    public static void saveMvMouseMode(Activity activity, GameEntry game, boolean enabled) {
        activity.getSharedPreferences(GAME_PREFS, Activity.MODE_PRIVATE).edit()
                .putBoolean(prefKey("mvmz_mouse_mode", game), enabled)
                .apply();
    }

    public static void saveMvRenderMode(Activity activity, GameEntry game, String mode) {
        if (mode == null || "INHERIT".equals(mode)) {
            activity.getSharedPreferences(GAME_PREFS, Activity.MODE_PRIVATE).edit()
                    .remove(prefKey("mvmz_renderer", game)).apply();
            return;
        }
        String safe = "WEBGL".equals(mode) || "CANVAS".equals(mode) ? mode : "AUTO";
        activity.getSharedPreferences(GAME_PREFS, Activity.MODE_PRIVATE).edit()
                .putString(prefKey("mvmz_renderer", game), safe)
                .apply();
    }

    public static GameEntry applyDisplaySettings(Activity activity, GameEntry game) {
        String custom = customTitle(activity, game);
        if (custom == null || custom.trim().isEmpty()) return game;
        return game.withTitle(custom.trim());
    }

    public static void saveGameSettings(Activity activity, GameEntry game, String title,
                                        String ruby, boolean inheritPad, boolean enabled, boolean large) {
        SharedPreferences.Editor edit = activity.getSharedPreferences(GAME_PREFS, Activity.MODE_PRIVATE).edit();
        String cleaned = title == null ? "" : title.trim();
        if (cleaned.isEmpty()) edit.remove(prefKey("title", game));
        else edit.putString(prefKey("title", game), cleaned);
        if (ruby == null || "INHERIT".equals(ruby)) edit.remove(prefKey("ruby", game));
        else edit.putString(prefKey("ruby", game), ruby);
        if (inheritPad) {
            edit.remove(prefKey("pad_enabled", game));
            edit.remove(prefKey("pad_large", game));
        } else {
            edit.putBoolean(prefKey("pad_enabled", game), enabled);
            edit.putBoolean(prefKey("pad_large", game), large);
        }
        edit.apply();
    }

    private static String prefKey(String name, GameEntry game) {
        return name + "_" + safeId(game);
    }

    private static String safeId(GameEntry game) {
        return Integer.toHexString(game.stableId().hashCode());
    }

    private static boolean supportsArm64() {
        for (String abi : Build.SUPPORTED_ABIS) {
            if ("arm64-v8a".equals(abi)) return true;
        }
        return false;
    }
}
