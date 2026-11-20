#include "TMS9900Codegen.h"

#include <cstdint>
#include <stdexcept>

#include "TMS9900Assembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

// Code base; the simulator sets PC here directly, no reset vector. WP (the Workspace
// Pointer - a real, separate CPU register, not one of R0-R15 themselves) must point
// somewhere in RAM before R0-R15 mean anything at all; this backend always points it here,
// 32 bytes (R0-R15) comfortably inside ordinary RAM and, not coincidentally, the same address
// the TI-99/4A's own 256-byte scratchpad RAM starts at ($8300) - see docs/platforms.md's own
// `tms9900` entry.
constexpr uint32_t kBaseAddress = 0x2000;
constexpr uint32_t kWorkspaceBase = 0x8300;
// Initial r15 (software stack pointer - the TMS9900 has no hardware PUSH/POP of its own);
// grows down into ordinary RAM, well clear of both the workspace and any reasonably-sized
// program's own code.
constexpr uint32_t kInitialSP = 0xF000;
}  // namespace

std::string TMS9900Codegen::arrayElemText(const ir::ArrayInitElem &e) {
    if (!e.isSymbol) return std::to_string(static_cast<int16_t>(static_cast<uint16_t>(e.value)));
    return e.slot ? label(e.slot) : label(e.func);
}

void TMS9900Codegen::declareArray(const std::string &lbl, const std::vector<ir::ArrayInitElem> &values,
                                   Type elemType) {
    if (values.empty()) return;
    int w = widthOfType(elemType);
    if (w > 2) failWide(0);
    const char *directive = w == 1 ? "db" : "dw";
    std::ostringstream line;
    line << lbl << ": " << directive << " ";
    bool first = true;
    for (const auto &e : values) {
        if (!first) line << ", ";
        line << arrayElemText(e);
        first = false;
    }
    trailer_.push_back(line.str());
    // Every instruction and every word-sized memory reference needs an even address; only a
    // `db` array can leave the running offset odd, so pad it back to even here.
    if (w == 1 && (values.size() % 2) != 0) trailer_.push_back("db 0");
}

void TMS9900Codegen::failWide(int line) const {
    err(line, "internal: 'long'/'long long' isn't implemented for --target=tms9900 yet - see "
               "TMS9900Codegen.h's own header comment");
}

std::string TMS9900Codegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    emitRaw(cat("org ", (objectMode ? 0 : kBaseAddress)));
    emitRaw("");
    if (!objectMode) {
        emitLabel("_start");
        emit("lwpi", cat(kWorkspaceBase));
        emit("li", cat("r15,", kInitialSP));
        emit("bl", cat("[", label(mainFn), "]"));
        emitLabel("halt");
        emit("jmp", "halt");
        emitRaw("");
    }

    for (const auto &fb : prog.functionBodies) genFunction(fb);

    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        if (slot->isExternal) continue;
        const std::string lbl = label(slot.get());
        if (slot->arraySize > 0) {
            declareArray(lbl, slot->arrayInit, slot->type);
        } else if (slot->type.base == BaseType::Struct) {
            declareArray(lbl, std::vector<ir::ArrayInitElem>(slot->type.structSize), Type{BaseType::Char, false});
        } else {
            int w = widthOfType(slot->type);
            if (w > 2) failWide(0);
            long v = slot->hasInit ? slot->initValue : 0;
            const char *directive = w == 1 ? "db" : "dw";
            trailer_.push_back(lbl + ": " + directive + " " + std::to_string(static_cast<int16_t>(v)) + "  ; " +
                                slot->sourceName);
            if (w == 1) trailer_.push_back("db 0");
        }
    }

    std::ostringstream out;
    out << code_.str();
    for (const auto &line : trailer_) out << line << "\n";
    return out.str();
}

std::map<std::string, std::string> TMS9900Codegen::perFileAsm() const { return perFileAsm_; }

