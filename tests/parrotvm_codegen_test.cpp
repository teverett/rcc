// Structural coverage of ParrotVmCodegen::generate() against small, hand-built ir::Program
// values - no front end, no real `parrot` interpreter (Parrot's own Homebrew formula is
// disabled/unmaintained upstream, so nothing here executes generated PIR, only inspects its
// shape - see ParrotVmCodegen.h's class comment for the synthetic-memory design these tests
// are checking).
#include "codegen/parrotvm/ParrotVmCodegen.h"

#include <iostream>
#include <string>
#include <vector>

#include "ir/IR.h"

using rcc::BaseType;
using rcc::ParrotVmCodegen;
using rcc::Type;
namespace ir = rcc::ir;

namespace {

int failures = 0;

void expectContains(const std::string &text, const std::string &needle, const char *why) {
    if (text.find(needle) == std::string::npos) {
        std::cerr << "FAIL " << why << ": expected to find '" << needle << "'\n";
        failures++;
        return;
    }
    std::cout << "PASS " << why << '\n';
}

int countOccurrences(const std::string &text, const std::string &needle) {
    int count = 0;
    size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        count++;
        pos += needle.size();
    }
    return count;
}

void expectCount(const std::string &text, const std::string &needle, int want, const char *why) {
    int got = countOccurrences(text, needle);
    if (got != want) {
        std::cerr << "FAIL " << why << ": expected " << want << " occurrences of '" << needle << "', got " << got
                   << '\n';
        failures++;
        return;
    }
    std::cout << "PASS " << why << '\n';
}

void expectFalse(bool condition, const char *why) {
    if (condition) {
        std::cerr << "FAIL " << why << '\n';
        failures++;
        return;
    }
    std::cout << "PASS " << why << '\n';
}

// --- Minimal ir::Program builder helpers --------------------------------------------------

ir::Slot *makeSlot(ir::Program &prog, ir::SlotKind kind, const std::string &name, Type type, int arraySize = 0) {
    auto slot = std::make_unique<ir::Slot>();
    slot->kind = kind;
    slot->sourceName = name;
    slot->type = type;
    slot->arraySize = arraySize;
    ir::Slot *raw = slot.get();
    prog.slots.push_back(std::move(slot));
    return raw;
}

ir::ExprPtr intLit(long v, Type t = Type(BaseType::Int)) {
    auto e = std::make_unique<ir::Expr>();
    e->kind = ir::ExprKind::IntLit;
    e->intValue = v;
    e->type = t;
    return e;
}

ir::ExprPtr varExpr(ir::Slot *s) {
    auto e = std::make_unique<ir::Expr>();
    e->kind = ir::ExprKind::Var;
    e->slot = s;
    e->type = s->type;
    return e;
}

ir::ExprPtr binExpr(const std::string &op, ir::ExprPtr lhs, ir::ExprPtr rhs, Type t = Type(BaseType::Int)) {
    auto e = std::make_unique<ir::Expr>();
    e->kind = ir::ExprKind::Binary;
    e->op = op;
    e->lhs = std::move(lhs);
    e->rhs = std::move(rhs);
    e->type = t;
    return e;
}

ir::ExprPtr unaryExpr(const std::string &op, ir::ExprPtr rhs, Type t) {
    auto e = std::make_unique<ir::Expr>();
    e->kind = ir::ExprKind::Unary;
    e->op = op;
    e->rhs = std::move(rhs);
    e->type = t;
    return e;
}

ir::ExprPtr assignExpr(ir::Slot *s, ir::ExprPtr rhs) {
    auto e = std::make_unique<ir::Expr>();
    e->kind = ir::ExprKind::Assign;
    e->slot = s;
    e->rhs = std::move(rhs);
    e->type = s->type;
    return e;
}

ir::ExprPtr indexedAssignExpr(ir::Slot *s, ir::ExprPtr idx, ir::ExprPtr rhs) {
    auto e = std::make_unique<ir::Expr>();
    e->kind = ir::ExprKind::Assign;
    e->slot = s;
    e->lhs = std::move(idx);
    e->rhs = std::move(rhs);
    e->type = s->type;
    return e;
}

ir::ExprPtr indexExpr(ir::Slot *s, ir::ExprPtr idx) {
    auto e = std::make_unique<ir::Expr>();
    e->kind = ir::ExprKind::Index;
    e->slot = s;
    e->lhs = std::move(idx);
    e->type = s->type;
    return e;
}

