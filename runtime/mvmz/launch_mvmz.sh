#!/bin/bash
set -u

echo "mvmz runtime patch=V108-json-compat"

ROOT="${1:?launcher root required}"
GAME="${2:?game root required}"
WEBROOT="${3:?web root required}"
ENGINE="${4:?engine required}"
VERSION="${5:?nwjs version required}"

COMPAT="$ROOT/runtime/mvmz"
REQUESTED_VERSION="$VERSION"
ORIGINAL_VERSION="${MKXP_MVMZ_ORIGINAL_NWJS:-$REQUESTED_VERSION}"
COMPAT_PROFILE=""
LEGACY_KHAS=0
MANUAL_NWJS=0
PROBE_MODE="${MKXP_MVMZ_COMPAT_PROBE:-0}"

# Old Windows MV packages often bundle an obsolete NW.js. On SteamOS the
# bundled Windows runtime is a compatibility hint, not a reason to pin the
# native Linux runtime forever. In AUTO mode, old MV runtimes are upgraded to
# the newest installed native NW.js. A game-specific mkxp-nwjs.txt pin remains
# authoritative and disables this modernization.
PRE_MINOR="$(printf '%s' "$REQUESTED_VERSION" | sed -n 's/^v0\.\([0-9][0-9]*\).*/\1/p')"
ORIGINAL_MINOR="$(printf '%s' "$ORIGINAL_VERSION" | sed -n 's/^v0\.\([0-9][0-9]*\).*/\1/p')"
PLUGINS_FILE="$WEBROOT/js/plugins.js"
if [ "$PROBE_MODE" = "1" ]; then
  MANUAL_NWJS=1
  echo "mvmz nwjs policy=probe-exact version=$REQUESTED_VERSION"
fi
if [ -f "$GAME/mkxp-nwjs.txt" ]; then
  PINNED_NWJS="$(tr -d '\r\n\t ' < "$GAME/mkxp-nwjs.txt" 2>/dev/null || true)"
  if [ -n "$PINNED_NWJS" ] && [ "$PINNED_NWJS" != "auto" ]; then
    MANUAL_NWJS=1
    echo "mvmz nwjs policy=manual pin=$PINNED_NWJS"
  fi
fi
if [ -f "$PLUGINS_FILE" ] && grep -q 'KhasUltraLighting' "$PLUGINS_FILE" 2>/dev/null; then
  LEGACY_KHAS=1
fi
if [ "$ENGINE" = "MV" ] && [ "$MANUAL_NWJS" -eq 0 ] && [ -n "$PRE_MINOR" ] && [ "$PRE_MINOR" -lt 50 ] 2>/dev/null; then
  LATEST_NWROOT="$(find "$ROOT/runtime/nwjs" -mindepth 1 -maxdepth 1 -type d -name 'v*' -printf '%f\n' 2>/dev/null | sort -V | tail -n 1)"
  LATEST_MINOR="$(printf '%s' "$LATEST_NWROOT" | sed -n 's/^v0\.\([0-9][0-9]*\).*/\1/p')"
  if [ -n "$LATEST_NWROOT" ] && [ -n "$LATEST_MINOR" ] && [ "$LATEST_MINOR" -ge 50 ] 2>/dev/null && \
     [ -x "$ROOT/runtime/nwjs/$LATEST_NWROOT/nw" ] && [ "$LATEST_NWROOT" != "$REQUESTED_VERSION" ]; then
    VERSION="$LATEST_NWROOT"
    COMPAT_PROFILE="legacy-mv-modern-nw"
    echo "mvmz compat: legacy MV -> modern NW.js $REQUESTED_VERSION -> $VERSION"
  fi
fi

EFFECTIVE_MINOR="$(printf '%s' "$VERSION" | sed -n 's/^v0\.\([0-9][0-9]*\).*/\1/p')"
if [ -z "$COMPAT_PROFILE" ] && [ "$ENGINE" = "MV" ] && [ -n "$ORIGINAL_MINOR" ] && \
   [ "$ORIGINAL_MINOR" -lt 50 ] 2>/dev/null && [ -n "$EFFECTIVE_MINOR" ] && \
   [ "$EFFECTIVE_MINOR" -ge 50 ] 2>/dev/null; then
  COMPAT_PROFILE="legacy-mv-modern-nw"
  echo "mvmz compat: legacy MV source $ORIGINAL_VERSION -> modern NW.js $VERSION"
