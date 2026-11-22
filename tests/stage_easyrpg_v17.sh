#!/usr/bin/env bash
set -euo pipefail
ROOT=/mnt/d/GPT/MKXP
SRC=/root/rpgmp-v17
OUT="$ROOT/work/v17_runtime_stage/runtime/easyrpg"
mkdir -p "$OUT/lib" "$OUT/LICENSES"
cp "$SRC/install/bin/easyrpg-player" "$OUT/easyrpg-player-v17"
chmod 755 "$OUT/easyrpg-player-v17"
cp "$SRC/easyrpg/player/COPYING" "$OUT/LICENSES/EasyRPG-Player-GPL-3.0.txt"
cp "$SRC/easyrpg/liblcf/COPYING" "$OUT/LICENSES/liblcf-License.txt"
for name in \
  libinih.so.1 libicui18n.so.74 libicuuc.so.74 libicudata.so.74 \
  libfmt.so.9 liblhasa.so.0 libspeexdsp.so.1 libopusfile.so.0 libxmp.so.4; do
  test -f "/lib/x86_64-linux-gnu/$name" || { echo "MISSING_EASYRPG_LIB=$name" >&2; exit 5; }
  cp -L "/lib/x86_64-linux-gnu/$name" "$OUT/lib/$name"
done
echo EASYRPG_BUILT_VERSION
LD_LIBRARY_PATH="$OUT/lib:$ROOT/lib" "$OUT/easyrpg-player-v17" --version
echo EASYRPG_DIRECT_DEPENDENCIES
LD_LIBRARY_PATH="$OUT/lib:$ROOT/lib" ldd "$OUT/easyrpg-player-v17"
echo EASYRPG_V17_STAGE_PASS
