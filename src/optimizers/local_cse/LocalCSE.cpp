#include "optimizers/local_cse/LocalCSE.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "codegen/Codegen.h"

namespace rcc {

namespace {

// Same as DeadCodeElimination.cpp's hasSideEffect, plus CallPtr.
bool hasSideEffect(const ir::Expr *e) {
    if (!e) return false;
    switch (e->kind) {
        case ir::ExprKind::Call:
        case ir::ExprKind::CallPtr:
        case ir::ExprKind::Intrinsic:
        case ir::ExprKind::Assign:
        case ir::ExprKind::DerefAssign:
        case ir::ExprKind::CompoundAssign:
        case ir::ExprKind::CompoundDerefAssign:
            return true;
        default:
            break;
    }
    if (hasSideEffect(e->lhs.get())) return true;
    if (hasSideEffect(e->rhs.get())) return true;
    for (const auto &arg : e->args) {
        if (hasSideEffect(arg.get())) return true;
    }
    return false;
}

// `backend`/`fn`/`body` are non-null exactly from
// runWithExpensiveVarCache() - `run()` passes nullptr for all three,
// which this leaves exactly as narrow as before (Var never eligible).
bool isEligible(const ir::Expr *e, const Backend *backend, const ir::Function *fn, const ir::Stmt *body) {
    if (e->kind == ir::ExprKind::Binary || e->kind == ir::ExprKind::Index) return !hasSideEffect(e);
    if (e->kind == ir::ExprKind::Var && backend && backend->isLocalReadExpensive(e->slot, fn, body)) return true;
    return false;
}

// Every Slot a subtree reads, for deciding which recorded candidates a
// later write invalidates.
void collectDeps(const ir::Expr *e, std::unordered_set<ir::Slot *> &deps) {
    if (!e) return;
    if (e->slot) deps.insert(e->slot);
    collectDeps(e->lhs.get(), deps);
    collectDeps(e->rhs.get(), deps);
    collectDeps(e->elseExpr.get(), deps);
    for (const auto &arg : e->args) collectDeps(arg.get(), deps);
}

// Canonical key for `e`'s shape: two subtrees match iff textually identical
// (no commutative-operand normalization, no alias reasoning). isEligible
// only calls this on subtrees free of side effects, so the side-effecting
// cases below can't occur in practice; their fallback (keyed by address,
// so it never matches another subtree) just avoids a silent false match if
// that invariant ever breaks.
std::string exprKey(const ir::Expr *e) {
    if (!e) return "_";
    switch (e->kind) {
        case ir::ExprKind::IntLit:
            return "I" + std::to_string(e->intValue);
        case ir::ExprKind::FloatLit:
            return "F" + std::to_string(e->floatValue);
        case ir::ExprKind::Var:
            return "V" + std::to_string(e->slot->id);
        case ir::ExprKind::Index:
            return "X" + std::to_string(e->slot->id) + "[" + exprKey(e->lhs.get()) + "]";
        case ir::ExprKind::Unary:
            return "U" + e->op + "(" + exprKey(e->rhs.get()) + ")";
        case ir::ExprKind::Binary:
            return "B" + e->op + "(" + exprKey(e->lhs.get()) + "," + exprKey(e->rhs.get()) + ")";
        case ir::ExprKind::Func:
            return "F" + std::to_string(e->func->id);
        case ir::ExprKind::Convert:
            return "C" + std::to_string(static_cast<int>(e->type.base)) + "_" +
                   std::to_string(e->type.isUnsigned) + "_" + std::to_string(e->type.ptrDepth) +
                   "(" + exprKey(e->lhs.get()) + ")";
        case ir::ExprKind::Ternary:
            return "T(" + exprKey(e->lhs.get()) + "," + exprKey(e->rhs.get()) + "," +
                   exprKey(e->elseExpr.get()) + ")";
        case ir::ExprKind::Assign:
        case ir::ExprKind::DerefAssign:
        case ir::ExprKind::CompoundAssign:
        case ir::ExprKind::CompoundDerefAssign:
        case ir::ExprKind::Call:
        case ir::ExprKind::CallPtr:
        case ir::ExprKind::Intrinsic:
            return "?" + std::to_string(reinterpret_cast<std::uintptr_t>(e));
    }
    return "?" + std::to_string(reinterpret_cast<std::uintptr_t>(e));
}

// For the console message only; exprKey is the canonical match key.
std::string describeExpr(const ir::Expr *e) {
    if (!e) return "?";
    switch (e->kind) {
        case ir::ExprKind::IntLit:
            return std::to_string(e->intValue);
        case ir::ExprKind::Var:
            return e->slot->sourceName;
        case ir::ExprKind::Index:
            return e->slot->sourceName + "[" + describeExpr(e->lhs.get()) + "]";
        case ir::ExprKind::Unary:
            return e->op + describeExpr(e->rhs.get());
        case ir::ExprKind::Binary:
            return describeExpr(e->lhs.get()) + " " + e->op + " " + describeExpr(e->rhs.get());
        default:
            return "<expr>";
    }
}

ir::ExprPtr makeVarRead(ir::Slot *slot, int line) {
    auto v = std::make_unique<ir::Expr>();
    v->kind = ir::ExprKind::Var;
    v->line = line;
    v->slot = slot;
    v->type = slot->type;
    return v;
}

// Wraps `original` into `temp = original`: evaluates at the same program
// point and to the same value (an Assign's value is the value stored),
// while also storing it for later occurrences to read.
ir::ExprPtr makeAssignWrap(ir::ExprPtr original, ir::Slot *temp) {
    auto assign = std::make_unique<ir::Expr>();
    assign->kind = ir::ExprKind::Assign;
    assign->line = original->line;
    assign->slot = temp;
    assign->type = temp->type;
    assign->rhs = std::move(original);
    return assign;
}

// `type` is the cached subexpression's resolved type: the temp must store
// and reload at that width/signedness, not always plain int.
// `isConst`: true when the cached expression is itself a bare Var -
// i.e. only from the runWithExpensiveVarCache() path (see isEligible()'s
// own comment on why Var is only ever a candidate there). Every CSE temp,
// regardless of kind, is assigned exactly once (by the makeAssignWrap()
// below, right where it's created) and never reassigned afterward - the
// same "never written after declaration" guarantee AddressHoisting.cpp's
// own makeAddrTemp() relies on for the identical reason - but marking it
// here is narrowed to the Var case: that's the one this fix targets, and
// widening it to every Binary/Index CSE temp too is a separate, broader
// change this doesn't make.
ir::Slot *makeTempSlot(ir::Program &prog, int &tempCounter, Type type, bool isConst = false) {
    auto slot = std::make_unique<ir::Slot>();
    slot->id = static_cast<int>(prog.slots.size());  // every Slot id must be unique
    slot->sourceName = "__cse" + std::to_string(tempCounter++);
    slot->kind = ir::SlotKind::Local;
    slot->type = type;
    slot->isConst = isConst;
    ir::Slot *raw = slot.get();
    prog.slots.push_back(std::move(slot));
    return raw;
}

// A bare declaration just reserves frame storage; inserted at the front of
// the scope's statement list so it precedes both occurrences. main.cpp
// must recompute frame layout after this pass runs.
ir::StmtPtr makeTempDecl(ir::Slot *slot, int line) {
    auto decl = std::make_unique<ir::Stmt>();
    decl->kind = ir::StmtKind::LocalDecl;
    decl->line = line;
    decl->declSlot = slot;
    return decl;
}

// One subexpression recorded in the current scope: waiting for a second
// sighting (tempSlot null, firstLoc pointing at the one occurrence so far),
// or already promoted (tempSlot set).
struct Avail {
    ir::ExprPtr *firstLoc = nullptr;
    ir::Slot *tempSlot = nullptr;
    std::unordered_set<ir::Slot *> deps;
};

using AvailMap = std::unordered_map<std::string, Avail>;
// Every scope enclosing the expression being walked, outermost first, so a
// write can invalidate a candidate cached by any of them, not just the
// innermost. Each cseBlock scope owns exactly one entry, pushed for the
// body's duration.
using AvailStack = std::vector<AvailMap *>;

void invalidateSlot(AvailStack &stack, ir::Slot *written) {
    for (AvailMap *avail : stack) {
        for (auto it = avail->begin(); it != avail->end();) {
            if (it->second.deps.count(written)) {
                it = avail->erase(it);
            } else {
                ++it;
            }
        }
    }
}

void clearAll(AvailStack &stack) {
    for (AvailMap *avail : stack) avail->clear();
}

// Bundles everything every traversal function below threads through
// unchanged, so adding the backend/fn/body a Var candidate needs (see
// isEligible()'s own comment) only widened this struct once, not
// every function's own parameter list. `backend`/`fn`/`rootBody` are
// null/unused from run() (today's exact, unchanged behavior); set
// from runWithExpensiveVarCache() instead.
struct CseCtx {
    ir::Program &prog;
    int &tempCounter;
    bool &changed;
    const std::string &sourceFile;
    bool verbose;
    const Backend *backend;
    const ir::Function *fn;
    const ir::Stmt *rootBody;
};

void cseExpr(ir::ExprPtr &e, AvailStack &stack, std::vector<ir::StmtPtr> &body, size_t &i, CseCtx &ctx) {
    if (!e) return;

    // `&operand` needs operand's real lvalue location, not a cached value
    // copy, so operand itself is skipped as a CSE candidate entirely. An
    // Index's own index sub-expression (e.g. i+1 in &arr[i+1]) has no such
    // restriction and still gets the normal treatment below.
    if (e->kind == ir::ExprKind::Unary && e->op == "&") {
        if (e->rhs && e->rhs->kind == ir::ExprKind::Index) {
            cseExpr(e->rhs->lhs, stack, body, i, ctx);
        }
        return;
    }

    // Bottom-up: a nested candidate is resolved before its enclosing
    // expression's key is computed (see LocalCSE.h's gap note).
    cseExpr(e->lhs, stack, body, i, ctx);
    cseExpr(e->rhs, stack, body, i, ctx);
    for (auto &arg : e->args) cseExpr(arg, stack, body, i, ctx);

    // Invalidation after children, which evaluate first. CompoundAssign/
    // CompoundDerefAssign need the same treatment as their non-compound
    // counterparts. It reaches every enclosing scope, not just this one,
    // since a write inside a nested if/while body still clobbers a value
    // an outer scope cached before the nested body ran.
    switch (e->kind) {
        case ir::ExprKind::Assign:
        case ir::ExprKind::CompoundAssign:
            invalidateSlot(stack, e->slot);
            break;
        case ir::ExprKind::DerefAssign:
        case ir::ExprKind::CompoundDerefAssign:
        case ir::ExprKind::Call:
        case ir::ExprKind::CallPtr:
        case ir::ExprKind::Intrinsic:
            clearAll(stack);
            break;
        default:
            break;
    }

    if (!isEligible(e.get(), ctx.backend, ctx.fn, ctx.rootBody)) return;

    AvailMap &avail = *stack.back();
    std::string key = exprKey(e.get());
    auto it = avail.find(key);
    if (it == avail.end()) {
        std::unordered_set<ir::Slot *> deps;
        collectDeps(e.get(), deps);
        avail.emplace(std::move(key), Avail{&e, nullptr, std::move(deps)});
        return;
    }
    int line = e->line;
    if (it->second.tempSlot) {
        if (ctx.verbose) {
            std::cerr << ctx.sourceFile << ": local-cse: line " << line << ": reused `"
                       << it->second.tempSlot->sourceName << "` instead of recomputing `" << describeExpr(e.get())
                       << "`\n";
        }
        e = makeVarRead(it->second.tempSlot, line);
        ctx.changed = true;
        return;
    }
    // Second sighting: promote now, wrapping the first occurrence in place,
    // turning this one into a plain read, and reserving the temp's storage
    // at the front of this scope.
    if (ctx.verbose) {
        std::cerr << ctx.sourceFile << ": local-cse: line " << line << ": hoisted repeated subexpression `"
                   << describeExpr(e.get()) << "` into a temporary\n";
    }
    ir::Slot *temp = makeTempSlot(ctx.prog, ctx.tempCounter, e->type, e->kind == ir::ExprKind::Var);
    *it->second.firstLoc = makeAssignWrap(std::move(*it->second.firstLoc), temp);
    e = makeVarRead(temp, line);
    body.insert(body.begin(), makeTempDecl(temp, line));
    ++i;  // every statement in this scope, including the one we're walking, shifted up by one
    it->second.tempSlot = temp;
    ctx.changed = true;
}

void cseVisit(ir::Stmt *s, AvailStack &stack, CseCtx &ctx);

// The CSE engine, scoped to one Block's own statement list. A nested
// If/While body gets its own entry pushed onto `stack` for candidate
// matching, but a write anywhere in it still invalidates this scope (and
// any further out) through that shared stack.
void cseBlock(std::vector<ir::StmtPtr> &body, AvailStack &stack, CseCtx &ctx) {
    AvailMap avail;
    stack.push_back(&avail);
    for (size_t i = 0; i < body.size(); i++) {
        ir::Stmt *s = body[i].get();
        switch (s->kind) {
            case ir::StmtKind::Block:
                cseBlock(s->body, stack, ctx);
                break;
            case ir::StmtKind::If:
                cseExpr(s->cond, stack, body, i, ctx);
                cseVisit(s->thenStmt.get(), stack, ctx);
                cseVisit(s->elseStmt.get(), stack, ctx);
                break;
            case ir::StmtKind::While:
                cseExpr(s->cond, stack, body, i, ctx);
                cseVisit(s->loopBody.get(), stack, ctx);
                cseVisit(s->postStmt.get(), stack, ctx);
                break;
            case ir::StmtKind::Return:
            case ir::StmtKind::LocalDecl:
            case ir::StmtKind::ExprStmt:
                cseExpr(s->initOrReturnOrExpr, stack, body, i, ctx);
                break;
            case ir::StmtKind::Break:
            case ir::StmtKind::Continue:
            case ir::StmtKind::Empty:
            case ir::StmtKind::Asm:
                break;
            // Only `switch` produces these (see IR.h). A Label is reachable
            // both by falling through from whatever precedes it here and by
            // a jump from one of the switch's own comparisons (which runs
            // with `avail` exactly as it stood on switch entry) - same
            // reasoning as ConstantPropagation.cpp's own Label case, so
            // every recorded candidate is given up rather than risk reusing
            // one some other path into this label never actually computed.
            // Goto needs nothing of its own.
            case ir::StmtKind::Label:
                avail.clear();
                break;
            case ir::StmtKind::Goto:
                break;
        }
    }
    stack.pop_back();
}

// Finds every Block reachable from `s`, each becoming its own cseBlock
// scope; a brace-less single-statement If/While body is left untouched.
void cseVisit(ir::Stmt *s, AvailStack &stack, CseCtx &ctx) {
    if (!s) return;
    switch (s->kind) {
        case ir::StmtKind::Block:
            cseBlock(s->body, stack, ctx);
            return;
        case ir::StmtKind::If:
            cseVisit(s->thenStmt.get(), stack, ctx);
            cseVisit(s->elseStmt.get(), stack, ctx);
            return;
        case ir::StmtKind::While:
            cseVisit(s->loopBody.get(), stack, ctx);
            cseVisit(s->postStmt.get(), stack, ctx);
            return;
        default:
            return;
    }
}

}  // namespace

bool LocalCSE::run(ir::Program &prog, const std::string &sourceFile, bool verbose) const {
    bool changed = false;
    int tempCounter = 0;
    for (auto &fb : prog.functionBodies) {
        AvailStack stack;
        CseCtx ctx{prog, tempCounter, changed, sourceFile, verbose, nullptr, nullptr, nullptr};
        cseVisit(fb.body.get(), stack, ctx);
    }
    return changed;
}

bool LocalCSE::runWithExpensiveVarCache(ir::Program &prog, const std::string &sourceFile, bool verbose,
                                         const Backend &backend) const {
    bool changed = false;
    int tempCounter = 0;
    for (auto &fb : prog.functionBodies) {
        AvailStack stack;
        CseCtx ctx{prog, tempCounter, changed, sourceFile, verbose, &backend, fb.func, fb.body.get()};
        cseVisit(fb.body.get(), stack, ctx);
    }
    return changed;
}

}  // namespace rcc
