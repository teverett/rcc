#include "VexCodegen.h"

#include <set>
#include <stdexcept>

#include "VexAssembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

namespace {

[[noreturn]] void fail(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": vex: " + msg);
}

std::string decimal(uint32_t v) { return std::to_string(v); }

// `e.intValue` is a signed `long`; vex arithmetic is plain 32-bit wrapping, so the bit pattern
// (not the signed decimal text) is what has to survive into the assembly - printed as the
// unsigned 32-bit decimal it wraps to, which VexAssembler's own expression evaluator reads back
// byte-identical either way.
uint32_t wrap32(long v) { return static_cast<uint32_t>(static_cast<int64_t>(v) & 0xFFFFFFFFLL); }

}  // namespace

// --- top-level driver -------------------------------------------------------

std::string VexCodegen::generate(const ir::Program &prog, bool objectMode) {
    recursiveFunctions_ = computeRecursiveFunctions(prog);
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }

    if (!objectMode) {
        code_ << "_start:\n";
        code_ << "    lit " << label(mainFn) << "\n";
        code_ << "    cal\n";
        // Normal halt (empty return stack) - see VexCodegen.h's own class comment for why this,
        // not an explicit DEO-to-port-0, is this backend's entry convention.
        code_ << "    ret\n\n";
        trailer_.push_back("G___discard:\n    .word 0\n");
    }

    for (const auto &fn : prog.functions) {
        if (!fn->hasAddress) continue;
        fail(0, "'@' addr on a function isn't supported on this backend (vex has no fixed entry-point convention "
                "beyond `main`)");
    }

    for (const auto &fb : prog.functionBodies) genFunction(fb);

    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        if (slot->isExternal) continue;
        declareGlobal(*slot);
    }

    if (!objectMode) ensureRuntimeRoutines();

    code_ << "\n";
    for (const auto &t : trailer_) code_ << t;
    std::string text = code_.str();

    // --dump-asm support: every backend-internal label/routine (entry sequence, runtime
    // division/shift helpers) has no single input file of its own, so - same convention as
    // Pdp1Codegen.cpp's own perFileAsm() - it's attributed to whichever file defines `main`,
    // falling back to the one file compiled when there's exactly one (every object-mode compile
    // here, since each file becomes its own object independently of every other).
    std::set<std::string> files;
    for (const auto &fn : prog.functions) {
        if (fn->hasPrototype && fn->isExternal) continue;
        if (!fn->sourceFile.empty()) files.insert(fn->sourceFile);
    }
    for (const auto &slot : prog.slots) {
        if (slot->kind == ir::SlotKind::Global && !slot->isExternal && !slot->sourceFile.empty()) {
            files.insert(slot->sourceFile);
        }
    }
    if (files.size() == 1) perFileAsm_[*files.begin()] = text;

    return text;
}

std::map<std::string, std::string> VexCodegen::perFileAsm() const { return perFileAsm_; }

