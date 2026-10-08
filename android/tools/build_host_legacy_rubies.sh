#!/usr/bin/env bash
set -euo pipefail

ROOT=/opt/rpgmp-host-rubies-src
R187_PREFIX=/opt/rpgmp-host-ruby187
R193_PREFIX=/opt/rpgmp-host-ruby193
JOBS="$(nproc)"

COMMON_CFLAGS='-O2 -Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types -Wno-error=int-conversion'
R193_CFLAGS="$COMMON_CFLAGS -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0"

mkdir -p "$ROOT"

echo '== host Ruby 1.9.3-p551 =='
if [ ! -x "$R193_PREFIX/bin/ruby" ]; then
  rm -rf "$R193_PREFIX"
  export RUBY_CONFIGURE_OPTS='--without-openssl --disable-install-doc --without-gdbm --without-tk'
  export RUBY_CFLAGS="$R193_CFLAGS"
  ruby-build -v 1.9.3-p551 "$R193_PREFIX"
fi
"$R193_PREFIX/bin/ruby" -v

echo '== host Ruby 1.8.7 =='
if [ ! -x "$R187_PREFIX/bin/ruby" ]; then
  rm -rf "$R187_PREFIX" "$ROOT/ruby187"
  git clone -q -c advice.detachedHead=false --single-branch --depth 1 \
    -b ruby_1_8_7 https://github.com/mkxp-z/ruby "$ROOT/ruby187"
  cd "$ROOT/ruby187"
  autoconf
  CFLAGS="$COMMON_CFLAGS" ./configure \
    --prefix="$R187_PREFIX" \
    --without-openssl \
    --disable-install-doc \
    --without-gdbm \
    --without-tk
  make -j"$JOBS"
  make install
fi
"$R187_PREFIX/bin/ruby" -v

echo HOST_LEGACY_RUBIES_READY
