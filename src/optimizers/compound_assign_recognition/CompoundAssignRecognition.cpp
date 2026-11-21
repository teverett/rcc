#include "optimizers/compound_assign_recognition/CompoundAssignRecognition.h"

#include <iostream>
#include <string>

namespace rcc {

namespace {

bool isCommutative(const std::string &op) { return op == "+" || op == "*" || op == "&" || op == "^" || op == "|"; }

// Every Binary op that can legally sit on a real assignment operator
// (`+=`, `-=`, ...) - every arithmetic/bitwise op this project's own
// grammar has, excluding the comparison/logical ones, which no C
// assignment operator exists for in the first place.
bool isAssignableOp(const std::string &op) {
    return op == "+" || op == "-" || op == "*" || op == "/" || op == "%" || op == "<<" || op == ">>" ||
           op == "&" || op == "^" || op == "|";
}

// Whether `a`/`b` are the exact same side-effect-free expression - the
// one thing that makes `t = t <op> e` (or `*p = *p <op> e`) safe to
// fold into `t <op>= e`: evaluating the matched sub-expression only
// once, as the compound form does, has to be indistinguishable from
// the original's own two separate evaluations. Deliberately narrow:
// the node kinds matched here can never read memory something else
// just wrote, call out to anything, or otherwise differ between two
// textually identical evaluations - a `Call`, `Assign`,
// `CompoundAssign`, or anything else not explicitly listed falls
// through to the conservative `false` default instead.
bool sameSideEffectFreeExpr(const ir::Expr *a, const ir::Expr *b) {
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;
    switch (a->kind) {
        case ir::ExprKind::IntLit:
            return a->intValue == b->intValue;
        case ir::ExprKind::Var:
            return a->slot == b->slot;
        case ir::ExprKind::Index:
            return a->slot == b->slot && sameSideEffectFreeExpr(a->lhs.get(), b->lhs.get());
        case ir::ExprKind::Unary:
            return a->op == b->op && sameSideEffectFreeExpr(a->rhs.get(), b->rhs.get());
        case ir::ExprKind::Binary:
            return a->op == b->op && sameSideEffectFreeExpr(a->lhs.get(), b->lhs.get()) &&
                   sameSideEffectFreeExpr(a->rhs.get(), b->rhs.get());
        case ir::ExprKind::Convert:
            return sameSideEffectFreeExpr(a->lhs.get(), b->lhs.get());
        default:
            return false;
    }
}

// Whether `e` alone is side-effect-free (no `Call`/`Assign`/
// `CompoundAssign`/anything else outside `sameSideEffectFreeExpr()`'s
// own whitelist). Required of the operand this pass *keeps* as the
// compound form's own new rhs, not just the one it folds away: C
// itself never orders a binary operator's two operands relative to
// each other, but a real implementation still has to pick ONE fixed
// order for each shape it emits, and `t = t <op> call()` vs `t <op>=
// call()` might not pick the *same* one - if `call()` has a side
// effect that `t`'s own stored value could observe (e.g. writing `t`
// itself), evaluating it before or after reading `t`'s old value
// gives a genuinely different, observable result. Never a risk when
// both operands are already known pure, so this only ever blocks a
// rewrite this pass would otherwise wrongly allow.
bool isSideEffectFree(const ir::Expr *e) {
    if (!e) return true;
    switch (e->kind) {
        case ir::ExprKind::IntLit:
        case ir::ExprKind::Var:
            return true;
        case ir::ExprKind::Index:
            return isSideEffectFree(e->lhs.get());
        case ir::ExprKind::Unary:
            return isSideEffectFree(e->rhs.get());
        case ir::ExprKind::Binary:
            return isSideEffectFree(e->lhs.get()) && isSideEffectFree(e->rhs.get());
        case ir::ExprKind::Convert:
            return isSideEffectFree(e->lhs.get());
        default:
            return false;
    }
}

bool isSameScalarRead(const ir::Expr *e, const ir::Slot *slot) {
    return e && e->kind == ir::ExprKind::Var && e->slot == slot;
}

bool isSameIndexRead(const ir::Expr *e, const ir::Slot *slot, const ir::Expr *index) {
    return e && e->kind == ir::ExprKind::Index && e->slot == slot && sameSideEffectFreeExpr(e->lhs.get(), index);
}

bool isSameDerefRead(const ir::Expr *e, const ir::Expr *ptr) {
    return e && e->kind == ir::ExprKind::Unary && e->op == "*" && sameSideEffectFreeExpr(e->rhs.get(), ptr);
}

// `rhs` is the Binary being folded; `isTargetRead` recognizes "a read
// of the exact target this assignment writes" for whichever of the
// three shapes (scalar/indexed/pointee) applies. Self-reference on the
// Binary's own left operand folds for any op (`t = t - e` -> `t -=
// e`); on the right, only for a commutative op (`t = e - t` is NOT
// `t -= e`, but `t = e + t` IS `t += e`). Returns the one operand to
// keep as the compound form's own new rhs, or nullptr if neither side
// matches at all.
template <typename IsTargetRead>
ir::Expr *matchSelfOperand(ir::Expr &rhs, IsTargetRead isTargetRead) {
    if (isTargetRead(rhs.lhs.get())) return rhs.rhs.get();
    if (isCommutative(rhs.op) && isTargetRead(rhs.rhs.get())) return rhs.lhs.get();
    return nullptr;
}

void recognizeInExpr(ir::ExprPtr &e, bool &changed, const std::string &sourceFile, const ir::Function *fn,
                      bool verbose) {
    if (!e) return;
    recognizeInExpr(e->lhs, changed, sourceFile, fn, verbose);
    recognizeInExpr(e->rhs, changed, sourceFile, fn, verbose);
    recognizeInExpr(e->elseExpr, changed, sourceFile, fn, verbose);
    for (auto &a : e->args) recognizeInExpr(a, changed, sourceFile, fn, verbose);

    if (e->kind != ir::ExprKind::Assign && e->kind != ir::ExprKind::DerefAssign) return;
    if (!e->rhs || e->rhs->kind != ir::ExprKind::Binary || !isAssignableOp(e->rhs->op)) return;
    // Never for a wide (`long`/`long long`) target: "wide" is relative
    // to each backend's own native width (confirmed directly - this
    // exact rewrite broke real-value, ordinary-looking wide arithmetic
    // like `t = t + 1` on `long t`, a genuine compile error on the
    // pdp7 target, not just a missed optimization), and this pass runs
    // once, backend-independently, with no target width to check
    // against. `Long`/`LongLong` are the one universally-safe signal
    // every backend agrees could be wide, so excluding them here,
    // unconditionally, is the only check that doesn't need one.
    if (e->type.base == BaseType::Long || e->type.base == BaseType::LongLong) return;

    ir::Expr *newRhs;
    if (e->kind == ir::ExprKind::Assign) {
        if (e->lhs) {
            ir::Expr *index = e->lhs.get();
            newRhs =
                matchSelfOperand(*e->rhs, [&](const ir::Expr *x) { return isSameIndexRead(x, e->slot, index); });
        } else {
            newRhs = matchSelfOperand(*e->rhs, [&](const ir::Expr *x) { return isSameScalarRead(x, e->slot); });
        }
    } else {
        ir::Expr *ptr = e->lhs.get();
        newRhs = matchSelfOperand(*e->rhs, [&](const ir::Expr *x) { return isSameDerefRead(x, ptr); });
    }
    if (!newRhs || !isSideEffectFree(newRhs)) return;

    bool isScalarOrIndex = e->kind == ir::ExprKind::Assign;
    if (verbose) {
        std::cerr << sourceFile << ": compound-assign-recognition: fn " << fn->sourceName << ": line " << e->line
                   << ": folded a hand-desugared `" << (isScalarOrIndex ? "t = t" : "*p = *p") << " " << e->rhs->op
                   << " <expr>` into the equivalent compound assignment, now computing "
                   << (isScalarOrIndex ? "the target's own address" : "the pointer") << " once instead of twice\n";
    }

    std::string op = e->rhs->op;
    ir::ExprPtr keptRhs = (e->rhs->lhs.get() == newRhs) ? std::move(e->rhs->lhs) : std::move(e->rhs->rhs);
    e->kind = isScalarOrIndex ? ir::ExprKind::CompoundAssign : ir::ExprKind::CompoundDerefAssign;
    e->op = op;
    e->rhs = std::move(keptRhs);
    changed = true;
}

void recognizeInStmt(ir::Stmt *s, bool &changed, const std::string &sourceFile, const ir::Function *fn,
                      bool verbose) {
    if (!s) return;
    switch (s->kind) {
        case ir::StmtKind::Block:
            for (auto &c : s->body) recognizeInStmt(c.get(), changed, sourceFile, fn, verbose);
            return;
        case ir::StmtKind::If:
            recognizeInExpr(s->cond, changed, sourceFile, fn, verbose);
            recognizeInStmt(s->thenStmt.get(), changed, sourceFile, fn, verbose);
            recognizeInStmt(s->elseStmt.get(), changed, sourceFile, fn, verbose);
            return;
        case ir::StmtKind::While:
            recognizeInExpr(s->cond, changed, sourceFile, fn, verbose);
            recognizeInStmt(s->loopBody.get(), changed, sourceFile, fn, verbose);
            recognizeInStmt(s->postStmt.get(), changed, sourceFile, fn, verbose);
            return;
        case ir::StmtKind::Return:
        case ir::StmtKind::LocalDecl:
        case ir::StmtKind::ExprStmt:
            recognizeInExpr(s->initOrReturnOrExpr, changed, sourceFile, fn, verbose);
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

bool CompoundAssignRecognition::run(ir::Program &prog, const std::string &sourceFile, bool verbose) const {
    bool changed = false;
    for (auto &fb : prog.functionBodies) recognizeInStmt(fb.body.get(), changed, sourceFile, fb.func, verbose);
    return changed;
}

}  // namespace rcc