std::optional<std::string> VexCodegen::assembleInProcess(const std::string &asmText, const std::string &format) const {
    if (format == "raw") return assembleVex(asmText);
    if (auto kind = fileFormatKindFromName(format)) {
        ObjectFile obj = assembleVexObject(asmText);
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

void VexCodegen::declareGlobal(const ir::Slot &slot) {
    rejectStruct(slot.type, 0, "global '" + slot.sourceName + "'");
    if (isWide(slot.type)) genWideUnsupported(0, "global '" + slot.sourceName + "'");
    std::ostringstream o;
    o << label(&slot) << ":\n";
    int count = slot.arraySize > 0 ? slot.arraySize : 1;
    if (!slot.arrayInit.empty()) {
        for (int i = 0; i < count; i++) {
            if (i < static_cast<int>(slot.arrayInit.size())) {
                const auto &e = slot.arrayInit[i];
                if (e.isSymbol) {
                    o << "    .word " << (e.slot ? label(e.slot) : label(e.func)) << "\n";
                } else {
                    o << "    .word " << decimal(wrap32(e.value)) << "\n";
                }
            } else {
                o << "    .word 0\n";
            }
        }
    } else {
        for (int i = 0; i < count; i++) {
            o << "    .word " << decimal(slot.hasInit ? wrap32(slot.initValue) : 0) << "\n";
        }
    }
    trailer_.push_back(o.str());
}

// --- leaf/framed classification --------------------------------------------

namespace {
void collectDirectCallsInExpr(const ir::Expr *e, std::vector<const ir::Function *> &out) {
    if (!e) return;
    if (e->kind == ir::ExprKind::Call && e->func) out.push_back(e->func);
    collectDirectCallsInExpr(e->lhs.get(), out);
    collectDirectCallsInExpr(e->rhs.get(), out);
    collectDirectCallsInExpr(e->elseExpr.get(), out);
    for (const auto &a : e->args) collectDirectCallsInExpr(a.get(), out);
}

void collectDirectCallsInStmt(const ir::Stmt *s, std::vector<const ir::Function *> &out) {
    if (!s) return;
    collectDirectCallsInExpr(s->cond.get(), out);
    collectDirectCallsInExpr(s->initOrReturnOrExpr.get(), out);
    for (const auto &c : s->body) collectDirectCallsInStmt(c.get(), out);
    collectDirectCallsInStmt(s->thenStmt.get(), out);
    collectDirectCallsInStmt(s->elseStmt.get(), out);
    collectDirectCallsInStmt(s->loopBody.get(), out);
    collectDirectCallsInStmt(s->postStmt.get(), out);
}
}  // namespace

// A function that merely CALLS something isn't unsafe for static (leaf) storage by itself - only
// one that might be ACTIVE TWICE AT ONCE is (see the class comment's "two storage classes"
// section): a plain `main()` calling a one-shot helper is perfectly safe with static storage,
// even though `stmtMakesCall()` alone can't tell that apart from real recursion. This builds the
// direct-call graph (CallPtr contributes no edge - its target isn't known here, the same
// conservative gap `@nonrecursive`'s own checkNonRecursiveFunctions() in IRBuilder.cpp accepts)
// and marks every function that can reach itself through one or more calls - directly,
// mutually, or through a longer cycle.
std::unordered_set<const ir::Function *> VexCodegen::computeRecursiveFunctions(const ir::Program &prog) {
    std::unordered_map<const ir::Function *, std::vector<const ir::Function *>> edges;
    for (const auto &fb : prog.functionBodies) {
        std::vector<const ir::Function *> calls;
        collectDirectCallsInStmt(fb.body.get(), calls);
        edges[fb.func] = std::move(calls);
    }
    std::unordered_set<const ir::Function *> recursive;
    for (const auto &kv : edges) {
        const ir::Function *start = kv.first;
        std::unordered_set<const ir::Function *> visited;
        std::vector<const ir::Function *> stack = kv.second;
        while (!stack.empty()) {
            const ir::Function *cur = stack.back();
            stack.pop_back();
            if (cur == start) {
                recursive.insert(start);
                break;
            }
            if (!visited.insert(cur).second) continue;
            auto it = edges.find(cur);
            if (it != edges.end()) {
                for (const ir::Function *next : it->second) stack.push_back(next);
            }
        }
    }
    return recursive;
}

bool VexCodegen::exprTakesAddressOfFrameSlot(const ir::Expr *e, const std::unordered_set<const ir::Slot *> &frameSlots,
                                              std::string *badName) {
    if (!e) return false;
    if (e->kind == ir::ExprKind::Unary && e->op == "&") {
        const ir::Expr *operand = e->rhs.get();
        if (operand && (operand->kind == ir::ExprKind::Var || operand->kind == ir::ExprKind::Index) &&
            operand->slot && frameSlots.count(operand->slot)) {
            *badName = operand->slot->sourceName;
            return true;
        }
    }
    if (exprTakesAddressOfFrameSlot(e->lhs.get(), frameSlots, badName)) return true;
    if (exprTakesAddressOfFrameSlot(e->rhs.get(), frameSlots, badName)) return true;
    if (exprTakesAddressOfFrameSlot(e->elseExpr.get(), frameSlots, badName)) return true;
    for (const auto &a : e->args) {
        if (exprTakesAddressOfFrameSlot(a.get(), frameSlots, badName)) return true;
    }
    return false;
}

namespace {
void collectFrameSlots(const ir::Stmt *s, std::unordered_set<const ir::Slot *> &out) {
    if (!s) return;
    if (s->kind == ir::StmtKind::LocalDecl && s->declSlot) out.insert(s->declSlot);
    for (const auto &c : s->body) collectFrameSlots(c.get(), out);
    collectFrameSlots(s->thenStmt.get(), out);
    collectFrameSlots(s->elseStmt.get(), out);
    collectFrameSlots(s->loopBody.get(), out);
    collectFrameSlots(s->postStmt.get(), out);
}

bool scanAddressTaken(const ir::Stmt *s, const std::unordered_set<const ir::Slot *> &frameSlots, std::string *bad) {
    if (!s) return false;
    if (VexCodegen::exprTakesAddressOfFrameSlot(s->cond.get(), frameSlots, bad)) return true;
    if (VexCodegen::exprTakesAddressOfFrameSlot(s->initOrReturnOrExpr.get(), frameSlots, bad)) return true;
    for (const auto &c : s->body) {
        if (scanAddressTaken(c.get(), frameSlots, bad)) return true;
    }
    if (scanAddressTaken(s->thenStmt.get(), frameSlots, bad)) return true;
    if (scanAddressTaken(s->elseStmt.get(), frameSlots, bad)) return true;
    if (scanAddressTaken(s->loopBody.get(), frameSlots, bad)) return true;
    if (scanAddressTaken(s->postStmt.get(), frameSlots, bad)) return true;
    return false;
}
}  // namespace

void VexCodegen::checkFrameableBody(const ir::Function *fn, const ir::Stmt *body) {
    std::unordered_set<const ir::Slot *> frameSlots;
    for (ir::Slot *p : fn->params) frameSlots.insert(p);
    collectFrameSlots(body, frameSlots);

    for (const ir::Slot *s : frameSlots) {
        rejectStruct(s->type, 0, "'" + s->sourceName + "' in function '" + fn->sourceName + "'");
        if (isWide(s->type)) genWideUnsupported(0, "'" + s->sourceName + "' in function '" + fn->sourceName + "'");
        if (s->arraySize > 0) {
            fail(0, "'" + s->sourceName + "' in function '" + fn->sourceName +
                         "' is an array local/param inside a recursive function - vex's return stack (where a "
                         "recursive function's locals/params live) isn't addressable memory, so an array can't be "
                         "stored there. Mark '" + fn->sourceName + "' '@nonrecursive', or restructure to avoid the "
                                                                    "array here.");
        }
    }
    std::string bad;
    if (scanAddressTaken(body, frameSlots, &bad)) {
        fail(0, "'&" + bad + "' inside recursive function '" + fn->sourceName +
                     "' - vex's return stack (where a recursive function's locals/params live) isn't addressable "
                     "memory, so '&' can't name a cell there. Mark '" + fn->sourceName +
                     "' '@nonrecursive', or avoid taking this address.");
    }
}

void VexCodegen::rejectStruct(Type t, int line, const std::string &what) {
    if (t.base == BaseType::Struct) fail(line, "struct isn't supported on this backend yet (" + what + ")");
}

void VexCodegen::genWideUnsupported(int line, const std::string &what) {
    fail(line, "'long long' isn't supported on this backend (" + what +
                   ") - a correct 64-bit add/sub/compare needs a carry chain, and vex's ADD/SUB report no "
                   "carry/overflow at all, so there is no way to detect one (see VexCodegen.h's class comment)");
}

// --- function bodies ---------------------------------------------------------

void VexCodegen::genFunction(const ir::FunctionBody &fb) {
    const ir::Function *fn = fb.func;
    curFunction_ = fn;
    curFunctionLabel_ = label(fn);
    loopLabels_.clear();
    retAdjust_ = 0;
    irLabels_.clear();

    currentFunctionIsLeaf_ = fn->isNonRecursive || !recursiveFunctions_.count(fn);

    if (!currentFunctionIsLeaf_) checkFrameableBody(fn, fb.body.get());

    for (ir::Slot *p : fn->params) {
        rejectStruct(p->type, fb.line, "parameter '" + p->sourceName + "' of '" + fn->sourceName + "'");
        if (isWide(p->type)) genWideUnsupported(fb.line, "parameter '" + p->sourceName + "' of '" + fn->sourceName + "'");
    }

    code_ << curFunctionLabel_ << ":\n";

    if (currentFunctionIsLeaf_) {
        // Bind params: they arrive on the data stack in declaration order (param[n-1] on top -
        // see genCallArgs()), so consume from the top down, storing each into its own static
        // cell.
        for (int i = static_cast<int>(fn->params.size()) - 1; i >= 0; i--) {
            const ir::Slot *p = fn->params[i];
            code_ << "    lit " << staticLabel(p) << "\n    st\n";
            trailer_.push_back(staticLabel(p) + ":\n    .word 0\n");
        }
        genStmt(*fb.body);
    } else {
        emitPrologue(fn);
        genStmt(*fb.body);
    }

    code_ << epilogueLabel() << ":\n";
    emitEpilogueAndReturn(!fn->isVoid);
}

int VexCodegen::paramBytesTotal(const ir::Function *fn) const {
    int total = 0;
    for (const ir::Slot *p : fn->params) total += widthOfType(p->type);
    return total;
}

// `frameOffset`/`frameSize` are in bytes (widthOfType()'s own units - see the class comment),
// but `LDS.r`/`STS.r` count cells (4 bytes each), so every byte count here divides by 4.
int VexCodegen::frameDepth(const ir::Slot *slot) const {
    // `retAdjust_` accounts for any scratch PUSH currently outstanding - see its own comment.
    if (slot->kind == ir::SlotKind::Param) return slot->frameOffset / 4 - 2 + retAdjust_;
    // Local.
    int reserved = 4;  // widthOf(Int) == 4 bytes (one cell) on this target.
    return paramBytesTotal(curFunction_) / 4 + (slot->frameOffset - reserved) / 4 + retAdjust_;
}

void VexCodegen::emitPrologue(const ir::Function *fn) {
    int frameCells = fn->frameSize / 4;
    for (int i = 0; i < frameCells; i++) code_ << "    lit 0\n    push\n";
    for (int i = static_cast<int>(fn->params.size()) - 1; i >= 0; i--) code_ << "    push\n";
}

void VexCodegen::emitEpilogueAndReturn(bool hasValue) {
    if (hasValue) code_ << "    lit G___retval\n    st\n";
    if (!currentFunctionIsLeaf_) {
        int totalCells = (curFunction_->frameSize + paramBytesTotal(curFunction_)) / 4;
        for (int i = 0; i < totalCells; i++) {
            code_ << "    pop\n    lit G___discard\n    st\n";
        }
    }
    // Every call leaves exactly one cell on top of the data stack, even a `void` function's -
    // its own caller never reads this one back (ExprStmt's own discard is the only consumer -
    // see genStmt()'s own comment), but giving every call the same shape means the caller never
    // needs to know the callee's void-ness to stay balanced. The reloaded value is otherwise
    // meaningless garbage left over from the last real return value.
    code_ << "    lit G___retval\n    ld\n";
    code_ << "    ret\n";
}

// --- statements ---------------------------------------------------------

void VexCodegen::genStmt(const ir::Stmt &s) {
    switch (s.kind) {
        case ir::StmtKind::Block:
            for (const auto &c : s.body) genStmt(*c);
            return;
        case ir::StmtKind::If: {
            std::string Lelse = newLabel();
            std::string Lend = newLabel();
            genCondBranch(*s.cond, Lelse);
            genStmt(*s.thenStmt);
            if (s.elseStmt) {
                code_ << "    lit " << Lend << "\n    jmp\n";
                code_ << Lelse << ":\n";
                genStmt(*s.elseStmt);
                code_ << Lend << ":\n";
            } else {
                code_ << Lelse << ":\n";
            }
            return;
        }
        case ir::StmtKind::While: {
            std::string Ltop = newLabel();
            std::string Lcont = newLabel();
            std::string Lend = newLabel();
            loopLabels_.push_back({Lcont, Lend});
            code_ << Ltop << ":\n";
            genCondBranch(*s.cond, Lend);
            genStmt(*s.loopBody);
            code_ << Lcont << ":\n";
            if (s.postStmt) genStmt(*s.postStmt);
            code_ << "    lit " << Ltop << "\n    jmp\n";
            code_ << Lend << ":\n";
            loopLabels_.pop_back();
            return;
        }
        case ir::StmtKind::Return:
            if (s.initOrReturnOrExpr) genExpr(*s.initOrReturnOrExpr);
            code_ << "    lit " << epilogueLabel() << "\n    jmp\n";
            return;
        case ir::StmtKind::Break:
            code_ << "    lit " << loopLabels_.back().second << "\n    jmp\n";
            return;
        case ir::StmtKind::Continue:
            code_ << "    lit " << loopLabels_.back().first << "\n    jmp\n";
            return;
        case ir::StmtKind::LocalDecl: {
            const ir::Slot *slot = s.declSlot;
            rejectStruct(slot->type, s.line, "local '" + slot->sourceName + "'");
            if (isWide(slot->type)) genWideUnsupported(s.line, "local '" + slot->sourceName + "'");
            bool isStatic = currentFunctionIsLeaf_;
            if (isStatic) {
                int count = slot->arraySize > 0 ? slot->arraySize : 1;
                std::ostringstream o;
                o << staticLabel(slot) << ":\n";
                if (!slot->arrayInit.empty()) {
                    for (int i = 0; i < count; i++) {
                        if (i < static_cast<int>(slot->arrayInit.size())) {
                            const auto &e = slot->arrayInit[i];
                            o << "    .word " << (e.isSymbol ? (e.slot ? label(e.slot) : label(e.func))
                                                              : decimal(wrap32(e.value)))
                              << "\n";
                        } else {
                            o << "    .word 0\n";
                        }
                    }
                } else {
                    for (int i = 0; i < count; i++) o << "    .word 0\n";
                }
                trailer_.push_back(o.str());
            }
            if (s.initOrReturnOrExpr && slot->arraySize == 0) {
                genExpr(*s.initOrReturnOrExpr);
                genStoreSlot(slot);
            }
            return;
        }
        case ir::StmtKind::ExprStmt: {
            const ir::Expr &e = *s.initOrReturnOrExpr;
            genExpr(e);
            // A call to a `void` function (genCall()'s own callee leaves nothing - see
            // emitEpilogueAndReturn()) and an Intrinsic (a plain no-op - see genExpr()'s own
            // case) are the only expression kinds that leave no value here; every other kind
            // leaves exactly one (including an assignment, see genAssignThroughAddress()'s own
            // comment), which this discards.
            // Every call leaves exactly one cell (see emitEpilogueAndReturn()'s own comment on
            // why even a `void` function's call does) and every other expression kind leaves
            // exactly one too (including an assignment - see genAssignThroughAddress()), except
            // Intrinsic (a plain no-op - see genExpr()'s own case).
            if (e.kind != ir::ExprKind::Intrinsic) code_ << "    lit G___discard\n    st\n";
            return;
        }
        case ir::StmtKind::Empty:
            return;
        case ir::StmtKind::Asm:
            code_ << s.asmText << "\n";
            return;
        case ir::StmtKind::Label:
            code_ << irLabel(s.labelId) << ":\n";
            return;
        case ir::StmtKind::Goto:
            code_ << "    lit " << irLabel(s.labelId) << "\n    jmp\n";
            return;
    }
}

void VexCodegen::genCondBranch(const ir::Expr &cond, const std::string &Lfalse) {
    genExpr(cond);
    // `cond` is any C truth value (not necessarily 0/1 - e.g. `*s`, a pointer, a raw `i`), so
    // branch-if-false has to test equality with 0 directly: `EQU 0` gives 1 exactly when `cond`
    // was 0, and JCN then jumps on that.
    code_ << "    lit 0\n    equ\n";
    code_ << "    lit " << Lfalse << "\n    jcn\n";
}

// --- expressions ---------------------------------------------------------

void VexCodegen::genExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::IntLit:
            code_ << "    lit " << decimal(wrap32(e.intValue)) << "\n";
            return;
        case ir::ExprKind::Var:
            genLoadSlot(e.slot);
            return;
        case ir::ExprKind::Index:
            genIndex(e);
            code_ << "    ld\n";
            return;
        case ir::ExprKind::Assign:
            genAssign(e);
            return;
        case ir::ExprKind::DerefAssign:
            genDerefAssign(e);
            return;
        case ir::ExprKind::CompoundAssign:
            genCompoundAssign(e);
            return;
        case ir::ExprKind::CompoundDerefAssign:
            genCompoundDerefAssign(e);
            return;
        case ir::ExprKind::Unary: {
            if (e.op == "&") {
                genAddrOf(*e.rhs);
                return;
            }
            genExpr(*e.rhs);
            if (e.op == "-") {
                code_ << "    lit 0xFFFFFFFF\n    xor\n    lit 1\n    add\n";
            } else if (e.op == "!") {
                code_ << "    lit 0\n    equ\n";
            } else if (e.op == "~") {
                code_ << "    lit 0xFFFFFFFF\n    xor\n";
            } else if (e.op == "*") {
                code_ << "    ld\n";
            } else {
                fail(e.line, "unsupported unary operator '" + e.op + "'");
            }
            return;
        }
        case ir::ExprKind::Binary:
            genBinary(e);
            return;
        case ir::ExprKind::Call:
            genCall(e);
            return;
        case ir::ExprKind::Func:
            code_ << "    lit " << label(e.func) << "\n";
            return;
        case ir::ExprKind::CallPtr:
            genCallPtr(e);
            return;
        case ir::ExprKind::Intrinsic:
            // No interrupt-mask instruction exists on this machine at all (docs/SPEC.md section
            // 7: "Not provided: ... interrupts") - a plain no-op, same as i8008's own ion/iof.
            (void)e.op;
            return;
        case ir::ExprKind::Convert:
            genConvert(e);
            return;
        case ir::ExprKind::Ternary: {
            std::string Lelse = newLabel();
            std::string Lend = newLabel();
            genCondBranch(*e.lhs, Lelse);
            genExpr(*e.rhs);
            code_ << "    lit " << Lend << "\n    jmp\n";
            code_ << Lelse << ":\n";
            genExpr(*e.elseExpr);
            code_ << Lend << ":\n";
            return;
        }
    }
}

