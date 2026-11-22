#pragma once

#include "optimizers/Optimizer.h"

namespace rcc {

class Backend;

// Local common-subexpression/redundant-load elimination: purely syntactic
// (not flow-sensitive) matching of identical Binary/Index subexpressions
// within one Block's own statement list (an If/While's `cond` belongs to
// its enclosing block; its then/else/loop body is its own separate scope).
//
// On a second identical, side-effect-free occurrence, the first occurrence
// is wrapped in place with an assignment into a fresh temporary (so nothing
// is reordered), and every later occurrence becomes a plain read of it.
//
// A recorded occurrence is dropped (or, if already promoted, invalidated)
// the moment something could change its value: an assignment to a Slot it
// depends on invalidates only that occurrence; a Call/CallPtr/Intrinsic/
// DerefAssign conservatively invalidates everything recorded in the scope.
//
// Deliberately narrow: only Binary/Index are candidates (Var/IntLit are
// already as cheap as a temp read; Unary is one cheap instruction); matches
// `a + b` against `a + b` only, never `b + a`, even for a commutative
// operator; and since a nested subexpression is resolved before its
// enclosing one is examined, a verbatim-repeated composite like
// `(a[i]+1) * (a[i]+1)` only gets its inner `a[i]` shared, not the outer
// `...+1` — a known gap this single, non-fixpoint pass doesn't close.
//
// A promoted expression's --dump-ir text can look ambiguous (e.g.
// `(x + tmp = y)` for `((tmp = y) + tmp)`, since IRPrinter's Assign case
// doesn't self-parenthesize) — cosmetic only; codegen and IRValidator work
// from the real tree.
class LocalCSE : public Optimizer {
   public:
    std::string name() const override { return "local-cse"; }
    bool run(ir::Program &prog, const std::string &sourceFile, bool verbose) const override;

    // Same pass, with one more candidate kind: a plain Var read, but
    // ONLY where `backend.isLocalReadExpensive(slot, fn, body)` says
    // re-reading it costs real instructions on that target (see
    // Codegen.h's own comment on that hook - false on most targets,
    // where a local already lives in a register or an equally cheap
    // fixed cell, so caching its read would only add overhead). Not a
    // second, independent pass: it's the exact same traversal/
    // invalidation machinery `run()` uses, just with this one
    // additional per-slot check consulted too - calling both `run()`
    // and this on the same Program would just redo the Binary/Index
    // work for nothing, not find anything new.
    bool runWithExpensiveVarCache(ir::Program &prog, const std::string &sourceFile, bool verbose,
                                   const Backend &backend) const;
};

}  // namespace rcc
