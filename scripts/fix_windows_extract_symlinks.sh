#!/usr/bin/env bash
set -euo pipefail

ROOT="/mnt/d/GPT/MKXP/DIST/rpg maker player"

replace_file() {
    local dst="$1"
    local src="$2"
    if [ ! -L "$dst" ]; then
        echo "SKIP $dst (already real)"
        return 0
    fi
    local tmp="${dst}.winreal"
    cp -fL "$src" "$tmp"
    mv -Tf "$tmp" "$dst"
    echo "FILE $dst <= $src"
}

replace_empty_dir() {
    local dst="$1"
    if [ -L "$dst" ]; then
        python3 - "$dst" <<'PY'
import os, sys
os.unlink(sys.argv[1])
PY
    fi
    mkdir -p "$dst"
    echo "DIR  $dst"
}

# Ruby 1.8
replace_file "$ROOT/runtime/ruby18/ruby/lib/libruby.so" "$ROOT/runtime/ruby18/ruby/lib/libruby.so.1.8.7"
replace_file "$ROOT/runtime/ruby18/ruby/lib/libruby.so.1.8" "$ROOT/runtime/ruby18/ruby/lib/libruby.so.1.8.7"
replace_file "$ROOT/runtime/ruby18/ruby/openssl/lib/libcrypto.so" "$ROOT/runtime/ruby18/ruby/openssl/lib/libcrypto.so.1.0.0"
replace_file "$ROOT/runtime/ruby18/ruby/openssl/lib/libssl.so" "$ROOT/runtime/ruby18/ruby/openssl/lib/libssl.so.1.0.0"
replace_file "$ROOT/runtime/ruby18/ruby/openssl/ssl/cert.pem" "/etc/ssl/certs/ca-certificates.crt"
replace_empty_dir "$ROOT/runtime/ruby18/ruby/openssl/ssl/certs"

# Ruby 1.9 nothreaded
replace_file "$ROOT/runtime/ruby19/nothreaded_ruby/lib/libyaml-0.so.2" "$ROOT/runtime/ruby19/nothreaded_ruby/lib/libyaml-0.so.2.0.4"
replace_file "$ROOT/runtime/ruby19/nothreaded_ruby/lib/libyaml.so" "$ROOT/runtime/ruby19/nothreaded_ruby/lib/libyaml-0.so.2.0.4"
replace_file "$ROOT/runtime/ruby19/nothreaded_ruby/openssl/lib/libcrypto.so" "$ROOT/runtime/ruby19/nothreaded_ruby/openssl/lib/libcrypto.so.1.0.0"
replace_file "$ROOT/runtime/ruby19/nothreaded_ruby/openssl/lib/libssl.so" "$ROOT/runtime/ruby19/nothreaded_ruby/openssl/lib/libssl.so.1.0.0"
replace_file "$ROOT/runtime/ruby19/nothreaded_ruby/openssl/ssl/cert.pem" "/etc/ssl/certs/ca-certificates.crt"
replace_empty_dir "$ROOT/runtime/ruby19/nothreaded_ruby/openssl/ssl/certs"

# Ruby 1.9 threaded
replace_file "$ROOT/runtime/ruby19/ruby/lib/libruby.so" "$ROOT/runtime/ruby19/ruby/lib/libruby.so.1.9.1"
replace_file "$ROOT/runtime/ruby19/ruby/lib/libruby.so.1.9" "$ROOT/runtime/ruby19/ruby/lib/libruby.so.1.9.1"
replace_file "$ROOT/runtime/ruby19/ruby/lib/libyaml-0.so.2" "$ROOT/runtime/ruby19/ruby/lib/libyaml-0.so.2.0.4"
replace_file "$ROOT/runtime/ruby19/ruby/lib/libyaml.so" "$ROOT/runtime/ruby19/ruby/lib/libyaml-0.so.2.0.4"
replace_file "$ROOT/runtime/ruby19/ruby/openssl/lib/libcrypto.so" "$ROOT/runtime/ruby19/ruby/openssl/lib/libcrypto.so.1.0.0"
replace_file "$ROOT/runtime/ruby19/ruby/openssl/lib/libssl.so" "$ROOT/runtime/ruby19/ruby/openssl/lib/libssl.so.1.0.0"
replace_file "$ROOT/runtime/ruby19/ruby/openssl/ssl/cert.pem" "/etc/ssl/certs/ca-certificates.crt"
replace_empty_dir "$ROOT/runtime/ruby19/ruby/openssl/ssl/certs"

# Ruby 3.1
replace_file "$ROOT/runtime/ruby31/ruby/lib/libruby.so" "$ROOT/runtime/ruby31/ruby/lib/libruby.so.3.1.3"
replace_file "$ROOT/runtime/ruby31/ruby/lib/libruby.so.3.1" "$ROOT/runtime/ruby31/ruby/lib/libruby.so.3.1.3"

echo WINDOWS_EXTRACT_SYMLINK_FIX_PASS
