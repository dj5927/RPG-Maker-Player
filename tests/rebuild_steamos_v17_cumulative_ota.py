"""Generate and verify a cumulative SteamOS v1.3 -> v1.7 OTA.

Steam's Settings > Update always fetches the latest release, so the OTA must
work from every supported update-capable base, not only v1.6.
The already-verified v1.7 FULL installer remains untouched.
"""
from __future__ import annotations
import copy
import hashlib
import os
from pathlib import Path, PurePosixPath
import shutil
import tarfile

ROOT = Path(r"D:\GPT\MKXP")
DIST = ROOT / "DIST"
WORK = ROOT / "work/v17_cumulative_build"
PREFIX = "rpg maker player/"
BASE = DIST / "RPG_Maker_Player_SteamOS_V1.3.tar.gz"
FULL = DIST / "RPG_Maker_Player_SteamOS_V1.7.tar.gz"
OTA_NAME = "RPG_Maker_Player_SteamOS_Update_V1.7.tar.gz"
OTA = DIST / OTA_NAME
NEW = WORK / OTA_NAME
OLD_BACKUP = DIST / "RPG_Maker_Player_SteamOS_Update_V1.7_v16ONLY_ARCHIVED_20261122.tar.gz"
OLD_SHA = "ffc471beeb5f57ed7352e37608ac24371cd2c6cc36af5387ed3b73c33f9a1bc4"
BASE_SHA = "582295402c623c0e5d55b009b63154ceaad6e58275e90f42cbf56e2d95083c22"
FULL_SHA = "15b35f035f45a2b109461d3fcb3c8a126c0a449b5dbdac4e543df55faf796b07"
PROTECTED = (
    "game/", "config/", "cache/", "logs/",
    "runtime/_compat/patches.json",
    "runtime/_compat/patches_before",
)
REQUIRED = {
    "MKXP_Launcher",
    "VERSION",
    "run.sh",
    "runtime/_compat/merge_registry.py",
    "runtime/_compat/verified_profiles.json",
    "runtime/ruby18/mkxp-z",
    "runtime/ruby19/mkxp-z",
    "runtime/ruby31/mkxp-z",
    "runtime/easyrpg/easyrpg-player-v17",
    "runtime/nwjs/v0.120.0/nw",
    "runtime/mvmz/launch_mvmz.sh",
    "preload/common/rpgmp_resource_casefold.rb",
}


