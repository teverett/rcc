#include "BasicFrontend.h"

#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ANTLRInputStream.h"
#include "BaseErrorListener.h"
#include "BasicLexer.h"
#include "BasicParser.h"
#include "CommonTokenStream.h"

// ---------------------------------------------------------------------
// Lowers a BasicParser parse tree straight into rcc::AST - see
// docs/basic-language.md for the dialect and BasicFrontend.h's own header
// comment for the overall shape. Two things don't map onto an existing
// AST node as directly as everything else does:
//
// - FOR/NEXT isn't syntactically nested (grammar/basic/BasicParser.g4's
//   `line`/`statement` have no concept of a loop body - NEXT is just
//   another statement, possibly lines later). lowerRange() below matches
//   each FOR with its NEXT by a simple depth-counting scan over the
//   flattened statement stream first, then recursively lowers the
//   enclosed range as the body of a synthesized While (the same
//   init-while-post desugaring CParser.g4's own `for` already uses - see
//   AST.h's Stmt::postStmt).
// - GOSUB/RETURN has no existing primitive at all (Goto has no return
//   address). It's synthesized here, not added as a new AST/IR node:
//   GOSUB pushes a small integer return-id onto a synthesized global
//   array (`__basic_gosub_stack`/`__basic_gosub_sp`) and `goto`s the
//   target line; RETURN `goto`s one shared dispatch label
//   (`__basic_gosub_dispatch`, appended once at the end of `main` - see
//   appendGosubDispatch) which pops that id and compares it against each
//   pending return site (a plain if/goto chain, not a switch - see that
//   function's own comment) to jump back to the right one. Needs zero
//   IRBuilder/codegen changes,
//   same as everything else this frontend lowers.
// ---------------------------------------------------------------------

namespace rcc {

namespace {

class CountingErrorListener : public antlr4::BaseErrorListener {
   public:
    void syntaxError(antlr4::Recognizer *, antlr4::Token *, size_t, size_t, const std::string &,
                      std::exception_ptr) override {
        count++;
    }
    int count = 0;
};

void err(int line, const std::string &srcPath, const std::string &msg) {
    throw std::runtime_error(srcPath + ": line " + std::to_string(line) + ": " + msg);
}

bool isStringName(const std::string &name) { return !name.empty() && name.back() == '$'; }

std::string decodeStringLit(const std::string &text) {
    // BasicLexer.g4's STRINGLIT has no escapes - just strip the quotes.
    return text.substr(1, text.size() - 2);
}

ExprPtr makeIntLit(long value, int line) {
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::IntLit;
    e->line = line;
    e->intValue = value;
    return e;
}

ExprPtr makeVar(const std::string &name, int line) {
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::Var;
    e->line = line;
    e->name = name;
    return e;
}

ExprPtr makeBinary(const std::string &op, ExprPtr lhs, ExprPtr rhs, int line) {
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::Binary;
    e->line = line;
    e->op = op;
    e->lhs = std::move(lhs);
    e->rhs = std::move(rhs);
    return e;
}

// A braced `{expr}` initializer list would try to copy-construct its
// unique_ptr element, which ExprPtr can't do - so a one-arg call site
// builds its vector through this instead.
std::vector<ExprPtr> oneArg(ExprPtr e) {
    std::vector<ExprPtr> v;
    v.push_back(std::move(e));
    return v;
}

ExprPtr makeCall(const std::string &name, std::vector<ExprPtr> args, int line) {
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::Call;
    e->line = line;
    e->name = name;
    e->args = std::move(args);
    return e;
}

StmtPtr makeExprStmt(ExprPtr e, int line) {
    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::ExprStmt;
    s->line = line;
    s->initOrReturnOrExpr = std::move(e);
    return s;
}

StmtPtr makeBlock(std::vector<StmtPtr> stmts, int line) {
    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::Block;
    s->line = line;
    s->body = std::move(stmts);
    return s;
}

StmtPtr makeEmpty(int line) {
    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::Empty;
    s->line = line;
    return s;
}

StmtPtr makeGoto(const std::string &target, int line) {
    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::Goto;
    s->line = line;
    s->declName = target;
    return s;
}

StmtPtr makeLabel(const std::string &name, StmtPtr wrapped, int line) {
    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::Label;
    s->line = line;
    s->declName = name;
    s->thenStmt = std::move(wrapped);
    return s;
}

// One entry per statement, in program order; `lineLabel` is set only on
// the first statement of each source line (see Lowerer::lowerProgram).
struct FlatStmt {
    std::string lineLabel;
    BasicParser::StatementContext *stmt = nullptr;
};

// Builds one `main` FunctionDecl's body, plus whatever GlobalVars its
// variables/DIMs/GOSUBs need, from one parsed BASIC file.
class Lowerer {
   public:
    Lowerer(std::string srcPath, Program &combined) : srcPath_(std::move(srcPath)), combined_(combined) {}

