# 8086-pcxt

A "hello world" for an IBM 5160 (PC/XT)-class machine, calling the BIOS directly
(`src/rt/pcbios/`: `INT 10h` video, `INT 16h` keyboard) instead of going through MS-DOS
(`src/rt/msdos/`, used here only for one `dos_create()` call that leaves a `DONE.TXT`
marker behind, proving the program ran to completion).

Still compiled for `--target=8086-dos` (the same backend/ABI `examples/8086-msdos/` uses):
rcc never builds a boot sector (out of scope for this project - see
`docs/known-bugs.md`'s `8086`/`8088` entry), so a BIOS-calling program still needs DOS (or
an emulator standing in for it) to get the CPU into real mode with the BIOS resident and
the program loaded at all. It just never calls back into DOS (`INT 21h`) for video or
keyboard I/O once running.

`make` runs the same three-step pipeline `examples/8086-msdos/` does, just linking both
`src/rt/pcbios/pcbios.lib` and `src/rt/msdos/msdos.lib` into `build/main.com`.

`make test` runs it headlessly under DOSBox-X (`brew install dosbox-x` or
`apt-get install dosbox-x`; skipped with a message if not installed) via `test.py`, which
checks for `DONE.TXT` rather than captured screen text - DOSBox-X has no headless way to
dump the screen, so `tests/run_pcbios_tests.sh`'s own tests (run at `make test`'s top
level, not from here) are what actually check `pcbios_putchar()`/`pcbios_write_attr()`'s
effect on video memory, via `INT 10h/08h` readback. `pcbios_getchar()`'s blocking keyboard
read is compile/link-tested only - there's no way to inject a real keystroke into DOSBox-X
headlessly, so nothing here ever calls it.
