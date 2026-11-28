# i286-dos

A "hello world" for an 80286-class PC/AT, calling the BIOS directly
(`src/rt/pcbios/`: `INT 10h` video, `INT 16h` keyboard - see
`docs/platforms.md`'s own `i286`/`8086-dos` entries) instead of going through
MS-DOS (`src/rt/msdos/`, used here only for one `dos_create()` call that leaves
a `DONE.TXT` marker behind, proving the program ran to completion). Same
backend/ABI as `examples/8086-pcxt/`, just compiled `--target=i286-dos`: every
instruction `--target=8086-dos` would emit is still valid here, plus
`main.c`'s own `asm { pusha \n popa }` block - a true no-op pair that only
exists to prove a real 80286-only instruction (one plain `8086-dos` would
reject outright) compiles, links, and runs correctly from inside an ordinary C
program.

`src/rt/pcbios/pcbios.lib`/`src/rt/msdos/msdos.lib` are built (or reused) as
plain `--target=8086-dos` COFF objects, not rebuilt for `i286-dos` - `i286-dos`
shares `8086-dos`'s exact COFF machine ID and entry/exit convention (see
`src/main.cpp`'s own `makeLinker()` comment), so linking them straight into
this program needs no i286-specific library build at all.

`make` runs the same three-step pipeline `examples/8086-pcxt/` does
(`--platform=i286-dos-raw` in place of `8086-dos-raw`), linking both libraries
into `build/main.com`.

`make test` runs it headlessly under DOSBox-X (`brew install dosbox-x` or
`apt-get install dosbox-x`; skipped with a message if not installed) via
`test.py`, which checks for `DONE.TXT` the same way `examples/8086-pcxt/`'s own
`test.py` does. This also exercises the `pusha`/`popa` pair actually running
under DOSBox-X's CPU core without trapping - about as close to a real
CPU-execution check as this project's tooling gets for `i286`'s general
(non-protected-mode) instructions. The protected-mode/descriptor-table group
(`lgdt`/`sgdt`/`lidt`/`sidt`/`lldt`/`sldt`/`ltr`/`str`/`lmsw`/`smsw`/`clts`/
`arpl`/`verr`/`verw`) has no execution check anywhere in this project - see
`docs/known-bugs.md`'s own `i286` entry for why.
