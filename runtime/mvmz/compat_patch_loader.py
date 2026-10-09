#!/usr/bin/env python3
"""Per-game MV/MZ text patches via an isolated symlink mirror.

The source game directory is NEVER edited. A missing or invalid manifest
leaves the game's original launch path entirely unchanged.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import sys
import time

MAX_MANIFEST = 256 * 1024
MAX_REGISTRY = 4 * 1024 * 1024
MAX_TEXT = 12 * 1024 * 1024
ID = re.compile(r"^[A-Za-z0-9._-]{1,70}$")
SHA = re.compile(r"^[0-9a-fA-F]{64}$")


def valid_path(value):
    if not isinstance(value, str) or len(value) > 250 or "\\" in value:
        return False
    p = PurePosixPath(value)
    if p.is_absolute() or any(s in ("..", ".", "") for s in value.split("/")):
        return False
    return (value == "index.html" or
            value.startswith("js/") and value.endswith(".js"))


def pairs_unique(pairs):
    obj = {}
    for k, v in pairs:
        if k in obj:
            raise ValueError("duplicate JSON key: " + k)
        obj[k] = v
    return obj


def select_central(root, engine, registry):
    if not registry or not registry.is_file():
        return None
    if registry.stat().st_size > MAX_REGISTRY:
        raise ValueError("central registry too large")
    source = json.loads(registry.read_text(encoding="utf-8-sig"),
                        object_pairs_hook=pairs_unique)
    if not isinstance(source, dict) or source.get("schema") != 2:
        raise ValueError("central schema=2 required")
    if source.get("enabled") is not True:
        return None
    games = source.get("games")
    if not isinstance(games, dict) or len(games) > 1000:
        raise ValueError("central games object missing or too large")
    cache = {}
    matches = []
    for name, profile in games.items():
        if not isinstance(profile, dict) or profile.get("engine") != engine:
            continue
        variants = profile.get("fingerprints")
        if not isinstance(variants, list) or len(variants) > 32:
            continue
        for fingerprint in variants:
            if not isinstance(fingerprint, dict) or not (2 <= len(fingerprint) <= 8):
                continue
            if "data/System.json" not in fingerprint or "js/plugins.js" not in fingerprint:
                continue
            found = True
            for rel, expected in fingerprint.items():
                path = root.joinpath(*rel.split("/"))
                if (not valid_path(rel) and not (rel.startswith("data/") and
                    rel.endswith(".json") and ".." not in rel)) or not isinstance(expected, str) or not SHA.fullmatch(expected):
                    found = False
                    break
                if not path.is_file() or not path.resolve().is_relative_to(root.resolve()):
                    found = False
                    break
                if rel not in cache:
                    digest = hashlib.sha256()
                    with path.open("rb") as infile:
                        for block in iter(lambda: infile.read(65536), b""):
                            digest.update(block)
                    cache[rel] = digest.hexdigest()
                if cache[rel] != expected.lower():
                    found = False
                    break
            if found:
                matches.append((name, profile))
                break
    if len(matches) > 1:
        raise ValueError("ambiguous central fingerprint; no patches applied")
    if not matches:
        print("[RPGMP-PATCH] central no matching game content", flush=True)
        return None
    print("[RPGMP-PATCH] central content match id=" + matches[0][0], flush=True)
    return matches[0][1]


def patch_sources(root, engine, manifest, central=None):
    data = select_central(root, engine, central)
    if data is None:
        if not manifest or not manifest.is_file():
            return {}
        if manifest.stat().st_size > MAX_MANIFEST:
            raise ValueError("manifest too large")
        data = json.loads(manifest.read_text(encoding="utf-8-sig"),
                          object_pairs_hook=pairs_unique)
        if not isinstance(data, dict) or data.get("schema") != 1:
            raise ValueError("requires schema=1")
    if data.get("enabled") is not True:
        print("[RPGMP-PATCH] manifest disabled", flush=True)
        return {}
    entries = data.get("patches")
    if not isinstance(entries, list) or len(entries) > 100:
        raise ValueError("patches must be an array (max 100)")
    original_by_path = {}
    patched_by_path = {}
    seen_ids = set()
    for row in entries:
        if not isinstance(row, dict):
            raise ValueError("patch entry must be object")
        if "enabled" in row and type(row["enabled"]) is not bool:
            raise ValueError("patch enabled must be boolean")
        patch_id, rel, selected = row.get("id"), row.get("file"), row.get("engine")
        if not isinstance(patch_id, str) or not ID.fullmatch(patch_id):
            raise ValueError("invalid patch id")
        if patch_id in seen_ids:
            raise ValueError("duplicate patch id: " + patch_id)
        seen_ids.add(patch_id)
        if row.get("enabled", True) is False:
            print("[RPGMP-PATCH] disabled id=" + patch_id, flush=True)
            continue
        if selected not in ("MV", "MZ") or not valid_path(rel):
            raise ValueError("unsafe/unsupported engine or file for " + patch_id)
        if selected != engine:
            print("[RPGMP-PATCH] engine-skip id=" + patch_id, flush=True)
            continue
        find, replacement = row.get("find"), row.get("replace")
        count = row.get("expected_matches", 1)
        expected_sha = row.get("sha256_before")
        if (not isinstance(find, str) or not find or len(find) > 8192 or
                not isinstance(replacement, str) or len(replacement) > 32768 or
                type(count) is not int or count != 1):
            raise ValueError("unsafe replacement or match count for " + patch_id)
        if expected_sha is not None and (not isinstance(expected_sha, str)
                                         or not SHA.fullmatch(expected_sha)):
            raise ValueError("invalid sha256_before for " + patch_id)
        path = root.joinpath(*rel.split("/"))
        if not path.is_file() or not path.resolve().is_relative_to(root.resolve()):
            print("[RPGMP-PATCH] missing/unsafe file id=" + patch_id, flush=True)
            continue
        if rel not in original_by_path:
            if path.stat().st_size > MAX_TEXT:
                print("[RPGMP-PATCH] file too large id=" + patch_id, flush=True)
                continue
            original_by_path[rel] = path.read_text(encoding="utf-8")
            patched_by_path[rel] = original_by_path[rel]
        original, current = original_by_path[rel], patched_by_path[rel]
        if expected_sha is not None and hashlib.sha256(
                original.encode("utf-8")).hexdigest().lower() != expected_sha.lower():
            print("[RPGMP-PATCH] hash-mismatch id=" + patch_id, flush=True)
            continue
        matches = current.count(find)
        if matches != 1:
            print("[RPGMP-PATCH] match-skip id={} count={}".format(
                patch_id, matches), flush=True)
            continue
        patched_by_path[rel] = current.replace(find, replacement, 1)
        print("[RPGMP-PATCH] applied id={} file={}".format(patch_id, rel),
              flush=True)
    return {rel: text for rel, text in patched_by_path.items()
            if text != original_by_path[rel]}


def create_view(root, changed, cache):
    cache.mkdir(parents=True, exist_ok=True)
    view = cache / ("p{}-{}".format(os.getpid(), time.time_ns()))
    view.mkdir()
    # Games using standard local save/ storage must not lose newly created
    # saves when each launch receives a new temporary patched webroot.
    if not (root / "save").exists():
        fallback_save = cache.parent / "patch-saves"
        fallback_save.mkdir(parents=True, exist_ok=True)
        os.symlink(fallback_save, view / "save", target_is_directory=True)
    changed_paths = {PurePosixPath(rel) for rel in changed}
    ancestor_paths = {PurePosixPath(".")}
    for path in changed_paths:
        for parent in path.parents:
            ancestor_paths.add(parent)

    def walk(source, destination, rel):
        for item in source.iterdir():
            if item.name == "save" and rel == PurePosixPath(".") and (view / "save").exists():
                continue
            relative = rel / item.name
            output = destination / item.name
            if relative in changed_paths:
                output.write_text(changed[relative.as_posix()], encoding="utf-8")
            elif relative in ancestor_paths and item.is_dir():
                output.mkdir()
                walk(item, output, relative)
            else:
                # Prefer symlinks over hardlinks: a patched mirror must never
                # allow accidental writes to mutate the original via inode.
                os.symlink(item.resolve(), output, target_is_directory=item.is_dir())
    walk(root, view, PurePosixPath("."))
    return view


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--webroot", required=True)
    parser.add_argument("--manifest", default="")
    parser.add_argument("--central", default="")
    parser.add_argument("--engine", choices=["MV", "MZ"], required=True)
    parser.add_argument("--cache", required=True)
    opts = parser.parse_args()
    root = Path(opts.webroot).resolve()
    try:
        changed = patch_sources(root, opts.engine,
                                Path(opts.manifest) if opts.manifest else None,
                                Path(opts.central) if opts.central else None)
        if changed:
            view = create_view(root, changed, Path(opts.cache))
            print("RPGMP_PATCH_VIEW=" + str(view), flush=True)
            print("[RPGMP-PATCH] ready files=" + str(len(changed)), flush=True)
        else:
            print("[RPGMP-PATCH] no applicable changes", flush=True)
    except (OSError, ValueError, UnicodeError, json.JSONDecodeError) as error:
        print("[RPGMP-PATCH] rejected: " + str(error), flush=True)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
