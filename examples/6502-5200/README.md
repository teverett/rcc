# 6502-5200

A "hello world" for the Atari 5200, compiled and linked against `src/rt/atari5200/` (see that
directory's own `atari5200.h`/`atari5200.c`).

Like `examples/6502-atari2600/`, the 5200 has no OS/CIO call to print text through (see
`src/rt/atari5200/atari5200.h`'s own header comment for why) - no character ROM-backed text
mode is set up here, no display list is built. So the de facto "hello world" is the same kind
of "it's alive" demo: `main.c` sets GTIA's background color (`atari5200_setcolor(8, ...)`),
starts a fixed tone on one POKEY sound channel (`atari5200_sound(0, ...)`), and kicks off a
controller potentiometer scan (`atari5200_potgo()`) - more than one register poke, so there's
something to verify beyond "did it compile".

`make` runs the three-file pipeline: `main.c` compiles on its own (`--target=5200`, `coff`),
`src/rt/atari5200/atari5200.lib` is built (or reused) via that directory's own Makefile, then
the two combine via `--link --target=5200 --format=a5200 --base='$8000'` into
`build/main.a5200` - `--base='$8000'` picks the 16K-cartridge size (`$B000`/`$A000`/`$8000`/
`$4000` are the only four bases `--format=a5200` accepts - a 4K/8K/16K/32K cartridge
respectively, every one of them ending at `$C000`; see `../../src/fileformat/a5200/
Atari5200RomFormat.h`). That file loads directly in any 5200 emulator that reads a bare
cartridge image (e.g. Altirra in 5200 mode).

Since no GTIA/POKEY/ANTIC hardware (and no 5200 BIOS ROM) is simulated in this project's own CI
(`py65`'s `MPU` is just the bare 6502 core), CI instead links the same program as
`build/main.raw` (`make build/main.raw`, identical bytes minus the cartridge header) and just
checks that GTIA's COLBK and POKEY's AUDF1/AUDC1 hold the expected byte values after the program
reaches its own halt loop - see `test.py`'s own header comment.
