package com.dj5927.rpgmakerplayer;

import android.app.Activity;
import android.app.AlertDialog;
import android.os.Build;
import android.os.Bundle;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.Toast;
import android.window.OnBackInvokedDispatcher;

import java.io.File;

public final class LibretroEasyRpgActivity extends Activity {
    public static final String EXTRA_GAME_PATH = "rpgmp_easyrpg_core_game";
    public static final String EXTRA_SAVE_DIR = "rpgmp_easyrpg_core_save";
    public static final String EXTRA_SYSTEM_DIR = "rpgmp_easyrpg_core_system";
    public static final String EXTRA_PAD_ENABLED = "rpgmp_easyrpg_pad_enabled";
    public static final String EXTRA_PAD_LARGE = "rpgmp_easyrpg_pad_large";
    public static final String EXTRA_KEYMAP = "rpgmp_easyrpg_keymap";
    public static final String EXTRA_LOCALE = "rpgmp_easyrpg_locale";

    static {
        System.loadLibrary("rpgmp_libretro_host");
    }

    private native boolean nativeStart(String corePath, String gamePath, String saveDir,
                                       String systemDir, Surface surface);
    private native void nativeSetSurface(Surface surface);
    private native void nativeSetButton(int id, boolean down);
    private native void nativeSetAxis(int axis, int value);
    private native void nativeStop();

