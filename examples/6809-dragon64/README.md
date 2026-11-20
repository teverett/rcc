# 6809-dragon64

A "hello world" for the Dragon 64 (Dragon Data Ltd), compiled and linked
against `src/rt/dragon64/` (see that directory's own
`dragon64.h`/`dragon64.c`): clears the 32x16 text screen and prints
`HELLO, WORLD!` centered on row 7, via direct stores into video RAM
(`$0400`-`$05FF`) rather than any ROM/BASIC call.

The Dragon 64 is a Dragon 32 with 64KB of RAM, a second banked 32KB ROM,
and an onboard 6551 ACIA serial port, none of which changes its CPU
(6809E), SAM memory-management IC, 6847 VDG video chip, or their wiring
(same PIA addresses, same default 32x16 text screen) - see
`examples/6809-dragon32`'s own README for the shared reasoning. This
example therefore reuses the plain `6809` target and its existing
`6809-raw`/`6809-decb` platforms exactly as `examples/6809-dragon32`
does, rather than needing a new target or file format of its own.

`make` compiles `main.c` and `api_test.c` (the latter exercises every
`dragon64.h` function, not just the hello-world subset `main.c` needs -
see that file's own header comment) against `--platform=6809-raw`,
builds (or reuses) `src/rt/dragon64/dragon64.lib`, then links each
object into `build/main.raw`/`build/api_test.raw` (flat binaries) and
`build/main.decb` (the real Dragon/CoCo `LOADM`/`CLOADM` load-module
byte stream - see `../../src/fileformat/decb/DecbFormat.h` - which
XRoar, the emulator covering both machines, loads directly), ready for
a real Dragon 64's `LOADM`/`EXEC` or `CLOADM`/`EXEC` from cassette.

There is no verified 6809 emulator in this project's CI yet, so CI only
confirms this example compiles and links
(`make -C examples/6809-dragon64`) - it doesn't execute either output
against anything.