// Prologue saves the real return-address register (r11, set by `bl` - see this class's own
// header comment) and the caller's frame pointer, establishes this call's own frame pointer,
// then reserves `frameSize` bytes of locals below it - all via the software stack `r15` bump-
// allocates. The epilogue (shared by every `return` and the fall-through exit) undoes exactly
// that, then returns via "b [r11]" - the real, documented TMS9900 subroutine-return idiom.
void TMS9900Codegen::genFunction(const ir::FunctionBody &fn) {
    curFunction_ = fn.func;
    emitLabel(label(fn.func));
    emit("dect", "r15");
    emit("mov", "r11,[r15]");
    emit("dect", "r15");
    emit("mov", "r10,[r15]");
    emit("mov", "r15,r10");
    if (fn.func->frameSize > 0) emit("ai", cat("r15,-", fn.func->frameSize));

    genStmt(*fn.body);

    emit("mov", "r10,r15");
    emit("mov", "[ir15],r10");
    emit("mov", "[ir15],r11");
    emit("b", "[r11]");
    emitRaw("");
}

void TMS9900Codegen::genStmt(const ir::Stmt &s) {
    switch (s.kind) {
        case ir::StmtKind::Block: {
            for (const auto &child : s.body) genStmt(*child);
            return;
        }
        case ir::StmtKind::If: {
            genExpr(*s.cond);
            if (isWide(s.cond->type)) failWide(s.line);
            emit("ci", "r0,0");
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            emit("jeq", s.elseStmt ? Lelse : Ldone);
            genStmt(*s.thenStmt);
            if (s.elseStmt) {
                emit("jmp", Ldone);
                emitLabel(Lelse);
                genStmt(*s.elseStmt);
            }
            emitLabel(Ldone);
            return;
        }
        case ir::StmtKind::While: {
            std::string Lstart = newLabel();
            std::string Lend = newLabel();
            std::string Lcont = s.postStmt ? newLabel() : Lstart;
            emitLabel(Lstart);
            genExpr(*s.cond);
            if (isWide(s.cond->type)) failWide(s.line);
            emit("ci", "r0,0");
            emit("jeq", Lend);
            loopLabels_.push_back({Lcont, Lend});
            genStmt(*s.loopBody);
            loopLabels_.pop_back();
            if (s.postStmt) {
                emitLabel(Lcont);
                genStmt(*s.postStmt);
            }
            emit("jmp", Lstart);
            emitLabel(Lend);
            return;
        }
        case ir::StmtKind::Break: {
            emit("jmp", loopLabels_.back().second);
            return;
        }
        case ir::StmtKind::Continue: {
            emit("jmp", loopLabels_.back().first);
            return;
        }
        case ir::StmtKind::Return: {
            if (s.initOrReturnOrExpr) genExpr(*s.initOrReturnOrExpr);
            emit("mov", "r10,r15");
            emit("mov", "[ir15],r10");
            emit("mov", "[ir15],r11");
            emit("b", "[r11]");
            return;
        }
        case ir::StmtKind::LocalDecl: {
            if (s.declSlot->arraySize > 0) {
                genLocalArrayInit(s.declSlot);
            } else if (s.initOrReturnOrExpr) {
                genExpr(*s.initOrReturnOrExpr);
                storeOperand(addr(s.declSlot), s.declSlot->type);
            }
            return;
        }
        case ir::StmtKind::ExprStmt: {
            genExpr(*s.initOrReturnOrExpr);
            return;
        }
        case ir::StmtKind::Empty:
            return;
        case ir::StmtKind::Asm: {
            emitVerbatim(s.asmText);
            return;
        }
        case ir::StmtKind::Label: {
            emitLabel(irLabel(s.labelId));
            return;
        }
        case ir::StmtKind::Goto: {
            emit("jmp", irLabel(s.labelId));
            return;
        }
    }
}