void VexCodegen::genConvert(const ir::Expr &e) {
    genExpr(*e.lhs);
    if (isWide(e.type) || isWide(e.lhs->type)) genWideUnsupported(e.line, "a 'long long' conversion");
    if (isPointer(e.type) || e.type.base == BaseType::Struct) return;
    if (e.type.base == BaseType::Char) {
        code_ << "    lit 0xFF\n    and\n";
        if (!e.type.isUnsigned) code_ << "    lit 0x80\n    xor\n    lit 0x80\n    sub\n";
    } else if (e.type.base == BaseType::Short) {
        code_ << "    lit 0xFFFF\n    and\n";
        if (!e.type.isUnsigned) code_ << "    lit 0x8000\n    xor\n    lit 0x8000\n    sub\n";
    }
    // Int/Long: already a full cell - no-op (see class comment's storage model).
}

void VexCodegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Index) {
        genIndex(operand);
        return;
    }
    if (operand.kind == ir::ExprKind::Var) {
        genSlotAddress(operand.slot);
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        // `&f` - IRBuilder already resolves a bare function name under `&` to this kind (see
        // IR.h's own ExprKind::Func comment) rather than wrapping a Var; its own entry label is
        // its address, the same one a direct call already targets.
        code_ << "    lit " << label(operand.func) << "\n";
        return;
    }
    fail(operand.line, "'&' isn't supported on this expression");
}

