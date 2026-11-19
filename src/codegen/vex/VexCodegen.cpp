#include "VexCodegen.h"

#include <set>
#include <sstream>
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

// --- register/branch helpers -------------------------------------------------

void VexCodegen::ldiNum(int rd, uint32_t v) { ldi(rd, decimal(v)); }

void VexCodegen::jmpTo(const std::string &label, int rtarget) {
    ldi(rtarget, label);
    code_ << "    jmp " << reg(rtarget) << "\n";
}

void VexCodegen::jcnTo(const std::string &label, int rcond, int rtarget) {
    ldi(rtarget, label);
    code_ << "    jcn " << reg(rtarget) << ", " << reg(rcond) << "\n";
}

void VexCodegen::calTo(const std::string &label, int rtarget) {
    ldi(rtarget, label);
    code_ << "    cal " << reg(rtarget) << "\n";
}

void VexCodegen::push(int rx, int rscratch) {
    int s = (rscratch == rx) ? 1 : rscratch;
    ldiNum(s, 4);
    alu3("sub", 5, 5, s);
    st(5, rx);
}

void VexCodegen::pop(int rx, int rscratch) {
    int s = (rscratch == rx) ? 1 : rscratch;
    ld(rx, 5);
    ldiNum(s, 4);
    alu3("add", 5, 5, s);
}

// --- top-level driver -------------------------------------------------------

std::string VexCodegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }

    if (!objectMode) {
        code_ << "_start:\n";
        ldiNum(5, kVexStackTop);
        calTo(label(mainFn));
        // Normal halt (this final `ret` pops the zero word below RS_TOP, never written by
        // anything - docs/SPEC.md section 3's own "Why RET halts on a popped zero") - see
        // VexCodegen.h's own class comment for why this, not an explicit DEO-to-port-0, is this
        // backend's entry convention.
        code_ << "    ret\n\n";
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
    irLabels_.clear();

    for (ir::Slot *p : fn->params) {
        rejectStruct(p->type, fb.line, "parameter '" + p->sourceName + "' of '" + fn->sourceName + "'");
        if (isWide(p->type)) genWideUnsupported(fb.line, "parameter '" + p->sourceName + "' of '" + fn->sourceName + "'");
    }

    code_ << curFunctionLabel_ << ":\n";
    emitPrologue(fn);
    genStmt(*fb.body);
    code_ << epilogueLabel() << ":\n";
    emitEpilogueAndReturn();
}

void VexCodegen::emitPrologue(const ir::Function *fn) {
    push(4);  // filler - see the class comment's "Stack frame" section for why.
    push(4);  // the real saved old FP.
    mov(4, 5);  // FP = SP.
    if (fn->frameSize > 0) {
        ldiNum(2, static_cast<uint32_t>(fn->frameSize));
        alu3("sub", 5, 5, 2);
    }
}

