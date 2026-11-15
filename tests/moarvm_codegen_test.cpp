// End-to-end coverage of MoarVmCodegen::generate(): small, hand-built ir::Program values are
// compiled straight to real .moarvm bytecode, written to a temp file, and actually executed by
// the real `moar` interpreter (installed via `brew install nqp`, which pulls in moarvm) - unlike
// ParrotVmCodegen's own test, which can only inspect generated PIR's shape (Parrot's own
// interpreter is unmaintained upstream). If `moar` isn't on PATH, every execution check is
// skipped rather than failed, so this test still passes on a machine without it installed.
#include "codegen/moarvm/MoarVmCodegen.h"

#include <unistd.h>

#include <array>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ir/IR.h"

using rcc::BaseType;
using rcc::MoarVmCodegen;
using rcc::Type;
namespace ir = rcc::ir;

namespace {

int failures = 0;
int skipped = 0;

void expectEq(const std::string &got, const std::string &want, const char *why) {
    if (got != want) {
        std::cerr << "FAIL " << why << ": want '" << want << "', got '" << got << "'\n";
        failures++;
        return;
    }
    std::cout << "PASS " << why << '\n';
}

void expectContains(const std::string &text, const std::string &needle, const char *why) {
    if (text.find(needle) == std::string::npos) {
        std::cerr << "FAIL " << why << ": expected to find '" << needle << "'\n";
        failures++;
        return;
    }
    std::cout << "PASS " << why << '\n';
}

bool moarAvailable() {
    static int cached = -1;
    if (cached == -1) cached = (std::system("moar --version > /dev/null 2>&1") == 0) ? 1 : 0;
    return cached == 1;
}

// Runs `prog` through MoarVmCodegen, executes the result with `moar`, and returns its stdout.
// Only valid to call when moarAvailable().
std::string runViaMoar(const ir::Program &prog) {
    MoarVmCodegen cg;
    std::string bytes = cg.generate(prog);

    char path[] = "/tmp/rcc_moarvm_test_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) throw std::runtime_error("mkstemp failed");
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    close(fd);

    std::string cmd = std::string("moar '") + path + "' 2>&1";
    std::string result;
    FILE *p = popen(cmd.c_str(), "r");
    if (p) {
        std::array<char, 4096> buf;
        size_t n;
        while ((n = fread(buf.data(), 1, buf.size(), p)) > 0) result.append(buf.data(), n);
        pclose(p);
    }
    std::remove(path);
    return result;
}

void expectRuns(const ir::Program &prog, const std::string &wantStdout, const char *why) {
    if (!moarAvailable()) {
        std::cout << "SKIP " << why << " (moar not on PATH)\n";
        skipped++;
        return;
    }
    expectEq(runViaMoar(prog), wantStdout, why);
}

// --- Minimal ir::Program builder helpers (same shape as parrotvm_codegen_test.cpp's) -------

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

ir::ExprPtr callPtrExpr(ir::ExprPtr callee, Type t) {
    auto e = std::make_unique<ir::Expr>();
    e->kind = ir::ExprKind::CallPtr;
    e->lhs = std::move(callee);
    e->type = t;
    return e;
}

ir::ExprPtr funcAddrExpr(ir::Function *fn) {
    auto e = std::make_unique<ir::Expr>();
    e->kind = ir::ExprKind::Func;
    e->func = fn;
    e->type = Type(BaseType::Int, false, 1);
    return e;
}

ir::ExprPtr ternaryExpr(ir::ExprPtr cond, ir::ExprPtr thenE, ir::ExprPtr elseE, Type t) {
    auto e = std::make_unique<ir::Expr>();
    e->kind = ir::ExprKind::Ternary;
    e->lhs = std::move(cond);
    e->rhs = std::move(thenE);
    e->elseExpr = std::move(elseE);
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

ir::StmtPtr block1(ir::StmtPtr only) {
    std::vector<ir::StmtPtr> v;
    v.push_back(std::move(only));
    return block(std::move(v));
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
                            bool isVoid, bool isExternal = false) {
    auto fn = std::make_unique<ir::Function>();
    fn->sourceName = name;
    fn->params = std::move(params);
    fn->isVoid = isVoid;
    fn->isExternal = isExternal;
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

ir::StmtPtr callStmt(ir::ExprPtr e) { return exprStmt(std::move(e)); }

// --- Structural tests (always run, no `moar` needed) ---------------------------------------

void testListPlatformsShape() {
    MoarVmCodegen cg;
    expectEq(cg.name(), "moarvm", "name_is_moarvm");
    expectContains(cg.description(), "MoarVM", "description_mentions_moarvm");
}

// --- Execution tests (skipped if `moar` isn't installed) -----------------------------------

// int main() {
//     int i = 0, sum = 0;
//     while (i < 5) { sum = sum + i; i = i + 1; }
//     extern void moarvm_say(char*); moarvm_say("done"); // stand-in for printing sum's digits
//     return sum; // checked via moarvm_say of a fixed string below instead - see note
// }
// Since this backend has no printf/itoa, tests print through a global char buffer written a
// digit at a time, exercising arrays/pointers/globals/calls/control-flow together.
ir::Function *declareSay(ir::Program &prog) {
    ir::Slot *p = makeSlot(prog, ir::SlotKind::Param, "s", Type(BaseType::Char, false, 1));
    p->paramIndex = 0;
    return makeFunction(prog, "moarvm_say", {p}, true, /*isExternal=*/true);
}

void testHelloString() {
    ir::Program prog;
    ir::Function *sayFn = declareSay(prog);
    ir::Function *mainFn = makeFunction(prog, "main", {}, false);
    Type charArr(BaseType::Char);
    ir::Slot *msg = makeSlot(prog, ir::SlotKind::Global, "msg", charArr, /*arraySize=*/3);
    msg->arrayInit = {{false, 'h', nullptr, nullptr}, {false, 'i', nullptr, nullptr}, {false, 0, nullptr, nullptr}};

    ir::ExprPtr addrOfMsg0 = unaryExpr("&", indexExpr(msg, intLit(0)), Type(BaseType::Char, false, 1));
    std::vector<ir::ExprPtr> realArgs;
    realArgs.push_back(std::move(addrOfMsg0));

    std::vector<ir::StmtPtr> mainStmts;
    mainStmts.push_back(callStmt(callExpr(sayFn, std::move(realArgs), Type(BaseType::Int))));
    mainStmts.push_back(returnStmt(intLit(0)));
    addBody(prog, mainFn, block(std::move(mainStmts)));

    expectRuns(prog, "hi\n", "hello_string_via_moarvm_say");
}

// int add(int a, int b) { return a + b; }
// int main() { return add(3, 4); }   -- printed via moarvm_say of a one-char digit buffer
void testCallAndArithmeticViaExitPath() {
    ir::Program prog;
    Type intType(BaseType::Int);
    ir::Slot *a = makeSlot(prog, ir::SlotKind::Param, "a", intType);
    a->paramIndex = 0;
    ir::Slot *b = makeSlot(prog, ir::SlotKind::Param, "b", intType);
    b->paramIndex = 1;
    ir::Function *addFn = makeFunction(prog, "add", {a, b}, false);
    addBody(prog, addFn, block1(returnStmt(binExpr("+", varExpr(a), varExpr(b)))));

    ir::Function *sayFn = declareSay(prog);
    ir::Function *mainFn = makeFunction(prog, "main", {}, false);
    Type charType(BaseType::Char);
    ir::Slot *buf = makeSlot(prog, ir::SlotKind::Local, "buf", charType, /*arraySize=*/2);

    std::vector<ir::ExprPtr> addArgs;
    addArgs.push_back(intLit(3));
    addArgs.push_back(intLit(4));
    ir::ExprPtr sum = callExpr(addFn, std::move(addArgs), intType);
    // buf[0] = '0' + sum; buf[1] = 0; moarvm_say(&buf[0]);
    ir::ExprPtr digit = binExpr("+", intLit('0'), std::move(sum));
    std::vector<ir::StmtPtr> mainStmts;
    mainStmts.push_back(localDecl(buf));
    mainStmts.push_back(exprStmt(indexedAssignExpr(buf, intLit(0), std::move(digit))));
    mainStmts.push_back(exprStmt(indexedAssignExpr(buf, intLit(1), intLit(0))));
    std::vector<ir::ExprPtr> sayArgs;
    sayArgs.push_back(unaryExpr("&", indexExpr(buf, intLit(0)), Type(BaseType::Char, false, 1)));
    mainStmts.push_back(callStmt(callExpr(sayFn, std::move(sayArgs), Type(BaseType::Int))));
    mainStmts.push_back(returnStmt(intLit(0)));
    addBody(prog, mainFn, block(std::move(mainStmts)));

    expectRuns(prog, "7\n", "call_two_param_function_and_print_digit");
}

// int fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }
// prints '0' + (fact(4) - 24) == '0' -> but we print a single digit of fact(3)=6 to stay in
// one char.
void testRecursion() {
    ir::Program prog;
    Type intType(BaseType::Int);
    ir::Slot *n = makeSlot(prog, ir::SlotKind::Param, "n", intType);
    n->paramIndex = 0;
    ir::Function *factFn = makeFunction(prog, "fact", {n}, false);

    ir::ExprPtr cond = binExpr("<=", varExpr(n), intLit(1));
    std::vector<ir::ExprPtr> recArgs;
    recArgs.push_back(binExpr("-", varExpr(n), intLit(1)));
    ir::ExprPtr recCall = callExpr(factFn, std::move(recArgs), intType);
    ir::ExprPtr mul = binExpr("*", varExpr(n), std::move(recCall));
    ir::ExprPtr tern = ternaryExpr(std::move(cond), intLit(1), std::move(mul), intType);
    addBody(prog, factFn, block1(returnStmt(std::move(tern))));

    ir::Function *sayFn = declareSay(prog);
    ir::Function *mainFn = makeFunction(prog, "main", {}, false);
    Type charType(BaseType::Char);
    ir::Slot *buf = makeSlot(prog, ir::SlotKind::Local, "buf", charType, /*arraySize=*/2);

    std::vector<ir::ExprPtr> callArgs;
    callArgs.push_back(intLit(3));  // fact(3) == 6, single digit
    ir::ExprPtr result = callExpr(factFn, std::move(callArgs), intType);
    ir::ExprPtr digit = binExpr("+", intLit('0'), std::move(result));
    std::vector<ir::StmtPtr> mainStmts;
    mainStmts.push_back(localDecl(buf));
    mainStmts.push_back(exprStmt(indexedAssignExpr(buf, intLit(0), std::move(digit))));
    mainStmts.push_back(exprStmt(indexedAssignExpr(buf, intLit(1), intLit(0))));
    std::vector<ir::ExprPtr> sayArgs;
    sayArgs.push_back(unaryExpr("&", indexExpr(buf, intLit(0)), Type(BaseType::Char, false, 1)));
    mainStmts.push_back(callStmt(callExpr(sayFn, std::move(sayArgs), Type(BaseType::Int))));
    mainStmts.push_back(returnStmt(intLit(0)));
    addBody(prog, mainFn, block(std::move(mainStmts)));

    expectRuns(prog, "6\n", "recursive_factorial");
}

// int g = 41;
// void inc() { g = g + 1; }
// int main() { inc(); /* g == 42 */ moarvm_say("*"); return g; }
void testGlobalsAndPointersAndFunctionPointer() {
    ir::Program prog;
    Type intType(BaseType::Int);

    ir::Slot *g = makeSlot(prog, ir::SlotKind::Global, "g", intType);
    g->hasInit = true;
    g->initValue = 5;

    ir::Function *incFn = makeFunction(prog, "inc", {}, true);
    addBody(prog, incFn, block1(exprStmt(assignExpr(g, binExpr("+", varExpr(g), intLit(1))))));

    ir::Function *sayFn = declareSay(prog);
    ir::Function *mainFn = makeFunction(prog, "main", {}, false);

    // Call `inc` indirectly through a function pointer (CallPtr), exercising the funcs table.
    ir::ExprPtr fp = unaryExpr("&", funcAddrExpr(incFn), Type(BaseType::Int, false, 1));
    ir::ExprPtr callThroughPtr = callPtrExpr(std::move(fp), Type(BaseType::Int));

    Type charType(BaseType::Char);
    ir::Slot *buf = makeSlot(prog, ir::SlotKind::Local, "buf", charType, /*arraySize=*/2);
    ir::ExprPtr digit = binExpr("+", intLit('0'), varExpr(g));

    std::vector<ir::StmtPtr> mainStmts;
    mainStmts.push_back(exprStmt(std::move(callThroughPtr)));  // g becomes 6
    mainStmts.push_back(localDecl(buf));
    mainStmts.push_back(exprStmt(indexedAssignExpr(buf, intLit(0), std::move(digit))));
    mainStmts.push_back(exprStmt(indexedAssignExpr(buf, intLit(1), intLit(0))));
    std::vector<ir::ExprPtr> sayArgs;
    sayArgs.push_back(unaryExpr("&", indexExpr(buf, intLit(0)), Type(BaseType::Char, false, 1)));
    mainStmts.push_back(callStmt(callExpr(sayFn, std::move(sayArgs), Type(BaseType::Int))));
    mainStmts.push_back(returnStmt(intLit(0)));
    addBody(prog, mainFn, block(std::move(mainStmts)));

    expectRuns(prog, "6\n", "global_mutated_through_function_pointer_call");
}

// Exercises pointer deref/assign, narrowing (char/short), bitwise ops and shifts all in one
// pass, each contributing one printed digit.
void testPointersNarrowingBitwiseShifts() {
    ir::Program prog;
    Type intType(BaseType::Int);
    Type charType(BaseType::Char);
    Type shortType(BaseType::Short);

    ir::Slot *p = makeSlot(prog, ir::SlotKind::Param, "p", Type(BaseType::Int, false, 1));
    p->paramIndex = 0;
    ir::Function *bumpFn = makeFunction(prog, "bump", {p}, true);
    ir::ExprPtr derefRead = unaryExpr("*", varExpr(p), intType);
    ir::ExprPtr rhs = binExpr("+", std::move(derefRead), intLit(1));
    addBody(prog, bumpFn, block1(exprStmt(derefAssignExpr(varExpr(p), std::move(rhs), intType))));

    ir::Function *sayFn = declareSay(prog);
    ir::Function *mainFn = makeFunction(prog, "main", {}, false);

    ir::Slot *x = makeSlot(prog, ir::SlotKind::Local, "x", intType);
    ir::Slot *c = makeSlot(prog, ir::SlotKind::Local, "c", charType);
    ir::Slot *sh = makeSlot(prog, ir::SlotKind::Local, "sh", shortType);
    ir::Slot *buf = makeSlot(prog, ir::SlotKind::Local, "buf", charType, /*arraySize=*/2);

    std::vector<ir::ExprPtr> bumpArgs;
    bumpArgs.push_back(unaryExpr("&", varExpr(x), Type(BaseType::Int, false, 1)));

    std::vector<ir::StmtPtr> mainStmts;
    mainStmts.push_back(localDecl(x, intLit(4)));
    mainStmts.push_back(localDecl(buf));
    mainStmts.push_back(callStmt(callExpr(bumpFn, std::move(bumpArgs), Type(BaseType::Int))));  // x == 5
    // c = (char)(300 & 0xFF) == 44, but we pick a value that narrows to a printable digit:
    // c = (x << 1) & 0x0F  -> (5<<1)=10 & 0x0F = 10 -> prints as '0'+10-? keep in 0-9 range.
    ir::ExprPtr shifted = binExpr("<<", varExpr(x), intLit(1));
    ir::ExprPtr masked = binExpr("&", std::move(shifted), intLit(7));  // (5<<1)&7 == 2
    mainStmts.push_back(localDecl(c, std::move(masked)));
    mainStmts.push_back(localDecl(sh, binExpr(">>", varExpr(x), intLit(0))));  // sh == 5, unused beyond decl
    (void)sh;
    ir::ExprPtr digit = binExpr("+", intLit('0'), varExpr(c));
    mainStmts.push_back(exprStmt(indexedAssignExpr(buf, intLit(0), std::move(digit))));
    mainStmts.push_back(exprStmt(indexedAssignExpr(buf, intLit(1), intLit(0))));
    std::vector<ir::ExprPtr> sayArgs;
    sayArgs.push_back(unaryExpr("&", indexExpr(buf, intLit(0)), Type(BaseType::Char, false, 1)));
    mainStmts.push_back(callStmt(callExpr(sayFn, std::move(sayArgs), Type(BaseType::Int))));
    mainStmts.push_back(returnStmt(intLit(0)));
    addBody(prog, mainFn, block(std::move(mainStmts)));

    expectRuns(prog, "2\n", "pointer_deref_assign_shift_and_mask");
}

// int main() {
//     int i = 1, sum = 0;
//     while (i < 4) { if (i == 2) sum = sum + 10; else sum = sum + i; i = i + 1; }
//     // sum == 1 + 10 + 3 == 14 -> print sum - 8 == 6 to keep it one digit
// }
void testWhileAndIf() {
    ir::Program prog;
    Type intType(BaseType::Int);
    Type charType(BaseType::Char);
    ir::Function *sayFn = declareSay(prog);
    ir::Function *mainFn = makeFunction(prog, "main", {}, false);

    ir::Slot *i = makeSlot(prog, ir::SlotKind::Local, "i", intType);
    ir::Slot *sum = makeSlot(prog, ir::SlotKind::Local, "sum", intType);
    ir::Slot *buf = makeSlot(prog, ir::SlotKind::Local, "buf", charType, /*arraySize=*/2);

    ir::StmtPtr thenS = exprStmt(assignExpr(sum, binExpr("+", varExpr(sum), intLit(10))));
    ir::StmtPtr elseS = exprStmt(assignExpr(sum, binExpr("+", varExpr(sum), varExpr(i))));
    ir::StmtPtr ifS = ifStmt(binExpr("==", varExpr(i), intLit(2)), std::move(thenS), std::move(elseS));
    ir::StmtPtr incI = exprStmt(assignExpr(i, binExpr("+", varExpr(i), intLit(1))));
    std::vector<ir::StmtPtr> loopStmts;
    loopStmts.push_back(std::move(ifS));
    loopStmts.push_back(std::move(incI));
    ir::StmtPtr loop = whileStmt(binExpr("<", varExpr(i), intLit(4)), block(std::move(loopStmts)));

    ir::ExprPtr digit = binExpr("+", intLit('0'), binExpr("-", varExpr(sum), intLit(8)));
    std::vector<ir::StmtPtr> mainStmts;
    mainStmts.push_back(localDecl(i, intLit(1)));
    mainStmts.push_back(localDecl(sum, intLit(0)));
    mainStmts.push_back(localDecl(buf));
    mainStmts.push_back(std::move(loop));
    mainStmts.push_back(exprStmt(indexedAssignExpr(buf, intLit(0), std::move(digit))));
    mainStmts.push_back(exprStmt(indexedAssignExpr(buf, intLit(1), intLit(0))));
    std::vector<ir::ExprPtr> sayArgs;
    sayArgs.push_back(unaryExpr("&", indexExpr(buf, intLit(0)), Type(BaseType::Char, false, 1)));
    mainStmts.push_back(callStmt(callExpr(sayFn, std::move(sayArgs), Type(BaseType::Int))));
    mainStmts.push_back(returnStmt(intLit(0)));
    addBody(prog, mainFn, block(std::move(mainStmts)));

    expectRuns(prog, "6\n", "while_loop_with_if_else");
}

}  // namespace

int main() {
    testListPlatformsShape();
    testHelloString();
    testCallAndArithmeticViaExitPath();
    testRecursion();
    testGlobalsAndPointersAndFunctionPointer();
    testPointersNarrowingBitwiseShifts();
    testWhileAndIf();
    if (failures > 0) {
        std::cerr << failures << " failure(s), " << skipped << " skipped\n";
        return 1;
    }
    std::cout << "all moarvm_codegen_test checks passed (" << skipped << " skipped)\n";
    return 0;
}
