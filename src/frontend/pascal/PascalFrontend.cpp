#include "PascalFrontend.h"

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
#include "CommonTokenStream.h"
#include "PascalLexer.h"
#include "PascalParser.h"

// ---------------------------------------------------------------------
// Lowers a PascalParser parse tree straight into rcc::AST - see
// docs/pascal-language.md for the v1 subset and PascalFrontend.h's own
// header comment for the overall shape. A few things don't map onto an
// existing AST node as directly as everything else does:
//
// - A function's own name, assigned to inside its body (`F := expr;`,
//   Pascal's way of setting the return value), lowers onto a
//   synthesized "__result" local instead of a real AST concept of its
//   own: every function gets one, assignments to the function's own
//   name redirect to it, and the body ends with `return __result;`.
// - `repeat ... until cond` lowers onto `while(1) { ...; if (cond)
//   break; }` - the same existing While/If/Break nodes, just assembled
//   so the condition is checked after the body instead of before.
// - A set (`set of T`) has no analog anywhere in rcc - there's no
//   bitset type. It's synthesized as a plain `int` bit-vector, which
//   only works for a base type with at most 32 distinct values (see
//   resolveSetType) - `+`/`-`/`*`/`in` lower to the matching bitwise
//   op, and a `[...]` set constructor lowers to a chain of `|`'d-in
//   bits (see lowerSetLiteralToBitmask). A set literal built directly
//   against a declared set variable (`s := [...]`, or `x in [...]`)
//   gets that variable's own low bound; built any other way it
//   defaults to a 0 low bound, so its own element values must already
//   sit in 0..31 - a deliberate v1 restriction, not a bug.
// - A `var` parameter lowers onto an ordinary pointer parameter with
//   every read/write auto-wrapped in a dereference at its use sites
//   (tracked via currentVarParams_ while that routine's body is being
//   lowered) - the parameter's own name never appears as a raw pointer
//   value except when forwarding it on to another `var` parameter.
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

// Pascal's only quoting form (also doubles as a char literal - see
// docs/pascal-language.md); a literal quote is a doubled quote.
std::string decodeStringLit(const std::string &text) {
    std::string inner = text.substr(1, text.size() - 2);
    std::string out;
    for (size_t i = 0; i < inner.size(); i++) {
        if (inner[i] == '\'' && i + 1 < inner.size() && inner[i + 1] == '\'') {
            out.push_back('\'');
            i++;
        } else {
            out.push_back(inner[i]);
        }
    }
    return out;
}

// --- AST construction helpers (no state needed) -------------------------

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

ExprPtr makeUnary(const std::string &op, ExprPtr rhs, int line) {
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::Unary;
    e->line = line;
    e->op = op;
    e->rhs = std::move(rhs);
    return e;
}

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

ExprPtr makeAssignPlain(const std::string &name, ExprPtr rhs, int line) {
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::Assign;
    e->line = line;
    e->name = name;
    e->rhs = std::move(rhs);
    return e;
}

