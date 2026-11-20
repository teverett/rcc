#pragma once

#include "optimizers/Optimizer.h"

namespace rcc {

// Collapses an `if`/`while` whose condition ConstantFolding has already
// reduced to an IntLit: `if(1)`/`if(0)` becomes its taken branch (or Empty,
// dropping an absent one), and a `for`/`while` whose condition is always
// false becomes Empty - the loop never runs even once, so its postStmt
// doesn't either.
//
// Safety uses a real CFG (ir::buildCFG), not just a structural scan: the
// branch being dropped might still be a live jump target for a `goto`
// outside it (switch's own desugaring - see IR.h's StmtKind comment, and
// IRBuilder.cpp's buildSwitch - is the only source of those in this IR,
// since there's no surface-level goto or computed label address). A branch
// is only dropped once every Label inside it is proven unreachable with
// the branch's own structural edge removed from the graph: if some other,
// still-live edge reaches one of its Labels instead, the whole `if`/`while`
// is left untouched.
class ConstantConditionElimination : public Optimizer {
   public:
    std::string name() const override { return "constant-condition-elimination"; }
    bool run(ir::Program &prog, const std::string &sourceFile) const override;
};

}  // namespace rcc