fi

NWROOT="$ROOT/runtime/nwjs/$VERSION"
NW="$NWROOT/nw"
CICPOFFS="$COMPAT/lib/cicpoffs"
PATCH="$COMPAT/jspatches/case-insensitive-nw.js"
LEGACY_PATCH="$COMPAT/jspatches/case-insensitive-nw-legacy.js"
EXIT_PATCH="$COMPAT/jspatches/start-select-exit.js"
PRENODE_DIAG="$COMPAT/jspatches/pre-node-error-diagnostics.js"
BOOT_DIAG="$COMPAT/jspatches/boot-ready-diagnostics.js"
FONT_COMPAT="$COMPAT/jspatches/font-compat.js"
PROBE_PATCH="$COMPAT/jspatches/compat-probe.js"
WORKBASE="$ROOT/cache/mvmz"
SAFE_NAME="$(basename "$GAME" | sed 's/[^A-Za-z0-9._-]/_/g')"
WORK="$WORKBASE/$SAFE_NAME"
WWW="$WORK/www"

if [ ! -x "$NW" ]; then
  echo "FATAL: NW.js executable not found: $NW"
  exit 41
fi
if [ ! -f "$WEBROOT/index.html" ]; then
  echo "FATAL: RPG Maker $ENGINE index.html not found: $WEBROOT/index.html"
  exit 42
fi

if command -v ldd >/dev/null 2>&1; then
  MISSING="$(ldd "$NW" 2>/dev/null | grep 'not found' || true)"
  if [ -n "$MISSING" ]; then
    echo "FATAL: NW.js missing shared libraries:"
    echo "$MISSING"
    exit 44
  fi
fi

mkdir -p "$WORK"
export LD_LIBRARY_PATH="$COMPAT/lib:$NWROOT${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
unset LD_PRELOAD 2>/dev/null || true
GLOBAL_FONTDIR="$ROOT/runtime/rtp/fonts"
export MKXP_GLOBAL_FONT_DIR="$GLOBAL_FONTDIR"

# Old NW.js/Chromium builds can abort on minimal SteamOS environments when
# Fontconfig cannot resolve a default family. Prefer the game's own fonts,
# then the launcher's shared runtime/rtp/fonts fallback, and finally the bundled
# MV/MZ compatibility font.
FONTDIR="$COMPAT/fonts"
GAME_FONTDIR="$WEBROOT/fonts"
if [ -d "$FONTDIR" ] || [ -d "$GLOBAL_FONTDIR" ] || [ -d "$GAME_FONTDIR" ]; then
  mkdir -p "$WORK/fontcache"
  cat > "$WORK/fonts.conf" <<EOF
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <dir>$GAME_FONTDIR</dir>
  <dir>$GLOBAL_FONTDIR</dir>
  <dir>$FONTDIR</dir>
  <cachedir>$WORK/fontcache</cachedir>
  <alias>
    <family>sans-serif</family>
    <prefer><family>WenQuanYi Micro Hei</family></prefer>
  </alias>
  <alias>
    <family>serif</family>
    <prefer><family>WenQuanYi Micro Hei</family></prefer>
  </alias>
  <alias>
    <family>monospace</family>
    <prefer><family>WenQuanYi Micro Hei</family></prefer>
  </alias>
</fontconfig>
EOF
  export FONTCONFIG_FILE="$WORK/fonts.conf"
  export FONTCONFIG_PATH="$WORK"
fi

cleanup_www() {
  if command -v mountpoint >/dev/null 2>&1 && mountpoint -q "$WWW" 2>/dev/null; then
    if command -v fusermount3 >/dev/null 2>&1; then
      fusermount3 -u "$WWW" 2>/dev/null || true
    elif command -v fusermount >/dev/null 2>&1; then
      fusermount -u "$WWW" 2>/dev/null || true
    fi
  fi
  if [ -L "$WWW" ]; then
    unlink "$WWW" 2>/dev/null || true
  elif [ -d "$WWW" ]; then
    rmdir "$WWW" 2>/dev/null || true
  fi
}