void VexCodegen::emitEpilogueAndReturn() {
    // The return value (if any) already sits in R0 from whatever last computed it - unlike the
    // old stack machine, there's no shared "every call leaves exactly one cell" bookkeeping to
    // maintain here; a void function's caller simply never reads R0.
    mov(5, 4);  // SP = FP: deallocate locals.
    pop(4);      // Restore the real saved FP.
    pop(6);        // Discard the filler (R6 is safe to clobber here - see the class comment).
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
                jmpTo(Lend);
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
            jmpTo(Ltop);
            code_ << Lend << ":\n";
            loopLabels_.pop_back();
            return;
        }
        case ir::StmtKind::Return:
            if (s.initOrReturnOrExpr) genExpr(*s.initOrReturnOrExpr);
            jmpTo(epilogueLabel());
            return;
        case ir::StmtKind::Break:
            jmpTo(loopLabels_.back().second);
            return;
        case ir::StmtKind::Continue:
            jmpTo(loopLabels_.back().first);
            return;
        case ir::StmtKind::LocalDecl: {
            const ir::Slot *slot = s.declSlot;
            rejectStruct(slot->type, s.line, "local '" + slot->sourceName + "'");
            if (isWide(slot->type)) genWideUnsupported(s.line, "local '" + slot->sourceName + "'");
            if (slot->arraySize > 0) {
                // Array locals live in this function's own frame (real addressable memory
                // now - see the class comment's "Stack frame" section), so only an element
                // with an explicit initializer gets written here; one with none is left
                // genuinely uninitialized, exactly like any other automatic C array - unlike
                // the old stack-machine backend (every leaf local was static, so the
                // language's own zero-init-for-statics rule forced every element to 0
                // regardless). An explicit partial initializer is already padded with
                // trailing zero elements by IRBuilder's own padArrayInit(), so this still
                // zero-fills the rest of a partially-initialized array correctly.
                if (!slot->arrayInit.empty()) {
                    int elemWidth = widthOfType(slot->type);
                    for (int i = 0; i < slot->arraySize; i++) {
                        genFrameAddress(slot, 2);
                        if (i > 0) {
                            ldiNum(3, static_cast<uint32_t>(i * elemWidth));
                            alu3("add", 2, 2, 3);
                        }
                        if (i < static_cast<int>(slot->arrayInit.size())) {
                            const auto &e = slot->arrayInit[i];
                            if (e.isSymbol) {
                                ldi(0, e.slot ? label(e.slot) : label(e.func));
                            } else {
                                ldiNum(0, wrap32(e.value));
                            }
                        } else {
                            ldiNum(0, 0);
                        }
                        st(2, 0);
                    }
                }
                return;
            }
            if (s.initOrReturnOrExpr) {
                genExpr(*s.initOrReturnOrExpr);
                genStoreSlot(slot);
            }
            return;
        }
        case ir::StmtKind::ExprStmt:
            // The result (if any) is simply left in R0 and ignored - unlike the old stack
            // machine, there's no stack cell left behind to clean up.
            genExpr(*s.initOrReturnOrExpr);
            return;
        case ir::StmtKind::Empty:
            return;
        case ir::StmtKind::Asm:
            code_ << s.asmText << "\n";
            return;
        case ir::StmtKind::Label:
            code_ << irLabel(s.labelId) << ":\n";
            return;
        case ir::StmtKind::Goto:
            jmpTo(irLabel(s.labelId));
            return;
    }
}

void VexCodegen::genCondBranch(const ir::Expr &cond, const std::string &Lfalse) {
    genExpr(cond);
    // `cond` is any C truth value (not necessarily 0/1 - e.g. `*s`, a pointer, a raw `i`), so
    // branch-if-false has to test equality with 0 directly: `CEQ 0` gives 1 exactly when `cond`
    // was 0, and JCN then jumps on that.
    ldiNum(2, 0);
    alu3("ceq", 0, 0, 2);
    jcnTo(Lfalse, /*rcond=*/0, /*rtarget=*/2);
}

// --- expressions ---------------------------------------------------------

void VexCodegen::genExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::IntLit:
            ldiNum(0, wrap32(e.intValue));
            return;
        case ir::ExprKind::FloatLit:
            fail(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::Var:
            genLoadSlot(e.slot);
            return;
        case ir::ExprKind::Index:
            genIndex(e);
            ld(0, 0);
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
                ldiNum(2, 0xFFFFFFFF);
                alu3("xor", 0, 0, 2);
                ldiNum(2, 1);
                alu3("add", 0, 0, 2);
            } else if (e.op == "!") {
                ldiNum(2, 0);
                alu3("ceq", 0, 0, 2);
            } else if (e.op == "~") {
                ldiNum(2, 0xFFFFFFFF);
                alu3("xor", 0, 0, 2);
            } else if (e.op == "*") {
                ld(0, 0);
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
            ldi(0, label(e.func));
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
            jmpTo(Lend);
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
        ldiNum(2, 0xFF);
        alu3("and", 0, 0, 2);
        if (!e.type.isUnsigned) {
            ldiNum(2, 0x80);
            alu3("xor", 0, 0, 2);
            alu3("sub", 0, 0, 2);
        }
    } else if (e.type.base == BaseType::Short) {
        ldiNum(2, 0xFFFF);
        alu3("and", 0, 0, 2);
        if (!e.type.isUnsigned) {
            ldiNum(2, 0x8000);
            alu3("xor", 0, 0, 2);
            alu3("sub", 0, 0, 2);
        }
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
        ldi(0, label(operand.func));
        return;
    }
    fail(operand.line, "'&' isn't supported on this expression");
}

