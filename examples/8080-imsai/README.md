# 8080-imsai

A "hello world" for the IMSAI 8080, compiled and linked against
`src/rt/imsai/` (see that directory's own `imsai.h`/`imsai.c`): writes
`HELLO, WORLD!\r\n` out through the SIO-2 serial board (data port `$02`,
status port `$03` - the convention Lifeboat CP/M's own BIOS and the
IMSAI SIO-2 emulators assume) via the 8080's `IN`/`OUT` instructions.

`make` runs the two-file pipeline: `main.c` compiles on its own
(`--platform=8080-raw`, which assumes `coff` since no `--link` is given yet
- see `docs/using.md`'s own `--platform` entry), `src/rt/imsai/imsai.lib`
is built (or reused) via that directory's own Makefile, then the two
combine via `--link --platform=8080-raw` into `build/main.raw` - a flat
binary loaded/entered at `$0100`, the same convention a real IMSAI's
front-panel switches or a paper-tape loader would deposit a program at.

Since there's no IMSAI emulator in this project's own CI, CI instead runs
`build/main.raw` on a real, independent Z80 core (`z80-python`, object-code
compatible with the 8080 - see `src/codegen/i8080/I8080Assembler.h`'s own
header comment), with `test.py`'s own `read_port`/`write_port` callbacks
standing in for the SIO-2 board: every byte written to the data port is
captured and compared against the expected string.
