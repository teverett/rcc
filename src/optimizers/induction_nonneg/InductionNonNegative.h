#pragma once

#include "optimizers/Optimizer.h"

namespace rcc {

// Proves non-negativity for one narrow, common loop idiom:
//
//     slot = <non-negative literal>;
//     while (<cond involving slot>) { <body>; slot = slot + <positive literal>; }
//
// (the `for(slot=0; slot<N; slot=slot+1)` desugaring - see BuildAst.cpp's
// ForStmtContext handling). When a Local `slot` is initialized this way and
// the only writes to it anywhere in the function are that initializer and
// that increment - no other assignment, no `&slot` taken anywhere - its
// value can never go negative, so this sets `slot->knownNonNegative` (see
// IR.h), which Pdp7Codegen.cpp's signed-comparison lowering already knows
// how to use the same way it already uses a literal operand's sign.
//
// Deliberately narrow rather than a general value-range-analysis pass (see
// docs/known-bugs.md's own PDP-7 entry for why that's a much bigger
// undertaking): this only recognizes the exact shape above, literal-init
// and literal-increment, and conservatively requires the whole function to
// have no other write/address-of for that slot - including a second,
// unrelated loop later in the same function reusing the same counter
// variable, which this pass leaves alone rather than trying to scope
// per-loop.
class InductionNonNegative : public Optimizer {
   public:
    std::string name() const override { return "induction-nonneg"; }
    bool run(ir::Program &prog, const std::string &sourceFile) const override;
};

}  // namespace rcc
