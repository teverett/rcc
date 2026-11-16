# 8080-altair

A "hello world" for the MITS Altair 8800, compiled and linked against
`src/rt/altair/` (see that directory's own `altair.h`/`altair.c`): writes
`HELLO, WORLD!\r\n` out through the 88-2SIO serial board (status port
`$10`, data port `$11` - the convention Altair BASIC's and CP/M's own BIOS
assume) via the 8080's `IN`/`OUT` instructions.

`make` runs the two-file pipeline: `main.c` compiles on its own
(`--platform=8080-raw`, which assumes `coff` since no `--link` is given yet
- see `docs/using.md`'s own `--platform` entry), `src/rt/altair/altair.lib`
is built (or reused) via that directory's own Makefile, then the two
combine via `--link --platform=8080-raw` into `build/main.raw` - a flat
binary loaded/entered at `$0100`, the same convention a real Altair's
front-panel switches or a paper-tape loader would deposit a program at.

Since there's no Altair emulator in this project's own CI, CI instead runs
`build/main.raw` on a real, independent Z80 core (`z80-python`, object-code
compatible with the 8080 - see `src/codegen/i8080/I8080Assembler.h`'s own
header comment), with `test.py`'s own `read_port`/`write_port` callbacks
standing in for the 2SIO board: every byte written to the data port is
captured and compared against the expected string.