def sha(path):
    with path.open("rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


def sha_stream(stream):
    h = hashlib.sha256()
    for chunk in iter(lambda: stream.read(1048576), b""):
        h.update(chunk)
    return h.hexdigest()


def allowed_path(relative):
    if not relative or relative.startswith("/") or "\\" in relative:
        return False
    if any(p in ("", ".", "..") for p in relative.split("/")):
        return False
    if relative.startswith(PROTECTED):
        return False
    return True


def scan(path):
    manifest = {}
    with tarfile.open(path, "r:gz") as archive:
        for n, member in enumerate(archive):
            if member.name.rstrip("/") == "rpg maker player":
                continue
            assert member.name.startswith(PREFIX), (path.name, member.name)
            relative = member.name[len(PREFIX):].rstrip("/")
            assert relative and relative not in manifest, (path.name, relative)
            if member.isdir():
                continue
            assert member.isfile() or member.issym(), (
                "unsupported tar file type", path.name, relative, member.type)
            if member.isfile():
                with archive.extractfile(member) as data:
                    identity = ("f", sha_stream(data), member.mode & 0o111)
            else:
                assert not os.path.isabs(member.linkname)
                assert ".." not in PurePosixPath(member.linkname).parts
                identity = ("l", member.linkname)
            manifest[relative] = identity
            if n and n % 14000 == 0:
                print("MANIFEST_PROGRESS", path.name, n, flush=True)
    return manifest


def safe_compare():
    assert sha(BASE) == BASE_SHA, "v1.3 full baseline changed"
    assert sha(FULL) == FULL_SHA, "v1.7 full baseline changed"
    original = scan(BASE)
    updated = scan(FULL)
    removed = set(original) - set(updated)
    if removed:
        print("FULL_REMOVED_LEGACY_PATHS", len(removed),
              sorted(removed)[:20], flush=True)
    changed = {
        rel for rel, identity in updated.items()
        if identity != original.get(rel)
    }
    assert len(changed) > 500, "cumulative delta implausibly small"
    assert REQUIRED.issubset(changed), sorted(REQUIRED - changed)
    assert all(allowed_path(rel) for rel in changed), [
        rel for rel in changed if not allowed_path(rel)
    ][:20]
    print("V13_MANIFEST", len(original), "V17_MANIFEST", len(updated),
          "CUMULATIVE_CHANGED", len(changed), flush=True)
    return updated, changed


def build(updated, changed):
    WORK.mkdir(parents=True, exist_ok=True)
    assert not NEW.exists(), "refusing to overwrite existing candidate OTA"
    written = set()
    with tarfile.open(FULL, "r:gz") as current, tarfile.open(
            NEW, "x:gz", compresslevel=5, format=tarfile.PAX_FORMAT) as dest:
        for member in current:
            if not member.name.startswith(PREFIX):
                continue
            relative = member.name[len(PREFIX):]
            if relative not in changed:
                continue
            assert allowed_path(relative)
            assert member.isfile() or member.issym()
            info = copy.copy(member)
            info.name = relative
            if member.isfile():
                with current.extractfile(member) as stream:
                    dest.addfile(info, stream)
            else:
                dest.addfile(info)
            written.add(relative)
            if len(written) % 250 == 0:
                print("CUMULATIVE_OTA_PROGRESS", len(written),
                      "/", len(changed), flush=True)
    assert written == changed, sorted(changed - written)[:20]
    print("CUMULATIVE_PACKAGE_GENERATED", NEW.stat().st_size,
          sha(NEW), flush=True)

    with tarfile.open(NEW, "r:gz") as stream:
        seen = set()
        for m in stream:
            assert allowed_path(m.name) and m.name not in seen
            seen.add(m.name)
            original = updated[m.name]
            if m.isfile():
                with stream.extractfile(m) as src:
                    assert original == ("f", sha_stream(src), m.mode & 0o111)
            elif m.issym():
                assert original == ("l", m.linkname)
            else:
                raise AssertionError("unsafe tar entry " + m.name)
        assert seen == changed
    print("CUMULATIVE_OTA_FILE_LEVEL_VERIFIED", len(seen), flush=True)


def publish_local():
    assert sha(OTA) == OLD_SHA, "existing v1.6-only OTA was unexpectedly modified"
    assert not OLD_BACKUP.exists(), "backup already exists; reconcile before retry"
    shutil.copy2(OTA, OLD_BACKUP)
    assert sha(OLD_BACKUP) == OLD_SHA
    shutil.copy2(OTA.with_name(OTA.name + ".sha256"),
                 OLD_BACKUP.with_name(OLD_BACKUP.name + ".sha256"))
    try:
        shutil.copy2(NEW, OTA)
        sha_value = sha(OTA)
        assert sha_value == sha(NEW)
        sidecar = OTA.with_name(OTA.name + ".sha256")
        sidecar.write_text(sha_value + "  " + OTA.name + "\n",
                           encoding="ascii", newline="\n")
        assert sidecar.read_text().split()[0] == sha(OTA)
    except Exception:
        shutil.copy2(OLD_BACKUP, OTA)
        shutil.copy2(OLD_BACKUP.with_name(OLD_BACKUP.name + ".sha256"),
                     OTA.with_name(OTA.name + ".sha256"))
        raise
    print("CUMULATIVE_OTA_READY_TO_UPLOAD", OTA.name, OTA.stat().st_size,
          sha(OTA), flush=True)
    print("OLD_V16_ONLY_OTA_BACKUP", OLD_BACKUP, flush=True)


if __name__ == "__main__":
    manifest, selected = safe_compare()
    build(manifest, selected)
    publish_local()
    print("STEAMOS_V17_CUMULATIVE_FROM_V13_PASS", flush=True)
