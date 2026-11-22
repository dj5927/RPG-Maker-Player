"""Validate v1.6 -> v1.7 OTA on an isolated Linux filesystem fixture."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

PROJECT = Path("/mnt/d/GPT/MKXP")
DIST = PROJECT / "DIST"
SCRIPT = PROJECT / "work/STEAMOS_20261122/runtime/update/update.sh"
STAGE = PROJECT / "work/v17_runtime_stage"
NAME = "RPG_Maker_Player_SteamOS_Update_V1.7.tar.gz"


def sha(file):
    with file.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    asset = DIST / NAME
    sidecar = DIST / (NAME + ".sha256")
    assert asset.is_file() and sidecar.is_file()
    assert sidecar.read_text().split()[0] == sha(asset)
    with tempfile.TemporaryDirectory(prefix="rpgmp-v17-ota-") as directory:
        base = Path(directory)
        root = base / "installed player"
        release = base / "releases/v1.7"
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
        (root / "VERSION").write_text("1.6\n")
        out = base / "ota.status"
        env = os.environ.copy()
        env["RPGMP_UPDATE_RELEASE_ROOT"] = release.parent.as_uri()
        command = ["bash", str(SCRIPT), "install", str(root), "1.6", "1.7", str(out)]
        proc = subprocess.run(command, env=env, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True, timeout=300)
        assert proc.returncode == 0, "OTA installer failed\n" + proc.stdout[-3000:]
        status = out.read_text()
        assert "STATUS=installed" in status and "LATEST=1.7" in status, status
        for name, data in protected.items():
            assert (root / name).read_bytes() == data, f"user file changed: {name}"
        assert (root / "VERSION").read_text().strip() == "1.7"
        for name in (
            "MKXP_Launcher",
            "run.sh",
            "runtime/ruby18/mkxp-z", "runtime/ruby19/mkxp-z", "runtime/ruby31/mkxp-z",
            "runtime/easyrpg/easyrpg-player-v17",
            "runtime/nwjs/v0.120.0/nw",
            "runtime/mvmz/launch_mvmz.sh"
        ):
            staged = STAGE / name
            installed = root / name
            assert installed.is_file(), f"missing updated {name}"
            assert sha(installed) == sha(staged), f"installed hash mismatch {name}"
        print("OTA_INSTALL_V17_VALIDATED_USER_SETTINGS_SAVES_COMPAT_FONTS", flush=True)
        print("OTA_INSTALLED_MKXP_RUBY18_RUBY19_RUBY31_EASYRPG_NWJS_0120", flush=True)
        print("OTA_TEST_PASS", flush=True)


if __name__ == "__main__":
    main()

