# 6502-oric

A "hello world" for the Tangerine Oric-1 / Oric Atmos, compiled and linked against
`src/rt/oric/` (see that directory's own `oric.h`/`oric.c`): clears the 40x28 text screen and
prints `HELLO, WORLD!` centered at row 13, via direct stores into the Oric's own memory-mapped
text screen RAM (`$BB80`-`$BFDF`) rather than any ROM call.

The Oric-1 and the Oric Atmos are one platform, not two: the Atmos is a 1985 revision of the
1983 Oric-1 (a better keyboard and a fixed/renumbered ROM) with the same 6502A CPU and the same
memory map, so this example - and `src/rt/oric/` - targets both under the plain `6502` target,
the same way `examples/6502-freestanding` does, needing no target or file-format changes of its
own.

`make` compiles `main.c` and `api_test.c` (the latter exercises every `oric.h` function, not
just the hello-world subset `main.c` needs - see that file's own header comment) against
`--platform=6502-raw`, builds (or reuses) `src/rt/oric/oric.lib`, then links each object into
`build/main.raw`/`build/api_test.raw` (flat binaries loaded and entered at `$0600`, the plain
`6502` target's own default).

`make test` runs both binaries in py65's 6502 MPU core (`pip install py65`) and reads the result
back out of the simulated screen RAM - see `test.py`'s and `api_test.py`'s own header comments.

There's no real Oric tape (`.tap`) or disk image format wired up here - see
`docs/known-bugs.md` for why.