void VexCodegen::genIndex(const ir::Expr &e) {
    const ir::Slot *slot = e.slot;
    int elemWidthBytes = widthOfType(e.type);  // widthOfType() is already a byte count here.
    // An array slot decays to its own base address (genSlotAddress()); a pointer slot's base
    // is the *value stored in it* (genLoadSlot()), not its own storage address - `p[i]` is
    // `*(p+i)`, computed from whatever address `p` currently holds.
    if (slot->arraySize > 0) {
        genSlotAddress(slot);
    } else {
        genLoadSlot(slot);
    }
    genExpr(*e.lhs);
    code_ << "    lit " << decimal(static_cast<uint32_t>(elemWidthBytes)) << "\n    mul\n    add\n";
}

void VexCodegen::genSlotAddress(const ir::Slot *slot) {
    if (slot->kind == ir::SlotKind::Global) {
        code_ << "    lit " << label(slot) << "\n";
        return;
    }
    if (currentFunctionIsLeaf_) {
        code_ << "    lit " << staticLabel(slot) << "\n";
        return;
    }
    fail(0, "internal: taking the address of a return-stack-resident slot should have been rejected already");
}

void VexCodegen::genLoadSlot(const ir::Slot *slot) {
    if (slot->kind == ir::SlotKind::Global) {
        code_ << "    lit " << label(slot) << "\n    ld\n";
        return;
    }
    if (currentFunctionIsLeaf_) {
        code_ << "    lit " << staticLabel(slot) << "\n    ld\n";
        return;
    }
    code_ << "    lit " << decimal(static_cast<uint32_t>(frameDepth(slot))) << "\n    lds.r\n";
}