void TMS9900Codegen::genExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit: {
            if (isWide(e.type)) failWide(e.line);
            emit("li", cat("r0,", static_cast<int16_t>(e.intValue)));
            return;
        }
        case ir::ExprKind::Var: {
            loadOperand(operandOf(e.slot), e.slot->type);
            return;
        }
        case ir::ExprKind::Index: {
            genIndex(e);
            return;
        }
        case ir::ExprKind::Assign: {
            if (e.lhs) {  // slot[idx] = rhs
                genIndexAddr(e.slot, *e.lhs);  // r4 = target address
                pushR4();
                genExpr(*e.rhs);
                popR4();
                storeIndirect(e.slot->type);
                return;
            }
            genExpr(*e.rhs);
            storeOperand(operandOf(e.slot), e.slot->type);
            return;
        }
        case ir::ExprKind::DerefAssign: {
            genExpr(*e.lhs);  // r0 = pointer value
            emit("mov", "r0,r4");
            pushR4();
            genExpr(*e.rhs);
            popR4();
            storeIndirect(e.type);
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            if (e.lhs) {  // slot[idx] op= rhs (including slot[idx]++/--)
                genIndexAddr(e.slot, *e.lhs);
                genCompoundThroughPtr(e);
                return;
            }
            std::string mem = operandOf(e.slot);
            genExpr(*e.rhs);  // r0 = rhs
            emit("mov", "r0,r1");
            loadOperand(mem, e.type);  // r0 = old
            if (e.yieldOld) pushR0();
            genCompoundOp(e.op, e.type);
            narrowR0To(e.type);
            storeOperand(mem, e.type);
            if (e.yieldOld) popR0();
            return;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            genExpr(*e.lhs);  // r0 = pointer value
            emit("mov", "r0,r4");
            genCompoundThroughPtr(e);
            return;
        }
        case ir::ExprKind::Ternary: {
            genExpr(*e.lhs);
            if (isWide(e.lhs->type)) failWide(e.line);
            emit("ci", "r0,0");
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            emit("jeq", Lelse);
            genExpr(*e.rhs);
            emit("jmp", Ldone);
            emitLabel(Lelse);
            genExpr(*e.elseExpr);
            emitLabel(Ldone);
            return;
        }
        case ir::ExprKind::Unary: {
            if (e.op == "-") {
                genExpr(*e.rhs);
                if (isWide(e.rhs->type)) failWide(e.line);
                emit("neg", "r0");
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                genExpr(*e.rhs);  // r0 = pointer value
                emit("mov", "r0,r4");
                loadIndirect(e.type);
            } else if (e.op == "~") {
                genExpr(*e.rhs);
                if (isWide(e.rhs->type)) failWide(e.line);
                emit("inv", "r0");
            } else {  // "&"
                genAddrOf(*e.rhs);
            }
            return;
        }
        case ir::ExprKind::Call: {
            // Arguments are pushed right-to-left so param 0 ends up closest to the saved
            // return address/frame pointer, matching every Param's own ascending
            // frameOffset (see addr()'s own comment); the caller deallocates them afterward.
            // Every argument reserves exactly one native word - there's no wider-than-int
            // argument this backend supports yet (see failWide()).
            int totalBytes = 0;
            for (size_t i = e.args.size(); i-- > 0;) {
                genExpr(*e.args[i]);
                if (isWide(e.args[i]->type)) failWide(e.line);
                pushR0();
                totalBytes += 2;
            }
            emit("bl", cat("[", label(e.func), "]"));
            if (totalBytes > 0) emit("ai", cat("r15,", totalBytes));
            return;
        }
        case ir::ExprKind::CallPtr: {
            genExpr(*e.lhs);  // r0 = the callee's own entry address
            emit("mov", "r0,r4");
            // Saves/restores r11 around the call exactly like a real `bl` would via another
            // register, since the callee's own prologue/epilogue already expects `bl`'s own
            // return-address convention - `bl` itself needs a Symbolic/label operand, so an
            // indirect call instead uses `b` to transfer control and plants the return address
            // (the next instruction after this sequence) into r11 itself first.
            std::string Lret = newLabel();
            emit("li", cat("r11,", Lret));
            emit("b", "[r4]");
            emitLabel(Lret);
            return;
        }
        case ir::ExprKind::Binary: {
            genBinary(e);
            return;
        }
        case ir::ExprKind::Intrinsic: {
            (void)e;  // no vector table set up - "ion"/"iof" are no-ops here, same as other backends
            return;
        }
        case ir::ExprKind::Convert: {
            genConvert(e);
            return;
        }
        case ir::ExprKind::Func:
            err(e.line, "internal: Func reached genExpr - should only ever appear as '&'s operand");
    }
}