ir::ExprPtr derefAssignExpr(ir::ExprPtr lhs, ir::ExprPtr rhs, Type t) {
    auto e = std::make_unique<ir::Expr>();
    e->kind = ir::ExprKind::DerefAssign;
    e->lhs = std::move(lhs);
    e->rhs = std::move(rhs);
    e->type = t;
    return e;
}

ir::ExprPtr callExpr(ir::Function *fn, std::vector<ir::ExprPtr> args, Type t) {
    auto e = std::make_unique<ir::Expr>();
    e->kind = ir::ExprKind::Call;
    e->func = fn;
    for (auto &a : args) e->args.push_back(std::move(a));
    e->type = t;
    return e;
}

ir::StmtPtr exprStmt(ir::ExprPtr e) {
    auto s = std::make_unique<ir::Stmt>();
    s->kind = ir::StmtKind::ExprStmt;
    s->initOrReturnOrExpr = std::move(e);
    return s;
}

ir::StmtPtr localDecl(ir::Slot *slot, ir::ExprPtr init = nullptr) {
    auto s = std::make_unique<ir::Stmt>();
    s->kind = ir::StmtKind::LocalDecl;
    s->declSlot = slot;
    s->initOrReturnOrExpr = std::move(init);
    return s;
}

ir::StmtPtr returnStmt(ir::ExprPtr e = nullptr) {
    auto s = std::make_unique<ir::Stmt>();
    s->kind = ir::StmtKind::Return;
    s->initOrReturnOrExpr = std::move(e);
    return s;
}

ir::StmtPtr block(std::vector<ir::StmtPtr> stmts) {
    auto s = std::make_unique<ir::Stmt>();
    s->kind = ir::StmtKind::Block;
    for (auto &c : stmts) s->body.push_back(std::move(c));
    return s;
}

ir::StmtPtr ifStmt(ir::ExprPtr cond, ir::StmtPtr thenS, ir::StmtPtr elseS = nullptr) {
    auto s = std::make_unique<ir::Stmt>();
    s->kind = ir::StmtKind::If;
    s->cond = std::move(cond);
    s->thenStmt = std::move(thenS);
    s->elseStmt = std::move(elseS);
    return s;
}

ir::StmtPtr whileStmt(ir::ExprPtr cond, ir::StmtPtr body) {
    auto s = std::make_unique<ir::Stmt>();
    s->kind = ir::StmtKind::While;
    s->cond = std::move(cond);
    s->loopBody = std::move(body);
    return s;
}

ir::Function *makeFunction(ir::Program &prog, const std::string &name, std::vector<ir::Slot *> params,
                            bool isVoid) {
    auto fn = std::make_unique<ir::Function>();
    fn->sourceName = name;
    fn->params = std::move(params);
    fn->isVoid = isVoid;
    ir::Function *raw = fn.get();
    prog.functions.push_back(std::move(fn));
    return raw;
}

void addBody(ir::Program &prog, ir::Function *fn, ir::StmtPtr body) {
    ir::FunctionBody fb;
    fb.func = fn;
    fb.body = std::move(body);
    prog.functionBodies.push_back(std::move(fb));
}

// --- Tests ---------------------------------------------------------------------------------

// int main() {
//     int i = 0;
//     int sum = 0;
//     while (i < 3) {
//         if (i == 1) { sum = sum + 10; } else { sum = sum + 1; }
//         i = i + 1;
//     }
//     return sum;
// }
void testArithmeticControlFlow() {
    ir::Program prog;
    Type intType(BaseType::Int);
    ir::Function *mainFn = makeFunction(prog, "main", {}, false);
    ir::Slot *i = makeSlot(prog, ir::SlotKind::Local, "i", intType);
    ir::Slot *sum = makeSlot(prog, ir::SlotKind::Local, "sum", intType);

    ir::StmtPtr thenS = exprStmt(assignExpr(sum, binExpr("+", varExpr(sum), intLit(10))));
    ir::StmtPtr elseS = exprStmt(assignExpr(sum, binExpr("+", varExpr(sum), intLit(1))));
    ir::StmtPtr ifS = ifStmt(binExpr("==", varExpr(i), intLit(1)), std::move(thenS), std::move(elseS));
    ir::StmtPtr incI = exprStmt(assignExpr(i, binExpr("+", varExpr(i), intLit(1))));
    std::vector<ir::StmtPtr> loopStmts;
    loopStmts.push_back(std::move(ifS));
    loopStmts.push_back(std::move(incI));
    ir::StmtPtr loop = whileStmt(binExpr("<", varExpr(i), intLit(3)), block(std::move(loopStmts)));

    std::vector<ir::StmtPtr> mainStmts;
    mainStmts.push_back(localDecl(i, intLit(0)));
    mainStmts.push_back(localDecl(sum, intLit(0)));
    mainStmts.push_back(std::move(loop));
    mainStmts.push_back(returnStmt(varExpr(sum)));
    addBody(prog, mainFn, block(std::move(mainStmts)));

    ParrotVmCodegen cg;
    std::string out = cg.generate(prog);

    expectCount(out, ".sub 'fn_main'\n", 1, "arith_one_main_sub");
    expectCount(out, ".sub '_start' :main\n", 1, "arith_one_entry_sub");
    expectCount(out, ".end\n", 2, "arith_balanced_sub_end_pairs");
    expectContains(out, "unless", "arith_has_conditional_branch");
    expectContains(out, "   goto L", "arith_has_loop_goto");
}

