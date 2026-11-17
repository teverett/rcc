#include "optimizers/induction_nonneg/InductionNonNegative.h"

#include <iostream>
#include <vector>

namespace rcc {

namespace {

bool isPositiveIntLit(const ir::Expr *e) { return e && e->kind == ir::ExprKind::IntLit && e->intValue > 0; }

// `slot = <rhs>` or `slot (op=) <rhs>` for a bare scalar `slot` (not an
// indexed/array write - those arrive with `lhs` set, see IR.h's own
// Assign/CompoundAssign comment).
bool isDirectWrite(const ir::Expr &e, const ir::Slot *slot) {
    if (e.lhs) return false;
    if (e.kind != ir::ExprKind::Assign && e.kind != ir::ExprKind::CompoundAssign) return false;
    return e.slot == slot;
}

// `slot = slot + <positive literal>` or `slot = <positive literal> + slot`
// (the `for(...; slot = slot + 1)` desugaring), or the equivalent
// `slot += <positive literal>` CompoundAssign. Returns the matched Expr
// (always the top-level Assign/CompoundAssign) so the caller can recognize
// this exact occurrence later as an allowed write, not a conflicting one.
ir::Expr *matchIncrement(ir::Stmt *post, ir::Slot *slot) {
    if (!post || post->kind != ir::StmtKind::ExprStmt) return nullptr;
    ir::Expr *e = post->initOrReturnOrExpr.get();
    if (!e) return nullptr;
    if (e->kind == ir::ExprKind::CompoundAssign && isDirectWrite(*e, slot) && e->op == "+" &&
        isPositiveIntLit(e->rhs.get())) {
        return e;
    }
    if (e->kind == ir::ExprKind::Assign && isDirectWrite(*e, slot) && e->rhs && e->rhs->kind == ir::ExprKind::Binary &&
        e->rhs->op == "+") {
        const ir::Expr *lhs = e->rhs->lhs.get();
        const ir::Expr *rhs = e->rhs->rhs.get();
        bool lhsIsSlot = lhs && lhs->kind == ir::ExprKind::Var && lhs->slot == slot;
        bool rhsIsSlot = rhs && rhs->kind == ir::ExprKind::Var && rhs->slot == slot;
        if ((lhsIsSlot && isPositiveIntLit(rhs)) || (rhsIsSlot && isPositiveIntLit(lhs))) return e;
    }
    return nullptr;
}

// `slot = slot - <positive literal>` (only this operand order - subtraction
// isn't commutative, so `<lit> - slot` is not a decrement of `slot`), or the
// equivalent `slot -= <positive literal>` CompoundAssign. Returns the
// matched Expr the same way matchIncrement() does, and the literal step
// subtracted each time.
ir::Expr *matchDecrement(ir::Stmt *post, ir::Slot *slot, long &step) {
    if (!post || post->kind != ir::StmtKind::ExprStmt) return nullptr;
    ir::Expr *e = post->initOrReturnOrExpr.get();
    if (!e) return nullptr;
    if (e->kind == ir::ExprKind::CompoundAssign && isDirectWrite(*e, slot) && e->op == "-" &&
        isPositiveIntLit(e->rhs.get())) {
        step = e->rhs->intValue;
        return e;
    }
    if (e->kind == ir::ExprKind::Assign && isDirectWrite(*e, slot) && e->rhs && e->rhs->kind == ir::ExprKind::Binary &&
        e->rhs->op == "-") {
        const ir::Expr *lhs = e->rhs->lhs.get();
        const ir::Expr *rhs = e->rhs->rhs.get();
        bool lhsIsSlot = lhs && lhs->kind == ir::ExprKind::Var && lhs->slot == slot;
        if (lhsIsSlot && isPositiveIntLit(rhs)) {
            step = rhs->intValue;
            return e;
        }
    }
    return nullptr;
}

// A decrementing loop's guard, `slot > <literal>` or `slot >= <literal>`,
// proves `slot` stays non-negative after a matched decrement only if the
// guard already forces `slot` high enough to absorb that decrement without
// crossing zero: whenever the guard lets the body (and so the decrement)
// run, `slot` is at least `bound` - `C+1` for `slot > C`, `C` for
// `slot >= C`. The caller still has to check `bound >= step` itself.
bool matchDecrementGuardBound(const ir::Expr *cond, const ir::Slot *slot, long &bound) {
    if (!cond || cond->kind != ir::ExprKind::Binary) return false;
    const ir::Expr *lhs = cond->lhs.get();
    const ir::Expr *rhs = cond->rhs.get();
    if (!lhs || lhs->kind != ir::ExprKind::Var || lhs->slot != slot) return false;
    if (!rhs || rhs->kind != ir::ExprKind::IntLit) return false;
    if (cond->op == ">") {
        bound = rhs->intValue + 1;
        return true;
    }
    if (cond->op == ">=") {
        bound = rhs->intValue;
        return true;
    }
    return false;
}

bool exprReferencesSlot(const ir::Expr *e, const ir::Slot *slot) {
    if (!e) return false;
    if (e->slot == slot) return true;
    if (exprReferencesSlot(e->lhs.get(), slot)) return true;
    if (exprReferencesSlot(e->rhs.get(), slot)) return true;
    if (exprReferencesSlot(e->elseExpr.get(), slot)) return true;
    for (auto &a : e->args) {
        if (exprReferencesSlot(a.get(), slot)) return true;
    }
    return false;
}

// `slot = <non-negative literal>;`, the `for(slot=0; ...)` desugaring's
// own init statement (see BuildAst.cpp's ForStmtContext handling).
bool matchInit(ir::Stmt *s, ir::Slot *&slot, ir::Expr *&initExpr) {
    if (!s || s->kind != ir::StmtKind::ExprStmt) return false;
    ir::Expr *e = s->initOrReturnOrExpr.get();
    if (!e || e->kind != ir::ExprKind::Assign || e->lhs) return false;
    if (!e->slot || e->slot->kind != ir::SlotKind::Local || e->slot->arraySize != 0) return false;
    if (!e->rhs || e->rhs->kind != ir::ExprKind::IntLit || e->rhs->intValue < 0) return false;
    slot = e->slot;
    initExpr = e;
    return true;
}

struct Candidate {
    ir::Slot *slot;
    ir::Expr *initExpr;
    ir::Expr *incExpr;
};

void findCandidates(ir::Stmt *s, std::vector<Candidate> &out) {
    if (!s) return;
    switch (s->kind) {
        case ir::StmtKind::Block:
            for (size_t i = 0; i < s->body.size(); ++i) {
                if (i + 1 < s->body.size() && s->body[i + 1]->kind == ir::StmtKind::While) {
                    ir::Slot *slot = nullptr;
                    ir::Expr *initExpr = nullptr;
                    if (matchInit(s->body[i].get(), slot, initExpr)) {
                        ir::Stmt *whileStmt = s->body[i + 1].get();
                        if (exprReferencesSlot(whileStmt->cond.get(), slot)) {
                            if (ir::Expr *incExpr = matchIncrement(whileStmt->postStmt.get(), slot)) {
                                out.push_back({slot, initExpr, incExpr});
                            } else {
                                long step = 0;
                                if (ir::Expr *decExpr = matchDecrement(whileStmt->postStmt.get(), slot, step)) {
                                    long bound = 0;
                                    if (matchDecrementGuardBound(whileStmt->cond.get(), slot, bound) &&
                                        bound >= step) {
                                        out.push_back({slot, initExpr, decExpr});
                                    }
                                }
                            }
                        }
                    }
                }
                findCandidates(s->body[i].get(), out);
            }
            return;
        case ir::StmtKind::If:
            findCandidates(s->thenStmt.get(), out);
            findCandidates(s->elseStmt.get(), out);
            return;
        case ir::StmtKind::While:
            findCandidates(s->loopBody.get(), out);
            findCandidates(s->postStmt.get(), out);
            return;
        case ir::StmtKind::Return:
        case ir::StmtKind::LocalDecl:
        case ir::StmtKind::ExprStmt:
        case ir::StmtKind::Break:
        case ir::StmtKind::Continue:
        case ir::StmtKind::Empty:
        case ir::StmtKind::Asm:
        case ir::StmtKind::Label:
        case ir::StmtKind::Goto:
            return;
    }
}

struct WriteScan {
    std::vector<ir::Expr *> writes;
    bool addressTaken = false;
};

void scanExpr(ir::Expr *e, const ir::Slot *slot, WriteScan &scan) {
    if (!e) return;
    if (e->kind == ir::ExprKind::Unary && e->op == "&" && e->rhs && e->rhs->kind == ir::ExprKind::Var &&
        e->rhs->slot == slot) {
        scan.addressTaken = true;
    }
    if (isDirectWrite(*e, slot)) scan.writes.push_back(e);
    scanExpr(e->lhs.get(), slot, scan);
    scanExpr(e->rhs.get(), slot, scan);
    scanExpr(e->elseExpr.get(), slot, scan);
    for (auto &a : e->args) scanExpr(a.get(), slot, scan);
}

void scanStmt(ir::Stmt *s, const ir::Slot *slot, WriteScan &scan) {
    if (!s) return;
    switch (s->kind) {
        case ir::StmtKind::Block:
            for (auto &c : s->body) scanStmt(c.get(), slot, scan);
            return;
        case ir::StmtKind::If:
            scanExpr(s->cond.get(), slot, scan);
            scanStmt(s->thenStmt.get(), slot, scan);
            scanStmt(s->elseStmt.get(), slot, scan);
            return;
        case ir::StmtKind::While:
            scanExpr(s->cond.get(), slot, scan);
            scanStmt(s->loopBody.get(), slot, scan);
            scanStmt(s->postStmt.get(), slot, scan);
            return;
        case ir::StmtKind::LocalDecl:
            // This slot's own one-time declaration. An initializer here is
            // a write distinct from the matched for-loop's own init/incr
            // (those are separate statements - see matchInit()), so it's
            // only ever safe when this slot has no initializer at all.
            if (s->declSlot == slot && s->initOrReturnOrExpr) scan.writes.push_back(s->initOrReturnOrExpr.get());
            return;
        case ir::StmtKind::Return:
        case ir::StmtKind::ExprStmt:
            scanExpr(s->initOrReturnOrExpr.get(), slot, scan);
            return;
        case ir::StmtKind::Break:
        case ir::StmtKind::Continue:
        case ir::StmtKind::Empty:
        case ir::StmtKind::Asm:
        case ir::StmtKind::Label:
        case ir::StmtKind::Goto:
            return;
    }
}

}  // namespace

bool InductionNonNegative::run(ir::Program &prog, const std::string &sourceFile) const {
    bool changed = false;
    for (auto &fb : prog.functionBodies) {
        if (!fb.body || fb.body->kind != ir::StmtKind::Block) continue;

        std::vector<Candidate> candidates;
        findCandidates(fb.body.get(), candidates);

        for (Candidate &c : candidates) {
            if (c.slot->knownNonNegative) continue;

            WriteScan scan;
            scanStmt(fb.body.get(), c.slot, scan);
            if (scan.addressTaken) continue;

            bool safe = true;
            for (ir::Expr *w : scan.writes) {
                if (w != c.initExpr && w != c.incExpr) {
                    safe = false;
                    break;
                }
            }
            if (!safe) continue;

            c.slot->knownNonNegative = true;
            changed = true;
            std::cerr << sourceFile << ": induction-nonneg: fn " << fb.func->sourceName << ": proved `"
                      << c.slot->sourceName << "` stays non-negative\n";
        }
    }
    return changed;
}

}  // namespace rcc
