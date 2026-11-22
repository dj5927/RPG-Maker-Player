#!/usr/bin/env python3
"""SteamOS exit integration: Steam signal -> game group -> launcher exit."""
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time


def check_one(binary: Path, run_script: Path, signum: int | None, use_wrapper: bool,
              stubborn: bool = False, sdl_event: str | None = None) -> None:
    with tempfile.TemporaryDirectory(prefix="rpgmp-exit-") as tmp:
        root = Path(tmp)
        target = root / "MKXP_Launcher"
        shutil.copy2(binary, target)
        target.chmod(0o755)
        wrapper = root / "run.sh"
        shutil.copy2(run_script, wrapper)
        wrapper.chmod(0o755)
        env = os.environ.copy()
        env.update({"SDL_VIDEODRIVER": "dummy",
                    "SDL_AUDIODRIVER": "dummy",
                    "MKXP_TEST_STEAM_EXIT_CHILD": "1"})
        if stubborn:
            env["MKXP_TEST_STEAM_EXIT_STUBBORN"] = "1"
        if sdl_event:
            env["MKXP_TEST_STEAM_EXIT_SDL_" + sdl_event] = "1"
        proc = subprocess.Popen([str(wrapper if use_wrapper else target)],
                                env=env, cwd=root, start_new_session=True,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            time.sleep(0.75)
            if proc.poll() is not None:
                raise AssertionError(f"exited before signal: {proc.returncode}")
            if use_wrapper:
                command_name = Path(f"/proc/{proc.pid}/comm").read_text().strip()
                if command_name != "MKXP_Launcher":
                    raise AssertionError(f"wrapper did not exec launcher: {command_name}")
            if signum is not None:
                proc.send_signal(signum)
            code = proc.wait(timeout=7)
            if code != 0:
                raise AssertionError(f"launcher exited with {code} for signal={signum}")
            log = (root / "logs/MKXP_Launcher.log").read_text()
            expected = "direct exit steam-window-close" if sdl_event else "direct exit steam-signal"
            if expected not in log:
                raise AssertionError(f"missing child cleanup log: {expected}")
            if stubborn and "SIGKILL timeout fallback" not in log:
                raise AssertionError("SIGKILL watchdog was not exercised")
            print(f"PASS wrapper={use_wrapper} signal={signum} sdl={sdl_event} stubborn={stubborn} pid={proc.pid}")
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait(timeout=5)


if __name__ == "__main__":
    executable = Path(sys.argv[1]).resolve()
    script = Path(__file__).resolve().parents[1] / "scripts/run.sh"
    for signum, wrapper in ((signal.SIGTERM, True),
                            (signal.SIGINT, False),
                            (signal.SIGHUP, False)):
        check_one(executable, script, signum, wrapper)
    check_one(executable, script, signal.SIGTERM, True, stubborn=True)
    check_one(executable, script, None, False, sdl_event="QUIT")
    check_one(executable, script, None, False, sdl_event="WINDOW")
    print("STEAMOS_STEAM_EXIT_SMOKE_PASS")