// void bump(int *p) { *p = *p + 1; }
// int main() {
//     int arr[4];
//     arr[2] = 7;
//     return arr[2];
// }
void testPointersAndArrays() {
    ir::Program prog;
    Type intType(BaseType::Int);
    Type intPtr(BaseType::Int, /*isUnsigned=*/false, /*ptrDepth=*/1);

    ir::Slot *p = makeSlot(prog, ir::SlotKind::Param, "p", intPtr);
    p->paramIndex = 0;
    ir::Function *bumpFn = makeFunction(prog, "bump", {p}, true);
    ir::ExprPtr derefRead = unaryExpr("*", varExpr(p), intType);
    ir::ExprPtr rhs = binExpr("+", std::move(derefRead), intLit(1));
    std::vector<ir::StmtPtr> bumpStmts;
    bumpStmts.push_back(exprStmt(derefAssignExpr(varExpr(p), std::move(rhs), intType)));
    addBody(prog, bumpFn, block(std::move(bumpStmts)));

    ir::Function *mainFn = makeFunction(prog, "main", {}, false);
    ir::Slot *arr = makeSlot(prog, ir::SlotKind::Local, "arr", intType, /*arraySize=*/4);
    std::vector<ir::StmtPtr> mainStmts;
    mainStmts.push_back(localDecl(arr));
    mainStmts.push_back(exprStmt(indexedAssignExpr(arr, intLit(2), intLit(7))));
    mainStmts.push_back(returnStmt(indexExpr(arr, intLit(2))));
    addBody(prog, mainFn, block(std::move(mainStmts)));

    ParrotVmCodegen cg;
    std::string out = cg.generate(prog);

    expectCount(out, "mem = new ['ResizableIntegerArray']", 1, "ptr_mem_pmc_declared_exactly_once");
    expectContains(out, "mem[$I", "ptr_slot_access_goes_through_mem");
    // Neither 'p' (the pointer param) nor 'arr' (the local array) should ever appear as a
    // bare named register - every access to either must go through mem[]/elementAddress().
    expectFalse(out.find("$p") != std::string::npos, "ptr_param_never_a_bare_register");
    expectFalse(out.find("$arr") != std::string::npos, "ptr_array_never_a_bare_register");
}

// int g = 41;
// void inc() { g = g + 1; }
// int main() { inc(); return g; }
void testGlobalsAndCalls() {
    ir::Program prog;
    Type intType(BaseType::Int);

    ir::Slot *g = makeSlot(prog, ir::SlotKind::Global, "g", intType);
    g->hasInit = true;
    g->initValue = 41;

    ir::Function *incFn = makeFunction(prog, "inc", {}, true);
    std::vector<ir::StmtPtr> incStmts;
    incStmts.push_back(exprStmt(assignExpr(g, binExpr("+", varExpr(g), intLit(1)))));
    addBody(prog, incFn, block(std::move(incStmts)));

    ir::Function *mainFn = makeFunction(prog, "main", {}, false);
    std::vector<ir::ExprPtr> noArgs;
    std::vector<ir::StmtPtr> mainStmts;
    mainStmts.push_back(exprStmt(callExpr(incFn, std::move(noArgs), intType)));
    mainStmts.push_back(returnStmt(varExpr(g)));
    addBody(prog, mainFn, block(std::move(mainStmts)));

    ParrotVmCodegen cg;
    std::string out = cg.generate(prog);

    expectContains(out, "mem[0] = 41", "globals_fixed_base_offset_and_literal_init");
    expectContains(out, "'fn_inc'()", "globals_call_uses_real_pir_sub_call");
    expectCount(out, "mem = new", 1, "globals_mem_pmc_created_only_once_in_entry_sub");
}

}  // namespace

int main() {
    testArithmeticControlFlow();
    testPointersAndArrays();
    testGlobalsAndCalls();
    if (failures > 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "all parrotvm_codegen_test checks passed\n";
    return 0;
}