// r4 holds the computed address on return. For an array (slot->arraySize > 0), the base is a
// compile-time-known address; otherwise `slot` is a plain pointer variable, so the base comes
// from reading its stored value.
void TMS9900Codegen::genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
    genExpr(idxExpr);  // r0 = index
    int shift = elemShift(slot->type);
    if (shift >= 0) {
        if (shift > 0) emit("sla", cat("r0,", shift));
    } else {
        // A struct element's non-power-of-two width - scale via the real hardware multiply
        // (always exact: both operands fit in 16 bits).
        emit("li", cat("r1,", widthOfType(slot->type)));
        emit("mov", "r0,r2");
        emit("mpy", "r1,r2");
        emit("mov", "r3,r0");
    }
    if (slot->arraySize > 0) {
        if (slot->kind == ir::SlotKind::Global) {
            emit("li", cat("r4,", label(slot)));
        } else {
            int base = slot->kind == ir::SlotKind::Param ? slot->frameOffset
                                                           : -ir::localLowOffset(*slot, typeWidths());
            emit("li", cat("r4,", base));
            emit("a", "r10,r4");
        }
    } else {
        emit("mov", cat(operandOf(slot), ",r4"));
    }
    emit("a", "r0,r4");
}

// r0=old, r1=rhs on entry (CompoundAssign's own contract); leaves the un-narrowed new value in
// r0. `type` is never wide here (IRBuilder's rejectIfWide() guarantees it).
void TMS9900Codegen::genCompoundOp(const std::string &op, Type type) {
    if (op == "+") {
        emit("a", "r1,r0");
        return;
    }
    if (op == "-") {
        emit("s", "r1,r0");
        return;
    }
    if (op == "*") {
        emit("mov", "r1,r2");
        emit("mpy", "r0,r2");
        emit("mov", "r3,r0");
        return;
    }
    if (op == "/" || op == "%") {
        genDivMod(type.isUnsigned, op == "%");
        return;
    }
    // TMS9900 has no plain AND instruction - only SZC ("set zeros corresponding", i.e.
    // dst &= ~src) and SOC ("set ones corresponding", dst |= src). AND is synthesized as
    // "dst &= ~(~src)", i.e. invert a copy of src first, then SZC it in.
    if (op == "&") {
        emit("mov", "r1,r2");
        emit("inv", "r2");
        emit("szc", "r2,r0");
        return;
    }
    if (op == "|") {
        emit("soc", "r1,r0");
        return;
    }
    if (op == "^") {
        emit("xor", "r1,r0");
        return;
    }
    if (op == "<<" || op == ">>") {
        genShiftCore(op == "<<", type.isUnsigned);
        return;
    }
    err(0, "internal: unhandled compound operator '" + op + "'");
}

// r4 must hold the resolved address on entry; stashed across e.rhs's evaluation since that can
// recursively clobber every register.
void TMS9900Codegen::genCompoundThroughPtr(const ir::Expr &e) {
    pushR4();
    genExpr(*e.rhs);       // r0 = rhs
    emit("mov", "r0,r1");  // r1 = rhs, held across the r4 reload below
    popR4();                 // r4 = address
    loadIndirect(e.type);     // r0 = old
    if (e.yieldOld) pushR0();
    genCompoundOp(e.op, e.type);  // r0 = new (raw)
    narrowR0To(e.type);
    if (e.yieldOld) {
        emit("mov", "r0,r2");  // r2 = new
        popR0();                       // r0 = old
        emit("mov", "r0,r3");     // r3 = old, preserved across the store below
        emit("mov", "r2,r0");      // r0 = new (storeIndirect always stores from r0)
        storeIndirect(e.type);               // r4 still addresses the target
        emit("mov", "r3,r0");            // r0 = old (final result)
    } else {
        storeIndirect(e.type);
    }
}

void TMS9900Codegen::genLocalArrayInit(const ir::Slot *slot) {
    int w = widthOfType(slot->type);
    if (w > 2) failWide(0);
    for (size_t i = 0; i < slot->arrayInit.size(); i++) {
        const ir::ArrayInitElem &elem = slot->arrayInit[i];
        int byteOffset = static_cast<int>(i) * w;
        const char *mnemonic = w == 1 ? "movb" : "mov";
        emit("li", cat("r0,", arrayElemText(elem)));
        emit(mnemonic, cat("r0,", addr(slot, byteOffset)));
    }
}

void TMS9900Codegen::genIndex(const ir::Expr &e) {
    genIndexAddr(e.slot, *e.lhs);
    loadIndirect(e.slot->type);
}

void TMS9900Codegen::narrowR0To(Type to) {
    int w = widthOfType(to);
    if (w > 2) failWide(0);
    if (w == 1) {
        // r0's real bits, on entry, sit in its own low byte (every arithmetic op here leaves
        // them there) - unsigned re-truncation is a plain mask; signed re-extension needs the
        // byte moved up into the high-byte position first (SLA 8) so its own bit 7 lands on
        // r0's bit 15, then arithmetic-shifted back down (SRA 8) to sign-extend from there.
        if (to.isUnsigned) {
            emit("andi", "r0,255");
        } else {
            emit("sla", "r0,8");
            emit("sra", "r0,8");
        }
    }
    // 2 bytes: nothing to do (r0 is already a full native word).
}

