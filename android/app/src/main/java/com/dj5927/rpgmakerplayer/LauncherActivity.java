package com.dj5927.rpgmakerplayer;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.SystemClock;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.KeyEvent;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.Window;
import android.view.WindowManager;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.widget.TextView;
import android.widget.Toast;
import android.widget.LinearLayout;
import android.widget.EditText;
import android.widget.Spinner;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.ScrollView;
import android.widget.ImageView;
import android.widget.FrameLayout;

import java.io.File;
import java.io.FileWriter;
import java.io.InputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.util.Arrays;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class LauncherActivity extends Activity implements LauncherView.Listener {
    private static final int REQUEST_GAME_FOLDER = 1001;
    private static final String PREFS = "launcher";
    private static final String KEY_GAME_ROOT = "game_root_uri";
    private static final String KEY_RECENT_GAMES = "recent_game_ids";
    private static final String KEY_ACTIVE_GAME_PROCESS = "active_game_process";
    private static final String KEY_INSTALLED_VERSION_CODE = "installed_version_code";
    private static final long GAME_PROCESS_FORCE_KILL_AFTER_MS = 3000L;
    private static final long GAME_PROCESS_POLL_MS = 100L;

    private LauncherView launcherView;
    private final ExecutorService scanner = Executors.newSingleThreadExecutor();
    private final ExecutorService runner = Executors.newSingleThreadExecutor();
    private final ExecutorService shutdownWatcher = Executors.newSingleThreadExecutor();
    private Uri gameRoot;
    private AlertDialog activeDialog;
    private int lastJoystickDirection;
    private volatile boolean gameShutdownGateRunning;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        installCrashHandler();
        appendDiagnostic("LauncherActivity onCreate begin | ABI=" + Arrays.toString(Build.SUPPORTED_ABIS));
        try {
            initializeLauncher(state);
            appendDiagnostic("LauncherActivity ready");
        } catch (Throwable t) {
            appendDiagnostic("launcher init failure\n" + stackTrace(t));
            showStartupFailure(t);
        }
    }

    private void initializeLauncher(Bundle state) {
        launcherView = new LauncherView(this);
        launcherView.setListener(this);
        launcherView.setRecentIds(loadRecentGameIds());
        setContentView(launcherView);
        launcherView.post(this::enterImmersive);
        requestAllFilesAccessIfNeeded();

        SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        migrateRuntimeStateAfterUpdate(prefs);
        String saved = prefs.getString(KEY_GAME_ROOT, null);
        if (saved != null) {
            gameRoot = Uri.parse(saved);
            launcherView.setFolderLabel(folderLabel(gameRoot));
            scanGames();
        }
    }

    private void migrateRuntimeStateAfterUpdate(SharedPreferences prefs) {
        int current = -1;
        try {
            if (Build.VERSION.SDK_INT >= 28) {
                current = (int) getPackageManager()
                        .getPackageInfo(getPackageName(), 0).getLongVersionCode();
            } else {
                current = getPackageManager()
                        .getPackageInfo(getPackageName(), 0).versionCode;
            }
        } catch (Exception ignored) {}
        if (current < 0) return;
        int previous = prefs.getInt(KEY_INSTALLED_VERSION_CODE, -1);
        if (previous == current) return;
        File runtimeDir = new File(getFilesDir(), "mkxp_runtime");
        File[] files = runtimeDir.listFiles();
        if (files != null) {
            for (File file : files) {
                if (file != null && file.isFile()) file.delete();
            }
        }
        prefs.edit()
                .putInt(KEY_INSTALLED_VERSION_CODE, current)
                .remove(KEY_ACTIVE_GAME_PROCESS)
                .apply();
        appendDiagnostic("runtime migration previous=" + previous + " current=" + current);
    }

    private void requestAllFilesAccessIfNeeded() {
        if (Build.VERSION.SDK_INT < 30 || android.os.Environment.isExternalStorageManager()) return;
        try {
            Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:" + getPackageName()));
            startActivity(intent);
        } catch (Exception first) {
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            } catch (Exception ignored) {}
        }
    }

    private void installCrashHandler() {
        Thread.UncaughtExceptionHandler previous = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((thread, error) -> {
            appendDiagnostic("UNCAUGHT thread=" + thread.getName() + "\n" + stackTrace(error));
            if (previous != null) previous.uncaughtException(thread, error);
        });
    }

    private void showStartupFailure(Throwable error) {
        TextView view = new TextView(this);
        view.setBackgroundColor(Color.rgb(20, 20, 24));
        view.setTextColor(Color.WHITE);
        view.setTextSize(14f);
        int pad = (int) (24 * getResources().getDisplayMetrics().density);
        view.setPadding(pad, pad, pad, pad);
        view.setText("RPG Maker Player 시작 오류\n\n" +
                error.getClass().getName() + ": " + String.valueOf(error.getMessage()) +
                "\n\n진단 로그:\n" + diagnosticFile().getAbsolutePath());
        setContentView(view);
    }

    private void appendDiagnostic(String message) {
        try (FileWriter writer = new FileWriter(diagnosticFile(), true)) {
            writer.write(System.currentTimeMillis() + " " + message + "\n");
        } catch (Exception ignored) {}
    }

    private File diagnosticFile() {
        File base = getExternalFilesDir(null);
        if (base == null) base = getFilesDir();
        File dir = new File(base, "diagnostics");
        if (!dir.exists()) dir.mkdirs();
        return new File(dir, "launcher-crash.txt");
    }

    private static String stackTrace(Throwable error) {
        StringWriter buffer = new StringWriter();
        error.printStackTrace(new PrintWriter(buffer));
        return buffer.toString();
    }

    private void enterImmersive() {
        View decor = getWindow().getDecorView();
        if (decor == null) return;
        applyLegacyImmersive(decor);
        try {
            getWindow().setStatusBarColor(android.graphics.Color.TRANSPARENT);
            getWindow().setNavigationBarColor(android.graphics.Color.TRANSPARENT);
            if (Build.VERSION.SDK_INT >= 29) {
                getWindow().setStatusBarContrastEnforced(false);
                getWindow().setNavigationBarContrastEnforced(false);
            }
            if (Build.VERSION.SDK_INT >= 30) {
                getWindow().setDecorFitsSystemWindows(false);
                if (!decor.isAttachedToWindow()) {
                    decor.post(this::enterImmersive);
                    return;
                }
                WindowInsetsController controller = decor.getWindowInsetsController();
                if (controller != null) {
                    controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                    controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                }
            }
        } catch (Throwable t) {
            appendDiagnostic("immersive fallback: " + t.getClass().getSimpleName() + ": " + t.getMessage());
        }
    }

    private void applyLegacyImmersive(View decor) {
        decor.setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                        View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
                        View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
    }

    @Override protected void onResume() {
        super.onResume();
        String activeProcess = getSharedPreferences(PREFS, MODE_PRIVATE)
                .getString(KEY_ACTIVE_GAME_PROCESS, "");
        if (activeProcess != null && !activeProcess.isEmpty()) {
            if (!RunnerCoordinator.hasActiveRubyFallback()) {
                beginGameShutdownGate(activeProcess);
            }
        } else {
            syncPendingGameSaveIfNeeded();
        }
        View decor = getWindow().getDecorView();
        if (decor != null) {
            decor.post(this::enterImmersive);
            decor.postDelayed(this::enterImmersive, 250);
        }
    }

    private void beginGameShutdownGate(String processName) {
        if (gameShutdownGateRunning || launcherView == null) return;
        gameShutdownGateRunning = true;
        launcherView.setBusy(true, "게임 종료 중…");
        appendDiagnostic("shutdown gate begin process=" + processName);
        shutdownWatcher.submit(() -> {
            boolean forced = false;
            try {
                long started = SystemClock.elapsedRealtime();
                int absentChecks = 0;
                while (absentChecks < 3) {
                    int pid = findOwnGameProcessPid(processName);
                    if (pid <= 0) {
                        absentChecks++;
                        Thread.sleep(GAME_PROCESS_POLL_MS);
                        continue;
                    }
                    absentChecks = 0;
                    long elapsed = SystemClock.elapsedRealtime() - started;
                    if (!forced && elapsed >= GAME_PROCESS_FORCE_KILL_AFTER_MS) {
                        forced = true;
                        final int stuckPid = pid;
                        appendDiagnostic("shutdown gate force-kill process=" + processName +
                                " pid=" + stuckPid + " elapsedMs=" + elapsed);
                        runOnUiThread(() -> launcherView.setBusyMessage("게임 프로세스 정리 중…"));
                        try { android.os.Process.killProcess(stuckPid); } catch (Throwable ignored) {}
                    }
                    Thread.sleep(GAME_PROCESS_POLL_MS);
                }

                appendDiagnostic("shutdown gate process gone process=" + processName +
                        " forced=" + forced);
                if (PersistentSaveManager.hasPending(this)) {
                    runOnUiThread(() -> launcherView.setBusyMessage("세이브 동기화 중…"));
                    PersistentSaveManager.finishPending(this);
                    appendDiagnostic("shutdown gate save sync completed");
                }

                getSharedPreferences(PREFS, MODE_PRIVATE).edit()
                        .remove(KEY_ACTIVE_GAME_PROCESS)
                        .apply();
                runOnUiThread(() -> {
                    gameShutdownGateRunning = false;
                    launcherView.setBusy(false, null);
                    launcherView.setStatus("게임 종료 완료");
                });
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                appendDiagnostic("shutdown gate interrupted process=" + processName);
            } catch (Exception e) {
                appendDiagnostic("shutdown gate failed: " + e.getClass().getSimpleName() +
                        ": " + String.valueOf(e.getMessage()));
                getSharedPreferences(PREFS, MODE_PRIVATE).edit()
                        .remove(KEY_ACTIVE_GAME_PROCESS)
                        .apply();
                runOnUiThread(() -> {
                    gameShutdownGateRunning = false;
                    launcherView.setBusy(false, null);
                    Toast.makeText(this, "게임 종료 처리 실패: " + e.getClass().getSimpleName(),
                            Toast.LENGTH_LONG).show();
                });
            }
        });
    }

    private int findOwnGameProcessPid(String processName) {
        ActivityManager manager = (ActivityManager) getSystemService(ACTIVITY_SERVICE);
        if (manager == null) return -1;
        List<ActivityManager.RunningAppProcessInfo> running = manager.getRunningAppProcesses();
        if (running == null) return -1;
        int uid = android.os.Process.myUid();
        for (ActivityManager.RunningAppProcessInfo info : running) {
            if (info == null || info.uid != uid || info.processName == null) continue;
            if (processName.equals(info.processName)) return info.pid;
        }
        return -1;
    }

    private void syncPendingGameSaveIfNeeded() {
        if (!PersistentSaveManager.hasPending(this)) return;
        launcherView.setBusy(true, "세이브 동기화 중…");
        scanner.submit(() -> {
            try {
                PersistentSaveManager.finishPending(this);
                appendDiagnostic("persistent save sync completed");
                runOnUiThread(() -> launcherView.setBusy(false, null));
            } catch (Exception e) {
                appendDiagnostic("persistent save sync failed: " + e.getClass().getSimpleName() +
                        ": " + String.valueOf(e.getMessage()));
                runOnUiThread(() -> {
                    launcherView.setBusy(false, null);
                    Toast.makeText(this, "세이브 동기화 실패: " + e.getClass().getSimpleName(),
                            Toast.LENGTH_LONG).show();
                });
            }
        });
    }

    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            View decor = getWindow().getDecorView();
            if (decor != null) {
                decor.post(this::enterImmersive);
                decor.postDelayed(this::enterImmersive, 250);
            }
        }
    }

    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        if (activeDialog != null && activeDialog.isShowing()) return super.dispatchKeyEvent(event);
        if (event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0 && launcherView != null) {
            int key = event.getKeyCode();
            if (key == KeyEvent.KEYCODE_BUTTON_L1 || key == KeyEvent.KEYCODE_PAGE_UP) {
                if (launcherView.scrollLibraryPage(-1)) return true;
            } else if (key == KeyEvent.KEYCODE_BUTTON_R1 || key == KeyEvent.KEYCODE_PAGE_DOWN) {
                if (launcherView.scrollLibraryPage(1)) return true;
            }
            if (launcherView.handleControllerKey(key)) return true;
        } else if (event.getAction() == KeyEvent.ACTION_UP && launcherView != null &&
                isLauncherControllerKey(event.getKeyCode())) {
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    @Override public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if (activeDialog != null && activeDialog.isShowing()) return super.dispatchGenericMotionEvent(event);
        if (launcherView != null && event.getAction() == MotionEvent.ACTION_MOVE &&
                (event.getSource() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK) {
            float x = event.getAxisValue(MotionEvent.AXIS_HAT_X);
            float y = event.getAxisValue(MotionEvent.AXIS_HAT_Y);
            if (Math.abs(x) < 0.2f) x = event.getAxisValue(MotionEvent.AXIS_X);
            if (Math.abs(y) < 0.2f) y = event.getAxisValue(MotionEvent.AXIS_Y);
            int direction = 0;
            if (Math.max(Math.abs(x), Math.abs(y)) >= 0.55f) {
                if (Math.abs(x) > Math.abs(y)) direction = x < 0 ? KeyEvent.KEYCODE_DPAD_LEFT : KeyEvent.KEYCODE_DPAD_RIGHT;
                else direction = y < 0 ? KeyEvent.KEYCODE_DPAD_UP : KeyEvent.KEYCODE_DPAD_DOWN;
            }
            if (direction == 0) {
                lastJoystickDirection = 0;
            } else if (direction != lastJoystickDirection) {
                lastJoystickDirection = direction;
                if (launcherView.handleControllerKey(direction)) return true;
            }
        }
        return super.dispatchGenericMotionEvent(event);
    }

    private static boolean isLauncherControllerKey(int key) {
        return key == KeyEvent.KEYCODE_DPAD_LEFT || key == KeyEvent.KEYCODE_DPAD_RIGHT ||
                key == KeyEvent.KEYCODE_DPAD_UP || key == KeyEvent.KEYCODE_DPAD_DOWN ||
                key == KeyEvent.KEYCODE_DPAD_CENTER || key == KeyEvent.KEYCODE_BUTTON_A ||
                key == KeyEvent.KEYCODE_BUTTON_B || key == KeyEvent.KEYCODE_BUTTON_SELECT ||
                key == KeyEvent.KEYCODE_BUTTON_L1 || key == KeyEvent.KEYCODE_BUTTON_R1 ||
                key == KeyEvent.KEYCODE_BUTTON_L2 || key == KeyEvent.KEYCODE_BUTTON_R2 ||
                key == KeyEvent.KEYCODE_PAGE_UP || key == KeyEvent.KEYCODE_PAGE_DOWN;
    }

    @Override public void onPickGameFolder() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                Intent.FLAG_GRANT_WRITE_URI_PERMISSION |
                Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION |
                Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        if (gameRoot != null && android.os.Build.VERSION.SDK_INT >= 26) {
            intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, gameRoot);
        }
        startActivityForResult(intent, REQUEST_GAME_FOLDER);
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_GAME_FOLDER || resultCode != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        int flags = data.getFlags() & (Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        try {
            getContentResolver().takePersistableUriPermission(uri, flags);
        } catch (SecurityException e) {
            try {
                getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (Exception ignored) {}
        }
        gameRoot = uri;
        getSharedPreferences(PREFS, MODE_PRIVATE).edit().putString(KEY_GAME_ROOT, uri.toString()).apply();
        launcherView.setFolderLabel(folderLabel(uri));
        scanGames();
    }

    @Override public void onRescan() {
        if (gameRoot == null) onPickGameFolder(); else scanGames();
    }

    private void scanGames() {
        if (gameRoot == null) return;
        launcherView.setStatus("게임 검색 중…");
        launcherView.setBusy(true, "게임 목록을 읽는 중…");
        Uri root = gameRoot;
        scanner.submit(() -> {
            try {
                String layoutWarning = null;
                try {
                    LibraryFolderLayout.ensure(this, root);
                } catch (Exception layoutError) {
                    layoutWarning = "공용 폴더 자동 생성 실패: " +
                            layoutError.getClass().getSimpleName() + ": " +
                            String.valueOf(layoutError.getMessage());
                    appendDiagnostic(layoutWarning + "\n" + stackTrace(layoutError));
                }
                List<GameEntry> games = GameScanner.scan(this, root);
                ArrayList<GameEntry> displayGames = new ArrayList<>(games.size());
                for (GameEntry game : games) {
                    displayGames.add(RunnerCoordinator.applyDisplaySettings(this, game));
                }
                final String finalLayoutWarning = layoutWarning;
                runOnUiThread(() -> {
                    launcherView.setGames(displayGames);
                    launcherView.setBusy(false, null);
                    if (finalLayoutWarning != null) {
                        Toast.makeText(LauncherActivity.this, finalLayoutWarning, Toast.LENGTH_LONG).show();
                    }
                });
                loadGameThumbnails(root, displayGames);
            } catch (Exception e) {
                runOnUiThread(() -> {
                    launcherView.setStatus("검색 실패: " + e.getClass().getSimpleName());
                    launcherView.setBusy(false, null);
                });
            }
        });
    }

    private void loadGameThumbnails(Uri root, List<GameEntry> games) {
        for (GameEntry game : games) {
            if (game.thumbnailDocumentId == null || game.thumbnailDocumentId.isEmpty()) continue;
            Bitmap bitmap = decodeThumbnail(root, game.thumbnailDocumentId, 640);
            if (bitmap == null) continue;
            String stableId = game.stableId();
            runOnUiThread(() -> {
                if (gameRoot != null && gameRoot.equals(root)) launcherView.setGameThumbnail(stableId, bitmap);
                else if (!bitmap.isRecycled()) bitmap.recycle();
            });
        }
    }

    private Bitmap decodeThumbnail(Uri root, String documentId, int targetPx) {
        Uri uri = GameScanner.documentUri(root, documentId);
        BitmapFactory.Options bounds = new BitmapFactory.Options();
        bounds.inJustDecodeBounds = true;
        try (InputStream input = getContentResolver().openInputStream(uri)) {
            BitmapFactory.decodeStream(input, null, bounds);
        } catch (Exception ignored) { return null; }
        int width = Math.max(1, bounds.outWidth);
        int height = Math.max(1, bounds.outHeight);
        int sample = 1;
        while (width / (sample * 2) >= targetPx && height / (sample * 2) >= targetPx) sample *= 2;
        BitmapFactory.Options opts = new BitmapFactory.Options();
        opts.inSampleSize = Math.max(1, sample);
        opts.inPreferredConfig = Bitmap.Config.RGB_565;
        try (InputStream input = getContentResolver().openInputStream(uri)) {
            return BitmapFactory.decodeStream(input, null, opts);
        } catch (Exception ignored) { return null; }
    }

    private ArrayList<String> loadRecentGameIds() {
        String raw = getSharedPreferences(PREFS, MODE_PRIVATE).getString(KEY_RECENT_GAMES, "");
        ArrayList<String> out = new ArrayList<>();
        if (raw == null || raw.isEmpty()) return out;
        for (String line : raw.split("\\n")) {
            if (!line.isEmpty() && !out.contains(line)) out.add(line);
            if (out.size() >= 5) break;
        }
        return out;
    }

    private void recordRecentGame(GameEntry game) {
        ArrayList<String> ids = loadRecentGameIds();
        ids.remove(game.stableId());
        ids.add(0, game.stableId());
        while (ids.size() > 5) ids.remove(ids.size() - 1);
        StringBuilder joined = new StringBuilder();
        for (String id : ids) {
            if (joined.length() > 0) joined.append('\n');
            joined.append(id);
        }
        getSharedPreferences(PREFS, MODE_PRIVATE).edit()
                .putString(KEY_RECENT_GAMES, joined.toString())
                .apply();
    }

    @Override public void onLaunch(GameEntry game) {
        if (gameRoot == null) return;
        if (gameShutdownGateRunning) {
            String active = getSharedPreferences(PREFS, MODE_PRIVATE)
                    .getString(KEY_ACTIVE_GAME_PROCESS, "");
            if (active == null || active.isEmpty() || findOwnGameProcessPid(active) <= 0) {
                gameShutdownGateRunning = false;
                getSharedPreferences(PREFS, MODE_PRIVATE).edit()
                        .remove(KEY_ACTIVE_GAME_PROCESS)
                        .apply();
                launcherView.setBusy(false, null);
                appendDiagnostic("launch guard self-heal: stale shutdown gate cleared");
            } else {
                appendDiagnostic("launch blocked: shutdown gate process=" + active);
                return;
            }
        }
        String existingProcess = getSharedPreferences(PREFS, MODE_PRIVATE)
                .getString(KEY_ACTIVE_GAME_PROCESS, "");
        if (existingProcess != null && !existingProcess.isEmpty()) {
            int existingPid = findOwnGameProcessPid(existingProcess);
            if (existingPid > 0) {
                appendDiagnostic("launch blocked: active process=" + existingProcess +
                        " pid=" + existingPid);
                return;
            }
            appendDiagnostic("launch guard self-heal: stale process marker cleared process=" +
                    existingProcess);
            getSharedPreferences(PREFS, MODE_PRIVATE).edit()
                    .remove(KEY_ACTIVE_GAME_PROCESS)
                    .apply();
        }
        final String launchedProcess = gameProcessName(game.engine);
        recordRecentGame(game);
        launcherView.setRecentIds(loadRecentGameIds());
        launcherView.setBusy(true, "게임 실행 준비중");
        RunnerCoordinator.launch(this, runner, gameRoot, game, new RunnerCoordinator.Callback() {
            @Override public void onStatus(String message) {
                launcherView.setStatus(message);
                launcherView.setBusyMessage(message);
            }

            @Override public void onError(String message) {
                getSharedPreferences(PREFS, MODE_PRIVATE).edit()
                        .remove(KEY_ACTIVE_GAME_PROCESS)
                        .apply();
                Toast.makeText(LauncherActivity.this, message, Toast.LENGTH_LONG).show();
                launcherView.setStatus(message);
                launcherView.setBusy(false, null);
            }

            @Override public void onStarted() {
                getSharedPreferences(PREFS, MODE_PRIVATE).edit()
                        .putString(KEY_ACTIVE_GAME_PROCESS, launchedProcess)
                        .apply();
                appendDiagnostic("game process armed=" + launchedProcess + " title=" + game.title);
                launcherView.setBusy(false, null);
            }
        });
    }

    private String gameProcessName(GameEntry.Engine engine) {
        String suffix;
        if (engine == GameEntry.Engine.MV || engine == GameEntry.Engine.MZ) suffix = ":mvmz";
        else if (engine == GameEntry.Engine.EASYRPG) suffix = ":easyrpg";
        else suffix = ":mkxp";
        return getPackageName() + suffix;
    }

    @Override public void onEngineSettings() {
        final GameEntry.Engine[] engines = {
                GameEntry.Engine.EASYRPG, GameEntry.Engine.XP, GameEntry.Engine.VX,
                GameEntry.Engine.VXACE, GameEntry.Engine.MV, GameEntry.Engine.MZ
        };
        String[] labels = {"EasyRPG · 2K / 2K3", "XP", "VX", "VX Ace", "MV", "MZ"};
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(18), dp(16), dp(18), dp(14));
        root.setBackground(releaseRounded(Color.rgb(12, 27, 41), 18,
                Color.rgb(63, 92, 117), 1));
        TextView title = releaseText("엔진별 설정", 21, true, Color.WHITE);
        title.setPadding(dp(2), 0, 0, dp(12));
        root.addView(title);
        final AlertDialog[] holder = new AlertDialog[1];
        Button first = null;
        for (int i = 0; i < engines.length; i++) {
            GameEntry.Engine engine = engines[i];
            Button button = releaseButton(labels[i], false);
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, dp(46));
            lp.setMargins(0, 0, 0, dp(7));
            root.addView(button, lp);
            if (first == null) first = button;
            button.setOnClickListener(v -> {
                if (holder[0] != null) holder[0].dismiss();
                showEngineSettingsDialog(engine);
            });
        }
        TextView info = releaseText("게임별 설정이 없을 경우 사용되는 기본값 설정입니다.",
                12, false, Color.rgb(152, 169, 188));
        info.setPadding(dp(4), dp(8), dp(4), 0);
        root.addView(info);
        AlertDialog dialog = new AlertDialog.Builder(this).setView(root).create();
        holder[0] = dialog;
        dialog.show();
        styleReleaseDialogWindow(dialog, 0.58f, 0.78f);
        prepareDialogForGamepad(dialog, first);
    }

    private void showEngineSettingsDialog(GameEntry.Engine engine) {
        LinearLayout form = new LinearLayout(this);
        form.setOrientation(LinearLayout.VERTICAL);
        form.setPadding(dp(18), dp(10), dp(18), dp(12));
        Spinner rubySpinner = null, renderSpinner = null, soundfontSpinner = null,
                font1Spinner = null, font2Spinner = null;
        Spinner localeSpinner = new Spinner(this);
        CheckBox padVisible = null, padLarge = null;
        Button keyMapButton = null;
        List<EasyRpgCoreSettings.Choice> sfChoices = null, fontChoices = null;

        TextView localeLabel = new TextView(this);
        localeLabel.setText(UiText.t(this, "로케일"));
        localeLabel.setTextSize(15);
        form.addView(localeLabel);
        localeSpinner.setAdapter(new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_dropdown_item, GameLocaleSettings.engineLabels(this)));
        localeSpinner.setSelection(GameLocaleSettings.engineIndex(EngineSettings.locale(this, engine)));
        form.addView(localeSpinner);

        if (engine == GameEntry.Engine.EASYRPG) {
            sfChoices = EasyRpgCoreSettings.soundfontChoices(this, gameRoot);
            fontChoices = EasyRpgCoreSettings.fontChoices(this, gameRoot);
            TextView sfLabel = new TextView(this); sfLabel.setText("MIDI SoundFont"); sfLabel.setTextSize(15); form.addView(sfLabel);
            soundfontSpinner = new Spinner(this);
            soundfontSpinner.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item,
                    EasyRpgCoreSettings.labels(sfChoices)));
            soundfontSpinner.setSelection(EasyRpgCoreSettings.indexOf(sfChoices, EngineSettings.easySoundfont(this)));
            form.addView(soundfontSpinner);
            TextView f1 = new TextView(this); f1.setText("Font 1"); f1.setTextSize(15); f1.setPadding(0, dp(10), 0, 0); form.addView(f1);
            font1Spinner = new Spinner(this);
            font1Spinner.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item,
                    EasyRpgCoreSettings.labels(fontChoices)));
            font1Spinner.setSelection(EasyRpgCoreSettings.indexOf(fontChoices, EngineSettings.easyFont1(this)));
            form.addView(font1Spinner);
            TextView f2 = new TextView(this); f2.setText("Font 2"); f2.setTextSize(15); f2.setPadding(0, dp(10), 0, 0); form.addView(f2);
            font2Spinner = new Spinner(this);
            font2Spinner.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item,
                    EasyRpgCoreSettings.labels(fontChoices)));
            font2Spinner.setSelection(EasyRpgCoreSettings.indexOf(fontChoices, EngineSettings.easyFont2(this)));
            form.addView(font2Spinner);
            padVisible = new CheckBox(this); padVisible.setText(UiText.t(this, "가상패드 표시"));
            padVisible.setChecked(EngineSettings.easyPadEnabled(this)); form.addView(padVisible);
            padLarge = new CheckBox(this); padLarge.setText(UiText.t(this, "가상패드 크게 표시"));
            padLarge.setChecked(EngineSettings.easyPadLarge(this)); form.addView(padLarge);
        } else if (engine == GameEntry.Engine.XP || engine == GameEntry.Engine.VX || engine == GameEntry.Engine.VXACE) {
            TextView label = new TextView(this); label.setText(UiText.t(this, "Ruby 런타임")); label.setTextSize(15); form.addView(label);
            rubySpinner = new Spinner(this);
            rubySpinner.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item,
                    UiText.array(this, "자동 (정밀판별 + fallback)", "Ruby 1.8", "Ruby 1.9", "Ruby 3.1")));
            String ruby = EngineSettings.rubyMode(this, engine);
            rubySpinner.setSelection("1.8".equals(ruby) ? 1 : "1.9".equals(ruby) ? 2 : "3.1".equals(ruby) ? 3 : 0);
            form.addView(rubySpinner);
            keyMapButton = new Button(this); keyMapButton.setText(UiText.t(this, "패드 키 설정")); form.addView(keyMapButton);
        } else {
            TextView label = new TextView(this); label.setText(UiText.t(this, "렌더링 모드")); label.setTextSize(15); form.addView(label);
            renderSpinner = new Spinner(this);
            renderSpinner.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item,
                    UiText.array(this, "자동 (권장)", "WebGL", "Canvas")));
            String render = EngineSettings.renderMode(this, engine);
            renderSpinner.setSelection("WEBGL".equals(render) ? 1 : "CANVAS".equals(render) ? 2 : 0);
            form.addView(renderSpinner);
            keyMapButton = new Button(this); keyMapButton.setText(UiText.t(this, "패드 키 설정")); form.addView(keyMapButton);
        }

        TextView info = releaseText("게임별 설정이 없을 경우 사용되는 기본값 설정입니다.", 12, false,
                Color.rgb(152, 169, 188));
        info.setPadding(0, dp(14), 0, dp(4)); form.addView(info);
        styleReleaseDialogTree(form);

        LinearLayout root = new LinearLayout(this); root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(18), dp(16), dp(18), dp(14));
        root.setBackground(releaseRounded(Color.rgb(12, 27, 41), 18, Color.rgb(63, 92, 117), 1));
        TextView title = releaseText((engine == GameEntry.Engine.EASYRPG ? "EasyRPG" : engine.label) + " 기본 설정",
                20, true, Color.WHITE); title.setPadding(dp(2), 0, 0, dp(10)); root.addView(title);
        ScrollView scroll = new ScrollView(this); scroll.addView(form);
        root.addView(scroll, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));
        LinearLayout actions = new LinearLayout(this); actions.setOrientation(LinearLayout.HORIZONTAL); actions.setPadding(0, dp(10), 0, 0);
        Button reset = releaseButton("앱 기본값 복원", false), close = releaseButton("닫기", false), save = releaseButton("저장", true);
        actions.addView(reset, new LinearLayout.LayoutParams(0, dp(44), 1f));
        actions.addView(close, new LinearLayout.LayoutParams(0, dp(44), 1f));
        actions.addView(save, new LinearLayout.LayoutParams(0, dp(44), 1f)); root.addView(actions);
        AlertDialog dialog = new AlertDialog.Builder(this).setView(root).create();

        final Spinner flocale = localeSpinner, fruby = rubySpinner, frender = renderSpinner, fsf = soundfontSpinner,
                ff1 = font1Spinner, ff2 = font2Spinner;
        final CheckBox fpad = padVisible, fpadLarge = padLarge;
        final Button fkey = keyMapButton;
        final List<EasyRpgCoreSettings.Choice> fsfChoices = sfChoices, ffontChoices = fontChoices;
        reset.setOnClickListener(v -> { EngineSettings.resetEngine(this, engine); dialog.dismiss();
            Toast.makeText(this, "엔진 설정을 앱 기본값으로 복원했습니다.", Toast.LENGTH_SHORT).show(); });
        close.setOnClickListener(v -> dialog.dismiss());
        save.setOnClickListener(v -> {
            EngineSettings.saveLocale(this, engine,
                    GameLocaleSettings.engineCodeAt(flocale.getSelectedItemPosition()));
            if (engine == GameEntry.Engine.EASYRPG) {
                EngineSettings.saveEasyChoices(this, EasyRpgCoreSettings.idAt(fsfChoices, fsf.getSelectedItemPosition()),
                        EasyRpgCoreSettings.idAt(ffontChoices, ff1.getSelectedItemPosition()),
                        EasyRpgCoreSettings.idAt(ffontChoices, ff2.getSelectedItemPosition()));
                EngineSettings.saveEasyPad(this, fpad.isChecked(), fpadLarge.isChecked());
            } else if (fruby != null) {
                int p = fruby.getSelectedItemPosition();
                EngineSettings.saveRubyMode(this, engine, p == 1 ? "1.8" : p == 2 ? "1.9" : p == 3 ? "3.1" : "AUTO");
            } else if (frender != null) {
                int p = frender.getSelectedItemPosition();
                EngineSettings.saveRenderMode(this, engine, p == 1 ? "WEBGL" : p == 2 ? "CANVAS" : "AUTO");
            }
            dialog.dismiss(); Toast.makeText(this, "엔진 기본 설정 저장 완료", Toast.LENGTH_SHORT).show();
        });
        if (fkey != null) fkey.setOnClickListener(v -> showEngineKeyMapDialog(engine));
        dialog.show(); styleReleaseDialogWindow(dialog, 0.68f, 0.82f);
        View focus = flocale;
        prepareDialogForGamepad(dialog, focus != null ? focus : save);
    }

    private void showEngineKeyMapDialog(GameEntry.Engine engine) {
        int[] current = EngineSettings.keyMap(this, engine);
        LinearLayout form = new LinearLayout(this);
        form.setOrientation(LinearLayout.VERTICAL);
        form.setPadding(dp(14), dp(8), dp(14), dp(8));
        Spinner[] spinners = new Spinner[GameKeyMap.SOURCES.length];
        for (int i = 0; i < GameKeyMap.SOURCES.length; i++) {
            LinearLayout row = new LinearLayout(this);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setGravity(Gravity.CENTER_VERTICAL);
            TextView label = new TextView(this);
            label.setText(GameKeyMap.LABELS[i] + "  →");
            label.setTextSize(15);
            row.addView(label, new LinearLayout.LayoutParams(0,
                    LinearLayout.LayoutParams.WRAP_CONTENT, 0.36f));
            Spinner spinner = new Spinner(this);
            spinner.setAdapter(new ArrayAdapter<>(this,
                    android.R.layout.simple_spinner_dropdown_item, GameKeyMap.LABELS));
            int selected = GameKeyMap.targetIndex(current[i]);
            spinner.setSelection(selected < 0 ? i : selected);
            spinners[i] = spinner;
            row.addView(spinner, new LinearLayout.LayoutParams(0,
                    LinearLayout.LayoutParams.WRAP_CONTENT, 0.64f));
            form.addView(row);
        }
        TextView info = releaseText("게임별 키 설정이 없을 경우 사용되는 엔진 기본 키 설정입니다.",
                12, false, Color.rgb(152, 169, 188));
        info.setPadding(0, dp(10), 0, dp(4));
        form.addView(info);
        ScrollView scroll = new ScrollView(this);
        scroll.addView(form);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(18), dp(16), dp(18), dp(14));
        root.setBackground(releaseRounded(Color.rgb(12, 27, 41), 18,
                Color.rgb(63, 92, 117), 1));
        TextView title = releaseText(engine.label + " · 패드 키 기본 설정", 20, true, Color.WHITE);
        title.setPadding(dp(2), 0, 0, dp(10));
        root.addView(title);
        root.addView(scroll, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));
        LinearLayout actions = new LinearLayout(this);
        actions.setOrientation(LinearLayout.HORIZONTAL);
        actions.setPadding(0, dp(8), 0, 0);
        Button reset = releaseButton("앱 기본 키맵", false);
        Button save = releaseButton("저장", true);
        actions.addView(reset, new LinearLayout.LayoutParams(0, dp(44), 1f));
        actions.addView(save, new LinearLayout.LayoutParams(0, dp(44), 1f));
        root.addView(actions);
        AlertDialog dialog = new AlertDialog.Builder(this).setView(root).create();
        reset.setOnClickListener(v -> {
            EngineSettings.resetKeyMap(this, engine);
            dialog.dismiss();
            Toast.makeText(this, "엔진 키맵을 앱 기본값으로 복원했습니다.", Toast.LENGTH_SHORT).show();
        });
        save.setOnClickListener(v -> {
            int[] targets = new int[GameKeyMap.SOURCES.length];
            for (int i = 0; i < targets.length; i++) {
                int p = Math.max(0, Math.min(spinners[i].getSelectedItemPosition(), targets.length - 1));
                targets[i] = GameKeyMap.SOURCES[p];
            }
            EngineSettings.saveKeyMap(this, engine, targets);
            dialog.dismiss();
            Toast.makeText(this, "엔진 패드 키 설정 저장 완료", Toast.LENGTH_SHORT).show();
        });
        dialog.show();
        styleReleaseDialogWindow(dialog, 0.70f, 0.82f);
        prepareDialogForGamepad(dialog, spinners.length > 0 ? spinners[0] : save);
    }

    @Override public void onGameSettings(GameEntry game) {
        LinearLayout form = new LinearLayout(this);
        form.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(20);
        form.setPadding(pad, dp(8), pad, 0);

        TextView nameLabel = new TextView(this);
        nameLabel.setText(UiText.t(this, "게임 이름"));
        nameLabel.setTextSize(15);
        form.addView(nameLabel);

        EditText titleInput = new EditText(this);
        titleInput.setSingleLine(true);
        titleInput.setText(game.title);
        form.addView(titleInput, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

        TextView localeLabel = new TextView(this);
        localeLabel.setText(UiText.t(this, "로케일"));
        localeLabel.setTextSize(15);
        localeLabel.setPadding(0, dp(10), 0, 0);
        form.addView(localeLabel);

        Spinner localeSpinner = new Spinner(this);
        localeSpinner.setAdapter(new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_dropdown_item, GameLocaleSettings.gameLabels(this)));
        localeSpinner.setSelection(GameLocaleSettings.gameIndex(
                GameLocaleSettings.gameChoice(this, game)));
        form.addView(localeSpinner, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

        Spinner rubySpinner = null;
        Button rubyScanButton = null;
        Spinner rgssMouseSpinner = null;
        boolean rgss = game.engine == GameEntry.Engine.XP ||
                game.engine == GameEntry.Engine.VX || game.engine == GameEntry.Engine.VXACE;
        if (rgss) {
            TextView rubyLabel = new TextView(this);
            rubyLabel.setText(UiText.t(this, "Ruby 런타임"));
            rubyLabel.setTextSize(15);
            rubyLabel.setPadding(0, dp(10), 0, 0);
            form.addView(rubyLabel);

            rubySpinner = new Spinner(this);
            String[] rubyLabels = UiText.array(this, "엔진 기본값 사용",
                    "자동 (정밀판별 + fallback)", "Ruby 1.8", "Ruby 1.9", "Ruby 3.1");
            ArrayAdapter<String> adapter = new ArrayAdapter<>(this,
                    android.R.layout.simple_spinner_dropdown_item, rubyLabels);
            rubySpinner.setAdapter(adapter);
            String mode = RunnerCoordinator.gameRubyMode(this, game);
            int selected = mode == null ? 0 : "1.8".equals(mode) ? 2 :
                    "1.9".equals(mode) ? 3 : "3.1".equals(mode) ? 4 : 1;
            rubySpinner.setSelection(selected);
            form.addView(rubySpinner, new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

            TextView rubyDetect = new TextView(this);
            rubyDetect.setText(UiText.t(this,
                    "최근 판별: " + RubyRuntimeDetector.cachedDiagnostic(this, game)));
            rubyDetect.setTextSize(12);
            rubyDetect.setPadding(0, dp(4), 0, dp(4));
            form.addView(rubyDetect);

            rubyScanButton = new Button(this);
            rubyScanButton.setText(UiText.t(this, "Ruby 정밀 검사"));
            form.addView(rubyScanButton, new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

            TextView mouseLabel = new TextView(this);
            mouseLabel.setText(UiText.t(this, "RGSS 마우스 호환"));
            mouseLabel.setTextSize(15);
            mouseLabel.setPadding(0, dp(10), 0, 0);
            form.addView(mouseLabel);

            rgssMouseSpinner = new Spinner(this);
            String[] mouseLabels = UiText.array(this, "자동 (권장)", "호환 패치",
                    "마우스 스크립트 패스", "원본 유지");
            ArrayAdapter<String> mouseAdapter = new ArrayAdapter<>(this,
                    android.R.layout.simple_spinner_dropdown_item, mouseLabels);
            rgssMouseSpinner.setAdapter(mouseAdapter);
            String mouseMode = RunnerCoordinator.rgssMouseMode(this, game);
            int mousePos = "PATCH".equals(mouseMode) ? 1 : "PASS".equals(mouseMode) ? 2 :
                    "ORIGINAL".equals(mouseMode) ? 3 : 0;
            rgssMouseSpinner.setSelection(mousePos);
            form.addView(rgssMouseSpinner, new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        }

        Spinner mvRenderSpinner = null;
        Button mvScanButton = null;
        CheckBox mvMouseModeCheck = null;
        Spinner easySoundfontSpinner = null;
        Spinner easyFont1Spinner = null;
        Spinner easyFont2Spinner = null;
        List<EasyRpgCoreSettings.Choice> easySoundfontChoices = null;
        List<EasyRpgCoreSettings.Choice> easyFontChoices = null;
        if (game.engine == GameEntry.Engine.EASYRPG) {
            easySoundfontChoices = EasyRpgCoreSettings.gameSoundfontChoices(this, gameRoot);
            easyFontChoices = EasyRpgCoreSettings.gameFontChoices(this, gameRoot);

            TextView sfLabel = new TextView(this);
            sfLabel.setText("MIDI SoundFont");
            sfLabel.setTextSize(15);
            sfLabel.setPadding(0, dp(10), 0, 0);
            form.addView(sfLabel);

            easySoundfontSpinner = new Spinner(this);
            easySoundfontSpinner.setAdapter(new ArrayAdapter<>(this,
                    android.R.layout.simple_spinner_dropdown_item,
                    EasyRpgCoreSettings.labels(easySoundfontChoices)));
            easySoundfontSpinner.setSelection(EasyRpgCoreSettings.indexOf(
                    easySoundfontChoices, EasyRpgCoreSettings.gameSoundfontChoice(this, game)));
            form.addView(easySoundfontSpinner, new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

            TextView font1Label = new TextView(this);
            font1Label.setText("Font 1");
            font1Label.setTextSize(15);
            font1Label.setPadding(0, dp(10), 0, 0);
            form.addView(font1Label);

            easyFont1Spinner = new Spinner(this);
            easyFont1Spinner.setAdapter(new ArrayAdapter<>(this,
                    android.R.layout.simple_spinner_dropdown_item,
                    EasyRpgCoreSettings.labels(easyFontChoices)));
            easyFont1Spinner.setSelection(EasyRpgCoreSettings.indexOf(
                    easyFontChoices, EasyRpgCoreSettings.gameFont1Choice(this, game)));
            form.addView(easyFont1Spinner, new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

            TextView font2Label = new TextView(this);
            font2Label.setText("Font 2");
            font2Label.setTextSize(15);
            font2Label.setPadding(0, dp(10), 0, 0);
            form.addView(font2Label);

            easyFont2Spinner = new Spinner(this);
            easyFont2Spinner.setAdapter(new ArrayAdapter<>(this,
                    android.R.layout.simple_spinner_dropdown_item,
                    EasyRpgCoreSettings.labels(easyFontChoices)));
            easyFont2Spinner.setSelection(EasyRpgCoreSettings.indexOf(
                    easyFontChoices, EasyRpgCoreSettings.gameFont2Choice(this, game)));
            form.addView(easyFont2Spinner, new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        }

        boolean webEngine = game.engine == GameEntry.Engine.MV || game.engine == GameEntry.Engine.MZ;
        if (webEngine) {
            TextView renderLabel = new TextView(this);
            renderLabel.setText(UiText.t(this, "렌더링 모드"));
            renderLabel.setTextSize(15);
            renderLabel.setPadding(0, dp(10), 0, 0);
            form.addView(renderLabel);

            mvRenderSpinner = new Spinner(this);
            String[] renderLabels = UiText.array(this, "엔진 기본값 사용",
                    "자동 (권장)", "WebGL", "Canvas");
            ArrayAdapter<String> renderAdapter = new ArrayAdapter<>(this,
                    android.R.layout.simple_spinner_dropdown_item, renderLabels);
            mvRenderSpinner.setAdapter(renderAdapter);
            String renderMode = RunnerCoordinator.gameMvRenderMode(this, game);
            mvRenderSpinner.setSelection(renderMode == null ? 0 : "WEBGL".equals(renderMode) ? 2 :
                    ("CANVAS".equals(renderMode) ? 3 : 1));
            form.addView(mvRenderSpinner, new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

            TextView mvDetect = new TextView(this);
            mvDetect.setText(UiText.t(this,
                    "최근 검사: " + RunnerCoordinator.mvScanDiagnostic(this, game)));
            mvDetect.setTextSize(12);
            mvDetect.setPadding(0, dp(4), 0, dp(4));
            form.addView(mvDetect);

            mvScanButton = new Button(this);
            mvScanButton.setText(UiText.t(this, "호환성 정밀 검사"));
            form.addView(mvScanButton, new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

            mvMouseModeCheck = new CheckBox(this);
            mvMouseModeCheck.setText(UiText.t(this, "가상패드 마우스 모드"));
            mvMouseModeCheck.setChecked(RunnerCoordinator.mvMouseMode(this, game));
            form.addView(mvMouseModeCheck);

            TextView mouseModeInfo = new TextView(this);
            mouseModeInfo.setText(UiText.t(this,
                    "왼쪽 아날로그=커서 · 좌클릭/우클릭 · 휠 위/아래"));
            mouseModeInfo.setTextSize(12);
            mouseModeInfo.setPadding(dp(28), 0, 0, dp(6));
            form.addView(mouseModeInfo);
        }

        boolean customKeyMapSupported = true;
        Button keyMapButton = null;
        if (customKeyMapSupported) {
            keyMapButton = new Button(this);
            keyMapButton.setText(UiText.t(this, "패드 키 설정"));
            form.addView(keyMapButton, new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        }

        CheckBox padVisible = new CheckBox(this);
        padVisible.setText(UiText.t(this, "가상패드 표시"));
        padVisible.setChecked(RunnerCoordinator.padEnabled(this, game));
        form.addView(padVisible);

        CheckBox padLarge = new CheckBox(this);
        padLarge.setText(UiText.t(this, "가상패드 크게 표시"));
        padLarge.setChecked(RunnerCoordinator.padLarge(this, game));
        form.addView(padLarge);

        CheckBox inheritPad = new CheckBox(this);
        inheritPad.setText(UiText.t(this, game.engine == GameEntry.Engine.EASYRPG ?
                "엔진 기본 패드 설정 사용" : "앱 기본 패드 설정 사용"));
        inheritPad.setChecked(!RunnerCoordinator.hasGamePadSettings(this, game));
        padVisible.setEnabled(!inheritPad.isChecked());
        padLarge.setEnabled(!inheritPad.isChecked());
        inheritPad.setOnCheckedChangeListener((button, checked) -> {
            padVisible.setEnabled(!checked);
            padLarge.setEnabled(!checked);
        });
        form.addView(inheritPad);

        final Spinner finalRubySpinner = rubySpinner;
        final Spinner finalLocaleSpinner = localeSpinner;
        final Button finalRubyScanButton = rubyScanButton;
        final Spinner finalRgssMouseSpinner = rgssMouseSpinner;
        final Spinner finalMvRenderSpinner = mvRenderSpinner;
        final Button finalMvScanButton = mvScanButton;
        final CheckBox finalMvMouseModeCheck = mvMouseModeCheck;
        final Spinner finalEasySoundfontSpinner = easySoundfontSpinner;
        final Spinner finalEasyFont1Spinner = easyFont1Spinner;
        final Spinner finalEasyFont2Spinner = easyFont2Spinner;
        final List<EasyRpgCoreSettings.Choice> finalEasySoundfontChoices = easySoundfontChoices;
        final List<EasyRpgCoreSettings.Choice> finalEasyFontChoices = easyFontChoices;
        final Button finalKeyMapButton = keyMapButton;
        styleReleaseDialogTree(form);

        LinearLayout dialogRoot = new LinearLayout(this);
        dialogRoot.setOrientation(LinearLayout.VERTICAL);
        dialogRoot.setPadding(dp(18), dp(16), dp(18), dp(14));
        dialogRoot.setBackground(releaseRounded(Color.rgb(12, 27, 41), 18,
                Color.rgb(63, 92, 117), 1));

        TextView dialogTitle = releaseText("게임 설정", 21, true, Color.WHITE);
        dialogTitle.setPadding(dp(4), 0, 0, dp(12));
        dialogRoot.addView(dialogTitle, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

        LinearLayout body = new LinearLayout(this);
        body.setOrientation(LinearLayout.HORIZONTAL);
        body.setGravity(Gravity.TOP);

        LinearLayout gamePanel = new LinearLayout(this);
        gamePanel.setOrientation(LinearLayout.VERTICAL);
        gamePanel.setPadding(dp(12), dp(12), dp(12), dp(12));
        gamePanel.setBackground(releaseRounded(Color.argb(220, 20, 39, 56), 14,
                Color.rgb(51, 74, 94), 1));

        FrameLayout coverFrame = new FrameLayout(this);
        coverFrame.setBackground(releaseRounded(Color.rgb(28, 48, 65), 12,
                Color.rgb(65, 91, 113), 1));
        coverFrame.setClipToOutline(true);
        ImageView cover = new ImageView(this);
        cover.setScaleType(ImageView.ScaleType.CENTER_CROP);
        Bitmap coverBitmap = launcherView.getGameThumbnail(game.stableId());
        if (coverBitmap == null && gameRoot != null && game.thumbnailDocumentId != null) {
            coverBitmap = decodeThumbnail(gameRoot, game.thumbnailDocumentId, 480);
        }
        if (coverBitmap != null) cover.setImageBitmap(coverBitmap);
        coverFrame.addView(cover, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        LinearLayout.LayoutParams coverLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f);
        coverLp.setMargins(0, 0, 0, dp(2));
        gamePanel.addView(coverFrame, coverLp);

        TextView gameName = releaseText(game.title, 16, true, Color.WHITE);
        gameName.setPadding(dp(2), dp(12), dp(2), dp(6));
        gameName.setMaxLines(2);
        gamePanel.addView(gameName);

        TextView engineTag = releaseText(game.engine.label, 11, true, Color.WHITE);
        engineTag.setGravity(Gravity.CENTER);
        engineTag.setPadding(dp(10), dp(5), dp(10), dp(5));
        engineTag.setBackground(releaseRounded(Color.argb(175, 82, 88, 96), 12,
                Color.TRANSPARENT, 0));
        LinearLayout.LayoutParams tagLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        gamePanel.addView(engineTag, tagLp);

        LinearLayout.LayoutParams gamePanelLp = new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.MATCH_PARENT, 0.29f);
        gamePanelLp.setMarginEnd(dp(12));
        body.addView(gamePanel, gamePanelLp);

        ScrollView settingsScroll = new ScrollView(this);
        settingsScroll.setFillViewport(true);
        form.setBackground(releaseRounded(Color.argb(185, 18, 34, 50), 14,
                Color.rgb(43, 67, 88), 1));
        settingsScroll.addView(form);
        body.addView(settingsScroll, new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.MATCH_PARENT, 0.71f));
        dialogRoot.addView(body, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));

        LinearLayout actions = new LinearLayout(this);
        actions.setOrientation(LinearLayout.HORIZONTAL);
        actions.setGravity(Gravity.END | Gravity.CENTER_VERTICAL);
        actions.setPadding(0, dp(12), 0, 0);
        Button runButton = releaseButton("바로 실행", false);
        Button closeButton = releaseButton("닫기", false);
        Button saveSettingsButton = releaseButton("저장", true);
        LinearLayout.LayoutParams actionLp = new LinearLayout.LayoutParams(0, dp(44), 1f);
        actionLp.setMarginStart(dp(7));
        actions.addView(runButton, actionLp);
        LinearLayout.LayoutParams closeLp = new LinearLayout.LayoutParams(0, dp(44), 1f);
        closeLp.setMarginStart(dp(7));
        actions.addView(closeButton, closeLp);
        LinearLayout.LayoutParams saveLp = new LinearLayout.LayoutParams(0, dp(44), 1f);
        saveLp.setMarginStart(dp(7));
        actions.addView(saveSettingsButton, saveLp);
        dialogRoot.addView(actions);

        AlertDialog settingsDialog = new AlertDialog.Builder(this)
                .setView(dialogRoot)
                .create();

        saveSettingsButton.setOnClickListener(v -> {
            String ruby = "INHERIT";
            if (finalRubySpinner != null) {
                int pos = finalRubySpinner.getSelectedItemPosition();
                ruby = pos == 0 ? "INHERIT" : pos == 2 ? "1.8" : pos == 3 ? "1.9" :
                        pos == 4 ? "3.1" : "AUTO";
            }
            RunnerCoordinator.saveGameSettings(this, game, titleInput.getText().toString(), ruby,
                    inheritPad.isChecked(), padVisible.isChecked(), padLarge.isChecked());
            GameLocaleSettings.saveGameChoice(this, game,
                    GameLocaleSettings.gameCodeAt(finalLocaleSpinner.getSelectedItemPosition()));
            if (finalRgssMouseSpinner != null) {
                int mousePos = finalRgssMouseSpinner.getSelectedItemPosition();
                String mouseMode = mousePos == 1 ? "PATCH" : mousePos == 2 ? "PASS" :
                        mousePos == 3 ? "ORIGINAL" : "AUTO";
                RunnerCoordinator.saveRgssMouseMode(this, game, mouseMode);
            }
            if (finalMvRenderSpinner != null) {
                int renderPos = finalMvRenderSpinner.getSelectedItemPosition();
                String render = renderPos == 0 ? "INHERIT" : renderPos == 2 ? "WEBGL" :
                        renderPos == 3 ? "CANVAS" : "AUTO";
                RunnerCoordinator.saveMvRenderMode(this, game, render);
            }
            if (finalMvMouseModeCheck != null) {
                RunnerCoordinator.saveMvMouseMode(this, game, finalMvMouseModeCheck.isChecked());
            }
            if (finalEasySoundfontSpinner != null && finalEasyFont1Spinner != null &&
                    finalEasyFont2Spinner != null) {
                EasyRpgCoreSettings.saveChoices(this, game,
                        EasyRpgCoreSettings.idAt(finalEasySoundfontChoices,
                                finalEasySoundfontSpinner.getSelectedItemPosition()),
                        EasyRpgCoreSettings.idAt(finalEasyFontChoices,
                                finalEasyFont1Spinner.getSelectedItemPosition()),
                        EasyRpgCoreSettings.idAt(finalEasyFontChoices,
                                finalEasyFont2Spinner.getSelectedItemPosition()));
            }
            settingsDialog.dismiss();
            launcherView.resetTouchInteractionState();
            Toast.makeText(this, UiText.t(this, "설정을 저장했습니다."),
                    Toast.LENGTH_SHORT).show();
            scanGames();
        });
        runButton.setOnClickListener(v -> {
            settingsDialog.dismiss();
            launcherView.resetTouchInteractionState();
            onLaunch(game);
        });
        closeButton.setOnClickListener(v -> {
            settingsDialog.dismiss();
            launcherView.resetTouchInteractionState();
        });
        settingsDialog.setOnShowListener(dialog -> {
            if (finalRubyScanButton != null) {
                finalRubyScanButton.setOnClickListener(v -> {
                    settingsDialog.dismiss();
                    runRubyDeepScan(game);
                });
            }
            if (finalMvScanButton != null) {
                finalMvScanButton.setOnClickListener(v -> {
                    settingsDialog.dismiss();
                    runMvMzCompatibilityScan(game);
                });
            }
            if (finalKeyMapButton != null) {
                finalKeyMapButton.setOnClickListener(v -> showKeyMapDialog(game));
            }
        });
        settingsDialog.show();
        float aspect = getResources().getDisplayMetrics().widthPixels /
                (float) Math.max(1, getResources().getDisplayMetrics().heightPixels);
        styleReleaseDialogWindow(settingsDialog, aspect >= 2.0f ? 0.88f : 0.86f,
                aspect >= 2.0f ? 0.94f : 0.86f);
        View settingsFocus = finalRubySpinner != null ? finalRubySpinner :
                (finalMvRenderSpinner != null ? finalMvRenderSpinner :
                        (finalEasySoundfontSpinner != null ? finalEasySoundfontSpinner :
                                (finalKeyMapButton != null ? finalKeyMapButton : padVisible)));
        prepareDialogForGamepad(settingsDialog, settingsFocus);
    }

    private void runRubyDeepScan(GameEntry game) {
        if (gameRoot == null) return;
        launcherView.setBusy(true, "Ruby 정밀 스캔 준비 중…");
        RunnerCoordinator.scanRubyRuntime(this, runner, gameRoot, game, new RunnerCoordinator.RubyScanCallback() {
            @Override public void onStatus(String message) {
                launcherView.setStatus(message);
                launcherView.setBusyMessage(message);
            }

            @Override public void onResult(String runtime, String diagnostic) {
                launcherView.setBusy(false, null);
                launcherView.setStatus("Ruby 자동 판별: " + runtime);
                Toast.makeText(LauncherActivity.this,
                        "정밀 스캔 완료 · Ruby " + runtime + "\n" + diagnostic,
                        Toast.LENGTH_LONG).show();
            }

            @Override public void onError(String message) {
                launcherView.setBusy(false, null);
                launcherView.setStatus(message);
                Toast.makeText(LauncherActivity.this, message, Toast.LENGTH_LONG).show();
            }
        });
    }

    private void runMvMzCompatibilityScan(GameEntry game) {
        if (gameRoot == null) return;
        launcherView.setBusy(true, "MV/MZ 호환성 정밀 검사 준비 중…");
        RunnerCoordinator.scanMvMzCompatibility(this, runner, gameRoot, game,
                new RunnerCoordinator.MvMzScanCallback() {
                    @Override public void onStatus(String message) {
                        launcherView.setStatus(message);
                        launcherView.setBusyMessage(message);
                    }

                    @Override public void onResult(String diagnostic, String recommendedRenderer) {
                        launcherView.setBusy(false, null);
                        launcherView.setStatus("MV/MZ 검사 완료 · 권장 " +
                                ("WEBGL".equals(recommendedRenderer) ? "WebGL" : "자동"));
                        Toast.makeText(LauncherActivity.this,
                                "MV/MZ 호환성 검사 완료\n" + diagnostic,
                                Toast.LENGTH_LONG).show();
                    }

                    @Override public void onError(String message) {
                        launcherView.setBusy(false, null);
                        launcherView.setStatus(message);
                        Toast.makeText(LauncherActivity.this, message, Toast.LENGTH_LONG).show();
                    }
                });
    }

    private void showKeyMapDialog(GameEntry game) {
        int[] current = GameKeyMap.load(this, game);
        LinearLayout form = new LinearLayout(this);
        form.setOrientation(LinearLayout.VERTICAL);
        form.setPadding(dp(14), dp(8), dp(14), dp(8));

        TextView info = new TextView(this);
        info.setText("");
        info.setVisibility(View.GONE);
        info.setTextSize(13);
        info.setPadding(0, 0, 0, dp(6));
        form.addView(info);

        TextView saveState = new TextView(this);
        saveState.setText(UiText.t(this, "저장 전까지 게임별 키맵은 변경되지 않습니다."));
        saveState.setTextSize(12);
        saveState.setPadding(0, 0, 0, dp(8));
        form.addView(saveState);

        Spinner[] spinners = new Spinner[GameKeyMap.SOURCES.length];

        for (int i = 0; i < GameKeyMap.SOURCES.length; i++) {
            LinearLayout row = new LinearLayout(this);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setGravity(android.view.Gravity.CENTER_VERTICAL);

            TextView label = new TextView(this);
            label.setText(GameKeyMap.LABELS[i] + "  →");
            label.setTextSize(15);
            row.addView(label, new LinearLayout.LayoutParams(0,
                    LinearLayout.LayoutParams.WRAP_CONTENT, 0.36f));

            Spinner spinner = new Spinner(this);
            ArrayAdapter<String> adapter = new ArrayAdapter<>(this,
                    android.R.layout.simple_spinner_dropdown_item, GameKeyMap.LABELS);
            spinner.setAdapter(adapter);
            int selected = GameKeyMap.targetIndex(current[i]);
            spinner.setSelection(selected < 0 ? i : selected);
            spinners[i] = spinner;
            row.addView(spinner, new LinearLayout.LayoutParams(0,
                    LinearLayout.LayoutParams.WRAP_CONTENT, 0.64f));
            form.addView(row);
        }

        ScrollView scroll = new ScrollView(this);
        scroll.addView(form);
        int targetHeight = Math.max(dp(180), (int) (getResources().getDisplayMetrics().heightPixels * 0.48f));
        scroll.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, targetHeight));

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(18), dp(16), dp(18), dp(14));
        root.setBackground(releaseRounded(Color.rgb(12, 27, 41), 18,
                Color.rgb(63, 92, 117), 1));
        TextView keyMapTitle = releaseText(game.title + "  ·  패드 키 설정", 20, true, Color.WHITE);
        keyMapTitle.setPadding(dp(2), 0, 0, dp(10));
        root.addView(keyMapTitle);
        root.addView(scroll);

        LinearLayout buttons = new LinearLayout(this);
        buttons.setOrientation(LinearLayout.HORIZONTAL);
        buttons.setPadding(0, dp(8), 0, 0);

        Button resetButton = new Button(this);
        resetButton.setText(UiText.t(this, "엔진 기본값 사용"));
        buttons.addView(resetButton, new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1f));

        Button saveButton = new Button(this);
        saveButton.setText(UiText.t(this, "저장 후 닫기"));
        buttons.addView(saveButton, new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        root.addView(buttons);
        styleReleaseDialogTree(root);
        styleReleaseButton(saveButton, true, false);
        saveButton.setOnFocusChangeListener((v, focused) -> styleReleaseButton(saveButton, true, focused));

        AlertDialog dialog = new AlertDialog.Builder(this)
                .setView(root)
                .create();

        resetButton.setOnClickListener(v -> {
            GameKeyMap.reset(this, game);
            Toast.makeText(this, UiText.t(this,
                    "게임별 키맵을 지우고 엔진 기본값을 사용합니다."),
                    Toast.LENGTH_SHORT).show();
            dialog.dismiss();
        });
        saveButton.setOnClickListener(v -> {
            for (int i = 0; i < spinners.length; i++) {
                int position = spinners[i].getSelectedItemPosition();
                int safe = Math.max(0, Math.min(position, GameKeyMap.SOURCES.length - 1));
                current[i] = GameKeyMap.SOURCES[safe];
            }
            GameKeyMap.save(this, game, current);
            Toast.makeText(this, UiText.t(this, "게임별 키맵 저장 완료"),
                    Toast.LENGTH_SHORT).show();
            dialog.dismiss();
        });
        dialog.show();
        styleReleaseDialogWindow(dialog, 0.70f, 0.82f);
        prepareDialogForGamepad(dialog, spinners.length > 0 ? spinners[0] : saveButton);
    }

    private void prepareDialogForGamepad(AlertDialog dialog, View preferredFocus) {
        AlertDialog previous = activeDialog;
        activeDialog = dialog;
        dialog.setOnDismissListener(d -> {
            if (activeDialog == dialog) {
                activeDialog = previous != null && previous.isShowing() ? previous : null;
            }
            if (launcherView != null) launcherView.resetTouchInteractionState();
            launcherView.post(this::enterImmersive);
        });
        dialog.setOnKeyListener((d, keyCode, event) -> {
            if ((keyCode == KeyEvent.KEYCODE_BUTTON_B || keyCode == KeyEvent.KEYCODE_BACK) &&
                    event.getAction() == KeyEvent.ACTION_UP) {
                dialog.dismiss();
                return true;
            }
            if (keyCode == KeyEvent.KEYCODE_BUTTON_A) {
                View focused = dialog.getCurrentFocus();
                if (focused == null) focused = preferredFocus;
                if (focused != null) {
                    KeyEvent mapped = new KeyEvent(event.getDownTime(), event.getEventTime(),
                            event.getAction(), KeyEvent.KEYCODE_DPAD_CENTER, event.getRepeatCount(),
                            event.getMetaState(), event.getDeviceId(), event.getScanCode(),
                            event.getFlags(), event.getSource());
                    focused.dispatchKeyEvent(mapped);
                }
                return true;
            }
            return false;
        });
        if (preferredFocus != null) preferredFocus.requestFocus();
    }

    private GradientDrawable releaseRounded(int color, int radiusDp, int strokeColor, int strokeDp) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(color);
        drawable.setCornerRadius(dp(radiusDp));
        if (strokeDp > 0 && Color.alpha(strokeColor) > 0) drawable.setStroke(dp(strokeDp), strokeColor);
        return drawable;
    }

    private TextView releaseText(String value, int sizeSp, boolean bold, int color) {
        TextView view = new TextView(this);
        view.setText(UiText.t(this, value));
        view.setTextSize(sizeSp);
        view.setTextColor(color);
        view.setTypeface(Typeface.create("sans", bold ? Typeface.BOLD : Typeface.NORMAL));
        return view;
    }

    private Button releaseButton(String label, boolean primary) {
        Button button = new Button(this);
        button.setText(UiText.t(this, label));
        button.setTextColor(Color.WHITE);
        button.setTextSize(14);
        button.setAllCaps(false);
        button.setPadding(dp(12), 0, dp(12), 0);
        styleReleaseButton(button, primary, false);
        button.setOnFocusChangeListener((v, focused) -> styleReleaseButton(button, primary, focused));
        return button;
    }

    private void styleReleaseButton(Button button, boolean primary, boolean focused) {
        int fill = primary ? Color.rgb(37, 103, 151) : Color.rgb(33, 51, 69);
        int stroke = focused ? Color.rgb(112, 198, 255) :
                (primary ? Color.rgb(63, 139, 194) : Color.rgb(55, 79, 99));
        button.setBackground(releaseRounded(fill, 9, stroke, focused ? 2 : 1));
    }

    private void styleReleaseDialogTree(View view) {
        if (view instanceof Button) {
            Button button = (Button) view;
            button.setTextColor(Color.WHITE);
            button.setTextSize(13);
            button.setAllCaps(false);
            styleReleaseButton(button, false, false);
            button.setOnFocusChangeListener((v, focused) -> styleReleaseButton(button, false, focused));
        } else if (view instanceof EditText) {
            EditText edit = (EditText) view;
            edit.setTextColor(Color.WHITE);
            edit.setHintTextColor(Color.rgb(133, 153, 173));
            edit.setBackground(releaseRounded(Color.rgb(27, 45, 61), 9,
                    Color.rgb(58, 85, 107), 1));
            edit.setPadding(dp(12), dp(7), dp(12), dp(7));
        } else if (view instanceof CheckBox) {
            CheckBox check = (CheckBox) view;
            check.setTextColor(Color.rgb(226, 235, 244));
            check.setPadding(dp(2), dp(4), dp(2), dp(4));
        } else if (view instanceof TextView) {
            TextView label = (TextView) view;
            label.setTextColor(Color.rgb(222, 232, 242));
        } else if (view instanceof Spinner) {
            Spinner spinner = (Spinner) view;
            spinner.setBackground(releaseRounded(Color.rgb(27, 45, 61), 9,
                    Color.rgb(58, 85, 107), 1));
            spinner.setPadding(dp(8), dp(3), dp(8), dp(3));
            spinner.setMinimumHeight(dp(42));
            spinner.setOnFocusChangeListener((v, focused) -> spinner.setBackground(
                    releaseRounded(Color.rgb(27, 45, 61), 9,
                            focused ? Color.rgb(112, 198, 255) : Color.rgb(58, 85, 107),
                            focused ? 2 : 1)));
        }

        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int i = 0; i < group.getChildCount(); i++) styleReleaseDialogTree(group.getChildAt(i));
        }
    }

    private void styleReleaseDialogWindow(AlertDialog dialog, float widthFraction, float heightFraction) {
        Window window = dialog.getWindow();
        if (window == null) return;
        window.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        window.addFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
        WindowManager.LayoutParams attrs = window.getAttributes();
        attrs.dimAmount = 0.78f;
        window.setAttributes(attrs);
        int width = Math.round(getResources().getDisplayMetrics().widthPixels * widthFraction);
        int height = Math.round(getResources().getDisplayMetrics().heightPixels * heightFraction);
        window.setLayout(width, height);
        View decor = window.getDecorView();
        if (decor != null) applyLegacyImmersive(decor);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private String folderLabel(Uri treeUri) {
        try {
            String id = DocumentsContract.getTreeDocumentId(treeUri);
            Uri doc = DocumentsContract.buildDocumentUriUsingTree(treeUri, id);
            try (android.database.Cursor c = getContentResolver().query(doc,
                    new String[]{DocumentsContract.Document.COLUMN_DISPLAY_NAME}, null, null, null)) {
                if (c != null && c.moveToFirst()) return c.getString(0) + "  ·  " + treeUri.getAuthority();
            }
        } catch (Exception ignored) {}
        return treeUri.toString();
    }

    @Override protected void onDestroy() {
        scanner.shutdownNow();
        runner.shutdownNow();
        shutdownWatcher.shutdownNow();
        super.onDestroy();
    }
}