    StmtPtr lowerProgram(BasicParser::ProgramContext *prog) {
        std::vector<FlatStmt> flat;
        for (auto *lineCtx : prog->line()) {
            if (!lineCtx->NUMBER()) continue;  // a blank line (bare NEWLINE)
            std::string lineLabel = lineCtx->NUMBER()->getText();
            bool first = true;
            for (auto *s : lineCtx->statement()) {
                flat.push_back({first ? lineLabel : std::string(), s});
                first = false;
            }
        }
        std::vector<StmtPtr> body = lowerRange(flat, 0, flat.size());
        if (gosubUsed_) appendGosubDispatch(body);
        body.push_back(makeExprStmtReturn0());
        return makeBlock(std::move(body), 0);
    }

   private:
    std::string srcPath_;
    Program &combined_;
    std::map<std::string, Type> varTypes_;
    std::set<std::string> arrayVars_;
    bool gosubGlobalsAdded_ = false;
    bool gosubUsed_ = false;
    int gosubCounter_ = 0;
    std::vector<int> pendingReturnIds_;
    std::set<std::string> externFnsAdded_;

    int lineOf(antlr4::ParserRuleContext *ctx) { return static_cast<int>(ctx->getStart()->getLine()); }

    StmtPtr makeExprStmtReturn0() {
        auto s = std::make_unique<Stmt>();
        s->kind = StmtKind::Return;
        s->initOrReturnOrExpr = makeIntLit(0, 0);
        return s;
    }

    void ensureScalar(const std::string &name, int line) {
        if (arrayVars_.count(name)) err(line, srcPath_, "'" + name + "' is already DIM'd as an array");
        if (varTypes_.count(name)) return;
        Type type = isStringName(name) ? Type{BaseType::Char, false, 1} : Type{BaseType::Int};
        varTypes_[name] = type;
        GlobalVar g;
        g.name = name;
        g.type = type;
        g.line = line;
        g.sourceFile = srcPath_;
        combined_.globals.push_back(std::move(g));
    }

    void ensureArray(const std::string &name, int size, int line) {
        if (varTypes_.count(name) || arrayVars_.count(name)) {
            err(line, srcPath_, "array '" + name + "' is already declared");
        }
        if (isStringName(name)) err(line, srcPath_, "string arrays ('" + name + "') aren't supported yet");
        Type type{BaseType::Int};
        varTypes_[name] = type;
        arrayVars_.insert(name);
        GlobalVar g;
        g.name = name;
        g.type = type;
        g.arraySize = size + 1;  // DIM A(n) is an inclusive 0..n range.
        g.arrayInit.resize(g.arraySize);  // zero-filled, same as a plain C `int a[n];` - see BuildAst.cpp's own padArrayInit.
        g.line = line;
        g.sourceFile = srcPath_;
        combined_.globals.push_back(std::move(g));
    }

    void ensureGosubGlobals(int line) {
        if (gosubGlobalsAdded_) return;
        gosubGlobalsAdded_ = true;
        GlobalVar stack;
        stack.name = "__basic_gosub_stack";
        stack.type = Type{BaseType::Int};
        stack.arraySize = 32;
        stack.arrayInit.resize(stack.arraySize);  // zero-filled - see ensureArray's own comment above.
        stack.line = line;
        stack.sourceFile = srcPath_;
        combined_.globals.push_back(std::move(stack));
        GlobalVar sp;
        sp.name = "__basic_gosub_sp";
        sp.type = Type{BaseType::Int};
        sp.hasInit = true;
        sp.initValue = 0;
        sp.line = line;
        sp.sourceFile = srcPath_;
        combined_.globals.push_back(std::move(sp));
    }