void VexCodegen::genStoreSlot(const ir::Slot *slot) {
    if (slot->kind == ir::SlotKind::Global) {
        code_ << "    lit " << label(slot) << "\n    st\n";
        return;
    }
    if (currentFunctionIsLeaf_) {
        code_ << "    lit " << staticLabel(slot) << "\n    st\n";
        return;
    }
    code_ << "    lit " << decimal(static_cast<uint32_t>(frameDepth(slot))) << "\n    sts.r\n";
}

// `addr` already sits on top of the return stack (depth 0) when this runs - shared by
// genAssign's indexed form and genDerefAssign. Stores `v` (computed here) at `addr` and leaves
// `v` itself on top of the data stack: an assignment is an expression with a value in C (e.g.
// `x = (y = 5)`, or just a bare `y = 5;` as a statement, whose ExprStmt discards exactly one
// value - see genStmt()'s own LocalDecl/ExprStmt cases), not merely a side effect.
void VexCodegen::genAssignThroughAddress(const ir::Expr &e) {
    genExpr(*e.rhs);                      // data=[v] - may itself reference a framed slot; the
                                            // caller's own addr PUSH is already tracked via
                                            // retAdjust_, so frameDepth() stays correct here.
    pushScratch();                          // retstack: addr(1), v(0); data=[]
    code_ << "    lit 0\n    lds.r\n";      // data=[v]
    code_ << "    lit 1\n    lds.r\n";      // data=[v,addr]
    code_ << "    st\n";                     // data=[]
    popScratch();                             // v -> kept as the result; data=[v]
    popScratch();
    code_ << "    lit G___discard\n    st\n";  // addr, discarded (lands above v; harmless)
}

void VexCodegen::genAssign(const ir::Expr &e) {
    if (e.lhs) {
        // slot[lhs] = rhs
        genIndex(e);            // data=[addr]
        pushScratch();           // retstack depth0=addr
        genAssignThroughAddress(e);
        return;
    }
    genExpr(*e.rhs);
    genStoreSlot(e.slot);
    genLoadSlot(e.slot);
}

void VexCodegen::genDerefAssign(const ir::Expr &e) {
    genExpr(*e.lhs);        // address
    pushScratch();           // retstack depth0=addr
    genAssignThroughAddress(e);
}

namespace {
std::string binOpInstr(const std::string &op) {
    if (op == "+") return "add";
    if (op == "-") return "sub";
    if (op == "*") return "mul";
    if (op == "&") return "and";
    if (op == "|") return "ora";
    if (op == "^") return "xor";
    return "";
}
}  // namespace

// Shared by both compound-assignment flavors below, once the target's current value is already
// on top of the data stack (`[old]`): combines it with `e.rhs`, leaving `[new]`.
void VexCodegen::applyCompoundOp(const ir::Expr &e) {
    std::string instr = binOpInstr(e.op);
    if (!instr.empty()) {
        genExpr(*e.rhs);
        code_ << "    " << instr << "\n";
        return;
    }
    if (e.op == "/" || e.op == "%") {
        genExpr(*e.rhs);
        genDivModFromStack(/*wantRemainder=*/e.op == "%", e.type.isUnsigned);
        return;
    }
    if (e.op == "<<" || e.op == ">>") {
        genExpr(*e.rhs);
        genShiftOp(e.op, e.type.isUnsigned);
        return;
    }
    fail(e.line, "unsupported compound operator '" + e.op + "='");
}

