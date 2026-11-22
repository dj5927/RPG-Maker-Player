"""Validate cumulative v1.3+ -> v1.7.1 OTA on isolated Linux fixtures."""
from pathlib import Path
import hashlib
import os
import subprocess
import sys
import tempfile

PROJECT = Path("/mnt/d/GPT/MKXP")
DIST = PROJECT / "DIST"
SCRIPT = PROJECT / "work/STEAMOS_20261122/runtime/update/update.sh"
STAGE = PROJECT / "work/v171_runtime_stage"
NAME = "RPG_Maker_Player_SteamOS_Update_V1.7.1.tar.gz"


def sha(file):
    with file.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    current = sys.argv[1] if len(sys.argv) > 1 else "1.3"
    assert current in ("1.3", "1.4", "1.5", "1.5.1", "1.5.2", "1.6", "1.7")
    asset = DIST / NAME
    sidecar = DIST / (NAME + ".sha256")
    assert asset.is_file() and sidecar.is_file()
    assert sidecar.read_text().split()[0] == sha(asset)
    with tempfile.TemporaryDirectory(prefix="rpgmp-v171-ota-") as directory:
        base = Path(directory)
        root = base / "installed player"
        release = base / "releases/v1.7.1"
        release.mkdir(parents=True)
        # Temp fixtures live on WSL ext4 while release assets live on /mnt/d.
        # A cross-device hardlink fails; a read-only symlink is sufficient.
        os.symlink(asset, release / NAME)
        os.symlink(sidecar, release / (NAME + ".sha256"))
        protected = {
            "config/launcher.json": b'{"userRoot":"/games"}\n',
            "game/gamelist.json": b'{"myGames":["user"]}\n',
            "game/MyGame/Save1.rxdata": b"SAVED_PROGRESS_123",
            "logs/previous.log": b"BEFORE_UPDATE",
            "runtime/_compat/patches.json": b'{"enabled":false,"userRules":["custom"]}\n',
            "runtime/rtp/fonts/user-font.ttf": b"USER_FONT",
        }
        for name, data in protected.items():
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        (root / "VERSION").write_text(current + "\n")
        out = base / "ota.status"
        env = os.environ.copy()
        env["RPGMP_UPDATE_RELEASE_ROOT"] = release.parent.as_uri()
        command = ["bash", str(SCRIPT), "install", str(root), current, "1.7.1", str(out)]
        proc = subprocess.run(command, env=env, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True, timeout=300)
        assert proc.returncode == 0, "OTA installer failed\n" + proc.stdout[-3000:]
        status = out.read_text()
        assert "STATUS=installed" in status and "LATEST=1.7.1" in status, status
        for name, data in protected.items():
            assert (root / name).read_bytes() == data, f"user file changed: {name}"
        assert (root / "VERSION").read_text().strip() == "1.7.1"
        # Updated files must be byte-identical to the new release staging.
        for name in ("MKXP_Launcher", "VERSION", "runtime/update/update.sh"):
            installed = root / name
            assert installed.is_file() and sha(installed) == sha(STAGE / name), name
        # v1.7 native engines remain byte-identical across the UI hotfix.
        previous = PROJECT / "work/v17_runtime_stage"
        for name in ("run.sh", "runtime/ruby18/mkxp-z", "runtime/ruby19/mkxp-z",
                     "runtime/ruby31/mkxp-z", "runtime/easyrpg/easyrpg-player-v17",
                     "runtime/nwjs/v0.120.0/nw"):
            installed = root / name
            assert installed.is_file() and sha(installed) == sha(previous / name), name
        progress = base / "ota.status.progress"
        assert progress.exists() and "PERCENT=100" in progress.read_text()
        print("OTA_INSTALL_V171_VALIDATED_USER_SETTINGS_SAVES_COMPAT_FONTS", flush=True)
        print("OTA_INSTALLED_MKXP_RUBY18_RUBY19_RUBY31_EASYRPG_NWJS_0120", flush=True)
        print("OTA_TEST_PASS_FROM", current, flush=True)


if __name__ == "__main__":
    main()
