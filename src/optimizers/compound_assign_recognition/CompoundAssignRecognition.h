#pragma once

#include "optimizers/Optimizer.h"

namespace rcc {

// Recognizes a compound assignment hand-desugared into its own two
// separate reads of the same target - `t = t <op> e`, or `*p = *p
// <op> e` - and rewrites it into the real `CompoundAssign`/
// `CompoundDerefAssign` IR node, which every backend already computes
// the target's own address/pointer for exactly once (see IR.h's own
// `ExprKind::CompoundAssign` comment: "kept as one node ... because
// the index/pointer sub-expression must be computed once"), instead
// of the desugared form's two independent address computations (one
// for each textual occurrence of `t`/`*p`).
//
// Only ever folds the two reads together when they're the exact same
// side-effect-free expression (see `sameSideEffectFreeExpr()` in the
// .cpp - a narrow, explicit vocabulary that excludes any Call or
// write, so it can never be fooled into merging two reads that could
// actually observe or produce different values) - this is what makes
// evaluating it once, as the compound form does, indistinguishable
// from the original's own two separate evaluations.
class CompoundAssignRecognition : public Optimizer {
   public:
    std::string name() const override { return "compound-assign-recognition"; }
    bool run(ir::Program &prog, const std::string &sourceFile) const override;
};

}  // namespace rcc
