#!/usr/bin/env python3
from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: fix_ruby193_openssl_extconf.py /path/to/extconf.rb", file=sys.stderr)
        return 2

    path = Path(sys.argv[1])
    text = path.read_text(encoding="utf-8")
    if 'have_func("SSLv3_method")' in text:
        print(f"ALREADY_PATCHED {path}")
        return 0

    anchor = 'have_func("SSLv2_client_method")\n'
    if anchor not in text:
        raise RuntimeError("SSLv2 client feature-probe anchor not found")

    insert = (
        anchor
        + 'have_func("SSLv3_method")\n'
        + 'have_func("SSLv3_server_method")\n'
        + 'have_func("SSLv3_client_method")\n'
    )
    path.write_text(text.replace(anchor, insert, 1), encoding="utf-8")
    print(f"PATCHED {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
