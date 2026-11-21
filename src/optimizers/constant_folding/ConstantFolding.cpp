#include "optimizers/constant_folding/ConstantFolding.h"

#include <iostream>
#include <optional>
#include <string>

namespace rcc {

namespace {

ir::ExprPtr makeIntLit(long value, int line, Type type) {
    auto lit = std::make_unique<ir::Expr>();
    lit->kind = ir::ExprKind::IntLit;
    lit->line = line;
    lit->intValue = value;
    lit->type = type;
    return lit;
}

ir::ExprPtr makeFloatLit(double value, int line, Type type) {
    auto lit = std::make_unique<ir::Expr>();
    lit->kind = ir::ExprKind::FloatLit;
    lit->line = line;
    lit->floatValue = value;
    lit->type = type;
    return lit;
}

std::optional<double> evalUnaryFloat(const std::string &op, double a) {
    if (op == "-") return -a;
    // "!" on a float yields a plain int (0/1), not a float - its own Expr
    // node's type is already Int from IRBuilder, so folding it still needs
    // makeIntLit, not makeFloatLit; left to the integer fold path by simply
    // not handling it here (the caller only reaches this function when
    // e->type.base is itself Float/Double).
    return std::nullopt;  // "~"/"*"/"&" never apply to a float operand
}

// nullopt means "don't fold" (currently just unconditional pass-through
// cases don't exist, but matches evalBinary's own optional shape for
// symmetry and in case a future non-finite-guard needs one).
std::optional<double> evalBinaryFloat(const std::string &op, double a, double b) {
    if (op == "+") return a + b;
    if (op == "-") return a - b;
    if (op == "*") return a * b;
    if (op == "/") return a / b;  // IEEE-754 division by zero yields +-Infinity/NaN, not UB
    return std::nullopt;  // relational/equality ops below yield Int, handled separately
}

// Float/double relational and equality operators yield a plain int, unlike
// the arithmetic ones above - same split evalBinary()'s own "==".."||"
// cases make for the all-int case, just needing float-typed operands here.
std::optional<long> evalBinaryFloatCompare(const std::string &op, double a, double b) {
    if (op == "==") return a == b ? 1 : 0;
    if (op == "!=") return a != b ? 1 : 0;
    if (op == "<") return a < b ? 1 : 0;
    if (op == "<=") return a <= b ? 1 : 0;
    if (op == ">") return a > b ? 1 : 0;
    if (op == ">=") return a >= b ? 1 : 0;
    return std::nullopt;
}

std::optional<long> evalUnary(const std::string &op, long a) {
    if (op == "-") return -a;
    if (op == "!") return a == 0 ? 1 : 0;
    if (op == "~") return ~a;
    return std::nullopt;  // "*"/"&" (deref/address-of): never foldable
}

// nullopt means "don't fold": div/mod by zero or a shift outside [0, 63]
// would need host-undefined-behavior to evaluate here.
std::optional<long> evalBinary(const std::string &op, long a, long b) {
    if (op == "+") return a + b;
    if (op == "-") return a - b;
    if (op == "*") return a * b;
    if (op == "/") return b != 0 ? std::optional<long>(a / b) : std::nullopt;
    if (op == "%") return b != 0 ? std::optional<long>(a % b) : std::nullopt;
    if (op == "<<") return (b >= 0 && b < 64) ? std::optional<long>(a << b) : std::nullopt;
    if (op == ">>") return (b >= 0 && b < 64) ? std::optional<long>(a >> b) : std::nullopt;
    if (op == "==") return a == b ? 1 : 0;
    if (op == "!=") return a != b ? 1 : 0;
    if (op == "<") return a < b ? 1 : 0;
    if (op == "<=") return a <= b ? 1 : 0;
    if (op == ">") return a > b ? 1 : 0;
    if (op == ">=") return a >= b ? 1 : 0;
    if (op == "&") return a & b;
    if (op == "^") return a ^ b;
    if (op == "|") return a | b;
    if (op == "&&") return (a != 0 && b != 0) ? 1 : 0;
    if (op == "||") return (a != 0 || b != 0) ? 1 : 0;
    return std::nullopt;
}

// Folds children first (bottom-up), then tries to fold `e` itself.
void foldExpr(ir::ExprPtr &e, bool &changed, const std::string &sourceFile, bool verbose) {
    if (!e) return;
    foldExpr(e->lhs, changed, sourceFile, verbose);
    foldExpr(e->rhs, changed, sourceFile, verbose);
    for (auto &arg : e->args) foldExpr(arg, changed, sourceFile, verbose);

    if (e->kind == ir::ExprKind::Unary && e->rhs->kind == ir::ExprKind::IntLit) {
        if (std::optional<long> v = evalUnary(e->op, e->rhs->intValue)) {
            // Unlike the Binary case below, the printed value often doesn't
            // change (e.g. `-64` is parsed as Unary("-", IntLit(64)), whose
            // fold prints as "-64" both before and after) - what's actually
            // collapsing here is two IR nodes into one, not the value, so
            // say that explicitly instead of a misleading "folded X to X".
            if (verbose) {
                std::cerr << sourceFile << ": constant-folding: line " << e->line << ": collapsed unary `" << e->op
                           << e->rhs->intValue << "` into the single literal `" << *v << "`\n";
            }
            e = makeIntLit(*v, e->line, e->type);
            changed = true;
        }
    } else if (e->kind == ir::ExprKind::Unary && e->rhs->kind == ir::ExprKind::FloatLit) {
        if (std::optional<double> v = evalUnaryFloat(e->op, e->rhs->floatValue)) {
            if (verbose) {
                std::cerr << sourceFile << ": constant-folding: line " << e->line << ": collapsed unary `" << e->op
                           << e->rhs->floatValue << "` into the single literal `" << *v << "`\n";
            }
            e = makeFloatLit(*v, e->line, e->type);
            changed = true;
        }
    } else if (e->kind == ir::ExprKind::Binary && e->lhs->kind == ir::ExprKind::IntLit &&
               e->rhs->kind == ir::ExprKind::IntLit) {
        if (std::optional<long> v = evalBinary(e->op, e->lhs->intValue, e->rhs->intValue)) {
            if (verbose) {
                std::cerr << sourceFile << ": constant-folding: line " << e->line << ": folded `" << e->lhs->intValue
                           << " " << e->op << " " << e->rhs->intValue << "` to `" << *v << "`\n";
            }
            e = makeIntLit(*v, e->line, e->type);
            changed = true;
        }
    } else if (e->kind == ir::ExprKind::Binary && e->lhs->kind == ir::ExprKind::FloatLit &&
               e->rhs->kind == ir::ExprKind::FloatLit) {
        if (std::optional<double> v = evalBinaryFloat(e->op, e->lhs->floatValue, e->rhs->floatValue)) {
            if (verbose) {
                std::cerr << sourceFile << ": constant-folding: line " << e->line << ": folded `"
                           << e->lhs->floatValue << " " << e->op << " " << e->rhs->floatValue << "` to `" << *v
                           << "`\n";
            }
            e = makeFloatLit(*v, e->line, e->type);
            changed = true;
        } else if (std::optional<long> cv = evalBinaryFloatCompare(e->op, e->lhs->floatValue, e->rhs->floatValue)) {
            if (verbose) {
                std::cerr << sourceFile << ": constant-folding: line " << e->line << ": folded `"
                           << e->lhs->floatValue << " " << e->op << " " << e->rhs->floatValue << "` to `" << *cv
                           << "`\n";
            }
            e = makeIntLit(*cv, e->line, e->type);
            changed = true;
        }
    }
}

void foldStmt(ir::Stmt *s, bool &changed, const std::string &sourceFile, bool verbose) {
    if (!s) return;
    switch (s->kind) {
        case ir::StmtKind::Block:
            for (auto &child : s->body) foldStmt(child.get(), changed, sourceFile, verbose);
            return;
        case ir::StmtKind::If:
            foldExpr(s->cond, changed, sourceFile, verbose);
            foldStmt(s->thenStmt.get(), changed, sourceFile, verbose);
            foldStmt(s->elseStmt.get(), changed, sourceFile, verbose);
            return;
        case ir::StmtKind::While:
            foldExpr(s->cond, changed, sourceFile, verbose);
            foldStmt(s->loopBody.get(), changed, sourceFile, verbose);
            foldStmt(s->postStmt.get(), changed, sourceFile, verbose);
            return;
        case ir::StmtKind::Return:
        case ir::StmtKind::LocalDecl:
        case ir::StmtKind::ExprStmt:
            foldExpr(s->initOrReturnOrExpr, changed, sourceFile, verbose);
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

bool ConstantFolding::run(ir::Program &prog, const std::string &sourceFile, bool verbose) const {
    bool changed = false;
    for (auto &fb : prog.functionBodies) foldStmt(fb.body.get(), changed, sourceFile, verbose);
    return changed;
}

}  // namespace rcc
