#pragma once

#include "optimizers/Optimizer.h"

namespace rcc {

// Proves non-negativity across an early-return/narrowing guard, the idiom
// InductionNonNegative.h's own header lists as still open: a value that
// isn't a loop induction variable at all, but is only ever used after a
// dominating `if (slot >= 0) { ...uses of slot... }` (or `> 0`/`0 <=
// slot`/`0 < slot`, any operand order) that the rest of the function never
// escapes with a possibly-negative `slot`.
//
// Rather than real dominance analysis, this stays sound the same narrow way
// InductionNonNegative does: it requires every read of `slot` anywhere in
// the function (other than the one inside the guard's own condition, which
// this pass isn't trying to speed up) to lie textually inside that one
// `If`'s then-branch, and requires `slot` to have at most one write in the
// whole function - a Param's implicit binding (no write at all), or a
// Local's own initializer (`int slot = <expr>;`, which always precedes
// every use by construction, since C has no "use before declaration") -
// with no `&slot` taken anywhere. Under those conditions the guard's
// then-branch is the only place `slot` is ever read, and every reach of it
// already passed the `>= 0` check, so marking `slot->knownNonNegative`
// (see IR.h) is sound for the slot's entire lifetime, the same flag
// Pdp7Codegen.cpp's signed-comparison lowering already knows how to use.
//
// Still doesn't attempt a real value-range-analysis pass (see
// docs/known-bugs.md's own PDP-7 entry): a slot used both inside and
// outside such a guard, a range narrower than "non-negative", or a bound
// proven by anything other than one of these four literal-against-slot
// comparison forms, are all left alone.
class GuardNonNegative : public Optimizer {
   public:
    std::string name() const override { return "guard-nonneg"; }
    bool run(ir::Program &prog, const std::string &sourceFile, bool verbose) const override;
};

}  // namespace rcc