cleanup_www
trap cleanup_www EXIT INT TERM

CHROMIUM_ARGS="--enable-webgl --ignore-gpu-blocklist --force-gpu-rasterization --enable-gpu-memory-buffer-video-frames --enable-native-gpu-memory-buffers --enable-zero-copy --enable-gpu-async-worker-context --disable-password-generation --enable-logging=stderr"
WINDOW_SHOW=true
if [ "$PROBE_MODE" = "1" ]; then
  WINDOW_SHOW=false
fi

# Compatibility profile: old MV + Khas Ultra Lighting requires a real WebGL
# renderer. NW.js 0.29 uses an older Chromium where the GPU override flag is
# still named --ignore-gpu-blacklist; on SteamOS/Mesa, forcing desktop GL also
# avoids falling through to SwiftShader and then PixiJS Canvas.
if [ "$COMPAT_PROFILE" != "legacy-mv-modern-nw" ] && [ "$ENGINE" = "MV" ] && [ -n "$PRE_MINOR" ] && [ "$PRE_MINOR" -lt 50 ] 2>/dev/null; then
  if [ -f "$PLUGINS_FILE" ] && grep -q 'KhasUltraLighting' "$PLUGINS_FILE" 2>/dev/null; then
    CHROMIUM_ARGS="$CHROMIUM_ARGS --ignore-gpu-blacklist --use-gl=desktop"
    COMPAT_PROFILE="legacy-khas-webgl"
    echo "mvmz compat: legacy Khas WebGL -> desktop GL"
  fi
fi

cat > "$WORK/package.json" <<EOF
{
  "name": "MKXP-MV-MZ",
  "main": "www/index.html",
  "js-flags": "--expose-gc",
  "inject_js_start": "case-insensitive-nw.js",
  "chromium-args": "$CHROMIUM_ARGS",
  "window": {
    "toolbar": false,
    "show": $WINDOW_SHOW
  }
}
EOF

MOUNTED=0
PATCHED_WEBROOT=""
GAME_PATCH_MANIFEST=""
CENTRAL_PATCH_DB="${RPGMP_PATCH_DB:-$ROOT/runtime/_compat/patches.json}"
if [ -f "$GAME/rpgmp-patches.json" ]; then
  GAME_PATCH_MANIFEST="$GAME/rpgmp-patches.json"
elif [ -f "$WEBROOT/rpgmp-patches.json" ]; then
  GAME_PATCH_MANIFEST="$WEBROOT/rpgmp-patches.json"
fi
if [ -n "$GAME_PATCH_MANIFEST" ] || [ -f "$CENTRAL_PATCH_DB" ]; then
  if command -v python3 >/dev/null 2>&1; then
    PATCH_RESULT="$(python3 "$COMPAT/compat_patch_loader.py" \
      --webroot "$WEBROOT" --manifest "$GAME_PATCH_MANIFEST" \
      --central "$CENTRAL_PATCH_DB" \
      --engine "$ENGINE" --cache "$WORK/patchviews" 2>&1)"
    PATCH_RC=$?
    printf '%s\n' "$PATCH_RESULT"
    if [ "$PATCH_RC" -eq 0 ]; then
      PATCHED_WEBROOT="$(printf '%s\n' "$PATCH_RESULT" | sed -n 's/^RPGMP_PATCH_VIEW=//p' | tail -n 1)"
    else
      echo "[RPGMP-PATCH] invalid manifest; unmodified game launch"
    fi
  else
    echo "[RPGMP-PATCH] python3 unavailable; unmodified game launch"
  fi
fi

