#!/usr/bin/env python3
from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_mkxpz_cicpoffs_pre_eval.py /path/to/binding-mri.cpp", file=sys.stderr)
        return 2

    path = Path(sys.argv[1])
    text = path.read_text(encoding="utf-8")
    marker = "RPGMP_ANDROID_CICPOFFS_PRE_EVAL"
    if marker in text:
        print(f"ALREADY_PATCHED {path}")
        return 0

    anchor = '''    if (exc != Qnil)\n        return;\n\n'''
    insert = '''    if (exc != Qnil)\n        return;\n\n    // RPG Maker Player Android compatibility hook. At this point\n    // $RGSS_SCRIPTS is fully decoded and preload scripts have run, but no\n    // game script has executed yet. Apply signature-driven compatibility\n    // rewrites only to legacy Ruby runtimes. Ruby 3.x ports already contain\n    // modernized scripts and must not be rewritten again.\n#if defined(__ANDROID__) && RAPI_FULL <= 193\n    const char *rpgmpCompatEnv = getenv("RPGMP_CICPOFFS_COMPAT");\n    if (rpgmpCompatEnv && *rpgmpCompatEnv) {\n        const std::string rpgmpCompatScript = rpgmpCompatEnv;\n        Debug() << "RPGMP_ANDROID_CICPOFFS_PRE_EVAL " << rpgmpCompatScript;\n        runCustomScript(rpgmpCompatScript);\n    }\n#elif defined(__ANDROID__)\n    Debug() << "RPGMP_ANDROID_CICPOFFS_SKIP_RUBY31";\n#endif\n\n'''
    if anchor not in text:
        raise RuntimeError("pre-eval anchor not found")

    path.write_text(text.replace(anchor, insert, 1), encoding="utf-8")
    print(f"PATCHED {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
