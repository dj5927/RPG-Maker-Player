#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$ROOT/_dev/build"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cat >"$TMP/fake_steam_api.cpp" <<'EOF'
#include <cstdio>
#include <cstdlib>
extern "C" bool SteamAPI_Init() { return true; }
extern "C" void* SteamAPI_SteamUtils_v011() { return reinterpret_cast<void*>(0x1); }
extern "C" bool SteamAPI_ISteamUtils_ShowFloatingGamepadTextInput(void*, int, int, int, int, int) {
  const char* marker = std::getenv("MKXP_V093_MARKER");
  if (marker) {
    if (FILE* f = std::fopen(marker, "wb")) { std::fputs("shown\n", f); std::fclose(f); }
  }
  return true;
}
EOF
g++ -shared -fPIC "$TMP/fake_steam_api.cpp" -o "$TMP/libsteam_api.so"

grep -q 'SteamAPI_ISteamUtils_ShowFloatingGamepadTextInput' "$ROOT/_dev/src/main.cpp"
grep -q 'SteamAPI_SteamUtils_v011' "$ROOT/_dev/src/main.cpp"
grep -q 'SteamAPI_SteamUtils_v010' "$ROOT/_dev/src/main.cpp"
grep -q 'MKXP_STEAM_API_PATH' "$ROOT/_dev/src/main.cpp"
grep -q '\${CMAKE_DL_LIBS}' "$ROOT/_dev/CMakeLists.txt"

cat >"$TMP/probe.cpp" <<'EOF'
#include <dlfcn.h>
#include <cstdlib>
using Init = bool (*)();
using Utils = void* (*)();
using Show = bool (*)(void*, int, int, int, int, int);
int main(int argc, char** argv) {
  void* h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL); if (!h) return 1;
  auto init = reinterpret_cast<Init>(dlsym(h, "SteamAPI_Init"));
  auto utils = reinterpret_cast<Utils>(dlsym(h, "SteamAPI_SteamUtils_v011"));
  auto show = reinterpret_cast<Show>(dlsym(h, "SteamAPI_ISteamUtils_ShowFloatingGamepadTextInput"));
  if (!init || !utils || !show || !init()) return 2;
  return show(utils(), 0, 0, 0, 0, 0) ? 0 : 3;
}
EOF
g++ "$TMP/probe.cpp" -ldl -o "$TMP/probe"
marker="$TMP/steamworks_called.txt"
MKXP_V093_MARKER="$marker" "$TMP/probe" "$TMP/libsteam_api.so"
test -s "$marker"
test -x "$BUILD/MKXP_Launcher"
echo "V093 Steamworks keyboard bridge smoke PASS"