    // Registers a runtime function this frontend's lowering calls into
    // (see Phase 3 of the BASIC frontend plan - these aren't implemented
    // under src/rt/ yet) as an 'extern' prototype, so IRBuilder resolves
    // calls to it without needing a body in this file.
    void ensureExternFn(const std::string &name, bool isVoid, std::vector<Type> paramTypes) {
        if (externFnsAdded_.count(name)) return;
        externFnsAdded_.insert(name);
        FunctionDecl fn;
        fn.name = name;
        fn.isVoid = isVoid;
        fn.returnType = isVoid ? Type{} : Type{BaseType::Int};
        fn.isExternDecl = true;
        fn.sourceFile = srcPath_;
        for (size_t i = 0; i < paramTypes.size(); i++) {
            Param p;
            p.name = "a" + std::to_string(i);
            p.type = paramTypes[i];
            fn.params.push_back(p);
        }
        combined_.functions.push_back(std::move(fn));
    }

    Type stringType() { return Type{BaseType::Char, false, 1}; }

    // --- FOR/NEXT matching -------------------------------------------------

    size_t findMatchingNext(std::vector<FlatStmt> &flat, size_t forIdx, size_t end) {
        int depth = 1;
        for (size_t i = forIdx + 1; i < end; i++) {
            if (dynamic_cast<BasicParser::ForStmtContext *>(flat[i].stmt)) depth++;
            else if (dynamic_cast<BasicParser::NextStmtContext *>(flat[i].stmt)) {
                depth--;
                if (depth == 0) return i;
            }
        }
        err(lineOf(flat[forIdx].stmt), srcPath_, "FOR with no matching NEXT");
        return end;  // unreachable
    }

    std::vector<StmtPtr> lowerRange(std::vector<FlatStmt> &flat, size_t start, size_t end) {
        std::vector<StmtPtr> out;
        size_t i = start;
        while (i < end) {
            if (auto *forCtx = dynamic_cast<BasicParser::ForStmtContext *>(flat[i].stmt)) {
                size_t nextIdx = findMatchingNext(flat, i, end);
                auto *nextCtx = dynamic_cast<BasicParser::NextStmtContext *>(flat[nextIdx].stmt);
                if (nextCtx->IDENT() && nextCtx->IDENT()->getText() != forCtx->loopVar->getText()) {
                    err(lineOf(nextCtx), srcPath_,
                        "NEXT " + nextCtx->IDENT()->getText() + " doesn't match FOR " + forCtx->loopVar->getText());
                }
                std::vector<StmtPtr> bodyStmts = lowerRange(flat, i + 1, nextIdx);
                StmtPtr loop = buildForLoop(forCtx, std::move(bodyStmts));
                if (!flat[i].lineLabel.empty()) loop = makeLabel(flat[i].lineLabel, std::move(loop), lineOf(forCtx));
                out.push_back(std::move(loop));
                i = nextIdx + 1;
                continue;
            }
            if (dynamic_cast<BasicParser::NextStmtContext *>(flat[i].stmt)) {
                err(lineOf(flat[i].stmt), srcPath_, "NEXT without a matching FOR");
            }
            out.push_back(lowerOne(flat[i]));
            i++;
        }
        return out;
    }

