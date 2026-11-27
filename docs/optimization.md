# Optimizers

`--optimize` runs a pipeline of target-independent IR-to-IR passes
(`src/optimizers/`, each implementing the `Optimizer` interface in
`src/optimizers/Optimizer.h`) between `IRBuilder` and whichever
backend is selected — since every backend-specific decision
(instruction selection, register/stack strategy) happens strictly
after the IR exists, an optimizer written once benefits every target
(see `docs/platforms.md` for the full list).

Today there are thirteen, run in this order (`src/main.cpp`). Twelve
implement the shared `Optimizer` interface; `AddressHoisting` is the
exception (see below).

- **`ConstantPropagation`**
  (`src/optimizers/constant_propagation/ConstantPropagation.cpp`) —
  intraprocedural: walking one function body in source order, tracks
  which scalar Local/Param currently holds a known literal and
  replaces a later read with it directly, feeding `ConstantFolding`
  new foldable expressions and `DeadCodeElimination` more unused
  declarations. Restricted to a scalar whose address is never taken
  (so only a direct write to it can invalidate the tracked value, with
  no need to conservatively drop everything at a `Call`). An `If`
  keeps a value only when both branches agree on it; a `While`
  conservatively forgets every slot the loop could write, both before
  and after. Runs first, feeding every later pass.

- **`ConstantFolding`** (`src/optimizers/constant_folding/ConstantFolding.cpp`) —
  bottom-up folds any unary or binary expression whose operand(s) are
  already `IntLit`s into a single `IntLit` (e.g. `((2 + 3) * 4)` →
  `20`). Leaves an op unfolded when evaluating it on the host would
  require undefined behavior the real target's own instruction
  handles fine instead — division/modulo by a constant zero, or a
  shift by an out-of-range amount.

- **`ConstantConditionElimination`**
  (`src/optimizers/constant_condition_elimination/ConstantConditionElimination.cpp`)
  — collapses an `if`/`while` whose condition `ConstantFolding` has
  already reduced to an `IntLit`: `if(1)`/`if(0)` becomes its taken
  branch (or `Empty`, if the untaken one was the only one present), and
  a loop whose condition is always false becomes `Empty` outright (its
  body, and a `for`-loop's own post-expression, never run even once).
  Builds a real `ir::CFG` (`src/ir/CFG.h`) rather than relying on a
  structural scan, because the branch being dropped could still be a
  live `goto` target from outside it — a `switch`'s own desugaring (see
  `docs/ir.md`) is the only source of `Label`/`Goto` in this IR. A
  branch is only dropped once every `Label` inside it is proven
  unreachable with that branch's structural edge removed from the
  graph; if some other, still-live edge reaches one of its `Label`s
  instead, the `if`/`while` is left untouched. Runs right after
  `ConstantFolding`, before every pass below, so none of them waste
  work on a branch this one is about to delete.

- **`AlgebraicSimplification`**
  (`src/optimizers/algebraic_simplification/AlgebraicSimplification.cpp`) — strength reduction:
  rewrites a binary expression with exactly one `IntLit` operand into
  a cheaper equivalent that holds for every possible value of the
  other operand — `x * 2`/`2 * x` → `x << 1`, `x * -1` → `-x`, `x / 1`
  → `x`, `x / -1` → `-x`, `x + 0`/`0 + x` → `x`, `x - 0` → `x`,
  `x << 0`/`x >> 0` → `x`, `x | 0`/`0 | x` → `x`, `x ^ 0`/`0 ^ x` → `x`.
  This is strictly narrower than `ConstantFolding`: it never touches a
  both-literal expression (that's already folded by the time this
  pass runs) and never drops the non-literal operand's own
  subexpression, so a side effect inside it (a call) still runs
  exactly once either way. Deliberately *not* rewritten, because it
  would have to drop that operand entirely to reach the answer, which
  is only safe if it has no side effect: `x * 0`, `x % 1`, `x & 0`.
  Also not rewritten: `x / <power of two>` / `x % <power of two>` as a
  shift/mask — truncating (round-toward-zero) division isn't
  bit-identical to an arithmetic right-shift for a negative `x`, and
  this pass has no way to know `x`'s sign at compile time in general.
  Helps every target, but especially one like pdp7 with no hardware
  multiply/divide shortcut for small constants.

