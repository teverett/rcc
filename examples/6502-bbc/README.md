# 6502-bbc

A "hello world" for the BBC Micro (Model B), compiled and linked against
`src/rt/bbc/` (see that directory's own `bbc.h`/`bbc.c`): clears the
screen and prints `HELLO, WORLD!` via MOS's `OSWRCH` ($FFEE) call, then
moves to a new line via `OSNEWL` ($FFE7).

`make` runs the three-file pipeline: `main.c` compiles on its own
(`--platform=6502-ssd`, which assumes `coff` since no `--link` is given
yet - see `docs/using.md`'s own `--platform` entry), `src/rt/bbc/bbc.lib`
is built (or reused) via that directory's own Makefile, then the two
combine via `--link --platform=6502-ssd --base='$1900'` into
`build/main.ssd` - `--base=$1900` is `PAGE`, the BBC Micro's own
user-program start address once the Disc Filing System is in use (see
`docs/platforms.md`'s own "bbc" entry for the memory-map sources this is
taken from). `main.ssd` is a real Acorn DFS single-sided disc image (see
`../../src/fileformat/dfs/DfsFormat.h`'s own header comment): mount it in
any DFS-based BBC Micro emulator and `*LOAD CODE`/`*RUN CODE` (or just
`*RUN CODE`) it from the `*CAT`-alogued `CODE` file it holds - no boot
option is set, so nothing runs automatically just from inserting the
disc.

Since there's no BBC Micro MOS ROM in this project's own CI (`py65`'s
`MPU` is just the bare 6502 core, no firmware loaded), CI instead links
the same program as `build/main.raw` (`make build/main.raw`, identical
bytes minus the disc image's own catalogue sectors) and patches tiny
hand-written 6502 stubs into the simulator's memory at `OSWRCH`'s and
`OSNEWL`'s real vector addresses before running it, so `main()`'s real
`asm { jsr ... }` calls land somewhere that actually records what was
printed instead of crashing into empty memory - see `test.py`'s own
header comment for exactly what those stubs do and why each is only a
3-byte trampoline rather than the real logic inline.
