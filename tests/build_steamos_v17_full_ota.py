"""Build SteamOS 1.7 full package and initial 1.6 -> 1.7 delta.

For the *public* v1.7 OTA, run rebuild_steamos_v17_cumulative_ota.py
afterward to include every change since v1.3 (earliest updater version).

Base release bytes are immutable. New and replaced files are collected only
from the isolated v1.7 stage, never from the user's installed game library.
"""
from __future__ import annotations
import copy
import hashlib
import os
from pathlib import Path, PurePosixPath
import tarfile

ROOT = Path(r"D:\GPT\MKXP")
STAGE = ROOT / "work/v17_runtime_stage"
DIST = ROOT / "DIST"
BASE = DIST / "RPG_Maker_Player_SteamOS_V1.6.tar.gz"
FULL = DIST / "RPG_Maker_Player_SteamOS_V1.7.tar.gz"
OTA = DIST / "RPG_Maker_Player_SteamOS_Update_V1.7.tar.gz"
PREFIX = "rpg maker player/"
BASE_SHA = "1a82ccaa58a396677c540558a722bd0cb4ab623060fe6b61ce3bc27322862cba"
BANNED = ("config/", "game/", "cache/", "logs/", "runtime/_compat/patches.json",
          "runtime/_compat/patches_before")
ESSENTIAL = {
    "MKXP_Launcher", "run.sh", "VERSION",
    "runtime/ruby18/mkxp-z", "runtime/ruby19/mkxp-z", "runtime/ruby31/mkxp-z",
    "runtime/easyrpg/easyrpg-player-v17",
    "runtime/nwjs/v0.120.0/nw", "runtime/mvmz/launch_mvmz.sh",
}
EXEC = {
    "MKXP_Launcher", "run.sh",
    "runtime/ruby18/mkxp-z", "runtime/ruby19/mkxp-z", "runtime/ruby31/mkxp-z",
    "runtime/easyrpg/easyrpg-player-v17",
    "runtime/nwjs/install_nwjs.sh", "runtime/mvmz/launch_mvmz.sh",
}
VERSION = "1.7"


def sha(path: Path):
    with path.open("rb") as file:
        return hashlib.file_digest(file, "sha256").hexdigest()


def stream_sha(reader):
    h = hashlib.sha256()
    for data in iter(lambda: reader.read(1024 * 1024), b""):
        h.update(data)
    return h.hexdigest()


def safe(name: str) -> bool:
    return (
        bool(name) and not name.startswith("/")
        and name == PurePosixPath(name).as_posix()
        and all(part not in ("", ".", "..") for part in name.split("/"))
    )


def stage_manifest():
    entries = {}
    for file in STAGE.rglob("*"):
        if file.is_dir() and not file.is_symlink():
            continue
        rel = file.relative_to(STAGE).as_posix()
        assert safe(rel), rel
        assert not rel.startswith(BANNED), "user-owned path in stage: " + rel
        assert file.is_file() or file.is_symlink(), rel
        if file.is_symlink():
            destination = os.readlink(file)
            assert destination and not os.path.isabs(destination), (rel, destination)
            assert not PurePosixPath(destination).is_absolute()
            assert ".." not in PurePosixPath(destination).parts, (rel, destination)
        entries[rel] = file
    assert ESSENTIAL.issubset(entries), ESSENTIAL - set(entries)
    assert (STAGE / "VERSION").read_text().strip() == VERSION
    elf = (STAGE / "MKXP_Launcher").read_bytes()
    assert elf.startswith(b"\x7fELF") and b"1.7\x00" in elf
    assert b"V170-STEAMOS-UPSTREAM" in elf
    for runtime in ("ruby18", "ruby19", "ruby31"):
        path = entries[f"runtime/{runtime}/mkxp-z"]
        data = path.read_bytes()
        assert data.startswith(b"\x7fELF") and b"rpgmp_resource_casefold.rb" in data
    print("STAGE_FILES", len(entries), "TOTAL_BYTES",
          sum(p.lstat().st_size for p in entries.values()), flush=True)
    return entries


def metadata(rel: str, path: Path):
    info = tarfile.TarInfo(rel)
    info.mtime = int(path.lstat().st_mtime)
    name = PurePosixPath(rel).name
    info.mode = 0o755 if (rel in EXEC or
                           (rel.startswith("runtime/nwjs/v0.120.0/") and
                            name in ("nw", "chrome_crashpad_handler", "nacl_helper",
                                     "nacl_helper_bootstrap"))) else 0o644
    if path.is_symlink():
        info.type = tarfile.SYMTYPE
        info.linkname = os.readlink(path)
    else:
        info.type = tarfile.REGTYPE
        info.size = path.stat().st_size
    return info


def sidecar(archive: Path):
    name = archive.name
    checksum = sha(archive)
    archive.with_name(name + ".sha256").write_text(
        checksum + "  " + name + "\n", encoding="ascii", newline="\n")
    print("SHA256", name, archive.stat().st_size, checksum, flush=True)


