#!/usr/bin/env bash
set -euo pipefail

NDK=/opt/rpgmp-android-ndk-r25c/android-ndk-r25c
CC="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android21-clang"
JNI=/opt/rpgmp-mkxpz-build/app/jni
PROJECT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$PROJECT/app/src/main/jniLibs/arm64-v8a"

cat > /tmp/rpgmp_mblen.c <<'EOF'
#include <stddef.h>
#include <wchar.h>
#include <string.h>
int mblen(const char *s, size_t n) {
  static mbstate_t state;
  if (!s) { memset(&state, 0, sizeof(state)); return 0; }
  size_t r = mbrlen(s, n, &state);
  if (r == (size_t)-1 || r == (size_t)-2) {
    memset(&state, 0, sizeof(state));
    return -1;
  }
  return (int)r;
}
EOF

"$CC" -c /tmp/rpgmp_mblen.c -o /tmp/rpgmp_mblen.o

cd "$JNI/ruby187"
"$CC" -o ruby187-probe main.o libruby.a /tmp/rpgmp_mblen.o -lz -lm -ldl -llog

cd "$JNI/ruby193"
"$CC" -o ruby193-probe main.o libruby-static.a /tmp/rpgmp_mblen.o -lz -lm -ldl -llog

cd "$JNI/ruby"
"$CC" -o ruby31-probe main.o "$JNI/build-arm64-v8a/lib/libruby-static.a" \
  -lz -lm -ldl -llog -landroid

cp "$JNI/ruby187/ruby187-probe" "$OUT/libprobe-ruby18.so"
cp "$JNI/ruby193/ruby193-probe" "$OUT/libprobe-ruby19.so"
cp "$JNI/ruby/ruby31-probe" "$OUT/libprobe-ruby31.so"

echo RUBY_PROBE_ARM64_BUILD_PASS