void VexCodegen::genIndex(const ir::Expr &e) {
    const ir::Slot *slot = e.slot;
    int elemWidthBytes = widthOfType(e.type);
    // An array slot decays to its own base address (genSlotAddress()); a pointer slot's base
    // is the *value stored in it* (genLoadSlot()), not its own storage address - `p[i]` is
    // `*(p+i)`, computed from whatever address `p` currently holds.
    if (slot->arraySize > 0) {
        genSlotAddress(slot);
    } else {
        genLoadSlot(slot);
    }
    push(0);  // protect the base address across evaluating the subscript expr.
    genExpr(*e.lhs);
    pop(1);  // R1 = base, R0 = index.
    ldiNum(2, static_cast<uint32_t>(elemWidthBytes));
    alu3("mul", 0, 0, 2);
    alu3("add", 0, 1, 0);
}

void VexCodegen::genFrameAddress(const ir::Slot *slot, int rd) {
    if (slot->kind == ir::SlotKind::Param) {
        ldiNum(rd, static_cast<uint32_t>(slot->frameOffset));
        alu3("add", rd, 4, rd);
        return;
    }
    // Local: an array addresses from its lowest byte (element 0), via ir::localLowOffset() -
    // frameOffset alone is only an ordinary scalar's own address (vex has no 'long long'/wide
    // scalar to need ir::localOffsetDescendingWide()'s other case - see M6809Codegen.cpp's own
    // loadFrameAddr() for the general three-way version this specializes).
    uint32_t off = slot->arraySize > 0 ? static_cast<uint32_t>(ir::localLowOffset(*slot, typeWidths()))
                                        : static_cast<uint32_t>(slot->frameOffset);
    ldiNum(rd, off);
    alu3("sub", rd, 4, rd);
}

void VexCodegen::genSlotAddress(const ir::Slot *slot) {
    if (slot->kind == ir::SlotKind::Global) {
        ldi(0, label(slot));
        return;
    }
    genFrameAddress(slot, 0);
}

void VexCodegen::genLoadSlot(const ir::Slot *slot) {
    if (slot->kind == ir::SlotKind::Global) {
        ldi(0, label(slot));
        ld(0, 0);
        return;
    }
    genFrameAddress(slot, 0);
    ld(0, 0);
}

void VexCodegen::genStoreSlot(const ir::Slot *slot) {
    if (slot->kind == ir::SlotKind::Global) {
        ldi(2, label(slot));
        st(2, 0);
        return;
    }
    genFrameAddress(slot, 2);
    st(2, 0);
}

// Precondition: the target address is in R0. Stores the value genExpr(e.rhs) computes there and
// leaves that same value in R0 - an assignment is an expression with a value in C (e.g.
// `x = (y = 5)`, or just a bare `y = 5;` as a statement), not merely a side effect.
void VexCodegen::genAssignThroughAddress(const ir::Expr &e) {
    push(0);              // Protect the address; stack:[addr].
    genExpr(*e.rhs);        // R0 = value (R1-R3 free to use; addr is safe on the stack).
    pop(2);                   // R2 = addr; R0 still holds the value.
    st(2, 0);
}

void VexCodegen::genAssign(const ir::Expr &e) {
    if (e.lhs) {
        genIndex(e);  // R0 = addr.
        genAssignThroughAddress(e);
        return;
    }
    genExpr(*e.rhs);  // R0 = value.
    genStoreSlot(e.slot);  // Stores; R0 (the result) is left unchanged.
}

void VexCodegen::genDerefAssign(const ir::Expr &e) {
    genExpr(*e.lhs);  // R0 = address.
    genAssignThroughAddress(e);
}

