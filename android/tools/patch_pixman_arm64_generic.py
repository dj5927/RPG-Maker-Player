#!/usr/bin/env python3
from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_pixman_arm64_generic.py /path/to/jni", file=sys.stderr)
        return 2

    jni = Path(sys.argv[1])
    mk = jni / "pixman.mk"
    cfg = jni / "preconfigured" / "arm64-v8a" / "pixman" / "config.h"

    text = mk.read_text(encoding="utf-8")
    old = '''else ifeq ($(TARGET_ARCH_ABI), arm64-v8a)
\tLOCAL_CFLAGS += -DUSE_ARM_NEON -DUSE_ARM_A64_NEON
\tLOCAL_SRC_FILES += \\
\t\t$(LOCAL_PATH)/pixman/pixman-arm-neon.c \\
\t\t$(LOCAL_PATH)/pixman/pixman-arma64-neon-asm.S \\
\t\t$(LOCAL_PATH)/pixman/pixman-arma64-neon-asm-bilinear.S
'''
    new = '''else ifeq ($(TARGET_ARCH_ABI), arm64-v8a)
\t# RPGMP: old pixman AArch64 GAS macros are incompatible with modern
\t# Clang integrated assembler. Use the portable C implementation.
'''
    if old in text:
        text = text.replace(old, new, 1)
        mk.write_text(text, encoding="utf-8")
    elif "RPGMP: old pixman AArch64 GAS macros" not in text:
        raise RuntimeError("arm64 pixman block not found")

    cfg_text = cfg.read_text(encoding="utf-8")
    cfg_text = cfg_text.replace("#define USE_ARM_A64_NEON 1", "/* #undef USE_ARM_A64_NEON */")
    cfg_text = cfg_text.replace("#define USE_ARM_NEON 1", "/* #undef USE_ARM_NEON */")
    cfg.write_text(cfg_text, encoding="utf-8")

    print(f"PATCHED {mk}")
    print(f"PATCHED {cfg}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
