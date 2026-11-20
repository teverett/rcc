# basic-6502-dos33

A BASIC program (`main.bas`, parsed by `src/frontend/basic/BasicFrontend.cpp` - see
`docs/basic-language.md`) running on the Apple ][: sums 1 to 10 with a `FOR`/`NEXT` loop, doubles
the result via `GOSUB`/`RETURN`, and `PRINT`s both along the way, via
`src/rt/apple2/basic_apple2.c`'s implementation of the runtime calls PRINT lowers to
(`__basic_print_int`/`__basic_print_str`/`__basic_print_newline`), themselves built on
`src/rt/apple2/apple2.c`'s own `a2_cout()`/`a2_puts()`/`a2_crout()` monitor-ROM wrappers.

`make` runs the same three-file pipeline as `../6502-dos33/`'s own C example, just with `main.bas`
instead of `main.c` and two runtime objects instead of one: `main.bas` compiles on its own
(`--platform=6502-prodos`), `src/rt/apple2/apple2.lib` and `basic_apple2.lib` are built (or
reused) via that directory's own Makefile, then all three combine via `--link --platform=6502-
prodos --base=$2000` into `build/main.prodos` - see `../6502-dos33/README.md`'s own paragraph on
why `--base=$2000` is needed and what ProDOS does with it.

`make test` links the same program as `build/main.raw` and runs it under `test.py`'s py65
simulation (same `COUT`/`CROUT` stub approach as `../6502-dos33/test.py` - see that file's own
header comment), checking it printed exactly the expected banner/sum/doubled-sum sequence.

GOSUB/RETURN has no existing AST/IR primitive of its own; see `BasicFrontend.cpp`'s own header
comment for how it's synthesized from a small return-address stack and an `if`/`goto` dispatch
chain, entirely within the frontend, with no codegen changes. See `docs/known-bugs.md`'s own "The
6502 backend corrupts a call made right after a `goto`-skipped local reads a global array" entry
for why that dispatch specifically avoids `switch` and any local variable of its own.
