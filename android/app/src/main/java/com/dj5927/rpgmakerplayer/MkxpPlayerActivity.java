package com.dj5927.rpgmakerplayer;

import android.app.AlertDialog;
import android.content.pm.ActivityInfo;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.system.Os;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.KeyEvent;
import android.window.OnBackInvokedDispatcher;
import android.widget.RelativeLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.easyrpg.player.R;
import org.libsdl.app.SDLActivity;
import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStreamWriter;
import java.nio.charset.StandardCharsets;
import java.util.List;

public final class MkxpPlayerActivity extends SDLActivity {
    public static final String EXTRA_GAME_PATH = "rpgmp_game_path";
    public static final String EXTRA_RTP_ROOT_PATH = "rpgmp_rtp_root_path";
    public static final String EXTRA_COMPAT_ROOT_PATH = "rpgmp_compat_root_path";
    public static final String EXTRA_ENGINE = "rpgmp_engine";
    public static final String EXTRA_TITLE = "rpgmp_title";
    public static final String EXTRA_PAD_ENABLED = "rpgmp_pad_enabled";
    public static final String EXTRA_PAD_LARGE = "rpgmp_pad_large";
    public static final String EXTRA_RUBY_MODE = "rpgmp_ruby_mode";
    public static final String EXTRA_AUTO_RUBY_FALLBACK = "rpgmp_auto_ruby_fallback";
    public static final String EXTRA_RUBY_FALLBACK_TOKEN = "rpgmp_ruby_fallback_token";
    public static final String EXTRA_RUBY_FALLBACK_INDEX = "rpgmp_ruby_fallback_index";
    public static final String EXTRA_RUBY_FALLBACK_FAIL_MARKER =
            "rpgmp_ruby_fallback_fail_marker";
    public static final String EXTRA_RUBY_FALLBACK_SUCCESS_MARKER =
            "rpgmp_ruby_fallback_success_marker";
    public static final String EXTRA_KEYMAP = "rpgmp_keymap";
    public static final String EXTRA_LOCALE = "rpgmp_locale";
    public static final String EXTRA_LOG_PATH = "rpgmp_log_path";