// Precondition: the compound assignment's current value is in R0.
void VexCodegen::applyCompoundOp(const ir::Expr &e) {
    std::string instr = binOpInstr(e.op);
    if (!instr.empty()) {
        push(0);            // Protect the current value.
        genExpr(*e.rhs);      // R0 = rhs.
        pop(1);                 // R1 = current, R0 = rhs.
        alu3(instr, 0, 1, 0);    // R0 = current OP rhs.
        return;
    }
    if (e.op == "/" || e.op == "%") {
        push(0);
        genExpr(*e.rhs);
        pop(1);  // R1 = current (a), R0 = rhs (b) - exactly genDivModFromRegs()'s own convention.
        genDivModFromRegs(/*wantRemainder=*/e.op == "%", e.type.isUnsigned);
        return;
    }
    if (e.op == "<<" || e.op == ">>") {
        push(0);
        genExpr(*e.rhs);
        pop(1);  // R1 = current (value), R0 = rhs (count) - genShiftOp()'s own convention.
        genShiftOp(e.op, e.type.isUnsigned);
        return;
    }
    fail(e.line, "unsupported compound operator '" + e.op + "='");
}

// Precondition: the target address is in R0. Loads the current value, combines it with `e.rhs`
// per `e.op`, stores the result back, and leaves the compound assignment's own value (old if
// `e.yieldOld`, else new) in R0.
void VexCodegen::genCompoundThroughAddress(const ir::Expr &e) {
    push(0);               // Protect the address; stack:[addr]; R0 still == addr too.
    ld(0, 0);                 // R0 = old value (load through the same register - the address is
                                // read before being overwritten, same as everywhere else here).
    if (e.yieldOld) push(0);     // stack:[addr, old] if yieldOld, else just [addr].
    applyCompoundOp(e);            // R0 = new value; balanced internally, our own pushes above
                                     // are left exactly as they are.
    if (e.yieldOld) {
        pop(3);  // R3 = old.
        pop(2, /*rscratch=*/1);  // R2 = addr - R1 as the scratch, not the default R3, which is
                                   // still holding "old" from the pop above.
        st(2, 0);    // Store the new value.
        mov(0, 3);    // R0 = old (the result).
    } else {
        pop(2);  // R2 = addr (the only thing on the stack).
        st(2, 0);  // Store the new value; R0 (already new) is the result.
    }
}

void VexCodegen::genCompoundAssign(const ir::Expr &e) {
    if (e.lhs) {
        genIndex(e);  // R0 = addr.
        genCompoundThroughAddress(e);
        return;
    }
    const ir::Slot *slot = e.slot;
    genLoadSlot(slot);  // R0 = old.
    if (e.yieldOld) push(0);
    applyCompoundOp(e);  // R0 = new.
    genStoreSlot(slot);   // Stores; R0 unchanged.
    if (e.yieldOld) pop(0);  // R0 = old (overwrites new, which is already stored).
    // Else: R0 already holds the new value - the correct result, no reload needed.
}

void VexCodegen::genCompoundDerefAssign(const ir::Expr &e) {
    genExpr(*e.lhs);  // R0 = address.
    genCompoundThroughAddress(e);
}

