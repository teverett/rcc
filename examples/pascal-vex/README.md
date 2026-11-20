# pascal-vex

The Pascal-frontend analog of `../basic-vex/main.bas` (parsed by `src/frontend/pascal/
PascalFrontend.cpp` - see `docs/pascal-language.md`), running on Vex (an 8-register, 32-bit
register machine): sums 1 to 10 with a `for` loop, doubles the result through a `var` parameter
(`DoubleIt`), and `writeln`s both along the way, via `src/rt/vex/pascal_vex.c`'s implementation
of the runtime calls `write`/`writeln` lower to (`__pascal_write_int`/`__pascal_write_str`/
`__pascal_write_newline`), themselves built on `src/rt/vex/vex.c`'s own `vex_putc()`/`vex_puts()`
console-device wrappers.

`make` runs the same object-then-link pipeline as `../basic-vex/`'s own example, just with
`main.pas` instead of `main.bas` and `pascal_vex.lib` instead of `basic_vex.lib`: `main.pas`
compiles on its own (`--target=vex --format=coff`), `src/rt/vex/vex.lib` and `pascal_vex.lib` are
built (or reused) via that directory's own Makefile, then all three link together with `--link
--platform=vex-raw` into `build/main.raw`.

`make test` builds the reference interpreter vendored at `tests/sim/vex/vex.c` (same as
`../basic-vex/Makefile`'s own `test` target) and runs `build/main.raw` under it for real, checking
it printed exactly the expected banner/sum/doubled-sum sequence to standard output.

Unlike BASIC's `GOSUB`/`RETURN` (synthesized from scratch - see `../basic-6502-dos33/README.md`),
`DoubleIt`'s `var x: integer` parameter lowers onto an ordinary pointer parameter with every
read/write inside `DoubleIt` auto-dereferenced at its use sites (see `PascalFrontend.cpp`'s own
header comment) - no new AST/IR primitive needed, same as everything else this frontend lowers.
