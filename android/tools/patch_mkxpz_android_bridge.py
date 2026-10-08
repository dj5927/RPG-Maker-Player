#!/usr/bin/env python3
from pathlib import Path
import re
import sys


def replace_once(text: str, pattern: str, repl: str, label: str) -> str:
    new, count = re.subn(pattern, repl, text, count=1, flags=re.S)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, got {count}")
    return new


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_mkxpz_android_bridge.py /path/to/mkxp-z", file=sys.stderr)
        return 2

    root = Path(sys.argv[1])
    main_cpp = root / "src" / "main.cpp"
    config_cpp = root / "src" / "config.cpp"
    mtool_cpp = root / "src" / "MtoolProc.cpp"

    marker = "RPGMP_ANDROID_LAUNCH_BRIDGE_V2"

    text = main_cpp.read_text(encoding="utf-8")
    if marker not in text:
        text = replace_once(
            text,
            r'#ifdef MKXPZ_BUILD_ANDROID\n\s*// Set application window orientation to landscape\n\s*SDL_SetHint\(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight"\);\n.*?MtoolProc::notifyLoadingStatus\(1\);\n#endif',
            '''#ifdef MKXPZ_BUILD_ANDROID
    // RPGMP_ANDROID_LAUNCH_BRIDGE_V2
    // Keep orientation handling, but remove the mtool-specific Java class
    // lookup. Our host Activity is org.libsdl.app based and does not ship the
    // original app.mtool.mtoolmobile.mkxpz.MKXPZActivity class.
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#endif''',
            "main initJNI block",
        )
        text = replace_once(
            text,
            r'#ifdef MKXPZ_BUILD_ANDROID\n\s*char sdkVersionChar\[PROP_VALUE_MAX\];.*?env->DeleteLocalRef\(cls\);\n#endif',
            '''#ifdef MKXPZ_BUILD_ANDROID
    // RPG Maker Player uses SAF -> app-owned mirror storage. The game root is
    // supplied by the explicit JSON config passed in argv[1], so no static
    // GAME_PATH/SERVER_PORT fields and no broad storage permission are needed.
#endif''',
            "main GAME_PATH/storage block",
        )
        main_cpp.write_text(text, encoding="utf-8")

    text = config_cpp.read_text(encoding="utf-8")
    if marker not in text:
        old = '''    editor.debug = false;
    editor.battleTest = false;
    
    if (argc > 1) {
        if (!strcmp(argv[1], "debug") || !strcmp(argv[1], "test"))
            editor.debug = true;
        else if (!strcmp(argv[1], "btest"))
            editor.battleTest = true;
        
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "debug"))
                launchArgs.push_back(argv[i]);
        }
    }
    
    json::value baseConf = readConfFile(CONF_FILE);
'''
        new = '''    // RPGMP_ANDROID_LAUNCH_BRIDGE_V2
    // Android host passes an absolute JSON config path as argv[1]. Treat it
    // as configuration, not as an RGSS launch argument.
    const char *baseConfPath = CONF_FILE;
    int launchArgStart = 1;
    if (argc > 1 && mkxp_fs::fileExists(argv[1])) {
        baseConfPath = argv[1];
        launchArgStart = 2;
    }

    editor.debug = false;
    editor.battleTest = false;

    if (argc > launchArgStart) {
        if (!strcmp(argv[launchArgStart], "debug") || !strcmp(argv[launchArgStart], "test"))
            editor.debug = true;
        else if (!strcmp(argv[launchArgStart], "btest"))
            editor.battleTest = true;

        for (int i = launchArgStart; i < argc; i++) {
            if (strcmp(argv[i], "debug"))
                launchArgs.push_back(argv[i]);
        }
    }

    json::value baseConf = readConfFile(baseConfPath);
'''
        if old not in text:
            raise RuntimeError("config argv/base config block not found")
        config_cpp.write_text(text.replace(old, new, 1), encoding="utf-8")

    text = mtool_cpp.read_text(encoding="utf-8")
    if marker not in text:
        anchor = '''    rb_define_global_function("callMtoolServer", RUBY_METHOD_FUNC(callMtoolServer), 2);
    rb_define_global_function("fixString", RUBY_METHOD_FUNC(rb_mri_fixString), 2);
    rb_define_global_function("doMToolEvals", RUBY_METHOD_FUNC(rb_MRI_doEvals), 0);

    m_host = "127.0.0.1"; // 默认主机地址
'''
        replacement = '''    rb_define_global_function("callMtoolServer", RUBY_METHOD_FUNC(callMtoolServer), 2);
    rb_define_global_function("fixString", RUBY_METHOD_FUNC(rb_mri_fixString), 2);
    rb_define_global_function("doMToolEvals", RUBY_METHOD_FUNC(rb_MRI_doEvals), 0);

    // RPGMP_ANDROID_LAUNCH_BRIDGE_V2
    // The standalone player has no MTool websocket server. Keep the Ruby
    // compatibility functions but do not create a localhost:0 client.
    if (MtoolServerport <= 0) {
        return;
    }

    m_host = "127.0.0.1"; // 默认主机地址
'''
        if anchor not in text:
            raise RuntimeError("Mtool init anchor not found")
        mtool_cpp.write_text(text.replace(anchor, replacement, 1), encoding="utf-8")

    print(f"PATCHED {main_cpp}")
    print(f"PATCHED {config_cpp}")
    print(f"PATCHED {mtool_cpp}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