if [ -z "$PATCHED_WEBROOT" ] && [ "$COMPAT_PROFILE" != "legacy-mv-modern-nw" ] && [ "${MKXP_MVMZ_DISABLE_CICPOFFS:-0}" != "1" ] && [ -x "$CICPOFFS" ]; then
  mkdir -p "$WWW"
  echo "mvmz casefold: trying cicpoffs"
  if "$CICPOFFS" "$WEBROOT" "$WWW"; then
    for _ in 1 2 3 4 5; do
      if [ -d "$WWW/js" ] && [ -f "$WWW/index.html" ]; then
        MOUNTED=1
        break
      fi
      sleep 1
    done
  fi
fi

if [ "$MOUNTED" -eq 1 ]; then
  echo "mvmz casefold: cicpoffs mounted"
  printf '%s\n' '// cicpoffs active: JS casefold hook not required' > "$WORK/case-insensitive-nw.js"
  unset KAWARIKI_NWJS_CIFS 2>/dev/null || true
else
  echo "mvmz casefold: cicpoffs unavailable/failed; using JS casefold fallback"
  cleanup_www
  if [ -n "$PATCHED_WEBROOT" ]; then
    ln -s "$PATCHED_WEBROOT" "$WWW"
    echo "[RPGMP-PATCH] isolated game webroot=$PATCHED_WEBROOT"
  elif [ "$COMPAT_PROFILE" = "legacy-mv-modern-nw" ] && [ "${MKXP_MVMZ_FORCE_SYMLINK:-0}" != "1" ]; then
    mkdir -p "$WWW"
    if cp -al -f "$WEBROOT/." "$WWW/" 2>/dev/null; then
      echo "mvmz webroot mirror=hardlink"
    else
      echo "mvmz webroot hardlink mirror failed; using file copy fallback"
      cp -a --reflink=auto "$WEBROOT/." "$WWW/"
      echo "mvmz webroot mirror=copy"
    fi
  else
    ln -s "$WEBROOT" "$WWW"
  fi
  export KAWARIKI_NWJS_CIFS=1
  FALLBACK_MINOR="$(printf '%s' "$VERSION" | sed -n 's/^v0\.\([0-9][0-9]*\).*/\1/p')"
  if [ -n "$FALLBACK_MINOR" ] && [ "$FALLBACK_MINOR" -lt 50 ] 2>/dev/null; then
    cp -f "$LEGACY_PATCH" "$WORK/case-insensitive-nw.js"
  else
    cp -f "$PATCH" "$WORK/case-insensitive-nw.js"
  fi
fi

if [ -f "$PRENODE_DIAG" ]; then
  TMP_INJECT="$WORK/case-insensitive-nw.js.tmp"
  cat "$PRENODE_DIAG" "$WORK/case-insensitive-nw.js" > "$TMP_INJECT"
  mv "$TMP_INJECT" "$WORK/case-insensitive-nw.js"
fi

if [ "$PROBE_MODE" = "1" ] && [ -f "$PROBE_PATCH" ]; then
  TMP_INJECT="$WORK/case-insensitive-nw.js.tmp"
  cat "$PROBE_PATCH" "$WORK/case-insensitive-nw.js" > "$TMP_INJECT"
  mv "$TMP_INJECT" "$WORK/case-insensitive-nw.js"
  export MKXP_MVMZ_PROBE_REQUIRE_WEBGL="$LEGACY_KHAS"
fi

if [ -f "$FONT_COMPAT" ]; then
  printf '\n' >> "$WORK/case-insensitive-nw.js"
  cat "$FONT_COMPAT" >> "$WORK/case-insensitive-nw.js"
fi

if [ "$PROBE_MODE" != "1" ] && [ "$COMPAT_PROFILE" = "legacy-mv-modern-nw" ] && [ "$LEGACY_KHAS" -eq 1 ] && [ -f "$BOOT_DIAG" ]; then
  printf '\n' >> "$WORK/case-insensitive-nw.js"
  cat "$BOOT_DIAG" >> "$WORK/case-insensitive-nw.js"
fi

if command -v sha256sum >/dev/null 2>&1; then
  INJECT_SHA="$(sha256sum "$WORK/case-insensitive-nw.js" | awk '{print $1}')"
else
  INJECT_SHA="unavailable"