    StmtPtr buildForLoop(BasicParser::ForStmtContext *ctx, std::vector<StmtPtr> bodyStmts) {
        int line = lineOf(ctx);
        std::string loopVar = ctx->loopVar->getText();
        ensureScalar(loopVar, line);
        int id = ++gosubCounter_;
        std::string toVar = "__basic_for_to_" + std::to_string(id);
        std::string stepVar = "__basic_for_step_" + std::to_string(id);
        ensureScalar(toVar, line);
        ensureScalar(stepVar, line);

        std::vector<StmtPtr> init;
        init.push_back(makeExprStmt(makeAssign(loopVar, nullptr, lowerExpr(ctx->from), line), line));
        init.push_back(makeExprStmt(makeAssign(toVar, nullptr, lowerExpr(ctx->to), line), line));
        ExprPtr stepExpr = ctx->step ? lowerExpr(ctx->step) : makeIntLit(1, line);
        init.push_back(makeExprStmt(makeAssign(stepVar, nullptr, std::move(stepExpr), line), line));

        auto cond = std::make_unique<Expr>();
        cond->kind = ExprKind::Ternary;
        cond->line = line;
        cond->lhs = makeBinary(">=", makeVar(stepVar, line), makeIntLit(0, line), line);
        cond->rhs = makeBinary("<=", makeVar(loopVar, line), makeVar(toVar, line), line);
        cond->elseExpr = makeBinary(">=", makeVar(loopVar, line), makeVar(toVar, line), line);

        auto post = makeExprStmt(makeCompoundAssign(loopVar, "+", makeVar(stepVar, line), line), line);

        auto whileStmt = std::make_unique<Stmt>();
        whileStmt->kind = StmtKind::While;
        whileStmt->line = line;
        whileStmt->cond = std::move(cond);
        whileStmt->loopBody = makeBlock(std::move(bodyStmts), line);
        whileStmt->postStmt = std::move(post);

        init.push_back(std::move(whileStmt));
        return makeBlock(std::move(init), line);
    }

    // --- GOSUB/RETURN dispatch ---------------------------------------------

    // A chain of `if`/`goto` rather than a `switch` on the popped id - see
    // docs/known-bugs.md's "The 6502 backend corrupts a local variable
    // declared inside a block only reached via `goto`": IRBuilder
    // desugars `switch (expr)` through a hidden local holding `expr`'s
    // value, and that local comes back corrupted on --target=6502 inside
    // a block (like this one) only ever reached via a non-local `goto`.
    // `if` needs no such local, so it isn't affected.
    void appendGosubDispatch(std::vector<StmtPtr> &body) {
        auto popSp = makeExprStmt(makeCompoundAssign("__basic_gosub_sp", "-", makeIntLit(1, 0), 0), 0);

        std::vector<StmtPtr> dispatchBody;
        dispatchBody.push_back(std::move(popSp));
        for (int id : pendingReturnIds_) {
            auto stackTop = std::make_unique<Expr>();
            stackTop->kind = ExprKind::Index;
            stackTop->name = "__basic_gosub_stack";
            stackTop->lhs = makeVar("__basic_gosub_sp", 0);

            auto ifStmt = std::make_unique<Stmt>();
            ifStmt->kind = StmtKind::If;
            ifStmt->cond = makeBinary("==", std::move(stackTop), makeIntLit(id, 0), 0);
            ifStmt->thenStmt = makeGoto("__basic_ret_" + std::to_string(id), 0);
            dispatchBody.push_back(std::move(ifStmt));
        }
        body.push_back(makeLabel("__basic_gosub_dispatch", makeBlock(std::move(dispatchBody), 0), 0));
    }

    // --- Assignment helpers --------------------------------------------------

    ExprPtr makeAssign(const std::string &name, ExprPtr idx, ExprPtr val, int line) {
        auto e = std::make_unique<Expr>();
        e->kind = ExprKind::Assign;
        e->line = line;
        e->name = name;
        e->lhs = std::move(idx);
        e->rhs = std::move(val);
        return e;
    }

    ExprPtr makeCompoundAssign(const std::string &name, const std::string &op, ExprPtr val, int line) {
        auto e = std::make_unique<Expr>();
        e->kind = ExprKind::CompoundAssign;
        e->line = line;
        e->name = name;
        e->op = op;
        e->rhs = std::move(val);
        return e;
    }

    // --- Statement lowering --------------------------------------------------

    StmtPtr lowerOne(FlatStmt &fs) {
        StmtPtr lowered = lowerStatementNode(fs.stmt);
        if (fs.lineLabel.empty()) return lowered;
        return makeLabel(fs.lineLabel, std::move(lowered), lineOf(fs.stmt));
    }

