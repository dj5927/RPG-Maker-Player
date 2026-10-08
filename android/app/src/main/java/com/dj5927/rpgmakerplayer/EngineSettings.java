package com.dj5927.rpgmakerplayer;

import android.content.Context;
import android.content.SharedPreferences;

/** Engine-wide defaults used only when a game has no explicit per-game override. */
final class EngineSettings {
    private static final String PREFS = "engine_settings";

    private EngineSettings() {}

    static String rubyMode(Context context, GameEntry.Engine engine) {
        return prefs(context).getString(key(engine, "ruby"), "AUTO");
    }

    static void saveRubyMode(Context context, GameEntry.Engine engine, String mode) {
        String safe = "1.8".equals(mode) || "1.9".equals(mode) || "3.1".equals(mode)
                ? mode : "AUTO";
        prefs(context).edit().putString(key(engine, "ruby"), safe).apply();
    }

    static String renderMode(Context context, GameEntry.Engine engine) {
        return prefs(context).getString(key(engine, "renderer"), "AUTO");
    }

    static String locale(Context context, GameEntry.Engine engine) {
        return GameLocaleSettings.normalize(prefs(context).getString(key(engine, "locale"), GameLocaleSettings.KO));
    }

    static void saveLocale(Context context, GameEntry.Engine engine, String locale) {
        prefs(context).edit().putString(key(engine, "locale"), GameLocaleSettings.normalize(locale)).apply();
    }

    static void saveRenderMode(Context context, GameEntry.Engine engine, String mode) {
        String safe = "WEBGL".equals(mode) || "CANVAS".equals(mode) ? mode : "AUTO";
        prefs(context).edit().putString(key(engine, "renderer"), safe).apply();
    }

    static boolean easyPadEnabled(Context context) {
        return prefs(context).getBoolean(key(GameEntry.Engine.EASYRPG, "pad_enabled"), true);
    }

    static boolean easyPadLarge(Context context) {
        return prefs(context).getBoolean(key(GameEntry.Engine.EASYRPG, "pad_large"), false);
    }

    static void saveEasyPad(Context context, boolean enabled, boolean large) {
        prefs(context).edit()
                .putBoolean(key(GameEntry.Engine.EASYRPG, "pad_enabled"), enabled)
                .putBoolean(key(GameEntry.Engine.EASYRPG, "pad_large"), large)
                .apply();
    }

    static String easySoundfont(Context context) {
        return prefs(context).getString(key(GameEntry.Engine.EASYRPG, "soundfont"), EasyRpgCoreSettings.AUTO);
    }

    static String easyFont1(Context context) {
        return prefs(context).getString(key(GameEntry.Engine.EASYRPG, "font1"), EasyRpgCoreSettings.AUTO);
    }

    static String easyFont2(Context context) {
        return prefs(context).getString(key(GameEntry.Engine.EASYRPG, "font2"), EasyRpgCoreSettings.AUTO);
    }

    static void saveEasyChoices(Context context, String soundfont, String font1, String font2) {
        prefs(context).edit()
                .putString(key(GameEntry.Engine.EASYRPG, "soundfont"), normalizeChoice(soundfont))
                .putString(key(GameEntry.Engine.EASYRPG, "font1"), normalizeChoice(font1))
                .putString(key(GameEntry.Engine.EASYRPG, "font2"), normalizeChoice(font2))
                .apply();
    }

    static int[] keyMap(Context context, GameEntry.Engine engine) {
        SharedPreferences prefs = prefs(context);
        int[] out = new int[GameKeyMap.SOURCES.length];
        for (int i = 0; i < out.length; i++) {
            out[i] = prefs.getInt(key(engine, "keymap_" + i), GameKeyMap.SOURCES[i]);
        }
        return out;
    }

    static void saveKeyMap(Context context, GameEntry.Engine engine, int[] targets) {
        SharedPreferences.Editor edit = prefs(context).edit();
        for (int i = 0; i < GameKeyMap.SOURCES.length; i++) {
            int value = targets != null && i < targets.length ? targets[i] : GameKeyMap.SOURCES[i];
            edit.putInt(key(engine, "keymap_" + i), value);
        }
        edit.apply();
    }

    static void resetKeyMap(Context context, GameEntry.Engine engine) {
        SharedPreferences.Editor edit = prefs(context).edit();
        for (int i = 0; i < GameKeyMap.SOURCES.length; i++) {
            edit.remove(key(engine, "keymap_" + i));
        }
        edit.apply();
    }

    static void resetEngine(Context context, GameEntry.Engine engine) {
        SharedPreferences.Editor edit = prefs(context).edit();
        edit.remove(key(engine, "ruby"));
        edit.remove(key(engine, "renderer"));
        edit.remove(key(engine, "locale"));
        edit.remove(key(engine, "pad_enabled"));
        edit.remove(key(engine, "pad_large"));
        edit.remove(key(engine, "soundfont"));
        edit.remove(key(engine, "font1"));
        edit.remove(key(engine, "font2"));
        for (int i = 0; i < GameKeyMap.SOURCES.length; i++) edit.remove(key(engine, "keymap_" + i));
        edit.apply();
    }

    private static String normalizeChoice(String value) {
        return value == null || value.trim().isEmpty() ? EasyRpgCoreSettings.AUTO : value.trim();
    }

    private static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    private static String key(GameEntry.Engine engine, String name) {
        return engine.name().toLowerCase(java.util.Locale.ROOT) + "_" + name;
    }
}
