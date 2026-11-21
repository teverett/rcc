#pragma once

#include "optimizers/Optimizer.h"

namespace rcc {

// Proves non-negativity for two narrow, common loop idioms, counting up:
//
//     slot = <non-negative literal>;
//     while (<cond involving slot>) { <body>; slot = slot + <positive literal>; }
//
// (the `for(slot=0; slot<N; slot=slot+1)` desugaring - see BuildAst.cpp's
// ForStmtContext handling) - safe unconditionally, since starting
// non-negative and only ever increasing can never go negative - or counting
// down:
//
//     slot = <non-negative literal>;
//     while (slot > C /* or slot >= C */) { <body>; slot = slot - <positive literal step>; }
//
// safe only when the guard already forces `slot` high enough to absorb the
// decrement without crossing zero (`C+1 >= step` for `>`, `C >= step` for
// `>=` - see matchDecrementGuardBound()), since nothing else bounds how far
// down a decrement could otherwise reach.
//
// When a Local `slot` is initialized this way and the only writes to it
// anywhere in the function are that initializer and that increment/decrement
// - no other assignment, no `&slot` taken anywhere - its value can never go
// negative, so this sets `slot->knownNonNegative` (see IR.h), which
// Pdp7Codegen.cpp's signed-comparison lowering already knows how to use the
// same way it already uses a literal operand's sign.
//
// Deliberately narrow rather than a general value-range-analysis pass (see
// docs/known-bugs.md's own PDP-7 entry for why that's a much bigger
// undertaking): this only recognizes the two exact shapes above, and
// conservatively requires the whole function to have no other write/
// address-of for that slot - including a second, unrelated loop later in
// the same function reusing the same counter variable, which this pass
// leaves alone rather than trying to scope per-loop. It also says nothing
// about a sign proven across an `if` guard, or any value range narrower
// than "always non-negative from zero" that isn't one of these two loop
// shapes - Pdp7Codegen.cpp's own hasStaticSign() picks up a sound sliver of
// that separately, for boolean-valued expressions, without needing this
// pass's help.
class InductionNonNegative : public Optimizer {
   public:
    std::string name() const override { return "induction-nonneg"; }
    bool run(ir::Program &prog, const std::string &sourceFile, bool verbose) const override;
};

}  // namespace rcc
