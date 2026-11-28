"""Runs main.com headlessly under DOSBox-X's own BIOS/DOS emulation and checks that it ran
to completion, via DONE.TXT (written by main.c's own dos_create() call, the same "did we
get this far without crashing" check tests/run_pcbios_tests.sh's own run_com() helper and
examples/8086-pcxt/'s own test.py use). This also exercises main.c's `asm { pusha \n popa }`
block actually running: a real 80286-only opcode pair executing inside DOSBox-X's own CPU
core without trapping is as close to CPU-execution verification as this project's tooling
gets for i286's general (non-protected-mode) instructions - see docs/known-bugs.md's own
i286 entry for why the protected-mode group isn't verified this way too.

Usage: test.py MAINCOM
"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

maincom = Path(sys.argv[1]).resolve()

with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    shutil.copy(maincom, tmp / "main.com")

    subprocess.run(
        [
            "dosbox-x",
            "-c", f"mount c {tmp}",
            "-c", "c:",
            "-c", "main.com",
            "-c", "exit",
            "-silent", "-exit", "-time-limit", "20",
            "-noconfig", "-fastlaunch", "-nogui", "-nomenu",
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        timeout=30,
    )

    if not (tmp / "DONE.TXT").exists():
        raise SystemExit("main.com never wrote DONE.TXT - it crashed or didn't run")

print("PASS")