- **`CompoundAssignRecognition`**
  (`src/optimizers/compound_assign_recognition/CompoundAssignRecognition.cpp`)
  — recognizes a compound assignment hand-desugared into its own two
  separate reads of the same target (`t = t <op> e`, or `*p = *p <op> e`)
  and rewrites it into the real `CompoundAssign`/`CompoundDerefAssign` IR
  node, so every backend computes the target's own address/pointer once
  instead of twice. Only folds the two reads together when they're the
  exact same side-effect-free expression (a narrow, explicit vocabulary
  that excludes any `Call` or write), and only keeps the *other* operand
  as the compound form's new rhs when that operand is itself
  side-effect-free too — otherwise a call on one side could observe or
  produce a different value depending on whether it runs before or after
  the read of the old target value, and the desugared and compound forms
  aren't free to pick different orders. Self-reference on the `Binary`'s
  left operand folds for any op; on the right, only for a commutative op
  (`t = e - t` is *not* `t -= e`, but `t = e + t` is `t += e`). Never
  rewrites a `Long`/`LongLong` target, since this pass runs once,
  backend-independently, with no target width to check a given backend's
  own notion of "wide" against. Runs after `AlgebraicSimplification`,
  before `LocalCSE`, so a hand-desugared `t = t <op> e` this pass folds
  looks like one compound op to every later canonicalization-sensitive
  pass, not two independent reads of `t`.

- **`LocalCSE`** (`src/optimizers/local_cse/LocalCSE.cpp`) — simple local
  common-subexpression elimination / redundant-load elimination:
  purely syntactic (not flow-sensitive) matching of identical
  `Binary`/`Index` subexpressions within one `Block`'s own statement
  list (an `If`/`While`'s own `cond` counts as part of its enclosing
  block; its `then`/`else`/loop body gets its own separate, unrelated
  scope). On a repeated, side-effect-free occurrence, the *first* one
  is wrapped in place with an assignment into a fresh temporary (still
  evaluating at exactly its original program point - nothing is ever
  moved), and every later occurrence becomes a plain read of it
  instead of recomputing it. An occurrence stops being eligible to
  match again the moment something could have changed its value: a
  direct write to a `Slot` it depends on drops only that occurrence,
  while a `Call`/`CallPtr`/`Intrinsic`/`DerefAssign` conservatively
  drops every occurrence recorded so far in the scope. Runs after
  `AlgebraicSimplification` so it sees each subexpression in its own
  already-canonicalized form. Introduces brand new locals after
  `IRBuilder` already assigned every other local's own frame offset
  once, which is why `main.cpp` calls `IRBuilder.h`'s own
  `recomputeFrameLayout` once the later passes below are done. See
  `LocalCSE.h`'s own comment for the deliberate gaps (no operand-order
  normalization, no iterating to a fixpoint, nothing but `Binary`/
  `Index` ever considered) and the one pre-existing `IRPrinter.cpp`
  cosmetic gap this pass makes far more visible (a promoted
  subexpression's `--dump-ir` text can read ambiguously, though the
  real tree - and so the actual compiled program - is unaffected).

- **`AddressHoisting`** (`src/optimizers/address_hoisting/AddressHoisting.cpp`)
  — hoists the base address of a real array (a Local/Global `Slot` with
  `arraySize > 0`) out of every repeated `Index`/`Assign`/
  `CompoundAssign` that names it within one function, into a single
  pointer-typed temporary computed once, instead of every backend
  re-deriving that address from scratch at each use. Deliberately
  conservative: a real array's storage address never changes during one
  function activation, so every occurrence naming the same array `Slot`
  is hoisted unconditionally once 2+ occurrences exist, with no
  invalidation analysis needed. Needs a target's `TypeWidths` (to scale
  the index by the array's element width), so unlike the other passes
  this doesn't implement the `Optimizer` interface — `main.cpp` calls it
  directly, like `recomputeFrameLayout`. Runs after `LocalCSE` for the
  same canonicalization reason the passes below do.