    // Used for IF's thenStmt/elseStmt: same as lowerOne but FOR/NEXT make
    // no sense as a single inline statement (see docs/basic-language.md's
    // "single-line only" IF restriction).
    StmtPtr lowerInlineStatement(BasicParser::StatementContext *s) {
        if (dynamic_cast<BasicParser::ForStmtContext *>(s) || dynamic_cast<BasicParser::NextStmtContext *>(s)) {
            err(lineOf(s), srcPath_, "FOR/NEXT can't be used as an IF/THEN inline statement");
        }
        return lowerStatementNode(s);
    }

    StmtPtr lowerStatementNode(BasicParser::StatementContext *s) {
        int line = lineOf(s);
        if (auto *c = dynamic_cast<BasicParser::LetStmtContext *>(s)) {
            std::string name = c->target->getText();
            if (c->idx) {
                if (!arrayVars_.count(name)) err(line, srcPath_, "array '" + name + "' used before DIM");
                return makeExprStmt(makeAssign(name, lowerExpr(c->idx), lowerExpr(c->val), line), line);
            }
            ensureScalar(name, line);
            return makeExprStmt(makeAssign(name, nullptr, lowerExpr(c->val), line), line);
        }
        if (auto *c = dynamic_cast<BasicParser::PrintStmtContext *>(s)) return lowerPrint(c);
        if (auto *c = dynamic_cast<BasicParser::InputStmtContext *>(s)) return lowerInput(c);
        if (auto *c = dynamic_cast<BasicParser::IfStmtContext *>(s)) {
            auto ifStmt = std::make_unique<Stmt>();
            ifStmt->kind = StmtKind::If;
            ifStmt->line = line;
            ifStmt->cond = lowerExpr(c->cond);
            ifStmt->thenStmt = lowerInlineStatement(c->thenStmt);
            if (c->elseStmt) ifStmt->elseStmt = lowerInlineStatement(c->elseStmt);
            return ifStmt;
        }
        if (auto *c = dynamic_cast<BasicParser::GotoStmtContext *>(s)) return makeGoto(c->target->getText(), line);
        if (auto *c = dynamic_cast<BasicParser::GotoShorthandStmtContext *>(s)) {
            return makeGoto(c->target->getText(), line);
        }
        if (auto *c = dynamic_cast<BasicParser::GosubStmtContext *>(s)) return lowerGosub(c);
        if (dynamic_cast<BasicParser::ReturnStmtContext *>(s)) {
            gosubUsed_ = true;
            return makeGoto("__basic_gosub_dispatch", line);
        }
        if (auto *c = dynamic_cast<BasicParser::DimStmtContext *>(s)) {
            ensureArray(c->name->getText(), std::stoi(c->size->getText()), line);
            return makeEmpty(line);
        }
        if (dynamic_cast<BasicParser::EndStmtContext *>(s)) {
            auto ret = std::make_unique<Stmt>();
            ret->kind = StmtKind::Return;
            ret->line = line;
            ret->initOrReturnOrExpr = makeIntLit(0, line);
            return ret;
        }
        err(line, srcPath_, "internal: unhandled BASIC statement");
        return makeEmpty(line);  // unreachable
    }

    StmtPtr lowerGosub(BasicParser::GosubStmtContext *c) {
        int line = lineOf(c);
        ensureGosubGlobals(line);
        gosubUsed_ = true;
        int id = ++gosubCounter_;
        pendingReturnIds_.push_back(id);
        std::string retLabel = "__basic_ret_" + std::to_string(id);

        std::vector<StmtPtr> stmts;
        stmts.push_back(makeExprStmt(
            makeAssign("__basic_gosub_stack", makeVar("__basic_gosub_sp", line), makeIntLit(id, line), line), line));
        stmts.push_back(
            makeExprStmt(makeCompoundAssign("__basic_gosub_sp", "+", makeIntLit(1, line), line), line));
        stmts.push_back(makeGoto(c->target->getText(), line));
        stmts.push_back(makeLabel(retLabel, makeEmpty(line), line));
        return makeBlock(std::move(stmts), line);
    }