// `type`'s real bits for a `char` sit in the high byte of the addressed word (MOVB always
// touches a word's own most-significant byte - see TMS9900Assembler.h's own header comment),
// so a byte load/store always goes through r0's high byte, then is moved down into the low-
// byte position (sign/zero-extended per `type`) every other value in this backend keeps.
void TMS9900Codegen::loadOperand(const std::string &mem, Type type) {
    int w = widthOfType(type);
    if (w > 2) failWide(0);
    if (w == 1) {
        emit("movb", cat(mem, ",r0"));
        if (type.isUnsigned) {
            emit("srl", "r0,8");
        } else {
            emit("sra", "r0,8");
        }
    } else {
        emit("mov", cat(mem, ",r0"));
    }
}

void TMS9900Codegen::storeOperand(const std::string &mem, Type type) {
    int w = widthOfType(type);
    if (w > 2) failWide(0);
    if (w == 1) {
        emit("sla", "r0,8");
        emit("movb", cat("r0,", mem));
        emit("sra", "r0,8");
    } else {
        emit("mov", cat("r0,", mem));
    }
}

void TMS9900Codegen::loadIndirect(Type type) {
    int w = widthOfType(type);
    if (w > 2) failWide(0);
    if (w == 1) {
        emit("movb", "[r4],r0");
        emit(type.isUnsigned ? "srl" : "sra", "r0,8");
    } else {
        emit("mov", "[r4],r0");
    }
}

void TMS9900Codegen::storeIndirect(Type type) {
    int w = widthOfType(type);
    if (w > 2) failWide(0);
    if (w == 1) {
        emit("sla", "r0,8");
        emit("movb", "r0,[r4]");
        emit("sra", "r0,8");
    } else {
        emit("mov", "r0,[r4]");
    }
}

void TMS9900Codegen::genConvert(const ir::Expr &e) {
    Type from = e.lhs->type;
    Type to = e.type;
    if (isWide(from) || isWide(to)) failWide(e.line);
    genExpr(*e.lhs);
    narrowR0To(to);
}

void TMS9900Codegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        if (operand.slot->kind == ir::SlotKind::Global) {
            emit("li", cat("r0,", label(operand.slot)));
        } else {
            int off = operand.slot->kind == ir::SlotKind::Param
                          ? operand.slot->frameOffset + paramByteFixup(operand.slot)
                          : -ir::localOffsetDescendingWide(*operand.slot, typeWidths());
            emit("li", cat("r0,", off));
            emit("a", "r10,r0");
        }
        return;
    }
    if (operand.kind == ir::ExprKind::Index) {
        genIndexAddr(operand.slot, *operand.lhs);
        emit("mov", "r4,r0");
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        emit("li", cat("r0,", label(operand.func)));
        return;
    }
    err(operand.line, "internal: unhandled addressable-operand kind");
}

void TMS9900Codegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    Type operandType = e.lhs->type;
    bool isUnsigned = operandType.isUnsigned;
    if (isWide(operandType) && op != "&&" && op != "||") failWide(e.line);
    if (op == "+" || op == "-") {
        genExpr(*e.lhs);
        pushR0();
        genExpr(*e.rhs);
        popR1();  // r1 = lhs, r0 = rhs
        if (op == "+") {
            emit("a", "r1,r0");
        } else {
            emit("s", "r0,r1");  // r1 = r1 - r0 = lhs - rhs
            emit("mov", "r1,r0");
        }
        return;
    }
    if (op == "*") {
        genMul(*e.lhs, *e.rhs);
        return;
    }
    if (op == "/" || op == "%") {
        genExpr(*e.lhs);
        pushR0();
        genExpr(*e.rhs);
        emit("mov", "r0,r1");
        popR0();  // r0 = lhs (dividend), r1 = rhs (divisor)
        genDivMod(isUnsigned, op == "%");
        return;
    }
    if (op == "&" || op == "|" || op == "^") {
        genExpr(*e.lhs);
        pushR0();
        genExpr(*e.rhs);
        popR1();  // r1 = lhs, r0 = rhs
        if (op == "&") {
            emit("mov", "r0,r2");
            emit("inv", "r2");
            emit("szc", "r2,r1");
            emit("mov", "r1,r0");
        } else if (op == "|") {
            emit("soc", "r0,r1");
            emit("mov", "r1,r0");
        } else {
            emit("xor", "r0,r1");
            emit("mov", "r1,r0");
        }
        return;
    }
    if (op == "<<" || op == ">>") {
        genExpr(*e.lhs);
        pushR0();
        genExpr(*e.rhs);  // r0 = count
        emit("mov", "r0,r1");
        popR0();  // r0 = value, r1 = count
        genShiftCore(op == "<<", isUnsigned);
        return;
    }
    if (op == "&&") {
        genLogicalAnd(*e.lhs, *e.rhs);
        return;
    }
    if (op == "||") {
        genLogicalOr(*e.lhs, *e.rhs);
        return;
    }
    genCompare(op, *e.lhs, *e.rhs);
}