// `slot (op=) rhs` (e.lhs unset) or `slot[lhs] (op=) rhs` (e.lhs is the subscript) - either way,
// the target is memory-addressed (Global, or a leaf function's static Local/Param: an indexed
// compound assignment can only reach an array, and VexCodegen.h's class comment already confines
// every array to one of those two storage classes). An address-based target is loaded/stored
// through one extra `push`/`lds.r`/`pop` round trip so the address survives across whatever
// `applyCompoundOp()` itself pushes/pops.
// `addr` is already on top of the return stack (depth `addrDepth`) when this runs; combines the
// current value at `addr` with `e.rhs`, stores the result back, and leaves the compound
// assignment's own value (old if `e.yieldOld`, else new) on top of the data stack - then pops
// every temporary this pushed, in the exact reverse order, discarding whichever of
// {new, old} isn't the result.
void VexCodegen::genCompoundThroughAddress(const ir::Expr &e) {
    code_ << "    lit 0\n    lds.r\n    ld\n";  // data=[old]; addr still at depth0
    if (e.yieldOld) {
        pushScratch();
        code_ << "    lit 0\n    lds.r\n";  // retstack: addr(1),old(0); data=[old]
    }
    applyCompoundOp(e);  // data=[new] - may itself reference a framed slot; every outstanding
                          // PUSH above is already tracked via retAdjust_, so frameDepth() stays
                          // correct inside it.
    pushScratch();  // retstack: addr(yieldOld?2:1), old(yieldOld?1:-), new(0)
    int addrDepth = e.yieldOld ? 2 : 1;
    code_ << "    lit 0\n    lds.r\n";                     // data=[new]  ("new" is always depth0)
    code_ << "    lit " << addrDepth << "\n    lds.r\n";  // data=[new,addr] - addr on top, as `ST`'s
                                                             // own `(v addr --)` effect needs.
    code_ << "    st\n";                                    // data=[]
    if (e.yieldOld) {
        popScratch();
        code_ << "    lit G___discard\n    st\n";  // new, discarded
        popScratch();                                 // old -> kept as the result
        popScratch();
        code_ << "    lit G___discard\n    st\n";  // addr, discarded (lands above old; harmless)
    } else {
        popScratch();                                 // new -> kept as the result
        popScratch();
        code_ << "    lit G___discard\n    st\n";  // addr, discarded (lands above new; harmless)
    }
}

void VexCodegen::genCompoundAssign(const ir::Expr &e) {
    if (e.lhs) {
        genIndex(e);  // data=[addr]
        pushScratch();  // retstack depth0=addr; data=[]
        genCompoundThroughAddress(e);
        return;
    }
    const ir::Slot *slot = e.slot;
    genLoadSlot(slot);  // data=[old]
    if (e.yieldOld) {
        pushScratch();
        code_ << "    lit 0\n    lds.r\n";
    }
    applyCompoundOp(e);  // data=[new]
    genStoreSlot(slot);  // data=[]
    if (e.yieldOld) {
        popScratch();  // old -> result
    } else {
        genLoadSlot(slot);  // new -> result
    }
}

void VexCodegen::genCompoundDerefAssign(const ir::Expr &e) {
    genExpr(*e.lhs);  // address
    pushScratch();     // retstack depth0=addr; data=[]
    genCompoundThroughAddress(e);
}

void VexCodegen::genShiftOp(const std::string &op, bool isUnsigned) {
    needVarShift_ = true;
    if (op == "<<") {
        code_ << "    lit FN___shl\n    cal\n";
    } else if (isUnsigned) {
        code_ << "    lit FN___ushr\n    cal\n";
    } else {
        // Signed >>: an arithmetic shift only has to replicate the sign bit when `a` is
        // actually negative - for a non-negative `a`, arithmetic and logical shift are the
        // same operation. (`NOT(lsr(NOT(a), n))` is NOT a sign-agnostic identity: for a
        // non-negative `a` it produces garbage, not `lsr(a, n)` - it only holds when `a` is
        // negative.) `[a,n]` (n on top) is already on the data stack.
        std::string Lneg = newLabel();
        std::string Ldone = newLabel();
        pushScratch();
        pushScratch();  // retstack: n(1), a(0)
        code_ << "    lit 0\n    lds.r\n    lit 0x80000000\n    lth\n    lit 1\n    xor\n";  // isNegA
        code_ << "    lit " << Lneg << "\n    jcn\n";
        // Non-negative: lsr(a, n) directly.
        code_ << "    lit 0\n    lds.r\n    lit 1\n    lds.r\n";
        code_ << "    lit FN___ushr\n    cal\n";
        code_ << "    lit " << Ldone << "\n    jmp\n";
        code_ << Lneg << ":\n";
        code_ << "    lit 0\n    lds.r\n    lit 0xFFFFFFFF\n    xor\n";  // NOT(a)
        code_ << "    lit 1\n    lds.r\n";
        code_ << "    lit FN___ushr\n    cal\n";
        code_ << "    lit 0xFFFFFFFF\n    xor\n";
        code_ << Ldone << ":\n";
        popScratch();
        code_ << "    lit G___discard\n    st\n";  // a
        popScratch();
        code_ << "    lit G___discard\n    st\n";  // n
    }
}

