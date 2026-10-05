#!/bin/bash
set -euo pipefail

BASE=/mnt/d/GPT/MKXP
REPO="$BASE/_dev/GITHUB_V1_SOURCE"
V101="$BASE/DIST/MKXP_V101_CICPOFFS_RGSS_COMPAT_OVERLAY.tar.gz"
FULL_OUT="$BASE/DIST/RPG_Maker_Player_SteamOS_V1.3.tar.gz"
UPDATE_OUT="$BASE/DIST/RPG_Maker_Player_SteamOS_Update_V1.3.tar.gz"

FULL_STAGE="$(mktemp -d /tmp/rpgmp_v13_full.XXXXXX)"
OLD_STAGE="$(mktemp -d /tmp/rpgmp_v10_old.XXXXXX)"
UPDATE_STAGE="$(mktemp -d /tmp/rpgmp_v13_update.XXXXXX)"
SIM_STAGE="$(mktemp -d /tmp/rpgmp_v13_sim.XXXXXX)"

V12_POINTER="$(git -C "$REPO" show v1.2:release-assets/RPG_Maker_Player_SteamOS_WINDOWS_SAFE.tar.gz)"
V12_OID="$(printf '%s\n' "$V12_POINTER" | awk -F: '/oid sha256:/ {print $2}')"
V12_OBJ="$REPO/.git/lfs/objects/${V12_OID:0:2}/${V12_OID:2:2}/$V12_OID"
test -f "$V12_OBJ"
tar -xzf "$V12_OBJ" -C "$FULL_STAGE"
FULL_ROOT="$FULL_STAGE/rpg maker player"
tar -xzf "$V101" -C "$FULL_ROOT"

cp "$BASE/MKXP_Launcher" "$FULL_ROOT/MKXP_Launcher"
mkdir -p "$FULL_ROOT/runtime/update"
cp "$BASE/runtime/update/update.sh" "$FULL_ROOT/runtime/update/update.sh"
cp "$BASE/preload/cicpoffs/LICENSE" "$FULL_ROOT/preload/cicpoffs/LICENSE"
cp "$REPO/README_KO.md" "$FULL_ROOT/README_KR.md"
cp "$REPO/README.md" "$FULL_ROOT/README_EN.md"
cp "$BASE/VERSION" "$FULL_ROOT/VERSION"
chmod +x "$FULL_ROOT/MKXP_Launcher" "$FULL_ROOT/runtime/update/update.sh"

tar -C "$FULL_STAGE" -czf "$FULL_OUT" 'rpg maker player'
gzip -t "$FULL_OUT"

POINTER="$(git -C "$REPO" show v1.0:release-assets/RPG_Maker_Player_SteamOS_WINDOWS_SAFE.tar.gz)"
V10_OID="$(printf '%s\n' "$POINTER" | awk -F: '/oid sha256:/ {print $2}')"
V10_OBJ="$REPO/.git/lfs/objects/${V10_OID:0:2}/${V10_OID:2:2}/$V10_OID"
test -f "$V10_OBJ"
tar -xzf "$V10_OBJ" -C "$OLD_STAGE"
OLD_ROOT="$OLD_STAGE/rpg maker player"

python3 - "$OLD_ROOT" "$FULL_ROOT" "$UPDATE_STAGE" <<'PY'
import hashlib, os, shutil, sys
from pathlib import Path

old = Path(sys.argv[1])
new = Path(sys.argv[2])
out = Path(sys.argv[3])
exclude_exact = {
    'config/launcher.json',
    'game/gamelist.json',
}
exclude_prefix = ('config/catalogs/', 'cache/', 'logs/', 'game/_image/')

def excluded(rel):
    s = rel.as_posix()
    return s in exclude_exact or any(s.startswith(p) for p in exclude_prefix)

def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.digest()

changed = []
for src in new.rglob('*'):
    if not src.is_file():
        continue
    rel = src.relative_to(new)
    if excluded(rel):
        continue
    before = old / rel
    if not before.is_file() or before.stat().st_size != src.stat().st_size or digest(before) != digest(src):
        dst = out / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        changed.append(rel.as_posix())

(out / 'UPDATE_FILE_LIST.txt').write_text('\n'.join(changed) + '\n', encoding='utf-8')
print('UPDATE_CHANGED_FILES=' + str(len(changed)))
PY

tar -C "$UPDATE_STAGE" -czf "$UPDATE_OUT" .
gzip -t "$UPDATE_OUT"
( cd "$BASE/DIST" && sha256sum "$(basename "$UPDATE_OUT")" >"$(basename "$UPDATE_OUT").sha256" )
( cd "$BASE/DIST" && sha256sum "$(basename "$FULL_OUT")" >"$(basename "$FULL_OUT").sha256" )

tar -xzf "$V10_OBJ" -C "$SIM_STAGE"
SIM_ROOT="$SIM_STAGE/rpg maker player"
tar -xzf "$UPDATE_OUT" -C "$SIM_ROOT"

python3 - "$FULL_ROOT" "$SIM_ROOT" <<'PY'
import hashlib, sys
from pathlib import Path

want = Path(sys.argv[1])
got = Path(sys.argv[2])
exclude_exact = {'config/launcher.json', 'game/gamelist.json'}
exclude_prefix = ('config/catalogs/', 'cache/', 'logs/', 'game/_image/')

def excluded(rel):
    s = rel.as_posix()
    return s in exclude_exact or any(s.startswith(p) for p in exclude_prefix)

def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()

checked = 0
for src in want.rglob('*'):
    if not src.is_file():
        continue
    rel = src.relative_to(want)
    if excluded(rel):
        continue
    dst = got / rel
    if not dst.is_file() or src.stat().st_size != dst.stat().st_size or digest(src) != digest(dst):
        raise SystemExit('OTA_VERIFY_FAIL: ' + rel.as_posix())
    checked += 1
print('OTA_VERIFY_FILES=' + str(checked))
PY

if tar -tzf "$UPDATE_OUT" | grep -E '^\./(config/launcher.json|config/catalogs/|cache/|logs/|game/gamelist.json|game/_image/)'; then
  echo 'ERROR: user-state path found in OTA package' >&2
  exit 9
fi

echo FULL_PACKAGE
sha256sum "$FULL_OUT"
stat -c '%s' "$FULL_OUT"
echo UPDATE_PACKAGE
sha256sum "$UPDATE_OUT"
stat -c '%s' "$UPDATE_OUT"
echo V13_PACKAGE_VERIFY_PASS
