#!/usr/bin/env python3
from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_ruby193_fake.py /path/to/arm-eabi-fake.rb", file=sys.stderr)
        return 2

    path = Path(sys.argv[1])
    text = path.read_text(encoding="utf-8")
    marker = "RPGMP_HOST_RUBY_TRUE_FALSE_COMPAT"
    if marker in text:
        print(f"ALREADY_PATCHED {path}")
        return 0

    text += (
        "\n# RPGMP_HOST_RUBY_TRUE_FALSE_COMPAT\n"
        "TRUE = true unless defined?(TRUE)\n"
        "FALSE = false unless defined?(FALSE)\n"
    )
    path.write_text(text, encoding="utf-8")
    print(f"PATCHED {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
