#!/usr/bin/env python3
"""Integration smoke for SteamOS OTA progress and user-data safety.

Downloads a throttled synthetic release through real curl, checks the progress
file and the script's full install. No real SteamOS user files are touched.
"""
from __future__ import annotations

import functools
import hashlib
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import io
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile
import threading
import time
from urllib.parse import unquote

HELPER = Path("/mnt/d/GPT/MKXP/work/STEAMOS_20261122/runtime/update/update.sh")
ASSET = "RPG_Maker_Player_SteamOS_Update_V1.7.1.tar.gz"


def make_archive(target: Path, unsafe: bool = False) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(target, "w:gz", compresslevel=1) as archive:
        items = {
            "VERSION": b"1.7.1\n",
            "MKXP_Launcher": b"ELF_PROBE",
            "run.sh": b"#!/bin/sh\nexit 0\n",
            "runtime/test/large-incompressible-data.bin": os.urandom(5 * 1024 * 1024),
        }
        for i in range(200):
            items[f"runtime/test/part-{i:03d}.txt"] = b"TEST_" + str(i).encode()
        if unsafe:
            items["../OTA_SHOULD_NEVER_ESCAPE"] = b"DANGER"
        for name, value in items.items():
            meta = tarfile.TarInfo(name)
            meta.size = len(value)
            meta.mode = 0o755 if name in ("MKXP_Launcher", "run.sh") else 0o644
            archive.addfile(meta, io.BytesIO(value))


def parse_status(path: Path) -> dict[str, str]:
    if not path.exists():
        return {}
    return dict(line.split("=", 1) for line in path.read_text().splitlines() if "=" in line)


class SlowHandler(SimpleHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def copyfile(self, source, outputfile):
        while piece := source.read(32768):
            outputfile.write(piece)
            outputfile.flush()
            if source.name.endswith(ASSET):
                time.sleep(0.003)


def case(testroot: Path, *, bad_sha: bool = False, malicious: bool = False):
    name = "bad_checksum" if bad_sha else ("malicious_tar" if malicious else "normal")
    directory = testroot / name
    server_dir = directory / "server"
    asset = server_dir / "v1.7.1" / ASSET
    make_archive(asset, unsafe=malicious)
    checksum = hashlib.sha256(asset.read_bytes()).hexdigest()
    if bad_sha:
        checksum = "0" * 64
    (asset.parent / (ASSET + ".sha256")).write_text(
        checksum + "  " + ASSET + "\n", encoding="ascii"
    )

    installed = directory / "installed RPG Maker Player"
    config = installed / "config" / "launcher.json"
    saves = installed / "game" / "user" / "Save1.rxdata"
    compat = installed / "runtime" / "_compat" / "patches.json"
    for file, contents in (
        (config, b"PERSONAL_UI_SETTINGS"),
        (saves, b"UNMODIFIED_GAME_SAVE"),
        (compat, b"PERSONAL_PATCH_RULES"),
    ):
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_bytes(contents)

    handler = functools.partial(SlowHandler, directory=str(server_dir))
    httpd = ThreadingHTTPServer(("127.0.0.1", 0), handler)
    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    status = directory / "update.status"
    progress = Path(str(status) + ".progress")
    env = os.environ.copy()
    env["RPGMP_UPDATE_RELEASE_ROOT"] = f"http://127.0.0.1:{httpd.server_port}"
    snapshots = []
    try:
        process = subprocess.Popen(
            ["bash", str(HELPER), "install", str(installed), "1.7", "1.7.1", str(status)],
            env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True,
        )
        started = time.monotonic()
        while process.poll() is None:
            if time.monotonic() - started > 90:
                process.kill()
                raise AssertionError("update smoke timed out")
            data = parse_status(progress)
            if data and (not snapshots or snapshots[-1] != data):
                snapshots.append(data)
            time.sleep(0.02)
        output = process.communicate(timeout=5)[0]
        state = parse_status(status)
        last = parse_status(progress)
        if last and (not snapshots or snapshots[-1] != last):
            snapshots.append(last)
        assert snapshots, f"no progress snapshots: {name}"
        pcts = [int(a["PERCENT"]) for a in snapshots]
        stages = [a["STAGE"] for a in snapshots]
        assert all(0 <= p <= 100 for p in pcts), pcts
        assert pcts == sorted(pcts), (name, list(zip(stages, pcts)))
        assert "download" in stages, stages
        assert any(a["STAGE"] == "download" and int(a["TOTAL"]) > 0
                   for a in snapshots), "missing actual content length"
        assert any(a["STAGE"] == "download" and 3 < int(a["PERCENT"]) < 72
                   for a in snapshots), "percent did not advance while downloading"
        if bad_sha or malicious:
            assert process.returncode != 0, (name, output)
            assert state.get("STATUS") == "error", state
            assert last["STAGE"] == "failed", last
            assert int(last["PERCENT"]) < 100
        else:
            assert process.returncode == 0, (name, output, state)
            assert state == {"STATUS": "installed", "LATEST": "1.7.1"}, state
            assert "extract" in stages or "install" in stages, stages
            assert last["STAGE"] == "done" and int(last["PERCENT"]) == 100, last
            assert (installed / "VERSION").read_text() == "1.7.1\n"
            assert (installed / "runtime/test/large-incompressible-data.bin").stat().st_size == 5 * 1024 * 1024
            assert os.access(installed / "run.sh", os.X_OK)
            print("PROGRESS_STAGES", " -> ".join(dict.fromkeys(stages)), flush=True)
            print("PROGRESS_DOWNLOAD_PERCENT_SAMPLES",
                  len([x for x in snapshots if x["STAGE"] == "download"]), flush=True)
        for file, contents in (
            (config, b"PERSONAL_UI_SETTINGS"), (saves, b"UNMODIFIED_GAME_SAVE"),
            (compat, b"PERSONAL_PATCH_RULES"),
        ):
            assert file.read_bytes() == contents, file
        assert not (directory / "OTA_SHOULD_NEVER_ESCAPE").exists()
        print("OTA_PROGRESS_" + name.upper() + "_PASS", flush=True)
    finally:
        httpd.shutdown()
        thread.join(timeout=10)
        httpd.server_close()


def main():
    with tempfile.TemporaryDirectory(prefix="rpgmp-v171-update-") as directory:
        root = Path(directory)
        case(root)
        case(root, bad_sha=True)
        case(root, malicious=True)
    print("STEAMOS_OTA_PROGRESS_INSTALL_SECURITY_SMOKE_PASS", flush=True)


if __name__ == "__main__":
    main()