    private SurfaceView surfaceView;
    private boolean nativeStarted;
    private boolean explicitExitRequested;
    private boolean exitDialogVisible;
    private boolean destroyHandled;
    private int[] keyMap;
    private boolean hatUp, hatDown, hatLeft, hatRight;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        super.onCreate(savedInstanceState);
        GameLocaleSettings.applyProcess(getIntent().getStringExtra(EXTRA_LOCALE));
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,
                WindowManager.LayoutParams.FLAG_FULLSCREEN);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        String gamePath = getIntent().getStringExtra(EXTRA_GAME_PATH);
        String saveDir = getIntent().getStringExtra(EXTRA_SAVE_DIR);
        String systemDir = getIntent().getStringExtra(EXTRA_SYSTEM_DIR);
        keyMap = getIntent().getIntArrayExtra(EXTRA_KEYMAP);
        if (gamePath == null || gamePath.isEmpty()) {
            finish();
            return;
        }
        if (saveDir == null) saveDir = "";
        if (systemDir == null) systemDir = getFilesDir().getAbsolutePath();

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(android.graphics.Color.BLACK);
        surfaceView = new SurfaceView(this);
        root.addView(surfaceView, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        if (getIntent().getBooleanExtra(EXTRA_PAD_ENABLED, true)) {
            VirtualGamepadView pad = new VirtualGamepadView(this);
            pad.setLargeMode(getIntent().getBooleanExtra(EXTRA_PAD_LARGE, false));
            pad.setListener((keyCode, down) -> sendAndroidKey(keyCode, down));
            root.addView(pad, new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        }
        setContentView(root);

        final String launchGamePath = gamePath;
        final String launchSaveDir = saveDir;
        final String launchSystemDir = systemDir;
        surfaceView.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override public void surfaceCreated(SurfaceHolder holder) {
                if (!nativeStarted) {
                    File core = new File(getApplicationInfo().nativeLibraryDir,
                            "libeasyrpg_libretro.so");
                    if (!core.isFile()) {
                        showFatal("EasyRPG libretro core를 찾을 수 없습니다.");
                        return;
                    }
                    nativeStarted = nativeStart(core.getAbsolutePath(), launchGamePath,
                            launchSaveDir, launchSystemDir, holder.getSurface());
                    if (!nativeStarted) showFatal("EasyRPG libretro core 시작에 실패했습니다.");
                } else {
                    nativeSetSurface(holder.getSurface());
                }
            }

            @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
                if (nativeStarted) nativeSetSurface(holder.getSurface());
            }

            @Override public void surfaceDestroyed(SurfaceHolder holder) {
                if (nativeStarted) nativeSetSurface(null);
            }
        });

        getWindow().getDecorView().post(this::enterImmersive);
        if (Build.VERSION.SDK_INT >= 33) {
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                    OnBackInvokedDispatcher.PRIORITY_DEFAULT, this::showExitConfirmation);
        }
    }

    private void showFatal(String message) {
        new AlertDialog.Builder(this)
                .setTitle("RPG Maker Player")
                .setMessage(message)
                .setCancelable(false)
                .setPositiveButton("확인", (d, w) -> finishAndKillEngine())
                .show();
    }

    public void onNativeCoreStopped(String error) {
        runOnUiThread(() -> {
            if (isFinishing() || isDestroyed()) return;
            if (error != null && !error.isEmpty()) {
                new AlertDialog.Builder(this)
                        .setTitle("EasyRPG core")
                        .setMessage(error)
                        .setCancelable(false)
                        .setPositiveButton("확인", (d, w) -> finishAndKillEngine())
                        .show();
            } else {
                finishAndKillEngine();
            }
        });
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
                    controller.setSystemBarsBehavior(
                            WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                }
            }
        } catch (Throwable ignored) {}
    }

    @Override protected void onResume() {
        super.onResume();
        View decor = getWindow().getDecorView();
        if (decor != null) {
            decor.post(this::enterImmersive);
            decor.postDelayed(this::enterImmersive, 250);
        }
    }

    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            View decor = getWindow().getDecorView();
            if (decor != null) decor.post(this::enterImmersive);
        }
    }

    @Override public void onBackPressed() {
        showExitConfirmation();
    }

    private void showExitConfirmation() {
        if (exitDialogVisible || isFinishing()) return;
        exitDialogVisible = true;
        AlertDialog dialog = new AlertDialog.Builder(this)
                .setTitle(UiText.t(this, "게임 종료"))
                .setMessage(UiText.t(this, "게임을 종료하시겠습니까?"))
                .setNegativeButton(UiText.t(this, "아니오"), (d, w) -> exitDialogVisible = false)
                .setPositiveButton(UiText.t(this, "예"), (d, w) -> {
                    exitDialogVisible = false;
                    explicitExitRequested = true;
                    finishAndKillEngine();
                })
                .create();
        dialog.setOnCancelListener(d -> exitDialogVisible = false);
        dialog.show();
    }

    private void finishAndKillEngine() {
        if (!destroyHandled) {
            destroyHandled = true;
            if (nativeStarted) {
                nativeStop();
                nativeStarted = false;
            }
        }
        finish();
        new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(
                () -> android.os.Process.killProcess(android.os.Process.myPid()), 500);
    }

    @Override protected void onDestroy() {
        if (!destroyHandled && nativeStarted) {
            destroyHandled = true;
            nativeStop();
            nativeStarted = false;
        }
        super.onDestroy();
    }

    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        int action = event.getAction();
        if (action != KeyEvent.ACTION_DOWN && action != KeyEvent.ACTION_UP) {
            return super.dispatchKeyEvent(event);
        }
        if (sendAndroidKey(event.getKeyCode(), action == KeyEvent.ACTION_DOWN)) return true;
        return super.dispatchKeyEvent(event);
    }

    private boolean sendAndroidKey(int source, boolean down) {
        int target = GameKeyMap.remap(source, keyMap);
        int retro = androidKeyToRetro(target);
        if (retro < 0) return false;
        if (nativeStarted) nativeSetButton(retro, down);
        return true;
    }

    private static int androidKeyToRetro(int keyCode) {
        switch (keyCode) {
            case KeyEvent.KEYCODE_BUTTON_B: return 0;
            case KeyEvent.KEYCODE_BUTTON_Y: return 1;
            case KeyEvent.KEYCODE_BUTTON_SELECT: return 2;
            case KeyEvent.KEYCODE_BUTTON_START: return 3;
            case KeyEvent.KEYCODE_DPAD_UP: return 4;
            case KeyEvent.KEYCODE_DPAD_DOWN: return 5;
            case KeyEvent.KEYCODE_DPAD_LEFT: return 6;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return 7;
            case KeyEvent.KEYCODE_BUTTON_A:
            case KeyEvent.KEYCODE_ENTER:
            case KeyEvent.KEYCODE_NUMPAD_ENTER: return 8;
            case KeyEvent.KEYCODE_BUTTON_X: return 9;
            case KeyEvent.KEYCODE_BUTTON_L1: return 10;
            case KeyEvent.KEYCODE_BUTTON_R1: return 11;
            case KeyEvent.KEYCODE_BUTTON_L2: return 12;
            case KeyEvent.KEYCODE_BUTTON_R2: return 13;
            case KeyEvent.KEYCODE_BUTTON_THUMBL: return 14;
            case KeyEvent.KEYCODE_BUTTON_THUMBR: return 15;
            default: return -1;
        }
    }

    @Override public boolean onGenericMotionEvent(MotionEvent event) {
        if ((event.getSource() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK &&
                event.getAction() == MotionEvent.ACTION_MOVE) {
            setAxis(0, event.getAxisValue(MotionEvent.AXIS_X));
            setAxis(1, event.getAxisValue(MotionEvent.AXIS_Y));
            setAxis(2, event.getAxisValue(MotionEvent.AXIS_Z));
            setAxis(3, event.getAxisValue(MotionEvent.AXIS_RZ));
            setTrigger(4, event.getAxisValue(MotionEvent.AXIS_LTRIGGER));
            setTrigger(5, event.getAxisValue(MotionEvent.AXIS_RTRIGGER));
            updateHat(event.getAxisValue(MotionEvent.AXIS_HAT_X),
                    event.getAxisValue(MotionEvent.AXIS_HAT_Y));
            return true;
        }
        return super.onGenericMotionEvent(event);
    }

    private void setAxis(int axis, float value) {
        if (!nativeStarted) return;
        float v = Math.max(-1f, Math.min(1f, value));
        if (Math.abs(v) < 0.15f) v = 0f;
        nativeSetAxis(axis, Math.round(v * 32767f));
    }

    private void setTrigger(int axis, float value) {
        if (!nativeStarted) return;
        float v = Math.max(0f, Math.min(1f, value));
        nativeSetAxis(axis, Math.round(v * 32767f));
    }

    private void updateHat(float x, float y) {
        boolean left = x < -0.5f, right = x > 0.5f;
        boolean up = y < -0.5f, down = y > 0.5f;
        if (left != hatLeft) nativeSetButton(6, left);
        if (right != hatRight) nativeSetButton(7, right);
        if (up != hatUp) nativeSetButton(4, up);
        if (down != hatDown) nativeSetButton(5, down);
        hatLeft = left; hatRight = right; hatUp = up; hatDown = down;
    }
}