// Precondition: R1 = a (value), R0 = n (count) - the natural landing spot of every call site's
// own push-left/evaluate-right/pop pattern. Leaves the result in R0.
void VexCodegen::genShiftOp(const std::string &op, bool isUnsigned) {
    needVarShift_ = true;
    if (op == "<<") {
        calTo("FN___shl");
        return;
    }
    if (isUnsigned) {
        calTo("FN___ushr");
        return;
    }
    // Signed >>: an arithmetic shift only has to replicate the sign bit when `a` is actually
    // negative - for a non-negative `a`, arithmetic and logical shift are the same operation.
    std::string Lneg = newLabel();
    std::string Ldone = newLabel();
    ldiNum(2, 0x80000000);
    alu3("and", 2, 1, 2);  // R2 = signbit(a).
    jcnTo(Lneg, /*rcond=*/2, /*rtarget=*/3);
    // Non-negative: ushr(a, n) directly.
    calTo("FN___ushr");
    jmpTo(Ldone, /*rtarget=*/3);
    code_ << Lneg << ":\n";
    ldiNum(2, 0xFFFFFFFF);
    alu3("xor", 1, 1, 2);  // R1 = NOT(a).
    calTo("FN___ushr");
    ldiNum(2, 0xFFFFFFFF);
    alu3("xor", 0, 0, 2);  // R0 = NOT(ushr(NOT(a), n)).
    code_ << Ldone << ":\n";
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
    if (e.op == "==" || e.op == "!=" || e.op == "<" || e.op == ">" || e.op == "<=" || e.op == ">=") {
        genCompare(e, isUnsigned);
        return;
    }
    if (e.op == "+" || e.op == "-" || e.op == "*" || e.op == "&" || e.op == "|" || e.op == "^") {
        genExpr(*e.lhs);
        push(0);
        genExpr(*e.rhs);
        pop(1);  // R1 = lhs, R0 = rhs.
        alu3(binOpInstr(e.op), 0, 1, 0);
        return;
    }
    if (e.op == "/" || e.op == "%") {
        genDivMod(*e.lhs, *e.rhs, /*wantRemainder=*/e.op == "%", isUnsigned);
        return;
    }
    if (e.op == "<<" || e.op == ">>") {
        genExpr(*e.lhs);
        push(0);
        genExpr(*e.rhs);
        pop(1);  // R1 = value, R0 = count.
        genShiftOp(e.op, isUnsigned);
        return;
    }
    fail(e.line, "unsupported binary operator '" + e.op + "'");
}

void VexCodegen::genCompare(const ir::Expr &e, bool isUnsigned) {
    if (e.op == "==" || e.op == "!=") {
        genExpr(*e.lhs);
        push(0);
        genExpr(*e.rhs);
        pop(1);  // R1 = lhs, R0 = rhs.
        alu3("ceq", 0, 1, 0);
        if (e.op == "!=") {
            ldiNum(2, 1);
            alu3("xor", 0, 0, 2);
        }
        return;
    }
    bool negate = (e.op == "<=" || e.op == ">=");
    bool swap = (e.op == ">" || e.op == "<=");
    const ir::Expr &a = swap ? *e.rhs : *e.lhs;
    const ir::Expr &b = swap ? *e.lhs : *e.rhs;
    genExpr(a);
    push(0);
    genExpr(b);
    pop(1);  // R1 = a, R0 = b.
    if (!isUnsigned) {
        // Flip the sign bit of both operands, turning a signed comparison into an equivalent
        // unsigned one (the same idiom docs/SPEC.md section 4 itself documents).
        ldiNum(2, 0x80000000);
        alu3("xor", 1, 1, 2);
        alu3("xor", 0, 0, 2);
    }
    alu3("clt", 0, 1, 0);
    if (negate) {
        ldiNum(2, 1);
        alu3("xor", 0, 0, 2);
    }
}

void VexCodegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string Lfalse = newLabel();
    std::string Lend = newLabel();
    genCondBranch(lhs, Lfalse);
    genCondBranch(rhs, Lfalse);
    ldiNum(0, 1);
    jmpTo(Lend);
    code_ << Lfalse << ":\n";
    ldiNum(0, 0);
    code_ << Lend << ":\n";
}

void VexCodegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string Lrhs = newLabel();
    std::string Lfalse = newLabel();
    std::string Ltrue = newLabel();
    std::string Lend = newLabel();
    genCondBranch(lhs, Lrhs);  // lhs false -> try rhs.
    jmpTo(Ltrue);
    code_ << Lrhs << ":\n";
    genCondBranch(rhs, Lfalse);  // rhs also false -> 0.
    code_ << Ltrue << ":\n";
    ldiNum(0, 1);
    jmpTo(Lend);
    code_ << Lfalse << ":\n";
    ldiNum(0, 0);
    code_ << Lend << ":\n";
}

void VexCodegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    push(0);
    genExpr(rhs);
    pop(1);  // R1 = lhs (a), R0 = rhs (b).
    genDivModFromRegs(wantRemainder, isUnsigned);
}