    StmtPtr lowerPrint(BasicParser::PrintStmtContext *c) {
        int line = lineOf(c);
        std::vector<StmtPtr> stmts;
        auto *list = c->printList();
        bool trailingSep = list && list->trailingSep != nullptr;
        if (list) {
            for (size_t i = 0; i < list->item.size(); i++) {
                stmts.push_back(makeExprStmt(lowerPrintItem(list->item[i]), line));
                if (i + 1 < list->item.size() && list->sep[i]->getText() == ",") {
                    stmts.push_back(makeExprStmt(makeCall("__basic_print_tab", {}, line), line));
                    ensureExternFn("__basic_print_tab", /*isVoid=*/true, {});
                }
            }
        }
        if (!trailingSep) {
            stmts.push_back(makeExprStmt(makeCall("__basic_print_newline", {}, line), line));
            ensureExternFn("__basic_print_newline", /*isVoid=*/true, {});
        }
        return makeBlock(std::move(stmts), line);
    }

    // A PRINT item is itself the Call to the right runtime print
    // function - not a bare value - so ExprStmt-wrapping it is enough.
    ExprPtr lowerPrintItem(BasicParser::ExprContext *itemCtx) {
        int line = lineOf(itemCtx);
        if (isStringExprSyntax(itemCtx)) {
            ensureExternFn("__basic_print_str", /*isVoid=*/true, {stringType()});
            return makeCall("__basic_print_str", oneArg(lowerExpr(itemCtx)), line);
        }
        ensureExternFn("__basic_print_int", /*isVoid=*/true, {Type{BaseType::Int}});
        return makeCall("__basic_print_int", oneArg(lowerExpr(itemCtx)), line);
    }

    StmtPtr lowerInput(BasicParser::InputStmtContext *c) {
        int line = lineOf(c);
        std::vector<StmtPtr> stmts;
        if (c->prompt) {
            ensureExternFn("__basic_print_str", /*isVoid=*/true, {stringType()});
            auto lit = std::make_unique<Expr>();
            lit->kind = ExprKind::StringLit;
            lit->line = line;
            lit->name = decodeStringLit(c->prompt->getText());
            stmts.push_back(
                makeExprStmt(makeCall("__basic_print_str", oneArg(std::move(lit)), line), line));
        }
        std::string target = c->target->getText();
        ensureScalar(target, line);
        if (isStringName(target)) {
            ensureExternFn("__basic_input_str", /*isVoid=*/false, {});
            stmts.push_back(
                makeExprStmt(makeAssign(target, nullptr, makeCall("__basic_input_str", {}, line), line), line));
        } else {
            ensureExternFn("__basic_input_int", /*isVoid=*/false, {});
            stmts.push_back(
                makeExprStmt(makeAssign(target, nullptr, makeCall("__basic_input_int", {}, line), line), line));
        }
        return makeBlock(std::move(stmts), line);
    }

    // --- Expression lowering --------------------------------------------------

    // Syntactic only (no real type inference - see BasicFrontend.h's own
    // header comment): a string literal, or a variable/array-element whose
    // name ends '$'. Good enough to pick the right PRINT/INPUT runtime
    // call and to reject string '+' before it's silently mis-lowered as
    // integer addition.
    bool isStringExprSyntax(BasicParser::ExprContext *ctx) {
        if (dynamic_cast<BasicParser::StringExprContext *>(ctx)) return true;
        if (auto *v = dynamic_cast<BasicParser::VarExprContext *>(ctx)) return isStringName(v->IDENT()->getText());
        if (auto *idx = dynamic_cast<BasicParser::IndexExprContext *>(ctx)) {
            return isStringName(idx->target->getText());
        }
        if (auto *p = dynamic_cast<BasicParser::ParenExprContext *>(ctx)) return isStringExprSyntax(p->expr());
        return false;
    }

