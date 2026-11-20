# cp1610-intv

A "hello world" for the Mattel Intellivision, compiled and linked against
`src/rt/intv/` (see that directory's own `intv.h`/`intv.c`): sets the
border/background colors, clears BACKTAB (the console's 20x12 background
card table), and pokes two raw GROM card numbers for "HI" into its first
two cells, via direct stores into STIC/System RAM addresses rather than
any EXEC ROM call - see `src/rt/intv/intv.h`'s own header comment for
exactly which facts are confirmed and which (the BACKTAB word's own bit
layout) are deliberately left as a raw poke instead of guessed.

`make` runs the same three-file pipeline `../6809-coco/Makefile` does:
`main.c` compiles on its own (`--platform=cp1610-intv-raw`, which assumes
`coff` since no `--link` is given yet), `src/rt/intv/intv.lib` is built
(or reused) via that directory's own Makefile, then the two combine via
`--link --platform=cp1610-intv-intvbin` into `build/main.bin` - a real,
loadable Intellivision cartridge ROM image (see
`../../src/fileformat/intvbin/IntvBinFormat.h`).

No accessible, simulator-runnable Intellivision machine exists in this
project's CI (jzIntv needs copyrighted EXEC/GROM ROM images this project
can't bundle or install - see `docs/known-bugs.md`), so `test` only
confirms `build/main.bin` exists and is non-empty; it doesn't execute it
against anything.