ExprPtr makeCompoundAssign(const std::string &name, const std::string &op, ExprPtr rhs, int line) {
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::CompoundAssign;
    e->line = line;
    e->name = name;
    e->op = op;
    e->rhs = std::move(rhs);
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

// --- Semantic type side-table -----------------------------------------
//
// rcc::Type alone can't tell a plain int apart from a set, or carry an
// array's per-dimension lower bound, so every variable/param/field this
// frontend resolves keeps one of these alongside its rcc::Type - see
// resolveTypeSpec.
struct PType {
    Type rcc;
    bool isArray = false;
    std::vector<int> dims;        // element count per dimension
    std::vector<long> lowBounds;  // declared lower bound per dimension
    Type elemType;                // valid when isArray
    bool isSet = false;
    long setLow = 0;  // lowest ordinal value this set's bit 0 represents
    int setCount = 0;

    PType() = default;
    PType(Type t) : rcc(t) {}
};

struct FuncSig {
    std::vector<Param> params;           // already var-wrapped (pointerTo) where isVarParam
    std::vector<bool> isVarParam;
    std::vector<PType> paramLogicalTypes;  // the Pascal-level type, no pointer wrapper
    bool isFunction = false;
    PType returnType;
};

enum class SuffixKind { Call, Index, Field, Deref };

struct DesigSuffix {
    SuffixKind kind;
    PascalParser::ExprListContext *exprList = nullptr;
    std::string fieldName;
};

// Walks a designator's raw children instead of its per-token-kind
// accessor lists: `(` and `[` suffixes both carry an ExprListContext,
// so the only way to tell a call's argument list apart from an array
// index (and to recover the real, possibly-interleaved suffix order -
// `arr[i].field`, `p^.field`, ...) is to read the children in sequence.
std::vector<DesigSuffix> parseSuffixes(PascalParser::DesignatorContext *ctx) {
    std::vector<DesigSuffix> out;
    auto &kids = ctx->children;
    size_t i = 1;  // kids[0] is the leading IDENT
    while (i < kids.size()) {
        auto *term = dynamic_cast<antlr4::tree::TerminalNode *>(kids[i]);
        std::string text = term ? term->getText() : "";
        if (text == "(") {
            DesigSuffix s;
            s.kind = SuffixKind::Call;
            i++;
            if (auto *el = dynamic_cast<PascalParser::ExprListContext *>(kids[i])) {
                s.exprList = el;
                i++;
            }
            i++;  // ')'
            out.push_back(s);
        } else if (text == "[") {
            DesigSuffix s;
            s.kind = SuffixKind::Index;
            i++;
            s.exprList = dynamic_cast<PascalParser::ExprListContext *>(kids[i]);
            i += 2;  // exprList, ']'
            out.push_back(s);
        } else if (text == ".") {
            DesigSuffix s;
            s.kind = SuffixKind::Field;
            i++;
            s.fieldName = dynamic_cast<antlr4::tree::TerminalNode *>(kids[i])->getText();
            i++;
            out.push_back(s);
        } else if (text == "^") {
            out.push_back(DesigSuffix{SuffixKind::Deref, nullptr, ""});
            i++;
        } else {
            i++;
        }
    }
    return out;
}

PascalParser::FactorContext *unwrapSimpleExprToFactor(PascalParser::SimpleExprContext *se) {
    if (se->sign || !se->addOp().empty()) return nullptr;
    auto *t = se->term(0);
    if (!t->mulOp().empty()) return nullptr;
    return t->factor(0);
}

PascalParser::FactorContext *unwrapToPlainFactor(PascalParser::ExprContext *ctx) {
    if (ctx->relOp()) return nullptr;
    return unwrapSimpleExprToFactor(ctx->simpleExpr(0));
}

// Builds one `main` FunctionDecl (the program's own statement part)
// plus one FunctionDecl per top-level `procedure`/`function`, and
// whatever GlobalVars/StructDecls they need, from one parsed Pascal
// file.
class Lowerer {
   public:
    Lowerer(std::string srcPath, Program &combined) : srcPath_(std::move(srcPath)), combined_(combined) {}

    void lowerProgram(PascalParser::ProgramContext *prog) {
        auto *blockCtx = prog->block();
        for (auto *dp : blockCtx->declPart()) processDeclPart(dp, /*topLevel=*/true);

        for (auto *p : procDeclsToLower_) {
            std::string name = p->IDENT()->getText();
            lowerRoutineBody(name, funcSigs_[name], p->block(), lineOf(p));
        }
        for (auto *f : funcDeclsToLower_) {
            std::string name = f->IDENT()->getText();
            lowerRoutineBody(name, funcSigs_[name], f->block(), lineOf(f));
        }

        localVars_.clear();
        currentVarParams_.clear();
        currentFunctionName_.clear();

        std::vector<StmtPtr> mainStmts;
        mainStmts.push_back(lowerCompoundStmt(blockCtx->compoundStmt()));
        auto ret = std::make_unique<Stmt>();
        ret->kind = StmtKind::Return;
        ret->initOrReturnOrExpr = makeIntLit(0, 0);
        mainStmts.push_back(std::move(ret));

        FunctionDecl main;
        main.name = "main";
        main.returnType = Type{BaseType::Int};
        main.sourceFile = srcPath_;
        main.body = makeBlock(std::move(mainStmts), 0);
        combined_.functions.push_back(std::move(main));
    }

   private:
    std::string srcPath_;
    Program &combined_;

    std::map<std::string, PType> typeAliases_;
    std::map<std::string, long> constInts_;
    std::map<std::string, double> floatConsts_;
    std::map<std::string, int> enumMemberCount_;
    std::set<std::string> structTags_;
    int anonRecordCounter_ = 0;
    int tempCounter_ = 0;

    std::map<std::string, PType> globalVars_;
    std::map<std::string, PType> localVars_;
    std::set<std::string> currentVarParams_;
    std::string currentFunctionName_;
    std::vector<StmtPtr> localDeclStmts_;

    std::map<std::string, FuncSig> funcSigs_;
    std::vector<PascalParser::ProcDeclContext *> procDeclsToLower_;
    std::vector<PascalParser::FuncDeclContext *> funcDeclsToLower_;
    std::set<std::string> externFnsAdded_;

    int lineOf(antlr4::ParserRuleContext *ctx) { return static_cast<int>(ctx->getStart()->getLine()); }

    const PType *lookupVarType(const std::string &name) {
        auto lit = localVars_.find(name);
        if (lit != localVars_.end()) return &lit->second;
        auto git = globalVars_.find(name);
        if (git != globalVars_.end()) return &git->second;
        return nullptr;
    }

    const FuncSig *lookupFuncSig(const std::string &name) {
        auto it = funcSigs_.find(name);
        return it == funcSigs_.end() ? nullptr : &it->second;
    }

    bool isVarParamName(const std::string &name) { return currentVarParams_.count(name) > 0; }

    // Registers a runtime function this frontend's lowering calls into
    // (write/writeln/read/readln - not implemented under src/rt/ yet)
    // as an 'extern' prototype, same as the BASIC frontend's own
    // ensureExternFn.
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

    // --- Compile-time constants ---------------------------------------

    long evalConstInt(PascalParser::ConstExprContext *ctx) {
        int line = lineOf(ctx);
        if (ctx->STRINGLIT()) {
            std::string s = decodeStringLit(ctx->STRINGLIT()->getText());
            if (s.size() != 1) err(line, srcPath_, "only a single-character string literal is valid as an integer constant here");
            return static_cast<unsigned char>(s[0]);
        }
        long sign = (ctx->sign && ctx->sign->getText() == "-") ? -1 : 1;
        if (ctx->INTLIT()) return sign * std::stol(ctx->INTLIT()->getText());
        if (ctx->REALLIT()) err(line, srcPath_, "a real literal isn't valid as an integer constant here");
        std::string name = ctx->IDENT()->getText();
        auto it = constInts_.find(name);
        if (it == constInts_.end()) {
            if (floatConsts_.count(name)) {
                err(line, srcPath_, "'" + name + "' is a real constant, not valid as an integer constant here");
            }
            err(line, srcPath_, "unknown constant '" + name + "'");
        }
        return sign * it->second;
    }

    // A restricted constant folder for a set literal's range bound
    // (`5..9` in `[1, 5..9]`), which the grammar spells as a general
    // `expr`, not a `constExpr` - see lowerSetLiteralToBitmask.
    long evalConstIntFromExpr(PascalParser::ExprContext *ctx) {
        int line = lineOf(ctx);
        auto *f = unwrapToPlainFactor(ctx);
        if (!f) err(line, srcPath_, "a set range bound must be a compile-time constant, not an expression");
        if (auto *se = ctx->simpleExpr(0); se->sign) {
            long sign = se->sign->getText() == "-" ? -1 : 1;
            return sign * evalConstIntFromFactor(f, line);
        }
        return evalConstIntFromFactor(f, line);
    }

    long evalConstIntFromFactor(PascalParser::FactorContext *f, int line) {
        if (auto *i = dynamic_cast<PascalParser::IntFactorContext *>(f)) return std::stol(i->INTLIT()->getText());
        if (auto *s = dynamic_cast<PascalParser::StringFactorContext *>(f)) {
            std::string str = decodeStringLit(s->STRINGLIT()->getText());
            if (str.size() != 1) err(line, srcPath_, "only a single-character string literal is a valid constant here");
            return static_cast<unsigned char>(str[0]);
        }
        if (auto *d = dynamic_cast<PascalParser::DesignatorFactorContext *>(f)) {
            if (!parseSuffixes(d->designator()).empty()) {
                err(line, srcPath_, "a compile-time constant here must be a plain name, not a call/index/field");
            }
            std::string name = d->designator()->IDENT(0)->getText();
            auto it = constInts_.find(name);
            if (it == constInts_.end()) err(line, srcPath_, "'" + name + "' isn't a known compile-time constant here");
            return it->second;
        }
        err(line, srcPath_, "this frontend needs a compile-time constant here");
    }

    // --- Type resolution -----------------------------------------------

    PType resolveTypeSpec(PascalParser::TypeSpecContext *ctx, const std::string &nameHint = "") {
        int line = lineOf(ctx);
        if (dynamic_cast<PascalParser::IntegerTypeContext *>(ctx)) return PType(Type{BaseType::Int});
        if (dynamic_cast<PascalParser::RealTypeContext *>(ctx)) return PType(Type{BaseType::Float});
        if (dynamic_cast<PascalParser::CharTypeContext *>(ctx)) return PType(Type{BaseType::Char});
        if (dynamic_cast<PascalParser::BooleanTypeContext *>(ctx)) return PType(Type{BaseType::Int});
        if (auto *c = dynamic_cast<PascalParser::PointerTypeContext *>(ctx)) {
            PType inner = resolveTypeSpec(c->typeSpec());
            if (inner.isArray) err(line, srcPath_, "a pointer to an array type isn't supported in this frontend");
            if (inner.isSet) err(line, srcPath_, "a pointer to a set type isn't supported in this frontend");
            PType pt(pointerTo(inner.rcc));
            return pt;
        }
        if (auto *c = dynamic_cast<PascalParser::ArrayTypeContext *>(ctx)) {
            PType elem = resolveTypeSpec(c->typeSpec());
            if (elem.isArray) {
                err(line, srcPath_,
                    "nested array types aren't supported - declare every dimension in one array[...] of ... instead");
            }
            PType pt;
            pt.isArray = true;
            pt.elemType = elem.rcc;
            pt.rcc = elem.rcc;
            for (auto *rs : c->rangeSpec()) {
                long lo = evalConstInt(rs->constExpr(0));
                long hi = evalConstInt(rs->constExpr(1));
                if (hi < lo) err(lineOf(rs), srcPath_, "an array bound's high value must not be less than its low value");
                pt.lowBounds.push_back(lo);
                pt.dims.push_back(static_cast<int>(hi - lo + 1));
            }
            return pt;
        }
        if (auto *c = dynamic_cast<PascalParser::SetTypeContext *>(ctx)) return resolveSetType(c, line);
        if (auto *c = dynamic_cast<PascalParser::RecordTypeContext *>(ctx)) return resolveRecordType(c, nameHint, line);
        if (auto *c = dynamic_cast<PascalParser::EnumTypeContext *>(ctx)) return resolveEnumType(c, nameHint);
        if (auto *c = dynamic_cast<PascalParser::SubrangeTypeContext *>(ctx)) {
            evalConstInt(c->constExpr(0));  // no range checking in v1 - see docs/pascal-language.md
            evalConstInt(c->constExpr(1));
            return PType(Type{BaseType::Int});
        }
        if (auto *c = dynamic_cast<PascalParser::NamedTypeContext *>(ctx)) {
            auto it = typeAliases_.find(c->IDENT()->getText());
            if (it == typeAliases_.end()) err(line, srcPath_, "unknown type '" + c->IDENT()->getText() + "'");
            return it->second;
        }
        err(line, srcPath_, "internal: unhandled Pascal typeSpec alternative");
    }

    PType resolveSetType(PascalParser::SetTypeContext *c, int line) {
        auto *base = c->typeSpec();
        long lo = 0;
        int count = 0;
        if (auto *sr = dynamic_cast<PascalParser::SubrangeTypeContext *>(base)) {
            lo = evalConstInt(sr->constExpr(0));
            long hi = evalConstInt(sr->constExpr(1));
            count = static_cast<int>(hi - lo + 1);
        } else if (dynamic_cast<PascalParser::BooleanTypeContext *>(base)) {
            count = 2;
        } else if (auto *nt = dynamic_cast<PascalParser::NamedTypeContext *>(base)) {
            auto it = enumMemberCount_.find(nt->IDENT()->getText());
            if (it == enumMemberCount_.end()) {
                err(line, srcPath_, "'set of " + nt->IDENT()->getText() +
                                         "' needs a subrange, boolean, or previously declared enumerated type");
            }
            count = it->second;
        } else {
            err(line, srcPath_,
                "'set of ...' needs a subrange, boolean, or enumerated type as its base in this frontend");
        }
        if (count <= 0 || count > 32) {
            err(line, srcPath_, "a set's base type must have between 1 and 32 distinct values in this frontend (has " +
                                     std::to_string(count) + ")");
        }
        PType pt(Type{BaseType::Int});
        pt.isSet = true;
        pt.setLow = lo;
        pt.setCount = count;
        return pt;
    }

    PType resolveRecordType(PascalParser::RecordTypeContext *c, const std::string &nameHint, int line) {
        std::string tag = nameHint.empty() ? ("__pascal_anon_record_" + std::to_string(++anonRecordCounter_)) : nameHint;
        if (structTags_.count(tag)) err(line, srcPath_, "redefinition of record type '" + tag + "'");
        StructDecl decl;
        decl.tag = tag;
        decl.line = line;
        decl.sourceFile = srcPath_;
        auto *fl = c->fieldList();
        auto idLists = fl->identList();
        auto typeSpecs = fl->typeSpec();
        for (size_t i = 0; i < idLists.size(); i++) {
            PType fieldPt = resolveTypeSpec(typeSpecs[i]);
            int fline = lineOf(idLists[i]);
            if (fieldPt.isArray && fieldPt.dims.size() > 1) {
                err(fline, srcPath_, "a record field's array must be one-dimensional in this frontend");
            }
            if (fieldPt.isArray && fieldPt.lowBounds[0] != 0) {
                err(fline, srcPath_, "a record field's array must start at a 0 lower bound in this frontend");
            }
            for (auto *idTok : idLists[i]->IDENT()) {
                StructMemberDecl m;
                m.name = idTok->getText();
                m.line = fline;
                m.type = fieldPt.isArray ? fieldPt.elemType : fieldPt.rcc;
                m.arraySize = fieldPt.isArray ? fieldPt.dims[0] : 0;
                decl.members.push_back(std::move(m));
            }
        }
        structTags_.insert(tag);
        combined_.structs.push_back(std::move(decl));
        PType pt(Type{BaseType::Struct});
        pt.rcc.structTag = tag;
        return pt;
    }

    PType resolveEnumType(PascalParser::EnumTypeContext *c, const std::string &nameHint) {
        int line = lineOf(c);
        long next = 0;
        for (auto *idTok : c->IDENT()) {
            const std::string &name = idTok->getText();
            if (constInts_.count(name) || floatConsts_.count(name)) err(line, srcPath_, "redefinition of constant '" + name + "'");
            constInts_[name] = next++;
        }
        if (!nameHint.empty()) enumMemberCount_[nameHint] = static_cast<int>(next);
        return PType(Type{BaseType::Int});
    }

    // --- Declaration lowering -------------------------------------------

    void lowerConstDecl(PascalParser::ConstDeclContext *ctx) {
        auto names = ctx->IDENT();
        auto exprs = ctx->constExpr();
        for (size_t i = 0; i < names.size(); i++) {
            std::string name = names[i]->getText();
            if (constInts_.count(name) || floatConsts_.count(name) || typeAliases_.count(name)) {
                err(lineOf(ctx), srcPath_, "redefinition of '" + name + "'");
            }
            auto *ce = exprs[i];
            if (ce->REALLIT()) {
                long sign = (ce->sign && ce->sign->getText() == "-") ? -1 : 1;
                floatConsts_[name] = sign * std::stod(ce->REALLIT()->getText());
            } else {
                constInts_[name] = evalConstInt(ce);
            }
        }
    }

    void lowerTypeDecl(PascalParser::TypeDeclContext *ctx) {
        auto names = ctx->IDENT();
        auto specs = ctx->typeSpec();
        for (size_t i = 0; i < names.size(); i++) {
            std::string name = names[i]->getText();
            if (typeAliases_.count(name)) err(lineOf(ctx), srcPath_, "redefinition of type '" + name + "'");
            typeAliases_[name] = resolveTypeSpec(specs[i], name);
        }
    }

    void lowerVarDecl(PascalParser::VarDeclContext *ctx, bool global) {
        auto idLists = ctx->identList();
        auto specs = ctx->typeSpec();
        for (size_t i = 0; i < idLists.size(); i++) {
            PType pt = resolveTypeSpec(specs[i]);
            for (auto *idTok : idLists[i]->IDENT()) {
                std::string name = idTok->getText();
                int line = lineOf(idLists[i]);
                if (constInts_.count(name) || floatConsts_.count(name) || typeAliases_.count(name)) {
                    err(line, srcPath_, "'" + name + "' is already a constant/type name");
                }
                auto &scope = global ? globalVars_ : localVars_;
                if (scope.count(name)) err(line, srcPath_, "redefinition of '" + name + "'");
                scope[name] = pt;

                int arraySize = 0;
                std::vector<int> arrayDims;
                std::vector<ArrayInitElem> arrayInit;
                Type storedType = pt.rcc;
                if (pt.isArray) {
                    storedType = pt.elemType;
                    arraySize = 1;
                    for (int d : pt.dims) arraySize *= d;
                    if (pt.dims.size() > 1) arrayDims = pt.dims;
                    arrayInit.resize(arraySize);
                }
                if (global) {
                    GlobalVar g;
                    g.name = name;
                    g.type = storedType;
                    g.line = line;
                    g.sourceFile = srcPath_;
                    g.arraySize = arraySize;
                    g.arrayDims = arrayDims;
                    g.arrayInit = arrayInit;
                    combined_.globals.push_back(std::move(g));
                } else {
                    auto s = std::make_unique<Stmt>();
                    s->kind = StmtKind::LocalDecl;
                    s->declName = name;
                    s->type = storedType;
                    s->line = line;
                    s->arraySize = arraySize;
                    s->arrayDims = arrayDims;
                    s->arrayInit = arrayInit;
                    localDeclStmts_.push_back(std::move(s));
                }
            }
        }
    }

    void buildParamsAndSig(PascalParser::FormalParamsContext *fp, FuncSig &sig) {
        if (!fp) return;
        for (auto *grp : fp->formalParamGroup()) {
            int line = lineOf(grp);
            bool isVar = grp->VAR_KW() != nullptr;
            PType pt = resolveTypeSpec(grp->typeSpec());
            if (pt.isArray) err(line, srcPath_, "array parameters aren't supported in this frontend (docs/pascal-language.md)");
            if (pt.rcc.base == BaseType::Struct && !isVar) {
                err(line, srcPath_, "record parameters must be 'var' - passing one by value isn't supported");
            }
            for (auto *idTok : grp->identList()->IDENT()) {
                std::string name = idTok->getText();
                sig.paramLogicalTypes.push_back(pt);
                sig.isVarParam.push_back(isVar);
                Param p;
                p.name = name;
                p.type = isVar ? pointerTo(pt.rcc) : pt.rcc;
                sig.params.push_back(p);
            }
        }
    }

    void registerProcSig(PascalParser::ProcDeclContext *p) {
        std::string name = p->IDENT()->getText();
        if (funcSigs_.count(name)) err(lineOf(p), srcPath_, "redefinition of '" + name + "'");
        FuncSig sig;
        sig.isFunction = false;
        buildParamsAndSig(p->formalParams(), sig);
        funcSigs_[name] = std::move(sig);
    }

    void registerFuncSig(PascalParser::FuncDeclContext *f) {
        std::string name = f->IDENT()->getText();
        if (funcSigs_.count(name)) err(lineOf(f), srcPath_, "redefinition of '" + name + "'");
        FuncSig sig;
        sig.isFunction = true;
        buildParamsAndSig(f->formalParams(), sig);
        sig.returnType = resolveTypeSpec(f->typeSpec());
        if (sig.returnType.isArray || sig.returnType.rcc.base == BaseType::Struct) {
            err(lineOf(f), srcPath_, "a function's return type must be a scalar or pointer in this frontend");
        }
        funcSigs_[name] = std::move(sig);
    }

    // declPart* is recursive in the grammar (a routine's own block can
    // in principle hold more declParts), but a nested procedure/
    // function is rejected here - see docs/pascal-language.md.
    void processDeclPart(PascalParser::DeclPartContext *dp, bool topLevel) {
        if (auto *c = dp->constDecl()) {
            lowerConstDecl(c);
        } else if (auto *t = dp->typeDecl()) {
            lowerTypeDecl(t);
        } else if (auto *v = dp->varDecl()) {
            lowerVarDecl(v, topLevel);
        } else if (auto *p = dp->procDecl()) {
            if (!topLevel) err(lineOf(p), srcPath_, "nested procedure declarations aren't supported (docs/pascal-language.md)");
            registerProcSig(p);
            procDeclsToLower_.push_back(p);
        } else if (auto *f = dp->funcDecl()) {
            if (!topLevel) err(lineOf(f), srcPath_, "nested function declarations aren't supported (docs/pascal-language.md)");
            registerFuncSig(f);
            funcDeclsToLower_.push_back(f);
        }
    }

    void lowerRoutineBody(const std::string &name, FuncSig &sig, PascalParser::BlockContext *blockCtx, int line) {
        FunctionDecl fn;
        fn.name = name;
        fn.sourceFile = srcPath_;
        fn.line = line;
        fn.isVoid = !sig.isFunction;
        if (sig.isFunction) fn.returnType = sig.returnType.rcc;
        fn.params = sig.params;

        localVars_.clear();
        localDeclStmts_.clear();
        currentVarParams_.clear();
        for (size_t i = 0; i < sig.paramLogicalTypes.size(); i++) {
            localVars_[sig.params[i].name] = sig.paramLogicalTypes[i];
            if (sig.isVarParam[i]) currentVarParams_.insert(sig.params[i].name);
        }
        currentFunctionName_ = name;
        if (sig.isFunction) {
            localVars_["__result"] = sig.returnType;
            auto resultDecl = std::make_unique<Stmt>();
            resultDecl->kind = StmtKind::LocalDecl;
            resultDecl->declName = "__result";
            resultDecl->type = sig.returnType.rcc;
            resultDecl->line = line;
            localDeclStmts_.push_back(std::move(resultDecl));
        }

        for (auto *dp : blockCtx->declPart()) processDeclPart(dp, /*topLevel=*/false);
        std::vector<StmtPtr> bodyStmts;
        for (auto &s : localDeclStmts_) bodyStmts.push_back(std::move(s));
        localDeclStmts_.clear();
        bodyStmts.push_back(lowerCompoundStmt(blockCtx->compoundStmt()));
        if (sig.isFunction) {
            auto ret = std::make_unique<Stmt>();
            ret->kind = StmtKind::Return;
            ret->line = line;
            ret->initOrReturnOrExpr = makeVar("__result", line);
            bodyStmts.push_back(std::move(ret));
        }
        fn.body = makeBlock(std::move(bodyStmts), line);

        localVars_.clear();
        currentVarParams_.clear();
        currentFunctionName_.clear();

        combined_.functions.push_back(std::move(fn));
    }

    // --- Statement lowering ------------------------------------------------

    StmtPtr lowerCompoundStmt(PascalParser::CompoundStmtContext *ctx) {
        std::vector<StmtPtr> stmts;
        for (auto *s : ctx->statement()) stmts.push_back(lowerStatement(s));
        return makeBlock(std::move(stmts), lineOf(ctx));
    }

    StmtPtr lowerStatement(PascalParser::StatementContext *s) {
        int line = lineOf(s);
        if (auto *c = dynamic_cast<PascalParser::CompoundStatementContext *>(s)) return lowerCompoundStmt(c->compoundStmt());
        if (auto *c = dynamic_cast<PascalParser::AssignStmtContext *>(s)) return lowerAssign(c, line);
        if (auto *c = dynamic_cast<PascalParser::CallStmtContext *>(s)) return lowerCallStmtNode(c, line);
        if (auto *c = dynamic_cast<PascalParser::IfStmtContext *>(s)) {
            auto st = std::make_unique<Stmt>();
            st->kind = StmtKind::If;
            st->line = line;
            st->cond = lowerExpr(c->expr());
            st->thenStmt = lowerStatement(c->statement(0));
            if (c->ELSE_KW()) st->elseStmt = lowerStatement(c->statement(1));
            return st;
        }
        if (auto *c = dynamic_cast<PascalParser::WhileStmtContext *>(s)) {
            auto st = std::make_unique<Stmt>();
            st->kind = StmtKind::While;
            st->line = line;
            st->cond = lowerExpr(c->expr());
            st->loopBody = lowerStatement(c->statement());
            return st;
        }
        if (auto *c = dynamic_cast<PascalParser::RepeatStmtContext *>(s)) return lowerRepeat(c, line);
        if (auto *c = dynamic_cast<PascalParser::ForStmtContext *>(s)) return lowerFor(c, line);
        if (auto *c = dynamic_cast<PascalParser::CaseStmtContext *>(s)) return lowerCase(c, line);
        if (dynamic_cast<PascalParser::EmptyStmtContext *>(s)) return makeEmpty(line);
        err(line, srcPath_, "internal: unhandled Pascal statement alternative");
    }

    StmtPtr lowerAssign(PascalParser::AssignStmtContext *c, int line) {
        ExprPtr rhs;
        if (auto *sf = setLiteralFactorOf(c->expr())) {
            if (const PType *target = plainVarSetTarget(c->designator())) {
                rhs = lowerSetLiteralToBitmask(sf, target->setLow, line);
            }
        }
        if (!rhs) rhs = lowerExpr(c->expr());
        return makeExprStmt(lowerDesignatorAssign(c->designator(), std::move(rhs)), line);
    }

    PascalParser::SetFactorContext *setLiteralFactorOf(PascalParser::ExprContext *ctx) {
        auto *f = unwrapToPlainFactor(ctx);
        return f ? dynamic_cast<PascalParser::SetFactorContext *>(f) : nullptr;
    }

    const PType *plainVarSetTarget(PascalParser::DesignatorContext *d) {
        if (!parseSuffixes(d).empty()) return nullptr;
        const PType *pt = lookupVarType(d->IDENT(0)->getText());
        return (pt && pt->isSet) ? pt : nullptr;
    }

    // `write`/`writeln`/`read`/`readln` are statements, not callable
    // values, so they're intercepted here rather than in lowerCall -
    // real Pascal doesn't let them appear in expression position either.
    StmtPtr lowerCallStmtNode(PascalParser::CallStmtContext *c, int line) {
        auto *d = c->designator();
        std::string name = d->IDENT(0)->getText();
        auto suf = parseSuffixes(d);
        bool isCallSuffix = suf.size() == 1 && suf[0].kind == SuffixKind::Call;
        std::vector<PascalParser::ExprContext *> args;
        if (isCallSuffix && suf[0].exprList) args = suf[0].exprList->expr();

        if ((name == "write" || name == "writeln") && (suf.empty() || isCallSuffix)) {
            return lowerWriteStmt(args, name == "writeln", line);
        }
        if ((name == "read" || name == "readln") && (suf.empty() || isCallSuffix)) {
            return lowerReadStmt(args, line);
        }
        if (!(suf.empty() || isCallSuffix)) {
            err(line, srcPath_, "only a procedure/function call is valid as a statement by itself");
        }
        ExprPtr callExpr = lowerCall(name, isCallSuffix ? suf[0].exprList : nullptr, line);
        return makeExprStmt(std::move(callExpr), line);
    }

    StmtPtr lowerRepeat(PascalParser::RepeatStmtContext *c, int line) {
        std::vector<StmtPtr> body;
        for (auto *s : c->statement()) body.push_back(lowerStatement(s));
        auto breakIf = std::make_unique<Stmt>();
        breakIf->kind = StmtKind::If;
        breakIf->line = line;
        breakIf->cond = lowerExpr(c->expr());
        auto brk = std::make_unique<Stmt>();
        brk->kind = StmtKind::Break;
        brk->line = line;
        breakIf->thenStmt = std::move(brk);
        body.push_back(std::move(breakIf));

        auto loop = std::make_unique<Stmt>();
        loop->kind = StmtKind::While;
        loop->line = line;
        loop->cond = makeIntLit(1, line);
        loop->loopBody = makeBlock(std::move(body), line);
        return loop;
    }

    StmtPtr lowerFor(PascalParser::ForStmtContext *c, int line) {
        std::string loopVar = c->IDENT()->getText();
        if (!lookupVarType(loopVar)) err(line, srcPath_, "'" + loopVar + "' must be declared before use as a FOR loop variable");
        if (isVarParamName(loopVar)) err(line, srcPath_, "a FOR loop variable can't be a 'var' parameter");
        bool down = c->dir->getText() == "downto";

        std::string boundName = "__pascal_for_bound_" + std::to_string(++tempCounter_);
        GlobalVar bound;
        bound.name = boundName;
        bound.type = Type{BaseType::Int};
        bound.line = line;
        bound.sourceFile = srcPath_;
        combined_.globals.push_back(std::move(bound));
        globalVars_[boundName] = PType(Type{BaseType::Int});

        std::vector<StmtPtr> init;
        init.push_back(makeExprStmt(lowerDesignatorAssignByName(loopVar, lowerExpr(c->expr(0)), line), line));
        init.push_back(makeExprStmt(makeAssignPlain(boundName, lowerExpr(c->expr(1)), line), line));

        ExprPtr cond = makeBinary(down ? ">=" : "<=", makeVar(loopVar, line), makeVar(boundName, line), line);
        auto post = makeExprStmt(makeCompoundAssign(loopVar, down ? "-" : "+", makeIntLit(1, line), line), line);

        auto whileStmt = std::make_unique<Stmt>();
        whileStmt->kind = StmtKind::While;
        whileStmt->line = line;
        whileStmt->cond = std::move(cond);
        whileStmt->loopBody = lowerStatement(c->statement());
        whileStmt->postStmt = std::move(post);

        init.push_back(std::move(whileStmt));
        return makeBlock(std::move(init), line);
    }

    // The FOR loop variable is never a 'var' parameter (checked above),
    // so this is always a plain name assign.
    ExprPtr lowerDesignatorAssignByName(const std::string &name, ExprPtr rhs, int line) {
        return makeAssignPlain(name, std::move(rhs), line);
    }

    StmtPtr lowerCase(PascalParser::CaseStmtContext *c, int line) {
        auto sw = std::make_unique<Stmt>();
        sw->kind = StmtKind::Switch;
        sw->line = line;
        sw->cond = lowerExpr(c->expr());
        for (auto *branch : c->caseBranch()) {
            auto consts = branch->constExpr();
            // Every constant but the last gets an empty, fallthrough-only
            // case (Switch/SwitchCase is C-shaped fallthrough - see
            // AST.h) so `1, 2: stmt;` doesn't need `stmt` lowered twice.
            for (size_t i = 0; i + 1 < consts.size(); i++) {
                SwitchCase empty;
                empty.value = evalConstInt(consts[i]);
                sw->cases.push_back(std::move(empty));
            }
            SwitchCase real;
            real.value = evalConstInt(consts.back());
            real.body.push_back(lowerStatement(branch->statement()));
            auto brk = std::make_unique<Stmt>();
            brk->kind = StmtKind::Break;
            brk->line = lineOf(branch);
            real.body.push_back(std::move(brk));
            sw->cases.push_back(std::move(real));
        }
        if (c->ELSE_KW()) {
            SwitchCase def;
            def.isDefault = true;
            def.body.push_back(lowerStatement(c->statement()));
            auto brk = std::make_unique<Stmt>();
            brk->kind = StmtKind::Break;
            brk->line = line;
            def.body.push_back(std::move(brk));
            sw->cases.push_back(std::move(def));
        }
        return sw;
    }

    // --- write/writeln/read/readln -----------------------------------------

    StmtPtr lowerWriteStmt(const std::vector<PascalParser::ExprContext *> &args, bool newline, int line) {
        std::vector<StmtPtr> stmts;
        for (auto *a : args) stmts.push_back(makeExprStmt(lowerWriteArg(a), lineOf(a)));
        if (newline) {
            ensureExternFn("__pascal_write_newline", true, {});
            stmts.push_back(makeExprStmt(makeCall("__pascal_write_newline", {}, line), line));
        }
        return makeBlock(std::move(stmts), line);
    }

    // Syntactic/semantic best-effort classification (no full type
    // inference) - good enough to pick the right runtime call. A
    // boolean argument prints as 0/1 (via __pascal_write_int) in this
    // frontend - a deliberate v1 simplification.
    ExprPtr lowerWriteArg(PascalParser::ExprContext *a) {
        int line = lineOf(a);
        if (auto *f = unwrapToPlainFactor(a)) {
            if (auto *sf = dynamic_cast<PascalParser::StringFactorContext *>(f)) {
                ensureExternFn("__pascal_write_str", true, {Type{BaseType::Char, false, 1}});
                auto lit = std::make_unique<Expr>();
                lit->kind = ExprKind::StringLit;
                lit->line = line;
                lit->name = decodeStringLit(sf->STRINGLIT()->getText());
                return makeCall("__pascal_write_str", oneArg(std::move(lit)), line);
            }
            if (auto *d = dynamic_cast<PascalParser::DesignatorFactorContext *>(f)) {
                if (parseSuffixes(d->designator()).empty()) {
                    const PType *pt = lookupVarType(d->designator()->IDENT(0)->getText());
                    if (pt && pt->rcc.base == BaseType::Char && pt->rcc.ptrDepth == 0 && !pt->isArray) {
                        ensureExternFn("__pascal_write_char", true, {Type{BaseType::Int}});
                        return makeCall("__pascal_write_char", oneArg(lowerExpr(a)), line);
                    }
                }
            }
        }
        ensureExternFn("__pascal_write_int", true, {Type{BaseType::Int}});
        return makeCall("__pascal_write_int", oneArg(lowerExpr(a)), line);
    }

    StmtPtr lowerReadStmt(const std::vector<PascalParser::ExprContext *> &args, int line) {
        std::vector<StmtPtr> stmts;
        for (auto *a : args) {
            int aline = lineOf(a);
            auto *f = unwrapToPlainFactor(a);
            auto *d = f ? dynamic_cast<PascalParser::DesignatorFactorContext *>(f) : nullptr;
            if (!d || !parseSuffixes(d->designator()).empty()) {
                err(aline, srcPath_, "read/readln needs a plain variable, not an expression");
            }
            std::string name = d->designator()->IDENT(0)->getText();
            const PType *pt = lookupVarType(name);
            if (!pt) err(aline, srcPath_, "unknown identifier '" + name + "'");
            bool isChar = pt->rcc.base == BaseType::Char && pt->rcc.ptrDepth == 0 && !pt->isArray;
            const char *fn = isChar ? "__pascal_read_char" : "__pascal_read_int";
            ensureExternFn(fn, false, {});
            stmts.push_back(
                makeExprStmt(lowerDesignatorAssign(d->designator(), makeCall(fn, {}, aline)), aline));
        }
        return makeBlock(std::move(stmts), line);
    }

    // --- Calls, including 'var' argument passing ----------------------------

    ExprPtr lowerCall(const std::string &name, PascalParser::ExprListContext *argsCtx, int line) {
        std::vector<PascalParser::ExprContext *> argExprs;
        if (argsCtx) argExprs = argsCtx->expr();

        const FuncSig *sig = lookupFuncSig(name);
        if (!sig) err(line, srcPath_, "unknown procedure/function '" + name + "'");
        if (argExprs.size() != sig->params.size()) {
            err(line, srcPath_, "'" + name + "' needs " + std::to_string(sig->params.size()) + " argument(s), got " +
                                     std::to_string(argExprs.size()));
        }
        std::vector<ExprPtr> args;
        for (size_t i = 0; i < argExprs.size(); i++) {
            if (sig->isVarParam[i]) args.push_back(lowerVarArgAddress(argExprs[i], line));
            else args.push_back(lowerExpr(argExprs[i]));
        }
        return makeCall(name, std::move(args), line);
    }

    // The shape behind both `@x`/`@arr[i]` (address-of) and a 'var'
    // call argument - a plain variable or array element, the same
    // shapes rcc's own '&' already supports (see IRBuilder's
    // buildAddressableOperand), so this never needs to invent a new
    // addressing mode. `@x` on a 'var' parameter `x` is just `x`'s own
    // already-held pointer value, not a further address-of - the same
    // forwarding a 'var' argument does when passed on to another 'var'
    // parameter.
    ExprPtr addressOfDesignator(PascalParser::DesignatorContext *d, int line) {
        auto suf = parseSuffixes(d);
        std::string name = d->IDENT(0)->getText();
        if (suf.empty()) {
            if (isVarParamName(name)) return makeVar(name, line);
            if (!lookupVarType(name)) err(line, srcPath_, "unknown identifier '" + name + "'");
            return makeUnary("&", makeVar(name, line), line);
        }
        if (suf.size() == 1 && suf[0].kind == SuffixKind::Index) {
            return makeUnary("&", readArrayElement(name, suf[0].exprList, line), line);
        }
        err(line, srcPath_, "'@'/'var' here needs a plain variable or array element, not a field/pointer expression");
    }

    ExprPtr lowerVarArgAddress(PascalParser::ExprContext *a, int line) {
        auto *f = unwrapToPlainFactor(a);
        auto *d = f ? dynamic_cast<PascalParser::DesignatorFactorContext *>(f) : nullptr;
        if (!d) err(line, srcPath_, "a 'var' argument must be a plain variable or array element, not an expression");
        return addressOfDesignator(d->designator(), line);
    }

    // --- Designators (read and assign) --------------------------------------

    ExprPtr lowerDesignatorRead(PascalParser::DesignatorContext *ctx) {
        std::string name = ctx->IDENT(0)->getText();
        int line = lineOf(ctx);
        auto suf = parseSuffixes(ctx);

        if (suf.empty()) return readPlainName(name, line);
        if (suf.size() == 1 && suf[0].kind == SuffixKind::Call) return lowerCall(name, suf[0].exprList, line);
        if (suf.size() == 1 && suf[0].kind == SuffixKind::Index) return readArrayElement(name, suf[0].exprList, line);
        if (suf.size() == 1 && suf[0].kind == SuffixKind::Deref) return readDeref(name, line);
        if (suf.size() == 1 && suf[0].kind == SuffixKind::Field) {
            return buildMemberExpr(name, false, nullptr, suf[0].fieldName, nullptr, nullptr, line);
        }
        if (suf.size() == 2 && suf[0].kind == SuffixKind::Index && suf[1].kind == SuffixKind::Field) {
            return buildMemberExpr(name, false, suf[0].exprList, suf[1].fieldName, nullptr, nullptr, line);
        }
        if (suf.size() == 2 && suf[0].kind == SuffixKind::Deref && suf[1].kind == SuffixKind::Field) {
            return buildMemberExpr(name, true, nullptr, suf[1].fieldName, nullptr, nullptr, line);
        }
        if (suf.size() == 2 && suf[0].kind == SuffixKind::Field && suf[1].kind == SuffixKind::Index) {
            ExprPtr idx = lowerSingleIndex(suf[1].exprList, line);
            return buildMemberExpr(name, false, nullptr, suf[0].fieldName, std::move(idx), nullptr, line);
        }
        err(line, srcPath_, "this designator is too complex for this frontend's v1 support (docs/pascal-language.md)");
    }

    ExprPtr lowerDesignatorAssign(PascalParser::DesignatorContext *ctx, ExprPtr rhs) {
        std::string name = ctx->IDENT(0)->getText();
        int line = lineOf(ctx);
        auto suf = parseSuffixes(ctx);

        if (suf.empty()) return assignPlainName(name, std::move(rhs), line);
        if (suf.size() == 1 && suf[0].kind == SuffixKind::Index) return assignArrayElement(name, suf[0].exprList, std::move(rhs), line);
        if (suf.size() == 1 && suf[0].kind == SuffixKind::Deref) return assignDeref(name, std::move(rhs), line);
        if (suf.size() == 1 && suf[0].kind == SuffixKind::Field) {
            return buildMemberExpr(name, false, nullptr, suf[0].fieldName, nullptr, std::move(rhs), line);
        }
        if (suf.size() == 2 && suf[0].kind == SuffixKind::Index && suf[1].kind == SuffixKind::Field) {
            return buildMemberExpr(name, false, suf[0].exprList, suf[1].fieldName, nullptr, std::move(rhs), line);
        }
        if (suf.size() == 2 && suf[0].kind == SuffixKind::Deref && suf[1].kind == SuffixKind::Field) {
            return buildMemberExpr(name, true, nullptr, suf[1].fieldName, nullptr, std::move(rhs), line);
        }
        if (suf.size() == 2 && suf[0].kind == SuffixKind::Field && suf[1].kind == SuffixKind::Index) {
            ExprPtr idx = lowerSingleIndex(suf[1].exprList, line);
            return buildMemberExpr(name, false, nullptr, suf[0].fieldName, std::move(idx), std::move(rhs), line);
        }
        if (suf.size() == 1 && suf[0].kind == SuffixKind::Call) err(line, srcPath_, "can't assign to the result of a call");
        err(line, srcPath_, "this designator is too complex for this frontend's v1 support (docs/pascal-language.md)");
    }

    ExprPtr readPlainName(const std::string &name, int line) {
        if (name == currentFunctionName_) return makeVar("__result", line);
        if (const FuncSig *sig = lookupFuncSig(name)) {
            if (!sig->params.empty()) err(line, srcPath_, "'" + name + "' needs " + std::to_string(sig->params.size()) + " argument(s)");
            if (!sig->isFunction) err(line, srcPath_, "'" + name + "' is a procedure, not a value");
            return makeCall(name, {}, line);
        }
        if (!lookupVarType(name)) {
            auto cit = constInts_.find(name);
            if (cit != constInts_.end()) return makeIntLit(cit->second, line);
            auto fit = floatConsts_.find(name);
            if (fit != floatConsts_.end()) {
                auto e = std::make_unique<Expr>();
                e->kind = ExprKind::FloatLit;
                e->line = line;
                e->floatValue = fit->second;
                return e;
            }
            err(line, srcPath_, "unknown identifier '" + name + "'");
        }
        if (isVarParamName(name)) return makeUnary("*", makeVar(name, line), line);
        return makeVar(name, line);
    }

    ExprPtr assignPlainName(const std::string &name, ExprPtr rhs, int line) {
        if (name == currentFunctionName_) return makeAssignPlain("__result", std::move(rhs), line);
        if (!lookupVarType(name)) {
            if (constInts_.count(name) || floatConsts_.count(name)) err(line, srcPath_, "can't assign to constant '" + name + "'");
            err(line, srcPath_, "unknown identifier '" + name + "'");
        }
        if (isVarParamName(name)) {
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::DerefAssign;
            e->line = line;
            e->lhs = makeVar(name, line);
            e->rhs = std::move(rhs);
            return e;
        }
        return makeAssignPlain(name, std::move(rhs), line);
    }

    ExprPtr adjustedIndex(const PType *pt, PascalParser::ExprContext *idxCtx, size_t dim, int line) {
        ExprPtr raw = lowerExpr(idxCtx);
        if (pt->lowBounds[dim] != 0) raw = makeBinary("-", std::move(raw), makeIntLit(pt->lowBounds[dim], line), line);
        return raw;
    }

    void fillArrayIndices(const std::string &name, PascalParser::ExprListContext *idxCtx, Expr &e, int line) {
        const PType *pt = lookupVarType(name);
        if (!pt || !pt->isArray) err(line, srcPath_, "'" + name + "' isn't an array");
        auto idxExprs = idxCtx->expr();
        if (idxExprs.size() != pt->dims.size()) {
            err(line, srcPath_, "'" + name + "' has " + std::to_string(pt->dims.size()) +
                                     " dimension(s) but was indexed with " + std::to_string(idxExprs.size()));
        }
        e.lhs = adjustedIndex(pt, idxExprs[0], 0, line);
        if (idxExprs.size() > 1) {
            for (size_t i = 0; i < idxExprs.size(); i++) e.indices.push_back(adjustedIndex(pt, idxExprs[i], i, line));
        }
    }

    ExprPtr readArrayElement(const std::string &name, PascalParser::ExprListContext *idxCtx, int line) {
        auto e = std::make_unique<Expr>();
        e->kind = ExprKind::Index;
        e->line = line;
        e->name = name;
        fillArrayIndices(name, idxCtx, *e, line);
        return e;
    }

    ExprPtr assignArrayElement(const std::string &name, PascalParser::ExprListContext *idxCtx, ExprPtr rhs, int line) {
        auto e = std::make_unique<Expr>();
        e->kind = ExprKind::Assign;
        e->line = line;
        e->name = name;
        fillArrayIndices(name, idxCtx, *e, line);
        e->rhs = std::move(rhs);
        return e;
    }

    ExprPtr readDeref(const std::string &name, int line) { return makeUnary("*", makeVar(name, line), line); }

    ExprPtr assignDeref(const std::string &name, ExprPtr rhs, int line) {
        auto e = std::make_unique<Expr>();
        e->kind = ExprKind::DerefAssign;
        e->line = line;
        e->lhs = makeVar(name, line);
        e->rhs = std::move(rhs);
        return e;
    }

    ExprPtr lowerSingleIndex(PascalParser::ExprListContext *idxCtx, int line) {
        auto exprs = idxCtx->expr();
        if (exprs.size() != 1) err(line, srcPath_, "a record field's array index must have exactly one subscript");
        return lowerExpr(exprs[0]);
    }

    // The one shared builder behind every MemberAccess/MemberAssign
    // shape this frontend supports (plain/pointer/array-element record,
    // optionally with the field itself indexed) - see
    // lowerDesignatorRead/lowerDesignatorAssign's dispatch above.
    ExprPtr buildMemberExpr(const std::string &name, bool derefFirst, PascalParser::ExprListContext *indexFirst,
                             const std::string &fieldName, ExprPtr indexedField, ExprPtr assignRhs, int line) {
        ExprPtr lhs;
        bool viaPointer;
        if (derefFirst || isVarParamName(name)) {
            // p^.field, or a 'var' record parameter (whose own name
            // already holds the address) - both are "the pointer value
            // itself".
            lhs = makeVar(name, line);
            viaPointer = true;
        } else if (indexFirst) {
            lhs = readArrayElement(name, indexFirst, line);
            viaPointer = false;
        } else {
            lhs = makeVar(name, line);
            viaPointer = false;
        }
        auto e = std::make_unique<Expr>();
        e->line = line;
        e->name = fieldName;
        e->viaPointer = viaPointer;
        e->lhs = std::move(lhs);
        e->memberIndex = std::move(indexedField);
        if (assignRhs) {
            e->kind = ExprKind::MemberAssign;
            e->rhs = std::move(assignRhs);
        } else {
            e->kind = ExprKind::MemberAccess;
        }
        return e;
    }

    // --- Expressions ---------------------------------------------------

    ExprPtr lowerExpr(PascalParser::ExprContext *ctx) {
        int line = lineOf(ctx);
        ExprPtr lhs = lowerSimpleExpr(ctx->simpleExpr(0));
        if (!ctx->relOp()) return lhs;
        std::string op = ctx->relOp()->getText();
        auto *rhsCtx = ctx->simpleExpr(1);
        if (op == "in") return lowerInExpr(std::move(lhs), rhsCtx, line);
        if (op == "=") op = "==";
        else if (op == "<>") op = "!=";
        return makeBinary(op, std::move(lhs), lowerSimpleExpr(rhsCtx), line);
    }

    ExprPtr lowerInExpr(ExprPtr lhsVal, PascalParser::SimpleExprContext *rhsCtx, int line) {
        if (auto *factor = unwrapSimpleExprToFactor(rhsCtx)) {
            if (auto *sf = dynamic_cast<PascalParser::SetFactorContext *>(factor)) {
                // No declared set type exists for an inline literal, so
                // its own element values are used as bit positions
                // directly (low bound 0) - see lowerSetLiteralToBitmask's
                // own comment.
                ExprPtr mask = lowerSetLiteralToBitmask(sf, 0, line);
                return testBit(std::move(mask), std::move(lhsVal), 0, line);
            }
        }
        long low = 0;
        if (auto *factor = unwrapSimpleExprToFactor(rhsCtx); factor) {
            if (auto *d = dynamic_cast<PascalParser::DesignatorFactorContext *>(factor)) {
                if (parseSuffixes(d->designator()).empty()) {
                    if (const PType *pt = lookupVarType(d->designator()->IDENT(0)->getText()); pt && pt->isSet) {
                        low = pt->setLow;
                        return testBit(lowerSimpleExpr(rhsCtx), std::move(lhsVal), low, line);
                    }
                }
            }
        }
        err(line, srcPath_, "the right side of 'in' must be a set literal ([...]) or a set-typed variable");
    }

    ExprPtr testBit(ExprPtr setVal, ExprPtr elemVal, long low, int line) {
        ExprPtr bitPos = low != 0 ? makeBinary("-", std::move(elemVal), makeIntLit(low, line), line) : std::move(elemVal);
        ExprPtr shifted = makeBinary(">>", std::move(setVal), std::move(bitPos), line);
        ExprPtr masked = makeBinary("&", std::move(shifted), makeIntLit(1, line), line);
        return makeBinary("!=", std::move(masked), makeIntLit(0, line), line);
    }

    // A set literal lowers to a chain of bits `|`'d together rather than
    // to a single constant, so a non-constant single element (`[x, 5]`)
    // still works; only a range endpoint (`5..9`) must be a compile-time
    // constant (evalConstIntFromExpr) - folding a dynamic range would
    // need a runtime-computed mask width this frontend doesn't build.
    ExprPtr lowerSetLiteralToBitmask(PascalParser::SetFactorContext *sf, long low, int line) {
        ExprPtr acc = makeIntLit(0, line);
        for (auto *elem : sf->setElement()) {
            auto exprs = elem->expr();
            if (exprs.size() == 2) {
                long lo = evalConstIntFromExpr(exprs[0]);
                long hi = evalConstIntFromExpr(exprs[1]);
                if (hi < lo) err(lineOf(elem), srcPath_, "a set range's high value must not be less than its low value");
                unsigned long width = static_cast<unsigned long>(hi - lo + 1);
                unsigned long mask = width >= 32 ? 0xFFFFFFFFul : ((1ul << width) - 1);
                mask <<= static_cast<unsigned long>(lo - low);
                acc = makeBinary("|", std::move(acc), makeIntLit(static_cast<long>(mask), line), line);
            } else {
                ExprPtr bitPos = lowerExpr(exprs[0]);
                if (low != 0) bitPos = makeBinary("-", std::move(bitPos), makeIntLit(low, line), line);
                ExprPtr bit = makeBinary("<<", makeIntLit(1, line), std::move(bitPos), line);
                acc = makeBinary("|", std::move(acc), std::move(bit), line);
            }
        }
        return acc;
    }

    bool isSetOperand(PascalParser::FactorContext *f) {
        if (dynamic_cast<PascalParser::SetFactorContext *>(f)) return true;
        if (auto *d = dynamic_cast<PascalParser::DesignatorFactorContext *>(f)) {
            if (parseSuffixes(d->designator()).empty()) {
                if (const PType *pt = lookupVarType(d->designator()->IDENT(0)->getText())) return pt->isSet;
            }
        }
        return false;
    }

    bool isSetOperandTerm(PascalParser::TermContext *t) {
        if (!t->mulOp().empty()) return false;
        return isSetOperand(t->factor(0));
    }

    ExprPtr lowerSimpleExpr(PascalParser::SimpleExprContext *ctx) {
        int line = lineOf(ctx);
        ExprPtr acc = lowerTerm(ctx->term(0));
        bool accIsSet = isSetOperandTerm(ctx->term(0));
        if (ctx->sign && ctx->sign->getText() == "-") acc = makeUnary("-", std::move(acc), line);

        auto terms = ctx->term();
        auto ops = ctx->addOp();
        for (size_t i = 0; i < ops.size(); i++) {
            int oline = lineOf(ops[i]);
            std::string op = ops[i]->getText();
            bool rhsIsSet = isSetOperandTerm(terms[i + 1]);
            ExprPtr rhs = lowerTerm(terms[i + 1]);
            if (op == "or") {
                acc = makeBinary("||", std::move(acc), std::move(rhs), oline);
                accIsSet = false;
                continue;
            }
            bool setOp = accIsSet && rhsIsSet;
            if (op == "+") {
                acc = makeBinary(setOp ? "|" : "+", std::move(acc), std::move(rhs), oline);
            } else {  // "-"
                if (setOp) {
                    ExprPtr notRhs = makeUnary("~", std::move(rhs), oline);
                    acc = makeBinary("&", std::move(acc), std::move(notRhs), oline);
                } else {
                    acc = makeBinary("-", std::move(acc), std::move(rhs), oline);
                }
            }
            accIsSet = setOp;
        }
        return acc;
    }

    ExprPtr lowerTerm(PascalParser::TermContext *ctx) {
        ExprPtr acc = lowerFactor(ctx->factor(0));
        bool accIsSet = isSetOperand(ctx->factor(0));
        auto factors = ctx->factor();
        auto ops = ctx->mulOp();
        for (size_t i = 0; i < ops.size(); i++) {
            int line = lineOf(ops[i]);
            std::string op = ops[i]->getText();
            bool rhsIsSet = isSetOperand(factors[i + 1]);
            ExprPtr rhs = lowerFactor(factors[i + 1]);
            if (op == "and") {
                acc = makeBinary("&&", std::move(acc), std::move(rhs), line);
                accIsSet = false;
                continue;
            }
            bool setOp = accIsSet && rhsIsSet;
            if (op == "*") acc = makeBinary(setOp ? "&" : "*", std::move(acc), std::move(rhs), line);
            else if (op == "/") acc = makeBinary("/", std::move(acc), std::move(rhs), line);
            else if (op == "div") acc = makeBinary("/", std::move(acc), std::move(rhs), line);
            else acc = makeBinary("%", std::move(acc), std::move(rhs), line);  // "mod"
            accIsSet = setOp;
        }
        return acc;
    }

    ExprPtr lowerFactor(PascalParser::FactorContext *ctx) {
        int line = lineOf(ctx);
        if (auto *c = dynamic_cast<PascalParser::DesignatorFactorContext *>(ctx)) return lowerDesignatorRead(c->designator());
        if (auto *c = dynamic_cast<PascalParser::IntFactorContext *>(ctx)) return makeIntLit(std::stol(c->INTLIT()->getText()), line);
        if (auto *c = dynamic_cast<PascalParser::RealFactorContext *>(ctx)) {
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::FloatLit;
            e->line = line;
            e->floatValue = std::stod(c->REALLIT()->getText());
            return e;
        }
        if (auto *c = dynamic_cast<PascalParser::StringFactorContext *>(ctx)) {
            std::string s = decodeStringLit(c->STRINGLIT()->getText());
            if (s.size() == 1) return makeIntLit(static_cast<unsigned char>(s[0]), line);  // a char literal
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::StringLit;
            e->line = line;
            e->name = s;
            return e;
        }
        if (dynamic_cast<PascalParser::TrueFactorContext *>(ctx)) return makeIntLit(1, line);
        if (dynamic_cast<PascalParser::FalseFactorContext *>(ctx)) return makeIntLit(0, line);
        if (auto *c = dynamic_cast<PascalParser::ParenFactorContext *>(ctx)) return lowerExpr(c->expr());
        if (auto *c = dynamic_cast<PascalParser::NotFactorContext *>(ctx)) return makeUnary("!", lowerFactor(c->factor()), line);
        if (auto *c = dynamic_cast<PascalParser::SetFactorContext *>(ctx)) return lowerSetLiteralToBitmask(c, 0, line);
        if (auto *c = dynamic_cast<PascalParser::AddrOfFactorContext *>(ctx)) return addressOfDesignator(c->designator(), line);
        err(line, srcPath_, "internal: unhandled Pascal factor alternative");
    }
};

}  // namespace

void PascalFrontend::parseAndMergeFile(const std::string &srcPath,
                                        const std::map<std::string, std::string> & /*predefinedMacros*/,
                                        Program &combined) const {
    std::ifstream in(srcPath);
    if (!in) throw std::runtime_error(srcPath + ": could not open file");
    std::stringstream ss;
    ss << in.rdbuf();
    std::string source = ss.str();

    CountingErrorListener errors;
    antlr4::ANTLRInputStream input(source);
    PascalLexer lexer(&input);
    lexer.removeErrorListeners();
    lexer.addErrorListener(&errors);

    antlr4::CommonTokenStream tokens(&lexer);
    PascalParser parser(&tokens);
    parser.removeErrorListeners();
    parser.addErrorListener(&errors);

    PascalParser::ProgramContext *tree = parser.program();
    if (errors.count > 0) throw std::runtime_error(srcPath + ": syntax errors, not compiling");

    Lowerer lowerer(srcPath, combined);
    lowerer.lowerProgram(tree);
}

}  // namespace rcc