fi
echo "mvmz inject=$WORK/case-insensitive-nw.js sha256=$INJECT_SHA"
if grep -q 'MKXP PRENODE' "$WORK/case-insensitive-nw.js" 2>/dev/null; then
  echo "mvmz inject prenode=present"
else
  echo "mvmz inject prenode=MISSING"
fi
if grep -q 'require unavailable' "$WORK/case-insensitive-nw.js" 2>/dev/null; then
  echo "mvmz inject require-guard=present"
else
  echo "mvmz inject require-guard=MISSING"
fi

# Keep the exit hotkey inside the focused NW.js process. SteamOS/Gamescope may
# route Steam Input only to the focused game, so the hidden parent launcher
# cannot reliably see Start+Select while a game is running.
if [ -f "$EXIT_PATCH" ]; then
  printf '
' >> "$WORK/case-insensitive-nw.js"
  cat "$EXIT_PATCH" >> "$WORK/case-insensitive-nw.js"
fi

# Some plugins enumerate files from process.cwd() instead of the web root.
# Mirror the upstream launcher's compatibility behavior with harmless symlinks.
if [ -d "$WEBROOT/js/plugins" ] && grep -RqsE 'readdirSync|accessSync' "$WEBROOT/js/plugins" 2>/dev/null; then
  for ITEM in "$GAME"/*; do
    [ -e "$ITEM" ] || continue
    BASE="$(basename "$ITEM")"
    case "$(printf '%s' "$BASE" | tr '[:upper:]' '[:lower:]')" in
      www|package.json|game.exe|game_en.exe|nw.dll|node.dll|nw.pak|nw_100_percent.pak|nw_200_percent.pak|resources.pak|icudtl.dat|ffmpeg.dll|d3dcompiler_47.dll|libegl.dll|libglesv2.dll|v8_context_snapshot.bin|snapshot_blob.bin|locales|swiftshader)
        continue
        ;;
    esac
    if [ ! -e "$WORK/$BASE" ] && [ ! -L "$WORK/$BASE" ]; then
      ln -s "$ITEM" "$WORK/$BASE" 2>/dev/null || true
    fi
  done
fi

export MKXP_MVMZ_ENGINE="$ENGINE"
export MKXP_MVMZ_GAME="$GAME"
export MKXP_MVMZ_WEBROOT="$WEBROOT"

OZONE=x11
MINOR="$(printf '%s' "$VERSION" | sed -n 's/^v0\.\([0-9][0-9]*\).*/\1/p')"
if [ -n "$MINOR" ] && [ "$MINOR" -ge 102 ] 2>/dev/null && [ "${XDG_SESSION_TYPE:-}" = "wayland" ]; then
  OZONE=wayland
fi

echo "mvmz launch: engine=$ENGINE nwjs=$VERSION ozone=$OZONE"
if [ "$REQUESTED_VERSION" != "$VERSION" ]; then
  echo "mvmz requested nwjs=$REQUESTED_VERSION effective nwjs=$VERSION"
fi
echo "mvmz game=$GAME"
echo "mvmz webroot=$WEBROOT"
[ -n "$COMPAT_PROFILE" ] && echo "mvmz compat profile=$COMPAT_PROFILE"
cd "$WORK" || exit 43

if [ "${MKXP_MVMZ_DRY_RUN:-0}" = "1" ]; then
  echo "mvmz dry-run PASS: package=$WORK/package.json www=$WWW"
  exit 0
fi

if [ "$PROBE_MODE" = "1" ]; then
  PROFILE="$ROOT/cache/nwprobe_profile/$VERSION/$SAFE_NAME"
else
  PROFILE="$ROOT/cache/nwprofile/$VERSION/$SAFE_NAME"
fi
mkdir -p "$PROFILE"
NW_ARGS=(--ozone-platform="$OZONE" --user-data-dir="$PROFILE")
if [ "${MKXP_MVMZ_NO_SANDBOX:-0}" = "1" ]; then
  NW_ARGS+=(--no-sandbox)
fi
"$NW" "${NW_ARGS[@]}" "$WORK"
exit $?

