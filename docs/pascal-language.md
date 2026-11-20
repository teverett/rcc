# The rcc Pascal frontend (scope)

This is the Phase 0 scoping document for a Pascal frontend: it fixes,
before any grammar work starts, exactly what Pascal subset the frontend
will accept. Nothing here is implemented yet - this is a decision
record, in the same spirit as `docs/language.md`'s C subset and
`docs/basic-language.md`'s BASIC dialect. Once a frontend exists it
will plug into `src/frontend/Frontend.h` the same way BASIC does:
source -> grammar/parser -> `rcc::AST` (`src/ast/AST.h`) -> the same
language-independent `ir::Program`, optimizers, and every target
`Codegen`. Everything past the AST-building step is shared and needs
no Pascal-specific changes.

## v1 scope: in

- **Ordinal types**: `integer`, `char`, `boolean`. These map directly
  onto `BaseType::Int`/`Char` (`src/Type.h`) - rcc has no dedicated
  `Bool` either, so Pascal `boolean` lowers the same way C's `int`-
  valued conditions already do (0/1).
- **Real numbers**: `real` maps onto `BaseType::Float`/`Double` - but
  see "Deferred to a later version" below: no backend currently
  supports it, so `real` is in the *grammar* but not usable until a
  target does.
- **Pointers**: `^T`, any depth, same as C's `T*` (`ptrDepth` in
  `Type.h`).
- **Fixed arrays**: `array[lo..hi] of T`, any number of dimensions
  (`array[1..3, 1..4] of integer`). Lowers onto the same flat
  `arraySize`/`arrayDims` machinery multi-dimensional C arrays and
  BASIC's `DIM A(r,c)` already use (`src/ast/AST.h`, `src/ir/IR.h`) -
  zero IR/codegen changes needed. Pascal's declared bounds don't have
  to start at 0 or 1; the frontend subtracts `lo` from every subscript
  at the access site, same idea as BASIC's inclusive-upper-bound `DIM`
  needing its own offset adjustment.