void VexCodegen::genBinary(const ir::Expr &e) {
    bool isUnsigned = e.lhs->type.isUnsigned;
    if (e.op == "&&") {
        genLogicalAnd(*e.lhs, *e.rhs);
        return;
    }
    if (e.op == "||") {
        genLogicalOr(*e.lhs, *e.rhs);
        return;
    }
    if (e.op == "==") {
        genExpr(*e.lhs);
        genExpr(*e.rhs);
        code_ << "    equ\n";
        return;
    }
    if (e.op == "!=") {
        genExpr(*e.lhs);
        genExpr(*e.rhs);
        code_ << "    equ\n    lit 1\n    xor\n";
        return;
    }
    if (e.op == "<" || e.op == ">" || e.op == "<=" || e.op == ">=") {
        bool negate = (e.op == "<=" || e.op == ">=");
        bool swap = (e.op == ">" || e.op == "<=");
        const ir::Expr &a = swap ? *e.rhs : *e.lhs;
        const ir::Expr &b = swap ? *e.lhs : *e.rhs;
        genExpr(a);
        genExpr(b);
        if (!isUnsigned) {
            code_ << "    lit 0x80000000\n    xor\n    push\n    lit 0x80000000\n    xor\n    pop\n";
        }
        code_ << "    lth\n";
        if (negate) code_ << "    lit 1\n    xor\n";
        return;
    }
    if (e.op == "+" || e.op == "-" || e.op == "*" || e.op == "&" || e.op == "|" || e.op == "^") {
        genExpr(*e.lhs);
        genExpr(*e.rhs);
        code_ << "    " << binOpInstr(e.op) << "\n";
        return;
    }
    if (e.op == "/") {
        genDivMod(*e.lhs, *e.rhs, /*wantRemainder=*/false, isUnsigned);
        return;
    }
    if (e.op == "%") {
        genDivMod(*e.lhs, *e.rhs, /*wantRemainder=*/true, isUnsigned);
        return;
    }
    if (e.op == "<<" || e.op == ">>") {
        genExpr(*e.lhs);
        genExpr(*e.rhs);
        genShiftOp(e.op, isUnsigned);
        return;
    }
    fail(e.line, "unsupported binary operator '" + e.op + "'");
}

void VexCodegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string Lfalse = newLabel();
    std::string Lend = newLabel();
    genCondBranch(lhs, Lfalse);
    genCondBranch(rhs, Lfalse);
    code_ << "    lit 1\n";
    code_ << "    lit " << Lend << "\n    jmp\n";
    code_ << Lfalse << ":\n    lit 0\n";
    code_ << Lend << ":\n";
}

void VexCodegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string Lrhs = newLabel();
    std::string Lfalse = newLabel();
    std::string Ltrue = newLabel();
    std::string Lend = newLabel();
    genCondBranch(lhs, Lrhs);           // lhs false -> try rhs
    code_ << "    lit " << Ltrue << "\n    jmp\n";
    code_ << Lrhs << ":\n";
    genCondBranch(rhs, Lfalse);         // rhs also false -> 0
    code_ << Ltrue << ":\n    lit 1\n";
    code_ << "    lit " << Lend << "\n    jmp\n";
    code_ << Lfalse << ":\n    lit 0\n";
    code_ << Lend << ":\n";
}

void VexCodegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    genExpr(rhs);
    genDivModFromStack(wantRemainder, isUnsigned);
}

// Assumes `[a,b]` (b on top) is already on the data stack - see the header's own comment.
void VexCodegen::genDivModFromStack(bool wantRemainder, bool isUnsigned) {
    if (!wantRemainder) {
        if (isUnsigned) {
            code_ << "    div\n";
        } else {
            needSDivMod_ = true;
            code_ << "    lit FN___sdiv\n    cal\n";
        }
        return;
    }
    if (!isUnsigned) {
        needSDivMod_ = true;
        code_ << "    lit FN___smod\n    cal\n";
        return;
    }
    pushScratch();
    pushScratch();  // depth0=a, depth1=b
    code_ << "    lit 0\n    lds.r\n    lit 1\n    lds.r\n    div\n";  // Q
    pushScratch();  // depth0=Q, depth1=a, depth2=b
    code_ << "    lit 1\n    lds.r\n";                        // a
    code_ << "    lit 0\n    lds.r\n    lit 2\n    lds.r\n    mul\n";  // Q*b
    code_ << "    sub\n";
    popScratch();
    code_ << "    lit G___discard\n    st\n";
    popScratch();
    code_ << "    lit G___discard\n    st\n";
    popScratch();
    code_ << "    lit G___discard\n    st\n";
}

void VexCodegen::genCall(const ir::Expr &e) {
    for (const auto &a : e.args) genExpr(*a);
    code_ << "    lit " << label(e.func) << "\n    cal\n";
}

void VexCodegen::genCallPtr(const ir::Expr &e) {
    genExpr(*e.lhs);
    code_ << "    cal\n";
}

// --- runtime routines ---------------------------------------------------
//
// Shared by ensureRuntimeRoutines() (a whole-program compile emits only whichever of these a
// program actually needs) and vexRuntimeAsm() (VexLinkerFactory.cpp's entry object always
// defines every one of them unconditionally, since a separately-compiled object can't know
// whether some OTHER object linked alongside it needs them too - exactly the shared-pseudo-
// register pattern M6809LinkerFactory.cpp's own entry object uses for `ac`/`_bc`/`cx`/`dx`/
// `ptr`). Either way, `needSDivMod_`/`needVarShift_` are only ever consulted in object mode to
// decide NOT to emit a definition locally - see genDivModFromStack()/genShiftOp() and
// VexCodegen::generate()'s own `if (!objectMode)` guard.

