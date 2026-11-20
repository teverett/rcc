#pragma once

#include <unordered_map>

#include "CFG.h"

namespace rcc::ir {

// Immediate-dominator tree over one CFG, computed by Cooper/Harvey/Kennedy's
// iterative reverse-postorder dataflow algorithm rather than Lengauer-Tarjan
// - simpler to get right, and rcc's own CFGs (PDP-7/PDP-11 kernel-sized
// functions) are far too small for the asymptotic difference to matter.
// Must not outlive the CFG it was computed from (same non-owning-pointer
// convention as BasicBlock itself).
struct Dominators {
    const CFG *cfg = nullptr;

    // Every block reachable from cfg->entry maps to its immediate
    // dominator; cfg->entry maps to itself. A block buildCFG created that's
    // unreachable from entry (dead code after an unconditional jump) has no
    // entry here.
    std::unordered_map<const BasicBlock *, BasicBlock *> idom;

    // True if every path from cfg->entry to `b` passes through `a` (a block
    // dominates itself). False if either block is unreachable from entry.
    bool dominates(const BasicBlock *a, const BasicBlock *b) const;
};

Dominators computeDominators(const CFG &cfg);

}  // namespace rcc::ir