// Precondition: R1 = a, R0 = b. Leaves the result in R0.
void VexCodegen::genDivModFromRegs(bool wantRemainder, bool isUnsigned) {
    if (!wantRemainder) {
        if (isUnsigned) {
            alu3("div", 0, 1, 0);
        } else {
            needSDivMod_ = true;
            calTo("FN___sdiv");
        }
        return;
    }
    if (!isUnsigned) {
        needSDivMod_ = true;
        calTo("FN___smod");
        return;
    }
    // Unsigned remainder: a - (a/b)*b. Plain ALU ops only write their own destination, so a
    // (R1) and b (R0) both survive untouched until the final SUB needs them.
    alu3("div", 2, 1, 0);  // R2 = a/b.
    alu3("mul", 3, 2, 0);    // R3 = (a/b)*b.
    alu3("sub", 0, 1, 3);     // R0 = a - (a/b)*b.
}

void VexCodegen::genCall(const ir::Expr &e) {
    // Pushed right-to-left so the last-pushed argument (param 0) ends up closest to the
    // callee's FP - matches IRBuilder's frameOffset convention (param 0 has the smallest
    // offset) with no vex-specific correction; see VexCodegen.h's "Stack frame" section.
    for (auto it = e.args.rbegin(); it != e.args.rend(); ++it) {
        genExpr(**it);
        push(0);
    }
    calTo(label(e.func));
    // The callee's own epilogue only deallocates its own frame (locals + the two prologue
    // pushes) - the caller is the one who pushed the arguments, so the caller is the one who
    // must deallocate them too, same cdecl-style convention MipsCodegen.cpp's own genCall-
    // equivalent uses. R0 (the return value) is untouched by this - only R2/R5 are used.
    if (!e.args.empty()) {
        ldiNum(2, static_cast<uint32_t>(4 * e.args.size()));
        alu3("add", 5, 5, 2);
    }
}

void VexCodegen::genCallPtr(const ir::Expr &e) {
    genExpr(*e.lhs);  // R0 = function address.
    mov(2, 0);
    code_ << "    cal " << reg(2) << "\n";
}

// --- runtime routines ---------------------------------------------------
//
// Shared by ensureRuntimeRoutines() (a whole-program compile emits only whichever of these a
// program actually needs) and vexRuntimeAsm() (VexLinkerFactory.cpp's entry object always
// defines every one of them unconditionally, since a separately-compiled object can't know
// whether some OTHER object linked alongside it needs them too). Register convention: R1 = a
// (the first/left operand - dividend, shift value), R0 = b (the second/right operand - divisor,
// shift count), result in R0 - the exact landing spot every call site's own push-left/evaluate-
// right/pop pattern already produces, so no register shuffling is ever needed at a call site.
// Each routine is a leaf (no CAL of its own) with no stack frame of its own, so it freely
// borrows R2/R3/R6 as scratch (R6 specifically for a branch target, never used anywhere else in
// this backend) without saving or restoring any of them - a (R1) and b (R0) themselves are also
// fair game to overwrite, since neither is useful to the caller after the call returns.

