# The rcc BASIC frontend

This is the language-level reference for rcc's BASIC frontend: a
classic, line-numbered BASIC dialect that lowers onto the same
`rcc::AST` (`src/ast/AST.h`) the C frontend builds, and from there onto
the same language-independent `ir::Program`, optimizers, and every
target `Codegen` - see `src/frontend/Frontend.h`. Nothing below this
frontend's own grammar/AST-building step is specific to BASIC.

## The language subset

A program is a sequence of numbered lines, each holding one or more
`:`-separated statements:

```
10 LET A = 1
20 PRINT A
30 GOTO 10
```

`LET` is optional (`A = 1` alone is the same statement). Supported
statements: `LET`, `PRINT` (`,`/`;`-separated items; `,` inserts the
same tab-stop spacing real BASIC's zone printing uses, `;` inserts
none, and a trailing `;` suppresses the newline), `INPUT`, `IF ...
THEN ... [ELSE ...]` (single-line only - no block `IF`/`END IF`),
`FOR ... TO ... [STEP ...]` / `NEXT`, `GOTO`, `GOSUB` / `RETURN`,
`DIM`, `END`.

Variables are untyped by declaration - a name's type is its sigil:
a bare name (`A`) or one ending `%` (`A%`) is an integer; one ending
`$` (`A$`) is a string. There is no floating-point type in this
dialect: rcc's own C subset has float/double AST and IR plumbing, but
no backend currently sets `TypeWidths::floatUnits`/`doubleUnits` above
0 (see `src/Type.h`), so there is no target to generate real-number
arithmetic for yet. Every variable is a single flat name with program-
wide scope (classic BASIC has no block scoping at all - GOSUB shares
the caller's variables, it isn't a function call into a fresh scope).

`DIM A(n)` declares a single-dimension, 0-based array of `n+1`
elements (matching classic BASIC's inclusive upper bound). `DIM
A(r,c)` (any number of dimensions) declares a multi-dimensional array
the same way - each dimension is 0-based inclusive, and `A(i,j)` must
subscript every declared dimension at once, matching rcc's own C-subset
`int a[r][c];` (see `docs/language.md`'s own array section).

String values support only concatenation (`+`) and comparison - no
other string arithmetic - so this frontend's runtime support needs no
heap/garbage collector, consistent with every rcc runtime targeting
freestanding/embedded backends.

## Line numbers, labels, and GOTO/GOSUB

Each line's number is a label `GOTO`/`GOSUB` can target, via the same
`StmtKind::Label`/`Goto` AST nodes the C frontend's own `goto` already
uses (see `docs/language.md`'s "goto"). The one restriction this
frontend adds on top of that existing mechanism: a `GOTO`/`GOSUB`
cannot jump into the middle of a `FOR`/`NEXT` body from outside it -
the same scope-leak rule the C frontend's own `goto` already enforces
(see `tests/goto_label_scope.c`), re-checked here because classic
BASIC's GOTO is otherwise unrestricted.

`GOSUB n ... RETURN` is the one new AST concept this frontend needs:
unlike `GOTO`, it has a return address, so it cannot lower onto `Goto`
alone.

## Scope and limitations

No user-defined functions/procedures (`GOSUB` targets share the
caller's variables, not a parameter list), no block `IF`, no
`DATA`/`READ`, no floating point (see above), no string functions
beyond `+`/comparison. These match the same "smallest subset that's
still recognizably the language" approach `docs/language.md` takes for
the C frontend, not an attempt at a full BASIC implementation.
