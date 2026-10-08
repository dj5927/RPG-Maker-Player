package com.dj5927.rpgmakerplayer;

import android.app.Activity;
import android.content.SharedPreferences;
import android.view.KeyEvent;

public final class GameKeyMap {
    public static final int[] SOURCES = {
            KeyEvent.KEYCODE_BUTTON_A,
            KeyEvent.KEYCODE_BUTTON_B,
            KeyEvent.KEYCODE_BUTTON_X,
            KeyEvent.KEYCODE_BUTTON_Y,
            KeyEvent.KEYCODE_BUTTON_L1,
            KeyEvent.KEYCODE_BUTTON_L2,
            KeyEvent.KEYCODE_BUTTON_R1,
            KeyEvent.KEYCODE_BUTTON_R2,
            KeyEvent.KEYCODE_BUTTON_START,
            KeyEvent.KEYCODE_BUTTON_SELECT
    };

    public static final String[] LABELS = {
            "A", "B", "X", "Y", "L1", "L2", "R1", "R2", "START", "SELECT"
    };

    private static final String PREFS = "game_settings";

    private GameKeyMap() {}

    public static int[] load(Activity activity, GameEntry game) {
        SharedPreferences prefs = activity.getSharedPreferences(PREFS, Activity.MODE_PRIVATE);
        int[] engineDefaults = EngineSettings.keyMap(activity, game.engine);
        int[] out = new int[SOURCES.length];
        for (int i = 0; i < SOURCES.length; i++) {
            String key = prefKey(game, i);
            out[i] = prefs.contains(key) ? prefs.getInt(key, SOURCES[i]) : engineDefaults[i];
        }
        return out;
    }

    public static int[] engineDefaults(Activity activity, GameEntry game) {
        return EngineSettings.keyMap(activity, game.engine);
    }

    public static int[] appDefaults() {
        return SOURCES.clone();
    }

    public static void save(Activity activity, GameEntry game, int[] targets) {
        SharedPreferences.Editor edit = activity.getSharedPreferences(PREFS, Activity.MODE_PRIVATE).edit();
        for (int i = 0; i < SOURCES.length; i++) {
            int value = targets != null && i < targets.length ? targets[i] : SOURCES[i];
            edit.putInt(prefKey(game, i), value);
        }
        edit.apply();
    }

    public static void reset(Activity activity, GameEntry game) {
        SharedPreferences.Editor edit = activity.getSharedPreferences(PREFS, Activity.MODE_PRIVATE).edit();
        for (int i = 0; i < SOURCES.length; i++) edit.remove(prefKey(game, i));
        edit.apply();
    }

    public static int remap(int sourceKeyCode, int[] targets) {
        for (int i = 0; i < SOURCES.length; i++) {
            if (SOURCES[i] == sourceKeyCode) {
                return targets != null && i < targets.length ? targets[i] : sourceKeyCode;
            }
        }
        return sourceKeyCode;
    }

    public static int sourceIndex(int keyCode) {
        for (int i = 0; i < SOURCES.length; i++) if (SOURCES[i] == keyCode) return i;
        return -1;
    }

    public static int targetIndex(int keyCode) {
        return sourceIndex(keyCode);
    }

    private static String prefKey(GameEntry game, int index) {
        return "keymap_" + Integer.toHexString(game.stableId().hashCode()) + "_" + index;
    }
}