- **Plain (non-variant) records**: `record field: T; ... end`, file
  scope only (matching `struct`'s own file-scope-only restriction).
  Lowers onto `StructDecl`/`computeStructLayout`
  (`src/StructLayout.cpp`) unchanged - sequential, pad-to-alignment
  layout, exactly like a C `struct`.
- **Enums-as-ordinals**: `(Red, Green, Blue)` as a type still lowers
  the same way C's `enum`/BASIC would, as parse-time-only named `int`
  constants with no runtime representation of their own (see
  `docs/language.md`'s "Enums" - `BuildAst.cpp`'s `g_enumTags`/
  `g_enumConstants` approach generalizes directly).
- **Subranges**: `1..10` as a type is accepted syntactically; no
  runtime range-checking is performed in v1 (a subrange is just its
  base ordinal type under the hood - optional for a later version, see
  below).
- **Sets**: `set of T` (`T` an ordinal/subrange), with `+` (union),
  `*` (intersection), `-` (difference), `in` (membership). No bitset
  type exists anywhere in rcc, so this is synthesized entirely in the
  frontend as a bit-vector over a plain integer or array - ordinary IR
  ops, no `Type.h`/IR changes.
- **`var` parameters**: pass-by-reference parameters lower to an
  implicit pointer parameter with the frontend inserting the
  dereference at every use site - frontend-only, same idea as a C
  function taking `T*` instead of `T`.
- **Procedures and functions**: `procedure`/`function` declarations
  with a fixed, by-value (or `var`) parameter list and (for `function`)
  a single scalar/pointer return type - this maps directly onto
  `FunctionDecl`. Nested procedures (a Pascal procedure declared inside
  another) are **not** supported in v1 - see below.
- **Control flow**: `if/then/else`, `while/do`, `repeat/until`,
  `for ... to/downto ... do`, `case ... of`, all desugaring onto the
  same `Stmt` node kinds (`If`/`While`/`For`-as-`While`/`Switch`) the C
  frontend already builds.
- **Statement sequencing**: `begin ... end` blocks, `:=` assignment.

## v1 scope: deferred to a later version (not an IR limitation - just cut for v1)

- **Variant records** (`case tag: T of ...`): `computeStructLayout` is
  strictly sequential/monotonic-offset; there's no concept anywhere in
  rcc of two fields sharing overlapping storage. Supporting this needs
  new, shared (target-independent) logic in `StructLayout.cpp` - real
  work, not reuse, so it's out of v1 by choice, not because it's
  impossible.
- **Range-checked subranges**: accept the syntax in v1, add the actual
  bounds check later; this is a frontend-only addition whenever it
  happens.

## v1 scope: out, and blocked on a real IR gap

- **Procedural types / passing a procedure or function as a value**
  (`procedure p(f: function(x: integer): integer)`, or any indirect
  call with a real signature): rcc has no `FunctionType` at all, and
  its only indirect-call mechanism, `CallPtr`, is hard-restricted to
  zero arguments with a return type hardcoded to `int`
  (`src/ir/IRBuilder.cpp`, the `ExprKind::CallPtr` case). This is a
  restriction baked into `ir::Call`/`CallPtr`'s own contract, not a
  missing frontend feature - lifting it risks touching call-lowering
  in every backend's `Codegen.cpp`. Out of scope until that IR-level
  work happens, independent of any Pascal-specific plan.
- **Nested procedures** (one `procedure`/`function` declared lexically
  inside another, closing over the outer one's locals): rcc's AST has
  no nested-function/closure concept - `FunctionDecl`s are always
  flat, file-scope - and faking lexical capture would need its own new
  scope-threading machinery through IRBuilder. Out of v1; every
  procedure/function in the v1 subset is file-scope-flat, same as a C
  function.

## Numeric model: real numbers are grammar-legal but unusable until a backend supports them

Checked against the current tree
(`src/Type.h`'s `TypeWidths::floatUnits`/`doubleUnits`, both default
`0`): no backend sets either above `0` today. The same contradiction
flagged in the BASIC plan applies here unchanged - `AST.h`/`IR.h`
already carry float plumbing (`FloatLit`, `isFloat`), but nothing
downstream can emit real arithmetic yet. `real` stays in the Pascal
grammar (it's too central to drop) but a program using it fails to
compile against any target until a backend adds float support,
exactly the same restriction a C program using `float`/`double` hits
today.

## Scope and limitations (preexisting rcc restrictions Pascal inherits as-is)

These aren't Pascal-specific decisions - they're already true of the
shared AST/IR/codegen layers and apply here the same way they apply to
C:

- No record-by-value parameters or returns (`rejectStructByValue`,
  `src/ir/IRBuilder.cpp`) - only a pointer to a record, matching C's
  own "no struct-by-value" restriction.
- No array-of-record for file-scope (global) variables - a local
  array of records works, matching the existing C restriction.
- No whole-record assignment (`a := b` for two record-typed
  variables) and no record-literal initializer, matching C's "no
  whole-struct assignment, no struct initializer".

## What's genuinely new (not reuse of an existing mechanism)

Scoped against `src/Type.h` + `src/ast/AST.h` + `src/ir/IR.h` +
`src/StructLayout.cpp`, the only machinery below this frontend's own
grammar/AST-building step that v1 Pascal needs and C/BASIC don't
already exercise:

- Sets (bit-vector synthesis, entirely frontend-local).
- `var` parameters (implicit-pointer-plus-deref, entirely
  frontend-local).

Everything else in the v1 "in" list rides unchanged on AST/IR/codegen
machinery the C and BASIC frontends already use.

## Next steps (Phase 1+)

Grammar (`grammar/PascalLexer.g4`/`PascalParser.g4`), a
`BuildPascalAst.cpp` lowering pass, runtime additions under `src/rt/`
for `write`/`writeln`/`read`/`readln`, driver/build integration
(`.pas` dispatch + grammar Makefile rule + CI + example program), and
tests (minimum two per construct per `AGENTS.md`) - not started yet.