namespace {
const char *const kVexSdivAsm =
    "FN___sdiv:\n"
    "    push\n    push\n"
    "    lit 0\n    lds.r\n    lit 0x80000000\n    lth\n    lit 1\n    xor\n"
    "    lit 1\n    lds.r\n    lit 0x80000000\n    lth\n    lit 1\n    xor\n"
    "    xor\n"
    "    push\n"
    "    lit 1\n    lds.r\n    lit 0x80000000\n    lth\n    lit 1\n    xor\n"
    "    lit L_sdiv_aneg\n    jcn\n"
    "    lit L_sdiv_adone\n    jmp\n"
    "L_sdiv_aneg:\n"
    "    lit 1\n    lds.r\n    lit 0xFFFFFFFF\n    xor\n    lit 1\n    add\n"
    "    lit 1\n    sts.r\n"
    "L_sdiv_adone:\n"
    "    lit 2\n    lds.r\n    lit 0x80000000\n    lth\n    lit 1\n    xor\n"
    "    lit L_sdiv_bneg\n    jcn\n"
    "    lit L_sdiv_bdone\n    jmp\n"
    "L_sdiv_bneg:\n"
    "    lit 2\n    lds.r\n    lit 0xFFFFFFFF\n    xor\n    lit 1\n    add\n"
    "    lit 2\n    sts.r\n"
    "L_sdiv_bdone:\n"
    "    lit 1\n    lds.r\n    lit 2\n    lds.r\n    div\n"
    "    lit 0\n    lds.r\n"
    "    lit L_sdiv_neg\n    jcn\n"
    "    lit L_sdiv_ret\n    jmp\n"
    "L_sdiv_neg:\n"
    "    lit 0xFFFFFFFF\n    xor\n    lit 1\n    add\n"
    "L_sdiv_ret:\n"
    "    pop\n    lit G___discard\n    st\n"
    "    pop\n    lit G___discard\n    st\n"
    "    pop\n    lit G___discard\n    st\n"
    "    ret\n";

const char *const kVexSmodAsm =
    "FN___smod:\n"
    "    push\n    push\n"
    "    lit 0\n    lds.r\n    lit 0x80000000\n    lth\n    lit 1\n    xor\n"
    "    push\n"
    "    lit 1\n    lds.r\n    lit 0x80000000\n    lth\n    lit 1\n    xor\n"
    "    lit L_smod_aneg\n    jcn\n"
    "    lit L_smod_adone\n    jmp\n"
    "L_smod_aneg:\n"
    "    lit 1\n    lds.r\n    lit 0xFFFFFFFF\n    xor\n    lit 1\n    add\n"
    "    lit 1\n    sts.r\n"
    "L_smod_adone:\n"
    "    lit 2\n    lds.r\n    lit 0x80000000\n    lth\n    lit 1\n    xor\n"
    "    lit L_smod_bneg\n    jcn\n"
    "    lit L_smod_bdone\n    jmp\n"
    "L_smod_bneg:\n"
    "    lit 2\n    lds.r\n    lit 0xFFFFFFFF\n    xor\n    lit 1\n    add\n"
    "    lit 2\n    sts.r\n"
    "L_smod_bdone:\n"
    "    lit 1\n    lds.r\n    lit 2\n    lds.r\n    div\n"
    "    push\n"
    "    lit 2\n    lds.r\n"
    "    lit 0\n    lds.r\n"
    "    lit 3\n    lds.r\n"
    "    mul\n"
    "    sub\n"
    "    lit 1\n    lds.r\n"
    "    lit L_smod_neg\n    jcn\n"
    "    lit L_smod_ret\n    jmp\n"
    "L_smod_neg:\n"
    "    lit 0xFFFFFFFF\n    xor\n    lit 1\n    add\n"
    "L_smod_ret:\n"
    "    pop\n    lit G___discard\n    st\n"
    "    pop\n    lit G___discard\n    st\n"
    "    pop\n    lit G___discard\n    st\n"
    "    pop\n    lit G___discard\n    st\n"
    "    ret\n";

const char *const kVexShlAsm =
    "FN___shl:\n"
    "    push\n    push\n"
    "L_shl_loop:\n"
    "    lit 1\n    lds.r\n"
    "    lit L_shl_body\n    jcn\n"
    "    lit L_shl_done\n    jmp\n"
    "L_shl_body:\n"
    "    lit 0\n    lds.r\n    lit 0\n    lds.r\n    add\n    lit 0\n    sts.r\n"
    "    lit 1\n    lds.r\n    lit 1\n    sub\n    lit 1\n    sts.r\n"
    "    lit L_shl_loop\n    jmp\n"
    "L_shl_done:\n"
    "    lit 0\n    lds.r\n"
    "    pop\n    lit G___discard\n    st\n"
    "    pop\n    lit G___discard\n    st\n"
    "    ret\n";

const char *const kVexUshrAsm =
    "FN___ushr:\n"
    "    push\n    push\n"
    "L_ushr_loop:\n"
    "    lit 1\n    lds.r\n"
    "    lit L_ushr_body\n    jcn\n"
    "    lit L_ushr_done\n    jmp\n"
    "L_ushr_body:\n"
    "    lit 0\n    lds.r\n    lit 2\n    div\n    lit 0\n    sts.r\n"
    "    lit 1\n    lds.r\n    lit 1\n    sub\n    lit 1\n    sts.r\n"
    "    lit L_ushr_loop\n    jmp\n"
    "L_ushr_done:\n"
    "    lit 0\n    lds.r\n"
    "    pop\n    lit G___discard\n    st\n"
    "    pop\n    lit G___discard\n    st\n"
    "    ret\n";
}  // namespace

void VexCodegen::ensureRuntimeRoutines() {
    if (needSDivMod_) {
        trailer_.push_back(kVexSdivAsm);
        trailer_.push_back(kVexSmodAsm);
    }
    if (needVarShift_) {
        trailer_.push_back(kVexShlAsm);
        trailer_.push_back(kVexUshrAsm);
    }
    trailer_.push_back("G___retval:\n    .word 0\n");
}

// Always every routine, unconditionally - see this section's own header comment.
std::string vexRuntimeAsm() {
    std::string text;
    text += "G___discard:\n    .word 0\n";
    text += "G___retval:\n    .word 0\n";
    text += kVexSdivAsm;
    text += kVexSmodAsm;
    text += kVexShlAsm;
    text += kVexUshrAsm;
    return text;
}

}  // namespace rcc
