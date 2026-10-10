"""SteamOS 1.6 full + real v1.4-to-v1.6 update; preserve user-owned files.

The update contains ALL changed files since the latest publicly available
stable 1.4, not merely changes since unpublished local candidate 1.5.2.
"""
from pathlib import Path, PurePosixPath
import hashlib
import json
import tarfile

ROOT = Path(r'D:\GPT\MKXP')
DIST = ROOT / 'DIST'
PREVIOUS = DIST / 'RPG_Maker_Player_SteamOS_V1.5.2.tar.gz'
BASE_PUBLIC = DIST / 'RPG_Maker_Player_SteamOS_V1.4.tar.gz'
FULL = DIST / 'RPG_Maker_Player_SteamOS_V1.6.tar.gz'
OTA = DIST / 'RPG_Maker_Player_SteamOS_Update_V1.6.tar.gz'
PREFIX = 'rpg maker player/'
PREV_SHA = 'b3850fb07606e3ebaee46b7da1c4fc1390eae1eeb592a7b5d18ef2427b79a656'
PUBLIC_SHA = 'caccfdae13c96e422ce0ba6bfd28b1daea1336a7e82be4397f3207938021df80'
REPLACE = {
    'MKXP_Launcher', 'VERSION',
    'runtime/_compat/verified_profiles.json',
    'runtime/_compat/merge_registry.py',
    'runtime/_compat/README.txt',
    'runtime/rtp/README.txt',
    'CENTRAL_PATCH_DB_GUIDE.md',
    'README_KR.md',
}
EXE = {'MKXP_Launcher', 'runtime/_compat/merge_registry.py',
       'runtime/ruby18/mkxp-z', 'runtime/ruby19/mkxp-z',
       'runtime/ruby31/mkxp-z', 'runtime/mvmz/launch_mvmz.sh'}
BLOCKED = {'runtime/_compat/patches.json',
           'runtime/_compat/patches_before_verified_restore.json',
           'runtime/_compat/patches_before_v16_cleanup.json'}


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for c in iter(lambda:f.read(1024*1024), b''):
            h.update(c)
    return h.hexdigest()


def bytes_digest(stream):
    h = hashlib.sha256()
    for data in iter(lambda:stream.read(1024*1024), b''):
        h.update(data)
    return h.hexdigest()


def safe(path):
    return bool(path and not path.startswith('/') and
                all(x not in ('', '.', '..') for x in PurePosixPath(path).parts))


def sidecar(archive):
    archive.with_name(archive.name + '.sha256').write_text(
        sha(archive) + '  ' + archive.name + '\n',
        encoding='ascii', newline='\n')


def verify_source():
    assert sha(PREVIOUS) == PREV_SHA, 'v1.5.2 baseline changed'
    assert sha(BASE_PUBLIC) == PUBLIC_SHA, 'public v1.4 baseline changed'
    assert (ROOT / 'VERSION').read_text(encoding='utf-8').strip() == '1.6'
    elf = (ROOT / 'MKXP_Launcher').read_bytes()
    assert elf[:4] == b'\x7fELF' and b'1.6\x00' in elf
    local = json.loads((ROOT / 'runtime/_compat/verified_profiles.json').read_text(encoding='utf-8'))
    android = json.loads((Path(r'D:\GPT\MKXP_ANDROID') / 'app/src/main/assets/rgss_compat/verified_profiles.json').read_text(encoding='utf-8'))
    assert local == android
    assert len(local['games']['curse_of_pleasure_v1']['patches']) == 4
    assert 'curse-xp-avatar-filetest-casefold' not in json.dumps(local)
    for file in REPLACE:
        assert safe(file) and (ROOT / file).is_file(), file
    for name in ('ruby18','ruby19','ruby31'):
        blob=(ROOT / ('runtime/'+name+'/mkxp-z')).read_bytes()
        assert blob.startswith(b'\x7fELF') and b'rpgmp_resource_casefold.rb' in blob
    for file in (FULL, OTA):
        assert not file.exists(), 'refusing overwrite of existing artifact: '+str(file)


