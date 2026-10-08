package com.dj5927.rpgmakerplayer;

import android.content.Context;
import android.content.SharedPreferences;
import android.net.Uri;
import android.provider.DocumentsContract;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;

final class EasyRpgCoreSettings {
    static final String AUTO = "auto";
    static final String INHERIT = "inherit";
    private static final String PREFS = "game_settings";
    private static final String BUILTIN_SF2 = "builtin:TimGM6mb.sf2";
    private static final String BUILTIN_FONT = "builtin:wqymicrohei.ttf";

    static final class Choice {
        final String id;
        final String label;
        Choice(String id, String label) {
            this.id = id;
            this.label = label;
        }
    }

    private EasyRpgCoreSettings() {}

    static List<Choice> soundfontChoices(Context context, Uri treeUri) {
        LinkedHashMap<String, Choice> out = new LinkedHashMap<>();
        out.put(AUTO, new Choice(AUTO, UiText.t(context, "EasyRPG 설정")));
        out.put(BUILTIN_SF2, new Choice(BUILTIN_SF2, "TimGM6mb.sf2"));
        scanRelativeDirectory(context, treeUri, true, out,
                "rtp", "easyrpg-player", "Soundfont");
        return new ArrayList<>(out.values());
    }

    static List<Choice> gameSoundfontChoices(Context context, Uri treeUri) {
        ArrayList<Choice> out = new ArrayList<>();
        out.add(new Choice(INHERIT, UiText.t(context, "엔진 기본값 사용")));
        out.addAll(soundfontChoices(context, treeUri));
        return out;
    }

    static List<Choice> fontChoices(Context context, Uri treeUri) {
        LinkedHashMap<String, Choice> out = new LinkedHashMap<>();
        out.put(AUTO, new Choice(AUTO, UiText.t(context, "EasyRPG 설정")));
        out.put(BUILTIN_FONT, new Choice(BUILTIN_FONT, "wqymicrohei.ttf"));
        scanRelativeDirectory(context, treeUri, false, out,
                "rtp", "easyrpg-player", "Font");
        scanRelativeDirectory(context, treeUri, false, out, "rtp", "fonts");
        return new ArrayList<>(out.values());
    }

    static List<Choice> gameFontChoices(Context context, Uri treeUri) {
        ArrayList<Choice> out = new ArrayList<>();
        out.add(new Choice(INHERIT, UiText.t(context, "엔진 기본값 사용")));
        out.addAll(fontChoices(context, treeUri));
        return out;
    }

    static String soundfontChoice(Context context, GameEntry game) {
        return effectiveChoice(context, game, "soundfont", EngineSettings.easySoundfont(context));
    }

    static String font1Choice(Context context, GameEntry game) {
        return effectiveChoice(context, game, "font1", EngineSettings.easyFont1(context));
    }

    static String font2Choice(Context context, GameEntry game) {
        return effectiveChoice(context, game, "font2", EngineSettings.easyFont2(context));
    }

    static String gameSoundfontChoice(Context context, GameEntry game) {
        return rawGameChoice(context, game, "soundfont");
    }

    static String gameFont1Choice(Context context, GameEntry game) {
        return rawGameChoice(context, game, "font1");
    }

    static String gameFont2Choice(Context context, GameEntry game) {
        return rawGameChoice(context, game, "font2");
    }

    static void saveChoices(Context context, GameEntry game, String soundfont, String font1, String font2) {
        SharedPreferences.Editor edit = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit();
        putOrRemove(edit, key("soundfont", game), soundfont);
        putOrRemove(edit, key("font1", game), font1);
        putOrRemove(edit, key("font2", game), font2);
        edit.apply();
    }

    static File prepareSystemDirectory(Context context, Uri treeUri, GameEntry game) throws Exception {
        File base = context.getExternalFilesDir("easyrpg_core");
        if (base == null) base = new File(context.getFilesDir(), "easyrpg_core");
        File systemDir = new File(base, safeId(game));
        File configRoot = new File(systemDir, "easyrpg-player");
        File soundfontDir = new File(configRoot, "Soundfont");
        File fontDir = new File(configRoot, "Font");
        if (!soundfontDir.exists() && !soundfontDir.mkdirs()) {
            throw new IllegalStateException("Cannot create EasyRPG Soundfont directory");
        }
        if (!fontDir.exists() && !fontDir.mkdirs()) {
            throw new IllegalStateException("Cannot create EasyRPG Font directory");
        }

        File builtinSf2 = copyAssetIfNeeded(context, "soundfonts/TimGM6mb.sf2",
                new File(soundfontDir, "TimGM6mb.sf2"));
        File builtinFont = RuntimeFontInstaller.ensure(context);
        File localBuiltinFont = new File(fontDir, builtinFont.getName());
        copyFileIfNeeded(builtinFont, localBuiltinFont);

        String soundfont = soundfontChoice(context, game);
        String font1 = font1Choice(context, game);
        String font2 = font2Choice(context, game);
        File soundfontFile = materializeChoice(context, treeUri, soundfont, true,
                soundfontDir, fontDir, builtinSf2, localBuiltinFont);
        File font1File = materializeChoice(context, treeUri, font1, false,
                soundfontDir, fontDir, builtinSf2, localBuiltinFont);
        File font2File = materializeChoice(context, treeUri, font2, false,
                soundfontDir, fontDir, builtinSf2, localBuiltinFont);

        File config = new File(configRoot, "config.ini");
        applyManagedSetting(context, game, config, "Audio", "Soundfont", "soundfont",
                soundfont, soundfontFile);
        applyManagedSetting(context, game, config, "Player", "Font1", "font1",
                font1, font1File);
        applyManagedSetting(context, game, config, "Player", "Font2", "font2",
                font2, font2File);
        return systemDir;
    }