- **`InductionNonNegative`**
  (`src/optimizers/induction_nonneg/InductionNonNegative.cpp`) — proves
  non-negativity for one narrow loop idiom: a Local `Slot` initialized to
  a non-negative literal and incremented only by a positive literal
  inside the one loop that owns it (the `for(i=0; i<N; i=i+1)`
  desugaring), with no other write to it and its address never taken
  anywhere in the function. Marks such a slot's `knownNonNegative` flag
  (`ir/IR.h`), which a backend's signed-comparison lowering may treat the
  same as a non-negative literal operand — `Pdp7Codegen.cpp`'s
  `genCompareCells()` does, letting `emitSignedLtCells()` skip that
  operand's own dynamic sign dispatch; `Pdp1Codegen.cpp`'s `emitLt()` does
  the equivalent skip on its own one's-complement, no-condition-code
  comparison lowering (neither `pdp10` nor `pdp11` need this: `pdp10`'s
  comparisons are a single native signed-jump instruction with no
  per-operand sign dispatch to skip, and `pdp11`'s condition codes make a
  comparison free regardless of sign). Deliberately narrow rather than a
  general value-range-analysis pass (see `docs/known-bugs.md`'s own
  PDP-7 entry for the general case this doesn't cover); conservative
  enough that two sequential loops in one function reusing the same
  counter variable disqualify each other, since each sees the other's
  init/increment as an unrelated write.

- **`GuardNonNegative`**
  (`src/optimizers/guard_nonneg/GuardNonNegative.cpp`) — proves
  non-negativity across an early-narrowing guard: a value that isn't a
  loop induction variable at all, but is only ever used after a dominating
  `if (slot >= 0) { ...uses of slot... }` (or `> 0`/`0 <= slot`/`0 <
  slot`, any operand order) that the rest of the function never escapes
  with a possibly-negative `slot`. Rather than real dominance analysis,
  stays sound the same narrow way `InductionNonNegative` does: requires
  every read of `slot` in the function (other than the one inside the
  guard's own condition) to lie textually inside that one `If`'s
  then-branch, and requires `slot` to have at most one write in the whole
  function - a `Param`'s implicit binding, or a `Local`'s own initializer
  - with no `&slot` taken anywhere. Marks the same `knownNonNegative` flag
  (`ir/IR.h`) `InductionNonNegative` does. Runs after `InductionNonNegative`
  so a slot it already proved non-negative is skipped here too; the two
  passes recognize disjoint idioms, so ordering between them otherwise
  doesn't matter.

- **`DeadStoreElimination`**
  (`src/optimizers/dead_store_elimination/DeadStoreElimination.cpp`) —
  removes a store to a scalar Local/Param whose value is never read
  again, closing the gap `ConstantPropagation` leaves open. Same
  address-never-taken restriction. Two cases, neither run to a
  fixpoint: a candidate written but never read has every store to it
  dropped; within one straight-line run of statements, a plain
  `slot = expr;` immediately followed (no intervening read) by another
  store to the same slot is dropped too. Runs before
  `DeadCodeElimination` so a `LocalDecl` it strips down to a bare,
  unread declaration looks unused to that pass too.

- **`DeadCodeElimination`** (`src/optimizers/dead_code_elimination/DeadCodeElimination.cpp`)
  — removes statements that can never run (anything after a `return`
  in the same block, bare `;` statements) and local variables that
  are declared but never read, written, or had their address taken,
  dropping their storage entirely unless their own initializer has a
  side effect (a call), in which case it's kept as a bare expression
  statement instead.

- **`IdenticalCodeFolding`**
  (`src/optimizers/identical_code_folding/IdenticalCodeFolding.cpp`) —
  merges functions with structurally identical bodies (up to
  alpha-renaming of Params/Locals) into one definition, redirecting
  every `Call`/`&fn` reference to a kept survivor. Changes which
  function a merged pair's `&fn` resolves to, so code comparing two
  originally-distinct function pointers for equality can observe the
  merge — the same tradeoff other compilers accept under `-Os`. Only
  a function with a body and no `@addr` pin participates, and only
  when `!allowExternal`: a separately compiled file might still
  reference a losing definition by name.

- **`UnusedGlobalElimination`**
  (`src/optimizers/unused_global_elimination/UnusedGlobalElimination.cpp`)
  — `DeadCodeElimination`'s own local-variable removal, extended to
  globals: drops a global `Slot` nothing reads, writes, or takes the
  address of, directly or (the dispatch-table idiom) through another
  still-live global's own array initializer. Same `!allowExternal`
  restriction as `IdenticalCodeFolding` — this subset has no `static`,
  so a global is implicitly external and another, separately compiled
  file could `extern` it. Also never removes an explicitly
  `@addr`-pinned global or a `Function`, even an uncalled one. Runs
  last, after `DeadCodeElimination`, so a global touched only from
  now-unreachable code correctly looks unused too.

Not yet attempted: a flow-sensitive (cross-block) CSE, which would need
dominator information (`src/ir/Dominators.h`) to know whether an earlier
computation still holds at a later use across a branch.

See `docs/ir.md` for the IR these passes operate on, its printed-text
grammar, and `IRValidator`'s self-check.