// r0=value, r1=count on entry (either real order genBinary/genCompoundOp naturally produces);
// the hardware shift instructions can only take their count from an immediate 1-15 or, when
// the instruction's own count field is 0, from r0's own low nibble - so the value is moved out
// of r0's way into r2 first, then the count into r0, before shifting.
void TMS9900Codegen::genShiftCore(bool isLeft, bool isUnsigned) {
    emit("mov", "r0,r2");
    emit("mov", "r1,r0");
    const char *insn = isLeft ? "sla" : (isUnsigned ? "srl" : "sra");
    emit(insn, "r2,0");
    emit("mov", "r2,r0");
}

// r0=rhs, r1=lhs preset (genBinary's own arrangement before calling this) - multiplication
// commutes, so which operand ends up as MPY's "source" vs "destination" register doesn't
// matter. The real hardware MPY always produces a correct 32-bit product; only the low word
// (the truncated 16x16->16 result C's `int*int` actually wants) is kept - truncated
// multiplication is identical whether the inputs are signed or unsigned (modular arithmetic),
// so no separate signed path is needed.
void TMS9900Codegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    pushR0();
    genExpr(rhs);
    popR1();
    emit("mov", "r1,r2");
    emit("mpy", "r0,r2");
    emit("mov", "r3,r0");
}

// r0=dividend, r1=divisor preset. The real hardware DIV is unsigned-only (a 32-bit dividend in
// a register pair divided by a 16-bit divisor, giving a 16-bit quotient/remainder - the
// dividend's own high word must be strictly less than the divisor, trivially true here since
// it's always seeded to 0). Signed division negates both operands to magnitudes first, divides
// unsigned, then restores the quotient's sign (negative iff the original signs differed) and
// the remainder's sign (always the dividend's own, matching C's truncating-division
// convention) - the TMS9900 has no signed divide instruction at all.
void TMS9900Codegen::genDivMod(bool isUnsigned, bool wantRemainder) {
    if (isUnsigned) {
        emit("li", "r2,0");
        emit("mov", "r0,r3");
        emit("div", "r1,r2");
        emit("mov", cat(wantRemainder ? "r3" : "r2", ",r0"));
        return;
    }
    std::string Lskip1 = newLabel(), Lskip2 = newLabel();
    emit("mov", "r0,r4");  // r4 = original dividend (sign + remainder-sign source)
    emit("mov", "r1,r5");  // r5 = original divisor (sign source)
    emit("ci", "r0,0");
    emit("jhe", Lskip1);
    emit("neg", "r0");
    emitLabel(Lskip1);
    emit("ci", "r1,0");
    emit("jhe", Lskip2);
    emit("neg", "r1");
    emitLabel(Lskip2);
    emit("li", "r2,0");
    emit("mov", "r0,r3");
    emit("div", "r1,r2");  // r2 = |quotient|, r3 = |remainder|
    emit("mov", "r4,r6");
    emit("xor", "r5,r6");  // r6's sign bit: original signs differed?
    std::string LnoQNeg = newLabel();
    emit("ci", "r6,0");
    emit("jhe", LnoQNeg);
    emit("neg", "r2");
    emitLabel(LnoQNeg);
    std::string LnoRNeg = newLabel();
    emit("ci", "r4,0");
    emit("jhe", LnoRNeg);
    emit("neg", "r3");
    emitLabel(LnoRNeg);
    emit("mov", cat(wantRemainder ? "r3" : "r2", ",r0"));
}

