"""Runs main.com headlessly under DOSBox-X's own BIOS/DOS emulation and checks that it ran
to completion, via DONE.TXT (written by main.c's own dos_create() call, the same "did we
get this far without crashing" check tests/run_pcbios_tests.sh's own run_com() helper uses -
DOSBox-X has no headless way to capture what pcbios_puts()/pcbios_write_attr() actually put
on screen). No boot sector or disk image is built anywhere in this - DOSBox-X's own `mount`
command maps this directory straight onto drive C: already running under DOS.

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