    private static void applyManagedSetting(Context context, GameEntry game, File config,
                                            String section, String iniKey, String prefName,
                                            String choice, File selected) throws Exception {
        SharedPreferences prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        String lastKey = key("managed_" + prefName, game);
        String previousManaged = prefs.getString(lastKey, "");
        if (!AUTO.equals(choice) && selected != null && selected.isFile()) {
            String path = selected.getAbsolutePath();
            updateIni(config, section, iniKey, path, null);
            prefs.edit().putString(lastKey, path).apply();
        } else {
            updateIni(config, section, iniKey, null,
                    previousManaged == null || previousManaged.isEmpty() ? null : previousManaged);
            prefs.edit().remove(lastKey).apply();
        }
    }

    private static File materializeChoice(Context context, Uri treeUri, String choice,
                                          boolean soundfont, File soundfontDir, File fontDir,
                                          File builtinSf2, File builtinFont) throws Exception {
        if (choice == null || AUTO.equals(choice)) return null;
        if (BUILTIN_SF2.equals(choice)) return builtinSf2;
        if (BUILTIN_FONT.equals(choice)) return builtinFont;
        if (!choice.startsWith("saf:")) return null;
        int split = choice.indexOf('|', 4);
        if (split <= 4) return null;
        String documentId = choice.substring(4, split);
        String name = choice.substring(split + 1);
        if (name.isEmpty()) return null;
        File target = new File(soundfont ? soundfontDir : fontDir, sanitizeFilename(name));
        Uri uri = GameScanner.documentUri(treeUri, documentId);
        try (InputStream in = context.getContentResolver().openInputStream(uri)) {
            if (in == null) return null;
            File tmp = new File(target.getParentFile(), target.getName() + ".tmp");
            try (FileOutputStream out = new FileOutputStream(tmp)) {
                byte[] buf = new byte[128 * 1024];
                int read;
                while ((read = in.read(buf)) >= 0) {
                    if (read > 0) out.write(buf, 0, read);
                }
            }
            if (target.exists() && !target.delete()) {
                throw new IllegalStateException("Cannot replace EasyRPG asset: " + target.getName());
            }
            if (!tmp.renameTo(target)) {
                copyFileIfNeeded(tmp, target);
                tmp.delete();
            }
        }
        return target;
    }

    private static void scanRelativeDirectory(Context context, Uri treeUri, boolean soundfont,
                                              LinkedHashMap<String, Choice> out,
                                              String... path) {
        if (treeUri == null) return;
        try {
            String parentId = DocumentsContract.getTreeDocumentId(treeUri);
            for (String component : path) {
                GameScanner.Child dir = findDirectory(context, treeUri, parentId, component);
                if (dir == null) return;
                parentId = dir.documentId;
            }
            for (GameScanner.Child child : GameScanner.listChildren(context, treeUri, parentId)) {
                if (child.directory || child.name == null || !validExtension(child.name, soundfont)) continue;
                String id = "saf:" + child.documentId + "|" + child.name;
                String dedupe = child.name.toLowerCase(Locale.ROOT);
                if (!out.containsKey(dedupe)) out.put(dedupe, new Choice(id, child.name));
            }
        } catch (Exception ignored) {}
    }

    private static GameScanner.Child findDirectory(Context context, Uri treeUri,
                                                   String parentId, String name) throws Exception {
        for (GameScanner.Child child : GameScanner.listChildren(context, treeUri, parentId)) {
            if (child.directory && name.equalsIgnoreCase(child.name)) return child;
        }
        return null;
    }

    private static boolean validExtension(String name, boolean soundfont) {
        String lower = name.toLowerCase(Locale.ROOT);
        if (soundfont) return lower.endsWith(".sf2");
        return lower.endsWith(".fon") || lower.endsWith(".fnt") || lower.endsWith(".bdf") ||
                lower.endsWith(".ttf") || lower.endsWith(".ttc") || lower.endsWith(".otf") ||
                lower.endsWith(".woff") || lower.endsWith(".woff2");
    }