    ExprPtr lowerExpr(BasicParser::ExprContext *ctx) {
        int line = lineOf(ctx);
        if (auto *c = dynamic_cast<BasicParser::IndexExprContext *>(ctx)) {
            std::string name = c->target->getText();
            if (!arrayVars_.count(name)) err(line, srcPath_, "array '" + name + "' used before DIM");
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Index;
            e->line = line;
            e->name = name;
            e->lhs = lowerExpr(c->idx);
            return e;
        }
        if (auto *c = dynamic_cast<BasicParser::VarExprContext *>(ctx)) {
            std::string name = c->IDENT()->getText();
            ensureScalar(name, line);
            return makeVar(name, line);
        }
        if (auto *c = dynamic_cast<BasicParser::NumExprContext *>(ctx)) {
            return makeIntLit(std::stol(c->NUMBER()->getText()), line);
        }
        if (auto *c = dynamic_cast<BasicParser::StringExprContext *>(ctx)) {
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::StringLit;
            e->line = line;
            e->name = decodeStringLit(c->STRINGLIT()->getText());
            return e;
        }
        if (auto *c = dynamic_cast<BasicParser::ParenExprContext *>(ctx)) return lowerExpr(c->expr());
        if (auto *c = dynamic_cast<BasicParser::NegExprContext *>(ctx)) {
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Unary;
            e->line = line;
            e->op = "-";
            e->rhs = lowerExpr(c->expr());
            return e;
        }
        if (auto *c = dynamic_cast<BasicParser::NotExprContext *>(ctx)) {
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Unary;
            e->line = line;
            e->op = "!";
            e->rhs = lowerExpr(c->expr());
            return e;
        }
        if (auto *c = dynamic_cast<BasicParser::MulExprContext *>(ctx)) {
            return makeBinary(c->op->getText(), lowerExpr(c->expr(0)), lowerExpr(c->expr(1)), line);
        }
        if (auto *c = dynamic_cast<BasicParser::AddExprContext *>(ctx)) {
            if (isStringExprSyntax(c->expr(0)) || isStringExprSyntax(c->expr(1))) {
                err(line, srcPath_, "string concatenation ('+') isn't supported yet");
            }
            return makeBinary(c->op->getText(), lowerExpr(c->expr(0)), lowerExpr(c->expr(1)), line);
        }
        if (auto *c = dynamic_cast<BasicParser::RelExprContext *>(ctx)) {
            std::string op = c->op->getText();
            if (op == "=") op = "==";
            else if (op == "<>") op = "!=";
            return makeBinary(op, lowerExpr(c->expr(0)), lowerExpr(c->expr(1)), line);
        }
        if (auto *c = dynamic_cast<BasicParser::AndExprContext *>(ctx)) {
            return makeBinary("&&", lowerExpr(c->expr(0)), lowerExpr(c->expr(1)), line);
        }
        if (auto *c = dynamic_cast<BasicParser::OrExprContext *>(ctx)) {
            return makeBinary("||", lowerExpr(c->expr(0)), lowerExpr(c->expr(1)), line);
        }
        err(line, srcPath_, "internal: unhandled BASIC expr");
        return makeIntLit(0, line);  // unreachable
    }
};

}  // namespace

void BasicFrontend::parseAndMergeFile(const std::string &srcPath,
                                       const std::map<std::string, std::string> & /*predefinedMacros*/,
                                       Program &combined) const {
    std::ifstream in(srcPath);
    if (!in) throw std::runtime_error(srcPath + ": could not open file");
    std::stringstream ss;
    ss << in.rdbuf();
    std::string source = ss.str();

    CountingErrorListener errors;
    antlr4::ANTLRInputStream input(source);
    BasicLexer lexer(&input);
    lexer.removeErrorListeners();
    lexer.addErrorListener(&errors);

    antlr4::CommonTokenStream tokens(&lexer);
    BasicParser parser(&tokens);
    parser.removeErrorListeners();
    parser.addErrorListener(&errors);

    BasicParser::ProgramContext *tree = parser.program();
    if (errors.count > 0) throw std::runtime_error(srcPath + ": syntax errors, not compiling");

    Lowerer lowerer(srcPath, combined);
    StmtPtr body = lowerer.lowerProgram(tree);

    FunctionDecl main;
    main.name = "main";
    main.returnType = Type{BaseType::Int};
    main.body = std::move(body);
    main.sourceFile = srcPath;
    combined.functions.push_back(std::move(main));
}

}  // namespace rcc
