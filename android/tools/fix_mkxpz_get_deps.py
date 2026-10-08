#!/usr/bin/env python3
from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: fix_mkxpz_get_deps.py /path/to/get_deps.sh", file=sys.stderr)
        return 2

    path = Path(sys.argv[1])
    text = path.read_text(encoding="utf-8")

    if not text.startswith("#!/bin/bash\nset -e\n"):
        text = text.replace("#!/bin/bash\n", "#!/bin/bash\nset -e\n", 1)

    # Upstream tests the wrong directory names, which makes a resumed fetch
    # try to clone Ruby into an already existing directory.
    text = text.replace('if [[ ! -d "ruby_187" ]]; then', 'if [[ ! -d "ruby187" ]]; then')
    text = text.replace('if [[ ! -d "ruby_193" ]]; then', 'if [[ ! -d "ruby193" ]]; then')

    ruby187 = "patch -p1 < ../patches/ruby187-remove-inline.patch\n"
    if ruby187 + "cd ..\n" not in text:
        if ruby187 not in text:
            raise RuntimeError("ruby187 patch anchor not found")
        text = text.replace(ruby187, ruby187 + "cd ..\n", 1)

    path.write_text(text, encoding="utf-8")

    # The current ruby_1_9_3 branch no longer has the TLSv1_* lines used as
    # trailing context by the upstream OpenSSL3 patch. Shrink only that first
    # hunk; the ossl_ssl.c hunk stays byte-for-byte upstream.
    openssl_patch = path.parent / "patches" / "ruby193files" / "openssl3.patch"
    patch_text = openssl_patch.read_text(encoding="utf-8")
    second_diff = patch_text.find("diff --git a/ext/openssl/ossl_ssl.c")
    if second_diff < 0:
        raise RuntimeError("openssl3 ossl_ssl.c hunk not found")
    fixed_first_hunk = '''diff --git a/ext/openssl/extconf.rb b/ext/openssl/extconf.rb
--- a/ext/openssl/extconf.rb
+++ b/ext/openssl/extconf.rb
@@ -104,3 +104,6 @@
 have_func("SSLv2_method")
 have_func("SSLv2_server_method")
 have_func("SSLv2_client_method")
+have_func("SSLv3_method")
+have_func("SSLv3_server_method")
+have_func("SSLv3_client_method")
'''
    openssl_patch.write_text(fixed_first_hunk + patch_text[second_diff:], encoding="utf-8")

    print(f"PATCHED {path}")
    print(f"PATCHED {openssl_patch}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
