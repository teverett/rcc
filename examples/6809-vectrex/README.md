# 6809-vectrex

A minimal GCE Vectrex cartridge, compiled and linked against `src/rt/vectrex/`
(see that directory's own `vectrex.h`/`vectrex.c`): sets the beam intensity
once, then forever waits for the next frame (`vectrex_wait_recal()`) and
plots the four corners of a 40x40 unit square centered on screen, via the
BIOS's own `Dot_d` routine rather than any memory-mapped video RAM - the
Vectrex has none; the display is an analog vector CRT the BIOS drives
through a DAC in real time.

`make` runs the three-file pipeline: `main.c` compiles on its own
(`--platform=6809-vectrex-raw`, which assumes `coff` since no `--link` is
given yet - see `docs/using.md`'s own `--platform` entry), `src/rt/vectrex/
vectrex.lib` is built (or reused) via that directory's own Makefile, then
the two combine via `--link` into `build/main.rom` - a real Vectrex
cartridge ROM image, starting with the BIOS-required "g GCE" header (see
`../../src/fileformat/vectrex/VectrexFormat.h`), ready to load into any
Vectrex hardware or emulator that accepts a raw cartridge dump.

There is no verified Vectrex emulator in this project's CI yet (see
`docs/known-bugs.md`), so `make test` only confirms this example compiles
and links - it doesn't execute the output against anything.