void TMS9900Codegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs) {
    if (isWide(lhs.type)) failWide(lhs.line);
    bool isUnsigned = lhs.type.isUnsigned;
    if (op == "==") return emitCompare(lhs, rhs, "jeq");
    if (op == "!=") return emitCompare(lhs, rhs, "jne");
    if (op == "<") return emitCompare(lhs, rhs, isUnsigned ? "jl" : "jlt");
    if (op == ">") return emitCompare(lhs, rhs, isUnsigned ? "jh" : "jgt");
    // The TMS9900 has no direct signed ">=" or unsigned "<=" jump - computed instead by
    // swapping which operand is the destination of `c` and testing the opposite-direction
    // condition that *is* available (see emitCompare()'s own header comment): "lhs<=rhs"
    // unsigned has no direct jump, but "rhs>=lhs" (jhe) does, and they're equivalent; signed
    // "<=" already has a direct jle, so only the unsigned case needs the swap, and
    // symmetrically only the signed case needs it for ">=".
    if (op == "<=") return emitCompare(lhs, rhs, isUnsigned ? "jhe" : "jle", /*swapOperands=*/isUnsigned);
    if (op == ">=") return emitCompare(lhs, rhs, isUnsigned ? "jhe" : "jle", /*swapOperands=*/!isUnsigned);
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

void TMS9900Codegen::emitCompare(const ir::Expr &lhs, const ir::Expr &rhs, const std::string &branchIfTrue,
                                  bool swapOperands) {
    genExpr(lhs);
    pushR0();
    genExpr(rhs);
    popR1();  // r1 = lhs, r0 = rhs
    // `c src,dst` sets condition codes for "dst compared to src". Ordinarily src=rhs(r0),
    // dst=lhs(r1), so the codes directly read as "lhs <cond> rhs"; `swapOperands` computes
    // "rhs <cond> lhs" instead (used only for the two conditions with no direct jump - see
    // genCompare()'s own comment for why that's exactly equivalent).
    if (swapOperands) {
        emit("c", "r1,r0");
    } else {
        emit("c", "r0,r1");
    }
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit(branchIfTrue, Ltrue);
    emit("li", "r0,0");
    emit("jmp", Ldone);
    emitLabel(Ltrue);
    emit("li", "r0,1");
    emitLabel(Ldone);
}

void TMS9900Codegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    if (isWide(lhs.type) || isWide(rhs.type)) failWide(lhs.line);
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    genExpr(lhs);
    emit("ci", "r0,0");
    emit("jeq", Lfalse);
    genExpr(rhs);
    emit("ci", "r0,0");
    emit("jeq", Lfalse);
    emit("jmp", Ltrue);
    emitLabel(Lfalse);
    emit("li", "r0,0");
    emit("jmp", Ldone);
    emitLabel(Ltrue);
    emit("li", "r0,1");
    emitLabel(Ldone);
}

void TMS9900Codegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    if (isWide(lhs.type) || isWide(rhs.type)) failWide(lhs.line);
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    genExpr(lhs);
    emit("ci", "r0,0");
    emit("jne", Ltrue);
    genExpr(rhs);
    emit("ci", "r0,0");
    emit("jne", Ltrue);
    emit("jmp", Lfalse);
    emitLabel(Ltrue);
    emit("li", "r0,1");
    emit("jmp", Ldone);
    emitLabel(Lfalse);
    emit("li", "r0,0");
    emitLabel(Ldone);
}

void TMS9900Codegen::genLogicalNot(const ir::Expr &rhs) {
    if (isWide(rhs.type)) failWide(rhs.line);
    genExpr(rhs);
    emit("ci", "r0,0");
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit("jeq", Ltrue);
    emit("li", "r0,0");
    emit("jmp", Ldone);
    emitLabel(Ltrue);
    emit("li", "r0,1");
    emitLabel(Ldone);
}

std::optional<std::string> TMS9900Codegen::assembleInProcess(const std::string &asmText,
                                                               const std::string &format) const {
    if (format == "raw") return assembleTMS9900(asmText);
    if (auto kind = fileFormatKindFromName(format)) {
        ObjectFile obj = assembleTMS9900Object(asmText);
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

}  // namespace rcc
