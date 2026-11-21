# vex

A "hello world" for Vex (`/Users/tom/projects/vex`), a 9-register, 32-bit register machine:
prints `Hello, Vex!` via `vex_puts()` (see `src/rt/vex/vex.h`), which writes it to standard
output one byte at a time through the machine's own console device (`DEO` to port `0x18`).

`make` runs the same object-then-link pipeline as `examples/6502-c64/`: `main.c` compiles on its
own (`--target=vex --format=coff`), `src/rt/vex/vex.lib` is built (or reused) via that directory's
own Makefile, then the two link together with `--link --platform=vex-raw` into `build/main.rom` -
a flat ROM image the real interpreter loads at `0x0100` (see `docs/SPEC.md` in the sibling vex
project).

`make test` builds the reference interpreter vendored at `tests/sim/vex/vex.c` and runs
`build/main.rom` under it for real, checking it printed exactly `Hello, Vex!` to standard output.
