package com.dj5927.rpgmakerplayer;

import android.content.Context;
import android.content.SharedPreferences;
import android.system.Os;

import java.util.Locale;

/** SteamOS-parity ko/ja/en locale hint with per-game > engine > app precedence. */
final class GameLocaleSettings {
    static final String INHERIT = "inherit";
    static final String KO = "ko";
    static final String JA = "ja";
    static final String EN = "en";
    private static final String PREFS = "game_settings";

    private GameLocaleSettings() {}

    static String effective(Context context, GameEntry game) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        String key = gameKey(game);
        if (prefs.contains(key)) return normalize(prefs.getString(key, KO));
        return EngineSettings.locale(context, game.engine);
    }

    static String gameChoice(Context context, GameEntry game) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        String key = gameKey(game);
        return prefs.contains(key) ? normalize(prefs.getString(key, KO)) : INHERIT;
    }

    static void saveGameChoice(Context context, GameEntry game, String value) {
        SharedPreferences.Editor edit = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit();
        if (value == null || INHERIT.equals(value)) edit.remove(gameKey(game));
        else edit.putString(gameKey(game), normalize(value));
        edit.apply();
    }

    static String[] gameLabels(Context context) {
        return UiText.array(context, "엔진 기본값 사용", "한국어", "일본어", "영어");
    }

    static String[] engineLabels(Context context) {
        return UiText.array(context, "한국어", "일본어", "영어");
    }

    static int gameIndex(String code) {
        if (INHERIT.equals(code)) return 0;
        if (JA.equals(code)) return 2;
        if (EN.equals(code)) return 3;
        return 1;
    }

    static int engineIndex(String code) {
        if (JA.equals(code)) return 1;
        if (EN.equals(code)) return 2;
        return 0;
    }

    static String gameCodeAt(int index) {
        return index == 0 ? INHERIT : index == 2 ? JA : index == 3 ? EN : KO;
    }

    static String engineCodeAt(int index) {
        return index == 1 ? JA : index == 2 ? EN : KO;
    }

    static void applyProcess(String code) {
        String safe = normalize(code);
        Locale locale = JA.equals(safe) ? Locale.JAPAN : EN.equals(safe) ? Locale.US : Locale.KOREA;
        Locale.setDefault(locale);
        System.setProperty("user.language", locale.getLanguage());
        System.setProperty("user.country", locale.getCountry());
        String posix = JA.equals(safe) ? "ja_JP.UTF-8" : EN.equals(safe) ? "en_US.UTF-8" : "ko_KR.UTF-8";
        String language = JA.equals(safe) ? "ja_JP:ja" : EN.equals(safe) ? "en_US:en" : "ko_KR:ko";
        try {
            Os.setenv("LANG", posix, true);
            Os.setenv("LANGUAGE", language, true);
            Os.setenv("LC_ALL", posix, true);
            Os.setenv("LC_CTYPE", posix, true);
            Os.setenv("LC_MESSAGES", posix, true);
            Os.setenv("MKXP_GAME_LOCALE", safe, true);
        } catch (Throwable ignored) {
            // Locale.setDefault and Java properties still provide a process-local hint.
        }
    }

    static String normalize(String value) {
        if (value == null) return KO;
        String lower = value.trim().toLowerCase(Locale.ROOT);
        if (JA.equals(lower) || "jp".equals(lower) || lower.startsWith("ja_")) return JA;
        if (EN.equals(lower) || lower.startsWith("en_")) return EN;
        return KO;
    }

    private static String gameKey(GameEntry game) {
        return "locale_" + Integer.toHexString(game.stableId().hashCode());
    }
}
