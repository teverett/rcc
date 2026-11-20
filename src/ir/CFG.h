#pragma once

#include <memory>
#include <vector>

#include "IR.h"

namespace rcc::ir {

// A control-flow graph over one FunctionBody's Stmt tree. Blocks hold
// non-owning pointers into that tree - this is purely an analysis overlay,
// built fresh by an optimizer that needs it and discarded before the next
// mutation (there's no incremental update if the Stmt tree changes).
struct BasicBlock {
    int id = 0;

    // Straight-line statements in execution order: LocalDecl, ExprStmt,
    // Asm, Empty. Block/If/While/Return/Break/Continue/Label/Goto never
    // appear here - they're what a block boundary is drawn at.
    std::vector<Stmt *> stmts;

    enum class Term {
        // Falls into `next` unconditionally with no branch condition - the
        // ordinary end of a straight-line block, and a Label's incoming edge.
        Fallthrough,
        // Evaluates `cond`; true takes `branchTarget`, false takes `next`.
        Branch,
        // Unconditional jump to `next` - a Goto, Break, or Continue, or a
        // loop body's edge back to its header.
        Jump,
        // Returns from the function; edge to the owning CFG's `exit`,
        // carried in `next` the same as any other single-successor terminator.
        Return,
        // No outgoing edge - only ever the owning CFG's `exit` block.
        Unreachable,
    };

    Term term = Term::Unreachable;
    Expr *cond = nullptr;  // Branch only
    BasicBlock *next = nullptr;
    BasicBlock *branchTarget = nullptr;  // Branch only
    std::vector<BasicBlock *> preds;
};

struct CFG {
    Function *func = nullptr;
    std::vector<std::unique_ptr<BasicBlock>> blocks;  // owns every block, entry/exit included
    BasicBlock *entry = nullptr;
    BasicBlock *exit = nullptr;  // synthetic, no stmts; every Return and fall-off-the-end edge lands here
};

// Builds a CFG for one function body. The Stmt tree `fb` points into must
// outlive the returned CFG. `switch`'s desugared Label/Goto pairs (see
// IR.h's own StmtKind comment) are resolved by a first pass over the tree
// that pre-creates each Label's block, so a Goto to a not-yet-visited
// label still finds (and links to) the right BasicBlock.
CFG buildCFG(const FunctionBody &fb);

}  // namespace rcc::ir
