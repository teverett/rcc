# basic-vex

The same BASIC program as `../basic-6502-dos33/main.bas` (parsed by `src/frontend/basic/
BasicFrontend.cpp` - see `docs/basic-language.md`), running on Vex (`/Users/tom/projects/vex`, an
9-register, 32-bit register machine) instead of the Apple ][: sums 1 to 10 with a `FOR`/`NEXT`
loop, doubles the result via `GOSUB`/`RETURN`, and `PRINT`s both along the way, via
`src/rt/vex/basic_vex.c`'s implementation of the runtime calls PRINT lowers to
(`__basic_print_int`/`__basic_print_str`/`__basic_print_newline`), themselves built on
`src/rt/vex/vex.c`'s own `vex_putc()`/`vex_puts()` console-device wrappers.

`make` runs the same object-then-link pipeline as `../vex/`'s own C example, just with `main.bas`
instead of `main.c` and two runtime objects instead of one: `main.bas` compiles on its own
(`--target=vex --format=coff`), `src/rt/vex/vex.lib` and `basic_vex.lib` are built (or reused) via
that directory's own Makefile, then all three link together with `--link --platform=vex-raw` into
`build/main.raw`.

`make test` builds the reference interpreter vendored at `tests/sim/vex/vex.c` (same as
`../vex/Makefile`'s own `test` target) and runs `build/main.raw` under it for real, checking it
printed exactly the expected banner/sum/doubled-sum sequence to standard output.

See `../basic-6502-dos33/README.md`'s own paragraph on how GOSUB/RETURN is synthesized (no
existing AST/IR primitive for it) - this example's passing on a completely different
architecture is what confirms that synthesis itself is portable, not an accident of the 6502
backend's own quirks (see `docs/known-bugs.md`'s 6502-specific entry, discovered building the
Apple ][ example, that the `if`/`goto` dispatch chain was written specifically to avoid).