    private String gamePath;
    private String sharedRtpPath;
    private String compatRootPath;
    private String engine;
    private String configPath;
    private String runtimeLibrary;
    private String rubyLibrary;
    private String gameLogPath;
    private int[] keyMap;
    private boolean exitDialogVisible;
    private boolean explicitExitRequested;
    private boolean autoRubyFallback;
    private String rubyFallbackToken;
    private String rubyFallbackSuccessMarkerPath;
    private View rubyLoadingOverlay;
    private final Handler rubyLoadingHandler = new Handler(Looper.getMainLooper());
    private final Runnable rubyLoadingWatcher = new Runnable() {
        @Override public void run() {
            if (!autoRubyFallback || rubyLoadingOverlay == null) return;
            if (rubyFallbackSuccessMarkerPath != null &&
                    !rubyFallbackSuccessMarkerPath.isEmpty() &&
                    new File(rubyFallbackSuccessMarkerPath).isFile()) {
                rubyLoadingOverlay.setVisibility(View.GONE);
                return;
            }
            rubyLoadingHandler.postDelayed(this, 120);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,
                WindowManager.LayoutParams.FLAG_FULLSCREEN);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        gamePath = getIntent().getStringExtra(EXTRA_GAME_PATH);
        sharedRtpPath = getIntent().getStringExtra(EXTRA_RTP_ROOT_PATH);
        compatRootPath = getIntent().getStringExtra(EXTRA_COMPAT_ROOT_PATH);
        engine = getIntent().getStringExtra(EXTRA_ENGINE);
        keyMap = getIntent().getIntArrayExtra(EXTRA_KEYMAP);
        gameLogPath = getIntent().getStringExtra(EXTRA_LOG_PATH);
        autoRubyFallback = getIntent().getBooleanExtra(EXTRA_AUTO_RUBY_FALLBACK, false);
        rubyFallbackToken = getIntent().getStringExtra(EXTRA_RUBY_FALLBACK_TOKEN);
        String rubyFallbackFailMarker =
                getIntent().getStringExtra(EXTRA_RUBY_FALLBACK_FAIL_MARKER);
        String rubyFallbackSuccessMarker =
                getIntent().getStringExtra(EXTRA_RUBY_FALLBACK_SUCCESS_MARKER);
        rubyFallbackSuccessMarkerPath = rubyFallbackSuccessMarker;
        try {
            Os.setenv("RPGMP_AUTO_RUBY_FALLBACK", autoRubyFallback ? "1" : "0", true);
            Os.setenv("RPGMP_RUBY_FALLBACK_FAIL_MARKER",
                    rubyFallbackFailMarker == null ? "" : rubyFallbackFailMarker, true);
            Os.setenv("RPGMP_RUBY_FALLBACK_SUCCESS_MARKER",
                    rubyFallbackSuccessMarker == null ? "" : rubyFallbackSuccessMarker, true);
        } catch (Throwable ignored) {}
        GameLocaleSettings.applyProcess(getIntent().getStringExtra(EXTRA_LOCALE));
        if (gamePath == null || engine == null) {
            finish();
            return;
        }
        applyGameUserEnvironment(new File(gamePath));
        applyNativeLogEnvironment();
        applyCompatEnvironment();
        GameLog.append(gameLogPath, "MKXP",
                "launch engine=" + engine + " gamePath=" + gamePath);

        String rubyMode = getIntent().getStringExtra(EXTRA_RUBY_MODE);
        if ("3.1".equals(rubyMode)) {
            runtimeLibrary = "mkxp-z-modern";
            rubyLibrary = "ruby";
            GameLog.append(gameLogPath, "MKXP",
                    "runtime=modern core=mkxp-z-2.4.2 git=37a04d1 ruby=3.1");
        } else if ("1.9".equals(rubyMode)) {
            runtimeLibrary = "mkxp-z-193";
            rubyLibrary = "ruby193";
            GameLog.append(gameLogPath, "MKXP",
                    "runtime=legacy ruby=1.9 fallback=" + autoRubyFallback);
        } else if ("1.8".equals(rubyMode)) {
            runtimeLibrary = "mkxp-z-187";
            rubyLibrary = "ruby187";
            GameLog.append(gameLogPath, "MKXP",
                    "runtime=legacy ruby=1.8 fallback=" + autoRubyFallback);
        } else if ("VXACE".equals(engine)) {
            runtimeLibrary = "mkxp-z-193";
            rubyLibrary = "ruby193";
        } else {
            runtimeLibrary = "mkxp-z-187";
            rubyLibrary = "ruby187";
        }
        try {
            configPath = writeConfig();
        } catch (Exception e) {
            android.util.Log.e("RPGMP-MKXP", "Config creation failed", e);
            finish();
            return;
        }

        super.onCreate(savedInstanceState);
        if (mBrokenLibraries) {
            if (autoRubyFallback) finish();
            return;
        }

        View navigation = findViewById(R.id.nav_view);
        if (navigation != null) navigation.setVisibility(View.GONE);

        ViewGroup main = findViewById(R.id.main_layout);
        if (main != null && mSurface != null) {
            if (mSurface.getParent() instanceof ViewGroup) {
                ((ViewGroup) mSurface.getParent()).removeView(mSurface);
            }
            mLayout = main;
            main.addView(mSurface, 0, new RelativeLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

            if (getIntent().getBooleanExtra(EXTRA_PAD_ENABLED, true)) {
                VirtualGamepadView pad = new VirtualGamepadView(this);
                pad.setLargeMode(getIntent().getBooleanExtra(EXTRA_PAD_LARGE, false));
                pad.setListener((keyCode, down) -> {
                    int mapped = mapVirtualKey(GameKeyMap.remap(keyCode, keyMap));
                    if (down) SDLActivity.onNativeKeyDown(mapped);
                    else SDLActivity.onNativeKeyUp(mapped);
                });
                main.addView(pad, new RelativeLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
            }
            if (autoRubyFallback) {
                showRubyLoadingOverlay(main, getIntent().getStringExtra(EXTRA_RUBY_MODE));
            }
        }

        getWindow().getDecorView().post(this::enterImmersive);

        if (Build.VERSION.SDK_INT >= 33) {
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                    OnBackInvokedDispatcher.PRIORITY_DEFAULT, this::showExitConfirmation);
        }
    }


    private void showRubyLoadingOverlay(ViewGroup root, String ruby) {
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setGravity(Gravity.CENTER);
        panel.setPadding(dp(34), dp(24), dp(34), dp(24));
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(Color.rgb(27, 37, 50));
        bg.setCornerRadius(dp(14));
        bg.setStroke(dp(1), Color.rgb(75, 112, 151));
        panel.setBackground(bg);

        TextView title = new TextView(this);
        title.setText(UiText.t(this, "게임에 알맞는 루비 탐색중"));
        title.setTextColor(Color.WHITE);
        title.setTextSize(22);
        title.setGravity(Gravity.CENTER);
        title.setTypeface(android.graphics.Typeface.DEFAULT, android.graphics.Typeface.BOLD);

        TextView status = new TextView(this);
        status.setText(UiText.t(this,
                "Ruby " + (ruby == null ? "" : ruby) + " 로딩중"));
        status.setTextColor(Color.rgb(190, 205, 222));
        status.setTextSize(16);
        status.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams statusLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        statusLp.topMargin = dp(12);
        panel.addView(title, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        panel.addView(status, statusLp);

        RelativeLayout.LayoutParams lp = new RelativeLayout.LayoutParams(
                dp(430), ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.addRule(RelativeLayout.CENTER_IN_PARENT);
        root.addView(panel, lp);
        rubyLoadingOverlay = panel;
        rubyLoadingHandler.removeCallbacks(rubyLoadingWatcher);
        rubyLoadingHandler.post(rubyLoadingWatcher);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
    private void enterImmersive() {
        View decor = getWindow().getDecorView();
        if (decor == null) return;
        decor.setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                        View.SYSTEM_UI_FLAG_LAYOUT_STABLE |
                        View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
                        View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
                        View.SYSTEM_UI_FLAG_FULLSCREEN |
                        View.SYSTEM_UI_FLAG_HIDE_NAVIGATION);
        try {
            getWindow().setStatusBarColor(android.graphics.Color.TRANSPARENT);
            getWindow().setNavigationBarColor(android.graphics.Color.TRANSPARENT);
            if (Build.VERSION.SDK_INT >= 29) {
                getWindow().setStatusBarContrastEnforced(false);
                getWindow().setNavigationBarContrastEnforced(false);
            }
            if (Build.VERSION.SDK_INT >= 30) {
                getWindow().setDecorFitsSystemWindows(false);
                WindowInsetsController controller = decor.getWindowInsetsController();
                if (controller != null) {
                    controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                    controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                }
            }
        } catch (Throwable t) {
            android.util.Log.w("RPGMP-MKXP", "immersive fallback", t);
        }
    }

    @Override protected void onResume() {
        super.onResume();
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        View decor = getWindow().getDecorView();
        if (decor != null) {
            decor.post(this::enterImmersive);
            decor.postDelayed(this::enterImmersive, 250);
        }
    }

    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
            View decor = getWindow().getDecorView();
            if (decor != null) {
                decor.post(this::enterImmersive);
                decor.postDelayed(this::enterImmersive, 250);
            }
        }
    }

    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        // SDL normally recalculates orientation from the game window size/hints.
        // RPGMP player mode must stay landscape like the launcher.
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        android.util.Log.i("RPGMP-MKXP",
                "ORIENTATION_LOCK landscape w=" + w + " h=" + h +
                        " resizable=" + resizable + " hint=" + hint);
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        int keyCode = event.getKeyCode();
        if (GameKeyMap.sourceIndex(keyCode) >= 0) {
            int target = GameKeyMap.remap(keyCode, keyMap);
            int mapped = mapVirtualKey(target);
            if (event.getAction() == KeyEvent.ACTION_DOWN) SDLActivity.onNativeKeyDown(mapped);
            else if (event.getAction() == KeyEvent.ACTION_UP) SDLActivity.onNativeKeyUp(mapped);
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    @Override
    public void onBackPressed() {
        showExitConfirmation();
    }

    private void showExitConfirmation() {
        if (exitDialogVisible || isFinishing() || isDestroyed()) return;
        exitDialogVisible = true;
        new AlertDialog.Builder(this)
                .setTitle(UiText.t(this, "게임 종료"))
                .setMessage(UiText.t(this, "게임을 종료하시겠습니까?"))
                .setPositiveButton(UiText.t(this, "예"), (dialog, which) -> requestRealExit())
                .setNegativeButton(UiText.t(this, "아니오"), (dialog, which) -> exitDialogVisible = false)
                .setOnCancelListener(dialog -> exitDialogVisible = false)
                .show();
    }

    private void requestRealExit() {
        exitDialogVisible = false;
        explicitExitRequested = true;
        markRubyFallbackUserExit();
        try {
            if (!mBrokenLibraries) SDLActivity.nativeSendQuit();
        } catch (Throwable ignored) {}
        finish();
    }

    private void markRubyFallbackUserExit() {
        if (!autoRubyFallback || rubyFallbackToken == null || rubyFallbackToken.isEmpty()) return;
        try {
            File dir = new File(getFilesDir(), "ruby_fallback");
            if (!dir.exists()) dir.mkdirs();
            File marker = new File(dir, rubyFallbackToken + ".user_exit");
            try (FileOutputStream out = new FileOutputStream(marker, false)) {
                out.write("1".getBytes(StandardCharsets.US_ASCII));
            }
        } catch (Exception ignored) {}
    }

    private int mapVirtualKey(int keyCode) {
        if ("XP".equals(engine) &&
                (keyCode == KeyEvent.KEYCODE_BUTTON_A ||
                        keyCode == KeyEvent.KEYCODE_BUTTON_START)) {
            return KeyEvent.KEYCODE_ENTER;
        }
        switch (keyCode) {
            case KeyEvent.KEYCODE_BUTTON_A: return KeyEvent.KEYCODE_Z;
            case KeyEvent.KEYCODE_BUTTON_B: return KeyEvent.KEYCODE_X;
            case KeyEvent.KEYCODE_BUTTON_X: return KeyEvent.KEYCODE_A;
            case KeyEvent.KEYCODE_BUTTON_Y: return KeyEvent.KEYCODE_S;
            case KeyEvent.KEYCODE_BUTTON_L1: return KeyEvent.KEYCODE_Q;
            case KeyEvent.KEYCODE_BUTTON_R1: return KeyEvent.KEYCODE_W;
            case KeyEvent.KEYCODE_BUTTON_L2: return KeyEvent.KEYCODE_SHIFT_LEFT;
            case KeyEvent.KEYCODE_BUTTON_R2: return KeyEvent.KEYCODE_D;
            case KeyEvent.KEYCODE_BUTTON_START: return KeyEvent.KEYCODE_ENTER;
            case KeyEvent.KEYCODE_BUTTON_SELECT: return KeyEvent.KEYCODE_ESCAPE;
            default: return keyCode;
        }
    }

    @Override
    protected void onDestroy() {
        rubyLoadingHandler.removeCallbacks(rubyLoadingWatcher);
        super.onDestroy();
        if (explicitExitRequested) {
            new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(
                    () -> android.os.Process.killProcess(android.os.Process.myPid()), 700);
        }
    }

    @Override
    protected boolean shouldCheckNativeSdlVersion() {
        // A009 self-built MKXP shares EasyRPG's newer SDL2 runtime. Required
        // SDL_* symbols are verified by tools/check_shared_sdl_abi.sh.
        return false;
    }

    @Override
    protected String[] getLibraries() {
        return new String[]{
                "SDL2",
                "SDL2_image",
                "SDL2_ttf",
                "SDL2_sound",
                "openal",
                rubyLibrary,
                runtimeLibrary
        };
    }

    @Override
    protected String[] getArguments() {
        return new String[]{configPath};
    }

    private String writeConfig() throws Exception {
        File game = new File(gamePath);
        File runtimeDir = new File(getFilesDir(), "mkxp_runtime");
        if (!runtimeDir.exists() && !runtimeDir.mkdirs()) {
            throw new IllegalStateException("Cannot create " + runtimeDir);
        }
        File cfg = new File(runtimeDir, Integer.toHexString(gamePath.hashCode()) + ".json");

        int rgssVersion;
        int width;
        int height;
        if ("XP".equals(engine)) {
            rgssVersion = 1;
            width = 640;
            height = 480;
        } else if ("VX".equals(engine)) {
            rgssVersion = 2;
            width = 544;
            height = 416;
        } else {
            rgssVersion = 3;
            width = 544;
            height = 416;
        }

        File userTemp = new File(game, "UserData/Temp");
        File userData = new File(game, "UserData/AppData");
        userTemp.mkdirs();
        userData.mkdirs();

        JSONObject root = new JSONObject();
        root.put("rgssVersion", rgssVersion);
        root.put("gameFolder", game.getAbsolutePath());
        root.put("execName", "Game");
        root.put("subImageFix", true);
        root.put("smoothScaling", 1);
        root.put("syncToRefreshrate", false);
        root.put("enableBlitting", false);
        root.put("vsync", false);
        root.put("frameSkip", false);
        root.put("fullscreen", true);
        root.put("fixedAspectRatio", true);
        root.put("integerScalingActive", true);
        root.put("integerScalingLastMile", true);
        root.put("smoothScalingDown", 1);
        root.put("defScreenW", width);
        root.put("defScreenH", height);
        root.put("debugMode", false);
        root.put("solidFonts", new JSONArray());
        root.put("useScriptNames", true);
        root.put("maxTextureSize", 0);
        root.put("pathCache", true);
        root.put("SESourceCount", 32);
        root.put("fontHeightReporting", 1);
        File mkxpSoundFont = ensureMkxpSoundFont();
        root.put("midiSoundFont", mkxpSoundFont.getAbsolutePath());
        root.put("midiChorus", false);
        root.put("midiReverb", false);
        GameLog.append(gameLogPath, "MKXP",
                "midiSoundFont=" + mkxpSoundFont.getAbsolutePath());

        JSONArray preloads = new JSONArray();
        File compatCommon = compatRootPath == null || compatRootPath.isEmpty()
                ? new File(game, "compat/common")
                : new File(compatRootPath, "compat/common");
        boolean modernRubyRuntime =
                "mkxp-z-31".equals(runtimeLibrary) || "mkxp-z-modern".equals(runtimeLibrary);
        preloads.put(new File(compatCommon, "ruby_classic_wrap.rb").getAbsolutePath());
        if (modernRubyRuntime) {
            preloads.put(new File(compatCommon, "ruby31_legacy_wrap.rb").getAbsolutePath());
        }
        preloads.put(new File(compatCommon, "mkxp_wrap.rb").getAbsolutePath());
        preloads.put(new File(compatCommon, "win32_wrap.rb").getAbsolutePath());
        preloads.put(new File(compatCommon, "kgl2_wrap.rb").getAbsolutePath());
        preloads.put(new File(compatCommon, "native_ext_compat.rb").getAbsolutePath());
        preloads.put(new File(compatCommon, "fix_volume.rb").getAbsolutePath());
        root.put("preloadScript", preloads);

        if (modernRubyRuntime) {
            File compatBase = compatRootPath == null || compatRootPath.isEmpty()
                    ? new File(game, "compat")
                    : new File(compatRootPath, "compat");
            File ruby31 = new File(compatBase, "ruby31lib");
            JSONArray rubyLoadpath = new JSONArray();
            rubyLoadpath.put(ruby31.getAbsolutePath());
            rubyLoadpath.put(new File(ruby31, "aarch64-linux-android-android").getAbsolutePath());
            root.put("rubyLoadpath", rubyLoadpath);
            GameLog.append(gameLogPath, "RUBY31",
                    "stdlib=" + ruby31.getAbsolutePath());
        }

        JSONArray rtp = new JSONArray();
        if (compatRootPath != null && !compatRootPath.isEmpty()) {
            rtp.put(new File(compatRootPath).getAbsolutePath());
        }
        File patch = new File(game, "patch");
        if (patch.isDirectory()) rtp.put(patch.getAbsolutePath());
        rtp.put(game.getAbsolutePath());
        File sharedRtpRoot = sharedRtpPath == null || sharedRtpPath.isEmpty()
                ? null : new File(sharedRtpPath);
        List<File> fallbackRoots = RgssRtpFallback.searchRootsFromRtpRoot(sharedRtpRoot, engine);
        for (File fallback : fallbackRoots) rtp.put(fallback.getAbsolutePath());
        root.put("RTP", rtp);

        if (!fallbackRoots.isEmpty()) {
            File fontDir = RgssRtpFallback.fontDirectoryFromRtpRoot(sharedRtpRoot);
            android.util.Log.i("RPGMP-MKXP", "RTP fallback engine=" + engine +
                    " roots=" + fallbackRoots +
                    " fonts=" + (fontDir == null ? "none" : fontDir.getAbsolutePath()));
        }

        try (OutputStreamWriter out = new OutputStreamWriter(
                new FileOutputStream(cfg, false), StandardCharsets.UTF_8)) {
            out.write(root.toString());
        }
        return cfg.getAbsolutePath();
    }

    private File ensureMkxpSoundFont() throws Exception {
        File dir = new File(getFilesDir(), "mkxp_soundfonts");
        if (!dir.exists() && !dir.mkdirs()) {
            throw new IllegalStateException("Cannot create " + dir);
        }
        File generalUser = new File(dir, "GeneralUser_GS_v1.471.sf2");
        if (generalUser.isFile() && generalUser.length() > 20L * 1024L * 1024L) {
            return generalUser;
        }
        try {
            File tmp = new File(dir, "GeneralUser_GS_v1.471.sf2.tmp");
            try (InputStream in = getAssets().open("soundfonts/GeneralUser_GS_v1.471.sf2");
                 FileOutputStream out = new FileOutputStream(tmp, false)) {
                byte[] buf = new byte[64 * 1024];
                int read;
                while ((read = in.read(buf)) > 0) {
                    out.write(buf, 0, read);
                }
                out.flush();
            }
            if (generalUser.exists() && !generalUser.delete()) {
                throw new IllegalStateException("Cannot replace " + generalUser);
            }
            if (!tmp.renameTo(generalUser)) {
                throw new IllegalStateException("Cannot install " + generalUser);
            }
            return generalUser;
        } catch (Throwable primary) {
            android.util.Log.w("RPGMP-MKXP",
                    "GeneralUser GS install failed, falling back to TimGM6mb", primary);
        }

        File tim = new File(dir, "TimGM6mb.sf2");
        if (tim.isFile() && tim.length() > 1024 * 1024) {
            return tim;
        }
        File timTmp = new File(dir, "TimGM6mb.sf2.tmp");
        try (InputStream in = getAssets().open("soundfonts/TimGM6mb.sf2");
             FileOutputStream out = new FileOutputStream(timTmp, false)) {
            byte[] buf = new byte[64 * 1024];
            int read;
            while ((read = in.read(buf)) > 0) {
                out.write(buf, 0, read);
            }
            out.flush();
        }
        if (tim.exists() && !tim.delete()) {
            throw new IllegalStateException("Cannot replace " + tim);
        }
        if (!timTmp.renameTo(tim)) {
            throw new IllegalStateException("Cannot install " + tim);
        }
        return tim;
    }

    private void applyGameUserEnvironment(File game) {
        try {
            File userRoot = new File(game, "UserData");
            File userTemp = new File(userRoot, "Temp");
            File userData = new File(userRoot, "AppData");
            userTemp.mkdirs();
            userData.mkdirs();
            String appDataPath = userData.getAbsolutePath();
            String tempPath = userTemp.getAbsolutePath();
            String homePath = userRoot.getAbsolutePath();
            Os.setenv("APPDATA", appDataPath, true);
            Os.setenv("LOCALAPPDATA", appDataPath, true);
            Os.setenv("HOME", homePath, true);
            Os.setenv("USERPROFILE", homePath, true);
            Os.setenv("TEMP", tempPath, true);
            Os.setenv("TMP", tempPath, true);
            android.util.Log.i("RPGMP-MKXP", "USER_ENV appdata=" + appDataPath +
                    " temp=" + tempPath);
        } catch (Throwable t) {
            android.util.Log.w("RPGMP-MKXP", "USER_ENV setup failed", t);
        }
    }

    private void applyNativeLogEnvironment() {
        try {
            if (gameLogPath == null || gameLogPath.isEmpty()) return;
            Os.setenv("RPGMP_GAME_LOG", gameLogPath, true);
            android.util.Log.i("RPGMP-MKXP", "NATIVE_LOG " + gameLogPath);
        } catch (Throwable t) {
            android.util.Log.w("RPGMP-MKXP", "NATIVE_LOG setup failed", t);
        }
    }

    private void applyCompatEnvironment() {
        try {
            if (compatRootPath == null || compatRootPath.isEmpty()) return;
            File script = new File(compatRootPath, "compat/common/cicpoffs_compat.rb");
            Os.setenv("RPGMP_CICPOFFS_COMPAT", script.getAbsolutePath(), true);
            android.util.Log.i("RPGMP-MKXP",
                    "COMPAT_ENV cicpoffs=" + script.getAbsolutePath());
        } catch (Throwable t) {
            android.util.Log.w("RPGMP-MKXP", "COMPAT_ENV setup failed", t);
        }
    }
}