namespace {
const char *const kVexSdivAsm =
    "FN___sdiv:\n"
    "    ldi r2, 0x80000000\n"
    "    and r3, r1, r2\n"
    "    and r2, r0, r2\n"
    "    xor r3, r3, r2\n"
    "    ldi r2, 0x80000000\n"
    "    and r2, r1, r2\n"
    "    ldi r6, L_sdiv_aneg\n"
    "    jcn r6, r2\n"
    "    ldi r6, L_sdiv_adone\n"
    "    jmp r6\n"
    "L_sdiv_aneg:\n"
    "    ldi r2, 0xFFFFFFFF\n"
    "    xor r1, r1, r2\n"
    "    ldi r2, 1\n"
    "    add r1, r1, r2\n"
    "L_sdiv_adone:\n"
    "    ldi r2, 0x80000000\n"
    "    and r2, r0, r2\n"
    "    ldi r6, L_sdiv_bneg\n"
    "    jcn r6, r2\n"
    "    ldi r6, L_sdiv_bdone\n"
    "    jmp r6\n"
    "L_sdiv_bneg:\n"
    "    ldi r2, 0xFFFFFFFF\n"
    "    xor r0, r0, r2\n"
    "    ldi r2, 1\n"
    "    add r0, r0, r2\n"
    "L_sdiv_bdone:\n"
    "    div r0, r1, r0\n"
    "    ldi r6, L_sdiv_neg\n"
    "    jcn r6, r3\n"
    "    ldi r6, L_sdiv_ret\n"
    "    jmp r6\n"
    "L_sdiv_neg:\n"
    "    ldi r2, 0xFFFFFFFF\n"
    "    xor r0, r0, r2\n"
    "    ldi r2, 1\n"
    "    add r0, r0, r2\n"
    "L_sdiv_ret:\n"
    "    ret\n";

const char *const kVexSmodAsm =
    "FN___smod:\n"
    "    ldi r2, 0x80000000\n"
    "    and r3, r1, r2\n"
    "    ldi r6, L_smod_aneg\n"
    "    jcn r6, r3\n"
    "    ldi r6, L_smod_adone\n"
    "    jmp r6\n"
    "L_smod_aneg:\n"
    "    ldi r2, 0xFFFFFFFF\n"
    "    xor r1, r1, r2\n"
    "    ldi r2, 1\n"
    "    add r1, r1, r2\n"
    "L_smod_adone:\n"
    "    ldi r2, 0x80000000\n"
    "    and r2, r0, r2\n"
    "    ldi r6, L_smod_bneg\n"
    "    jcn r6, r2\n"
    "    ldi r6, L_smod_bdone\n"
    "    jmp r6\n"
    "L_smod_bneg:\n"
    "    ldi r2, 0xFFFFFFFF\n"
    "    xor r0, r0, r2\n"
    "    ldi r2, 1\n"
    "    add r0, r0, r2\n"
    "L_smod_bdone:\n"
    "    div r2, r1, r0\n"
    "    mul r2, r2, r0\n"
    "    sub r0, r1, r2\n"
    "    ldi r6, L_smod_neg\n"
    "    jcn r6, r3\n"
    "    ldi r6, L_smod_ret\n"
    "    jmp r6\n"
    "L_smod_neg:\n"
    "    ldi r2, 0xFFFFFFFF\n"
    "    xor r0, r0, r2\n"
    "    ldi r2, 1\n"
    "    add r0, r0, r2\n"
    "L_smod_ret:\n"
    "    ret\n";

const char *const kVexShlAsm =
    "FN___shl:\n"
    "    mov r2, r1\n"
    "L_shl_loop:\n"
    "    ldi r6, L_shl_body\n"
    "    jcn r6, r0\n"
    "    ldi r6, L_shl_done\n"
    "    jmp r6\n"
    "L_shl_body:\n"
    "    add r2, r2, r2\n"
    "    ldi r6, 1\n"
    "    sub r0, r0, r6\n"
    "    ldi r6, L_shl_loop\n"
    "    jmp r6\n"
    "L_shl_done:\n"
    "    mov r0, r2\n"
    "    ret\n";

const char *const kVexUshrAsm =
    "FN___ushr:\n"
    "    mov r2, r1\n"
    "L_ushr_loop:\n"
    "    ldi r6, L_ushr_body\n"
    "    jcn r6, r0\n"
    "    ldi r6, L_ushr_done\n"
    "    jmp r6\n"
    "L_ushr_body:\n"
    "    ldi r6, 2\n"
    "    div r2, r2, r6\n"
    "    ldi r6, 1\n"
    "    sub r0, r0, r6\n"
    "    ldi r6, L_ushr_loop\n"
    "    jmp r6\n"
    "L_ushr_done:\n"
    "    mov r0, r2\n"
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
}

// Always every routine, unconditionally - see this section's own header comment.
std::string vexRuntimeAsm() {
    std::string text;
    text += kVexSdivAsm;
    text += kVexSmodAsm;
    text += kVexShlAsm;
    text += kVexUshrAsm;
    return text;
}

}  // namespace rcc