def collect_changes(entries):
    found = {}
    assert sha(BASE) == BASE_SHA, "original public v1.6 baseline changed"
    with tarfile.open(BASE, "r:gz") as src:
        for member in src:
            if member.name.rstrip("/") == "rpg maker player":
                continue
            assert member.name.startswith(PREFIX), member.name
            rel = member.name[len(PREFIX):].rstrip("/")
            assert safe(rel), rel
            assert rel not in found, "duplicate base path " + rel
            found[rel] = (member.type, member.size)
            if rel in entries:
                path = entries[rel]
                if path.is_file() and member.isfile():
                    with src.extractfile(member) as data:
                        found[rel] = (member.type, member.size, stream_sha(data))
                elif path.is_symlink() and member.issym():
                    found[rel] = (member.type, member.size, member.linkname)
    changed = set()
    for rel, path in entries.items():
        original = found.get(rel)
        if original is None:
            changed.add(rel)
        elif path.is_file() and original[:2] != (tarfile.REGTYPE, path.stat().st_size):
            changed.add(rel)
        elif path.is_file() and original[2] != sha(path):
            changed.add(rel)
        elif path.is_symlink() and (original[0] != tarfile.SYMTYPE or
                                   original[2] != os.readlink(path)):
            changed.add(rel)
    assert ESSENTIAL.issubset(changed), "expected version binaries were not changed"
    assert changed and not any(item.startswith(BANNED) for item in changed)
    print("OTA_CHANGED_FILES", len(changed), flush=True)
    return changed


def build_full(entries):
    old_count = new_count = replace_count = 0
    seen = set()
    with tarfile.open(BASE, "r:gz") as old, tarfile.open(
            FULL, "x:gz", compresslevel=2, format=tarfile.PAX_FORMAT) as target:
        for entry in old:
            old_count += 1
            if entry.name.rstrip("/") == "rpg maker player":
                target.addfile(entry)
                continue
            rel = entry.name[len(PREFIX):].rstrip("/")
            seen.add(rel)
            if rel in entries:
                path = entries[rel]
                info = metadata(rel, path)
                info.name = PREFIX + rel
                if path.is_symlink():
                    target.addfile(info)
                else:
                    with path.open("rb") as data:
                        target.addfile(info, data)
                replace_count += 1
            else:
                data = old.extractfile(entry) if entry.isfile() else None
                target.addfile(entry, data)
            if old_count % 14000 == 0:
                print("FULL_BASE_PROGRESS", old_count, flush=True)
        for rel in sorted(entries.keys() - seen):
            path = entries[rel]
            info = metadata(rel, path)
            info.name = PREFIX + rel
            if path.is_symlink():
                target.addfile(info)
            else:
                with path.open("rb") as data:
                    target.addfile(info, data)
            new_count += 1
    print("FULL_GENERATED", old_count, "replaced", replace_count,
          "added", new_count, flush=True)
    sidecar(FULL)


def build_ota(entries, changed):
    with tarfile.open(OTA, "x:gz", compresslevel=5, format=tarfile.PAX_FORMAT) as result:
        for i, rel in enumerate(sorted(changed)):
            path = entries[rel]
            info = metadata(rel, path)
            if path.is_symlink():
                result.addfile(info)
            else:
                with path.open("rb") as data:
                    result.addfile(info, data)
            if (i+1) % 100 == 0:
                print("OTA_PROGRESS", i+1, "/", len(changed), flush=True)
    sidecar(OTA)


def verify(entries, changed):
    with tarfile.open(OTA, "r:gz") as ota:
        names = set()
        for member in ota:
            assert safe(member.name)
            assert not member.name.startswith(BANNED)
            names.add(member.name)
            file = entries[member.name]
            if file.is_symlink():
                assert member.issym() and member.linkname == os.readlink(file)
            else:
                assert member.isfile() and member.size == file.stat().st_size
                if member.name in ESSENTIAL or member.name.endswith("VERSION"):
                    with ota.extractfile(member) as data:
                        assert stream_sha(data) == sha(file), member.name
            if member.name in EXEC or member.name == "runtime/nwjs/v0.120.0/nw":
                assert member.mode & 0o111, member.name
        assert names == changed
    with tarfile.open(FULL, "r:gz") as full:
        seen = set()
        selected = {}
        for member in full:
            if member.name.startswith(PREFIX):
                rel = member.name[len(PREFIX):].rstrip("/")
                assert rel not in seen, "duplicated file in full: " + rel
                seen.add(rel)
                if rel in ESSENTIAL:
                    selected[rel] = member.size
        assert ESSENTIAL.issubset(selected), ESSENTIAL - set(selected)
        assert len(seen) > 51000
    print("STEAMOS_V17_FULL_AND_OTA_VERIFIED", len(seen),
          "full entries and", len(changed), "OTA entries", flush=True)


def main():
    assert not FULL.exists() and not OTA.exists(), "refuse overwrite existing archives"
    entries = stage_manifest()
    changed = collect_changes(entries)
    build_full(entries)
    build_ota(entries, changed)
    verify(entries, changed)
    print("STEAMOS_V17_PACKAGE_PASS", flush=True)


if __name__ == "__main__":
    main()

