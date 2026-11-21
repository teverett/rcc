#include "optimizers/guard_nonneg/GuardNonNegative.h"

#include <iostream>
#include <vector>

namespace rcc {

namespace {

bool isZeroLit(const ir::Expr *e) { return e && e->kind == ir::ExprKind::IntLit && e->intValue == 0; }

bool isVarOf(const ir::Expr *e, const ir::Slot *slot) { return e && e->kind == ir::ExprKind::Var && e->slot == slot; }

// `slot >= 0`, `slot > 0`, `0 <= slot` or `0 < slot` - every literal-vs-slot
// comparison form that makes the then-branch below unreachable with a
// negative `slot`. Returns the matched `Var` node (the one read this pass
// deliberately leaves alone - see GuardNonNegative.h) so the caller can
// exclude it from the "every other read is inside thenStmt" scan.
const ir::Expr *matchNonNegativeGuard(const ir::Expr *cond, const ir::Slot *slot) {
    if (!cond || cond->kind != ir::ExprKind::Binary) return nullptr;
    const ir::Expr *lhs = cond->lhs.get();
    const ir::Expr *rhs = cond->rhs.get();
    if ((cond->op == ">=" || cond->op == ">") && isVarOf(lhs, slot) && isZeroLit(rhs)) return lhs;
    if ((cond->op == "<=" || cond->op == "<") && isZeroLit(lhs) && isVarOf(rhs, slot)) return rhs;
    return nullptr;
}

void findIfStmts(ir::Stmt *s, std::vector<ir::Stmt *> &out) {
    if (!s) return;
    switch (s->kind) {
        case ir::StmtKind::Block:
            for (auto &c : s->body) findIfStmts(c.get(), out);
            return;
        case ir::StmtKind::If:
            out.push_back(s);
            findIfStmts(s->thenStmt.get(), out);
            findIfStmts(s->elseStmt.get(), out);
            return;
        case ir::StmtKind::While:
            findIfStmts(s->loopBody.get(), out);
            findIfStmts(s->postStmt.get(), out);
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

void collectExprs(const ir::Expr *e, std::vector<const ir::Expr *> &out) {
    if (!e) return;
    out.push_back(e);
    collectExprs(e->lhs.get(), out);
    collectExprs(e->rhs.get(), out);
    collectExprs(e->elseExpr.get(), out);
    for (auto &a : e->args) collectExprs(a.get(), out);
}

void collectExprs(const ir::Stmt *s, std::vector<const ir::Expr *> &out) {
    if (!s) return;
    switch (s->kind) {
        case ir::StmtKind::Block:
            for (auto &c : s->body) collectExprs(c.get(), out);
            return;
        case ir::StmtKind::If:
            collectExprs(s->cond.get(), out);
            collectExprs(s->thenStmt.get(), out);
            collectExprs(s->elseStmt.get(), out);
            return;
        case ir::StmtKind::While:
            collectExprs(s->cond.get(), out);
            collectExprs(s->loopBody.get(), out);
            collectExprs(s->postStmt.get(), out);
            return;
        case ir::StmtKind::LocalDecl:
        case ir::StmtKind::Return:
        case ir::StmtKind::ExprStmt:
            collectExprs(s->initOrReturnOrExpr.get(), out);
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

bool contains(const ir::Stmt *root, const ir::Expr *target) {
    std::vector<const ir::Expr *> all;
    collectExprs(root, all);
    for (const ir::Expr *e : all) {
        if (e == target) return true;
    }
    return false;
}

// Every write to `slot` anywhere in the function, plus whether `&slot` is
// taken anywhere - the same two checks InductionNonNegative.cpp's own
// WriteScan makes, duplicated here rather than shared: both passes are
// deliberately narrow, self-contained analyses over the same IR shapes, not
// a case for a shared utility.
struct WriteScan {
    std::vector<const ir::Expr *> writes;
    bool addressTaken = false;
};

bool isDirectWrite(const ir::Expr &e, const ir::Slot *slot) {
    if (e.lhs) return false;
    if (e.kind != ir::ExprKind::Assign && e.kind != ir::ExprKind::CompoundAssign) return false;
    return e.slot == slot;
}

void scanExprWrites(const ir::Expr *e, const ir::Slot *slot, WriteScan &scan) {
    if (!e) return;
    if (e->kind == ir::ExprKind::Unary && e->op == "&" && e->rhs && e->rhs->kind == ir::ExprKind::Var &&
        e->rhs->slot == slot) {
        scan.addressTaken = true;
    }
    if (isDirectWrite(*e, slot)) scan.writes.push_back(e);
    scanExprWrites(e->lhs.get(), slot, scan);
    scanExprWrites(e->rhs.get(), slot, scan);
    scanExprWrites(e->elseExpr.get(), slot, scan);
    for (auto &a : e->args) scanExprWrites(a.get(), slot, scan);
}

void scanStmtWrites(const ir::Stmt *s, const ir::Slot *slot, WriteScan &scan) {
    if (!s) return;
    switch (s->kind) {
        case ir::StmtKind::Block:
            for (auto &c : s->body) scanStmtWrites(c.get(), slot, scan);
            return;
        case ir::StmtKind::If:
            scanExprWrites(s->cond.get(), slot, scan);
            scanStmtWrites(s->thenStmt.get(), slot, scan);
            scanStmtWrites(s->elseStmt.get(), slot, scan);
            return;
        case ir::StmtKind::While:
            scanExprWrites(s->cond.get(), slot, scan);
            scanStmtWrites(s->loopBody.get(), slot, scan);
            scanStmtWrites(s->postStmt.get(), slot, scan);
            return;
        case ir::StmtKind::LocalDecl:
            if (s->declSlot == slot && s->initOrReturnOrExpr) scan.writes.push_back(s->initOrReturnOrExpr.get());
            return;
        case ir::StmtKind::Return:
        case ir::StmtKind::ExprStmt:
            scanExprWrites(s->initOrReturnOrExpr.get(), slot, scan);
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

bool isSoleAllowedWrite(const ir::Slot *slot, const WriteScan &scan) {
    if (scan.addressTaken) return false;
    if (slot->kind == ir::SlotKind::Param) return scan.writes.empty();
    if (slot->kind != ir::SlotKind::Local) return false;
    // A Local's only write may be its own declaration initializer - always
    // the first thing that can happen to it, since C has no use-before-
    // declaration - never a later Assign/CompoundAssign (that could put a
    // negative value back in after the guarded region, which this pass
    // never sees since it only looks inside the one matched `If`).
    return scan.writes.size() == 1 && scan.writes[0]->kind != ir::ExprKind::Assign &&
           scan.writes[0]->kind != ir::ExprKind::CompoundAssign;
}

void findReads(const ir::Stmt *s, const ir::Slot *slot, std::vector<const ir::Expr *> &out) {
    std::vector<const ir::Expr *> all;
    collectExprs(s, all);
    for (const ir::Expr *e : all) {
        if (e->kind == ir::ExprKind::Var && e->slot == slot) out.push_back(e);
    }
}

}  // namespace

bool GuardNonNegative::run(ir::Program &prog, const std::string &sourceFile, bool verbose) const {
    bool changed = false;
    for (auto &fb : prog.functionBodies) {
        if (!fb.body || fb.body->kind != ir::StmtKind::Block) continue;

        std::vector<ir::Stmt *> ifStmts;
        findIfStmts(fb.body.get(), ifStmts);

        for (ir::Stmt *ifStmt : ifStmts) {
            if (!ifStmt->thenStmt) continue;

            // `cond`'s own slot isn't resolved by matchNonNegativeGuard()
            // directly - it needs a Slot*, not an Expr*, so try every Local/
            // Param slot this If's condition could plausibly name. In
            // practice a comparison only ever names one slot on one side;
            // trying the cond's own lhs/rhs Var (if any) covers it without
            // iterating the whole program's slot table.
            const ir::Expr *condLhs = ifStmt->cond ? ifStmt->cond->lhs.get() : nullptr;
            const ir::Expr *condRhs = ifStmt->cond ? ifStmt->cond->rhs.get() : nullptr;
            ir::Slot *candidate = nullptr;
            if (condLhs && condLhs->kind == ir::ExprKind::Var) candidate = condLhs->slot;
            if (!candidate && condRhs && condRhs->kind == ir::ExprKind::Var) candidate = condRhs->slot;
            if (!candidate) continue;
            if (candidate->kind != ir::SlotKind::Local && candidate->kind != ir::SlotKind::Param) continue;
            if (candidate->arraySize != 0) continue;
            if (candidate->knownNonNegative) continue;

            const ir::Expr *guardRead = matchNonNegativeGuard(ifStmt->cond.get(), candidate);
            if (!guardRead) continue;

            std::vector<const ir::Expr *> reads;
            findReads(fb.body.get(), candidate, reads);
            bool allInsideGuard = true;
            for (const ir::Expr *r : reads) {
                if (r == guardRead) continue;
                if (!contains(ifStmt->thenStmt.get(), r)) {
                    allInsideGuard = false;
                    break;
                }
            }
            if (!allInsideGuard) continue;

            WriteScan scan;
            scanStmtWrites(fb.body.get(), candidate, scan);
            if (!isSoleAllowedWrite(candidate, scan)) continue;

            candidate->knownNonNegative = true;
            changed = true;
            if (verbose) {
                std::cerr << sourceFile << ": guard-nonneg: fn " << fb.func->sourceName << ": proved `"
                          << candidate->sourceName << "` stays non-negative after its `>= 0` guard\n";
            }
        }
    }
    return changed;
}

}  // namespace rcc