    private static String effectiveChoice(Context context, GameEntry game, String name, String engineDefault) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        String k = key(name, game);
        return prefs.contains(k) ? normalize(prefs.getString(k, AUTO)) : normalize(engineDefault);
    }

    private static String rawGameChoice(Context context, GameEntry game, String name) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        String k = key(name, game);
        return prefs.contains(k) ? normalize(prefs.getString(k, AUTO)) : INHERIT;
    }

    private static void putOrRemove(SharedPreferences.Editor edit, String key, String value) {
        if (value == null || INHERIT.equals(value)) edit.remove(key);
        else edit.putString(key, normalize(value));
    }

    private static String normalize(String value) {
        return value == null || value.trim().isEmpty() ? AUTO : value.trim();
    }

    private static String key(String name, GameEntry game) {
        return "easyrpg_" + name + "_" + safeId(game);
    }

    private static String safeId(GameEntry game) {
        return Integer.toHexString(game.stableId().hashCode());
    }

    private static File copyAssetIfNeeded(Context context, String asset, File target) throws Exception {
        if (target.isFile() && target.length() > 0) return target;
        if (!target.getParentFile().exists()) target.getParentFile().mkdirs();
        try (InputStream in = context.getAssets().open(asset);
             FileOutputStream out = new FileOutputStream(target)) {
            byte[] buf = new byte[128 * 1024];
            int read;
            while ((read = in.read(buf)) >= 0) if (read > 0) out.write(buf, 0, read);
        }
        return target;
    }

    private static void copyFileIfNeeded(File source, File target) throws Exception {
        if (target.isFile() && target.length() == source.length()) return;
        if (!target.getParentFile().exists()) target.getParentFile().mkdirs();
        try (FileInputStream in = new FileInputStream(source);
             FileOutputStream out = new FileOutputStream(target)) {
            byte[] buf = new byte[128 * 1024];
            int read;
            while ((read = in.read(buf)) >= 0) if (read > 0) out.write(buf, 0, read);
        }
    }

    private static void updateIni(File file, String section, String iniKey,
                                  String value, String removeOnlyIfEquals) throws Exception {
        ArrayList<String> lines = new ArrayList<>();
        if (file.isFile()) {
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(
                    new FileInputStream(file), StandardCharsets.UTF_8))) {
                String line;
                while ((line = reader.readLine()) != null) lines.add(line);
            }
        }

        int sectionStart = -1;
        int sectionEnd = lines.size();
        for (int i = 0; i < lines.size(); i++) {
            String t = lines.get(i).trim();
            if (t.startsWith("[") && t.endsWith("]")) {
                String s = t.substring(1, t.length() - 1).trim();
                if (sectionStart >= 0) {
                    sectionEnd = i;
                    break;
                }
                if (s.equalsIgnoreCase(section)) sectionStart = i;
            }
        }

        if (sectionStart < 0) {
            if (value == null) return;
            if (!lines.isEmpty() && !lines.get(lines.size() - 1).isEmpty()) lines.add("");
            lines.add("[" + section + "]");
            lines.add(iniKey + "=" + value);
        } else {
            int keyIndex = -1;
            String current = null;
            for (int i = sectionStart + 1; i < sectionEnd; i++) {
                String t = lines.get(i).trim();
                int eq = t.indexOf('=');
                if (eq > 0 && t.substring(0, eq).trim().equalsIgnoreCase(iniKey)) {
                    keyIndex = i;
                    current = t.substring(eq + 1).trim();
                    break;
                }
            }
            if (value != null) {
                if (keyIndex >= 0) lines.set(keyIndex, iniKey + "=" + value);
                else lines.add(sectionEnd, iniKey + "=" + value);
            } else if (keyIndex >= 0 && removeOnlyIfEquals != null &&
                    removeOnlyIfEquals.equals(current)) {
                lines.remove(keyIndex);
            }
        }

        if (!file.getParentFile().exists() && !file.getParentFile().mkdirs()) {
            throw new IllegalStateException("Cannot create EasyRPG config directory");
        }
        try (FileOutputStream out = new FileOutputStream(file)) {
            for (String line : lines) {
                out.write(line.getBytes(StandardCharsets.UTF_8));
                out.write('\n');
            }
        }
    }

    private static String sanitizeFilename(String name) {
        return name.replace('/', '_').replace('\\', '_');
    }

    static int indexOf(List<Choice> choices, String id) {
        if (choices == null || choices.isEmpty()) return 0;
        for (int i = 0; i < choices.size(); i++) {
            if (choices.get(i).id.equals(id)) return i;
        }
        return 0;
    }

    static String idAt(List<Choice> choices, int index) {
        if (choices == null || choices.isEmpty()) return AUTO;
        if (index < 0 || index >= choices.size()) return AUTO;
        return choices.get(index).id;
    }

    static String[] labels(List<Choice> choices) {
        String[] out = new String[choices == null ? 0 : choices.size()];
        for (int i = 0; i < out.length; i++) out[i] = choices.get(i).label;
        return out;
    }
}