def build_full():
    total = replaced = 0
    seen = set()
    with tarfile.open(PREVIOUS, 'r:gz') as original, tarfile.open(FULL, 'w:gz', compresslevel=2) as out:
        for member in original:
            total += 1
            if member.name.rstrip('/') == 'rpg maker player':
                out.addfile(member)
                continue
            assert member.name.startswith(PREFIX)
            relative = member.name[len(PREFIX):].rstrip('/')
            assert safe(relative), relative
            seen.add(relative)
            assert relative not in BLOCKED, 'user-owned manifest included in old full: '+relative
            if relative in REPLACE and member.isfile():
                path=ROOT/relative
                member.size=path.stat().st_size
                member.mtime=int(path.stat().st_mtime)
                member.mode=0o755 if relative in EXE else (path.stat().st_mode & 0o777)
                with path.open('rb') as src:out.addfile(member,src)
                replaced+=1
            else:
                data=original.extractfile(member) if member.isfile() else None
                out.addfile(member,data)
            if total % 15000 == 0:print('FULL_MEMBERS',total,flush=True)
        for relative in sorted(REPLACE-seen):
            path=ROOT/relative
            meta=tarfile.TarInfo(PREFIX+relative)
            meta.mode=0o755 if relative in EXE else (path.stat().st_mode & 0o777)
            meta.size=path.stat().st_size
            with path.open('rb') as src:out.addfile(meta,src)
    assert replaced >= 7 and total > 50000, (replaced,total)
    sidecar(FULL)
    print('V16_FULL',FULL.stat().st_size,sha(FULL),'entries',total,flush=True)


def manifest(archive):
    result={}
    with tarfile.open(archive,'r:gz') as f:
        for member in f:
            if not member.isfile():continue
            assert member.name.startswith(PREFIX)
            relative=member.name[len(PREFIX):]
            assert safe(relative) and relative not in BLOCKED
            assert relative not in result, 'duplicate file '+relative
            result[relative]=(member.size,bytes_digest(f.extractfile(member)))
    return result


def build_ota_from_public():
    baseline=manifest(BASE_PUBLIC)
    current=manifest(FULL)
    assert not set(baseline)-set(current), 'unexpected deleted files in full distribution'
    changes={name for name,item in current.items() if item != baseline.get(name)}
    assert len(changes)>=18, 'expected all v1.4->1.6 feature changes'
    assert {'VERSION','MKXP_Launcher','runtime/rtp/README.txt',
            'runtime/_compat/verified_profiles.json',
            'runtime/_compat/merge_registry.py',
            'preload/common/rpgmp_resource_casefold.rb',
            'runtime/ruby18/mkxp-z','runtime/ruby19/mkxp-z',
            'runtime/ruby31/mkxp-z'}.issubset(changes)
    assert not any(x.startswith(('game/','cache/','config/')) for x in changes)
    print('OTA_FROM_V14_DIFF_FILES',len(changes),sorted(changes),flush=True)
    with tarfile.open(FULL,'r:gz') as src,tarfile.open(OTA,'w:gz',compresslevel=6) as delta:
        for member in src:
            if not member.isfile() or not member.name.startswith(PREFIX):continue
            relative=member.name[len(PREFIX):]
            if relative not in changes:continue
            member.name=relative
            with src.extractfile(member) as data:delta.addfile(member,data)
    sidecar(OTA)
    print('V16_OTA_FOR_PUBLIC_V14',OTA.stat().st_size,sha(OTA),flush=True)
    return changes, current


def verify(changes, final_manifest):
    with tarfile.open(OTA,'r:gz') as delta,tarfile.open(FULL,'r:gz') as full:
        assert set(delta.getnames())==changes
        full_names=set(full.getnames())
        assert all(PREFIX+name in full_names for name in changes)
        assert all(PREFIX+name not in full_names for name in BLOCKED)
        for member in delta:
            assert member.isfile() and safe(member.name)
            content=delta.extractfile(member).read()
            assert (len(content),hashlib.sha256(content).hexdigest())==final_manifest[member.name]
            if member.name in EXE:assert member.mode & 0o111
        assert json.loads(full.extractfile(full.getmember(PREFIX+'runtime/_compat/verified_profiles.json')).read())['games']['curse_of_pleasure_v1']['patches'].__len__() == 4
    for path in (FULL,OTA):
        assert path.with_name(path.name+'.sha256').read_text().split()[0]==sha(path)
    print('V16_PUBLIC_V14_UPGRADE_FULL_OTA_SHA_AND_FOUR_COMPAT_RULES_PASS',flush=True)


def main():
    verify_source()
    build_full()
    changes, final_manifest=build_ota_from_public()
    verify(changes, final_manifest)


if __name__=='__main__':main()
