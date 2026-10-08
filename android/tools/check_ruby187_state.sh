#!/usr/bin/env bash
set -u

ROOT=/opt/rpgmp-mkxpz-build/app/jni
cd "$ROOT"

echo RUBY_SO
if [ -f build-arm64-v8a/lib/libruby187.so ]; then
  stat -c '%n %s' build-arm64-v8a/lib/libruby187.so
else
  echo MISSING
fi

echo SSL_A
if [ -f build-arm64-v8a/lib/libssl.a ]; then
  stat -c '%n %s' build-arm64-v8a/lib/libssl.a
else
  echo MISSING
fi

echo CRYPTO_A
if [ -f build-arm64-v8a/lib/libcrypto.a ]; then
  stat -c '%n %s' build-arm64-v8a/lib/libcrypto.a
else
  echo MISSING
fi

echo RUBY187_STATUS
git -C ruby187 status --short || true

echo CONFIG_TAIL
tail -n 60 ruby187/config.log 2>/dev/null || true
