#!/usr/bin/env python3
from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_mkxp193_include.py /path/to/mkxp-z-193.mk", file=sys.stderr)
        return 2

    path = Path(sys.argv[1])
    text = path.read_text(encoding="utf-8")
    marker = "RPGMP_RUBY193_SOURCE_HEADERS"
    if marker in text:
        bad = '''LOCAL_C_INCLUDES := \\
\t# RPGMP_RUBY193_SOURCE_HEADERS: make install is intentionally skipped. \\
\t$(LOCAL_PATH)/../ruby193/include \\
\t$(LOCAL_PATH)/../ruby193/.ext/include/arm-eabi \\
\t$(LOCAL_PATH)/../ruby193 \\
'''
        good = '''# RPGMP_RUBY193_SOURCE_HEADERS: make install is intentionally skipped.
LOCAL_C_INCLUDES := \\
\t$(LOCAL_PATH)/../ruby193/include \\
\t$(LOCAL_PATH)/../ruby193/.ext/include/arm-eabi \\
\t$(LOCAL_PATH)/../ruby193 \\
'''
        if bad in text:
            path.write_text(text.replace(bad, good, 1), encoding="utf-8")
            print(f"REPAIRED {path}")
        else:
            print(f"ALREADY_PATCHED {path}")
        return 0

    old = '''LOCAL_C_INCLUDES := \\
\t$(LOCAL_BUILD_PATH)/include/ruby-1.9.3/ruby-1.9.1 \\
\t$(LOCAL_BUILD_PATH)/include/ruby-1.9.3/ruby-1.9.1/arm-eabi/ \\
'''
    new = '''# RPGMP_RUBY193_SOURCE_HEADERS: make install is intentionally skipped.
LOCAL_C_INCLUDES := \\
\t$(LOCAL_PATH)/../ruby193/include \\
\t$(LOCAL_PATH)/../ruby193/.ext/include/arm-eabi \\
\t$(LOCAL_PATH)/../ruby193 \\
'''
    if old not in text:
        raise RuntimeError("Ruby 1.9.3 include block not found")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"PATCHED {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
