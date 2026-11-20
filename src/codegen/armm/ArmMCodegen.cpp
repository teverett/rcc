#include "ArmMCodegen.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

#include "ArmMAssembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

// Where the whole program is loaded; the simulator sets the PC here directly.
constexpr uint32_t kBaseAddress = 0x1000u;

// Initial sp, set before calling main(); comfortably above the loaded program.
constexpr uint32_t kInitialSP = 0x100000u;
}  // namespace

void ArmMCodegen::declareTemp(const std::string &lbl) { trailer_.push_back(lbl + ": dw 0"); }

// A literal element is its decimal text; a symbol element is the referenced
// Slot's/Function's label, resolved later by the dw expression evaluator.
std::string ArmMCodegen::arrayElemText(const ir::ArrayInitElem &e, Type elemType) {
    if (!e.isSymbol) {
        // A long long literal needs two dw's (low/high word); handled by the caller.
        (void)elemType;
        return std::to_string(static_cast<int32_t>(static_cast<uint32_t>(e.value)));
    }
    // A function's address gets its low bit set (+1) for Thumb interworking:
    // BLX (register) raises a UsageFault if bit 0 isn't 1, since M-profile has
    // no ARM state. A plain data Slot is never called through, so it needs no bit.
    if (e.slot) return label(e.slot);
    return label(e.func) + "+1";
}

void ArmMCodegen::declareArray(const std::string &lbl, const std::vector<ir::ArrayInitElem> &values, Type elemType) {
    if (values.empty()) return;
    int w = widthOfType(elemType);
    const char *directive = w == 1 ? "db" : w == 2 ? "dh" : "dw";
    std::ostringstream line;
    line << lbl << ": " << directive << " ";
    bool first = true;
    for (const auto &e : values) {
        if (w == 8 && !e.isSymbol) {
            auto v = static_cast<uint64_t>(e.value);
            if (!first) line << ", ";
            line << static_cast<uint32_t>(v) << ", " << static_cast<uint32_t>(v >> 32);
            first = false;
            continue;
        }
        if (!first) line << ", ";
        line << arrayElemText(e, elemType);
        first = false;
    }
    trailer_.push_back(line.str());
}

void ArmMCodegen::loadAddress(const std::string &reg, const std::string &lbl) {
    emit("movw", cat(reg, ",", lbl));
    emit("movt", cat(reg, ",", lbl));
}

void ArmMCodegen::loadImm32(const std::string &reg, uint32_t value) {
    if (value <= 0xFF) {
        emit("movs", cat(reg, ",", static_cast<int32_t>(value)));
        return;
    }
    emit("movw", cat(reg, ",", static_cast<int32_t>(value & 0xFFFFu)));
    emit("movt", cat(reg, ",", static_cast<int32_t>((value >> 16) & 0xFFFFu)));
}

void ArmMCodegen::loadFrameByteAddr(const std::string &reg, int bytesFromFp) {
    emit("mov", cat(reg, ",r7"));
    if (bytesFromFp == 0) return;
    loadImm32("r3", static_cast<uint32_t>(bytesFromFp > 0 ? bytesFromFp : -bytesFromFp));
    emit((bytesFromFp > 0 ? "adds" : "subs"), cat(reg, ",", reg, ",r3"));
}

void ArmMCodegen::loadFrameAddr(const std::string &reg, const ir::Slot *slot) {
    int bytesFromFp =
        slot->kind == ir::SlotKind::Param ? slot->frameOffset : -ir::localLowOffset(*slot, typeWidths());
    loadFrameByteAddr(reg, bytesFromFp);
}

void ArmMCodegen::loadSlotAddr(const std::string &reg, const ir::Slot *slot) {
    if (slot->kind == ir::SlotKind::Global) {
        loadAddress(reg, label(slot));
    } else {
        loadFrameAddr(reg, slot);
    }
}

void ArmMCodegen::loadArrayInitElem(const std::string &reg, const ir::ArrayInitElem &e) {
    if (!e.isSymbol) {
        loadImm32(reg, static_cast<uint32_t>(e.value));
        return;
    }
    if (e.slot) {
        loadAddress(reg, label(e.slot));
        return;
    }
    // Same Thumb-interworking bit requirement as arrayElemText().
    loadAddress(reg, label(e.func));
    emit("adds", cat(reg, ",", reg, ",1"));
}

std::string ArmMCodegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    // In object mode, prog may not define main at all; ArmMLinkerFactory.cpp
    // synthesizes this entry sequence once instead, after linking.
    emitRaw(cat("org ", (objectMode ? 0 : kBaseAddress)));
    emitRaw("");
    if (!objectMode) {
        emitLabel("_start");
        // movw/movt can't target sp directly (Rd==13 is UNPREDICTABLE), so the
        // initial sp is built in a scratch register and moved into sp instead.
        loadImm32("r0", kInitialSP);
        emit("mov", "sp,r0");
        emit("bl", label(mainFn));
        emitLabel("halt");
        emit("b", "halt");
        emitRaw("");
    }

    for (const auto &fb : prog.functionBodies) genFunction(fb);

    // Pads ahead of a global wider than 1 byte, so an unaligned ldind/stind
    // doesn't round down and clobber a neighboring global.
    size_t dataOffset = 0;
    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        // An external global is never defined here; only referenced by name,
        // letting the assembler defer it to a relocation.
        if (slot->isExternal) continue;
        int align = std::min(widthOfType(slot->type), 4);
        size_t padding = (align - (dataOffset % align)) % align;
        if (padding > 0) {
            std::string fill = "0";
            for (size_t i = 1; i < padding; i++) fill += ", 0";
            trailer_.push_back("db " + fill);
            dataOffset += padding;
        }
        const std::string lbl = label(slot.get());
        if (slot->arraySize > 0) {
            declareArray(lbl, slot->arrayInit, slot->type);
            dataOffset += static_cast<size_t>(widthOfType(slot->type)) * static_cast<size_t>(slot->arraySize);
        } else if (slot->type.base == BaseType::Struct) {
            // No struct initializer exists in this subset, so this is
            // always zero-filled; reuses declareArray's own element-list
            // emission (one zero "db" per byte) rather than a new
            // directive, since this assembler dialect has no "reserve N
            // zero bytes" form of its own.
            declareArray(lbl, std::vector<ir::ArrayInitElem>(slot->type.structSize), Type{BaseType::Char, false});
            dataOffset += static_cast<size_t>(slot->type.structSize);
        } else {
            long v = slot->hasInit ? slot->initValue : 0;
            int w = widthOfType(slot->type);
            if (w == 8) {
                auto uv = static_cast<uint64_t>(v);
                trailer_.push_back(lbl + ": dw " + std::to_string(static_cast<uint32_t>(uv)) + ", " +
                                    std::to_string(static_cast<uint32_t>(uv >> 32)) + "  ; " + slot->sourceName);
            } else {
                const char *directive = w == 1 ? "db" : w == 2 ? "dh" : "dw";
                trailer_.push_back(lbl + ": " + std::string(directive) + " " +
                                    std::to_string(static_cast<int32_t>(static_cast<uint32_t>(v))) + "  ; " +
                                    slot->sourceName);
            }
            dataOffset += static_cast<size_t>(w);
        }
    }

    std::ostringstream out;
    out << code_.str();
    for (const auto &line : trailer_) out << line << "\n";
    return out.str();
}

std::map<std::string, std::string> ArmMCodegen::perFileAsm() const { return perFileAsm_; }

void ArmMCodegen::genFunction(const ir::FunctionBody &fn) {
    emitLabel(label(fn.func));
    emit("pushlr");
    emit("push", "r7");
    emit("mov", "r7,sp");
    // frameSize is a byte count; round up to whole words since each push below
    // reserves one word, with no sub sp,#imm available to reserve exactly.
    frameSize_ = (fn.func->frameSize + 3) / 4;
    for (int i = 0; i < frameSize_; i++) emit("push", "r0");
    genStmt(*fn.body);
    emitEpilogue();
    emitRaw("");
}

void ArmMCodegen::emitEpilogue() {
    for (int i = 0; i < frameSize_; i++) emit("pop", "r1");
    emit("pop", "r7");
    emit("poppc");
}

void ArmMCodegen::genStmt(const ir::Stmt &s) {
    switch (s.kind) {
        case ir::StmtKind::Block: {
            for (const auto &child : s.body) genStmt(*child);
            return;
        }
        case ir::StmtKind::If: {
            genExpr(*s.cond);
            if (isWide(s.cond->type)) emit("orr", "r0,r1");
            emit("cmp", "r0,0");
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            emit("beq", (s.elseStmt ? Lelse : Ldone));
            genStmt(*s.thenStmt);
            if (s.elseStmt) {
                emit("b", Ldone);
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
            if (isWide(s.cond->type)) emit("orr", "r0,r1");
            emit("cmp", "r0,0");
            emit("beq", Lend);
            loopLabels_.push_back({Lcont, Lend});
            genStmt(*s.loopBody);
            loopLabels_.pop_back();
            if (s.postStmt) {
                emitLabel(Lcont);
                genStmt(*s.postStmt);
            }
            emit("b", Lstart);
            emitLabel(Lend);
            return;
        }
        case ir::StmtKind::Break: {
            emit("b", loopLabels_.back().second);
            return;
        }
        case ir::StmtKind::Continue: {
            emit("b", loopLabels_.back().first);
            return;
        }
        case ir::StmtKind::Return: {
            if (s.initOrReturnOrExpr) genExpr(*s.initOrReturnOrExpr);
            // Early exit: the fall-off-the-end epilogue never runs for this path.
            emitEpilogue();
            return;
        }
        case ir::StmtKind::LocalDecl: {
            if (s.declSlot->arraySize > 0) {
                genLocalArrayInit(s.declSlot);
            } else if (s.initOrReturnOrExpr) {
                genExpr(*s.initOrReturnOrExpr);
                loadFrameAddr("r2", s.declSlot);
                storeOperand("r2", s.declSlot->type);
            }
            // An uninitialized scalar's frame word already exists (reserved
            // by genFunction()); nothing to emit.
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
            if (!s.asmText.empty() && s.asmText.back() != '\n') emitRaw("");
            return;
        }
        case ir::StmtKind::Label: {
            emitLabel(irLabel(s.labelId));
            return;
        }
        case ir::StmtKind::Goto: {
            emit("b", irLabel(s.labelId));
            return;
        }
    }
}

void ArmMCodegen::genExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit: {
            if (isWide(e.type)) {
                auto v = static_cast<uint64_t>(e.intValue);
                loadImm32("r0", static_cast<uint32_t>(v));
                loadImm32("r1", static_cast<uint32_t>(v >> 32));
                return;
            }
            loadImm32("r0", static_cast<uint32_t>(e.intValue));
            return;
        }
        case ir::ExprKind::Var: {
            loadSlotAddr("r2", e.slot);
            loadOperand("r2", e.slot->type);
            return;
        }
        case ir::ExprKind::Index: {
            genIndex(e);
            return;
        }
        case ir::ExprKind::Assign: {
            if (e.lhs) {  // slot[idx] = rhs
                genIndexAddr(e.slot, *e.lhs);  // r2 = target address
                emit("push", "r2");
                genExpr(*e.rhs);
                emit("pop", "r2");
                storeOperand("r2", e.slot->type);
                return;
            }
            genExpr(*e.rhs);
            loadSlotAddr("r2", e.slot);
            storeOperand("r2", e.slot->type);
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            if (e.lhs) {  // slot[idx] op= rhs (including slot[idx]++/--)
                genIndexAddr(e.slot, *e.lhs);  // r2 = target address
                genCompoundThroughR2(e);
                return;
            }
            // A plain scalar target still needs its address computed (ARM has
            // no absolute-addressing operand), so it shares the indexed tail.
            loadSlotAddr("r2", e.slot);
            genCompoundThroughR2(e);
            return;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            genExpr(*e.lhs);  // r0 = pointer value
            emit("mov", "r2,r0");  // r2 = address
            genCompoundThroughR2(e);
            return;
        }
        case ir::ExprKind::Ternary: {
            genExpr(*e.lhs);
            // The condition's type can still be wide even though the result
            // type is capped to native width, so this reuses the If/While
            // wide-OR-then-compare idiom.
            if (isWide(e.lhs->type)) emit("orr", "r0,r1");
            emit("cmp", "r0,0");
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            emit("beq", Lelse);
            genExpr(*e.rhs);
            emit("b", Ldone);
            emitLabel(Lelse);
            genExpr(*e.elseExpr);
            emitLabel(Ldone);
            return;
        }
        case ir::ExprKind::DerefAssign: {
            // Stores at e.type's width/signedness: the pointee type, or plain
            // native-int-width for the untyped-int-as-address style.
            genExpr(*e.lhs);  // r0 = pointer value
            emit("push", "r0");
            genExpr(*e.rhs);  // r0, or r0:r1 if wide
            emit("pop", "r2");
            storeOperand("r2", e.type);
            return;
        }
        case ir::ExprKind::Unary: {
            if (e.op == "-") {
                genExpr(*e.rhs);
                if (isWide(e.rhs->type)) {
                    genNegate64();
                } else {
                    emit("neg", "r0,r0");
                }
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                // Loads at e.type's width/signedness, as DerefAssign above.
                genExpr(*e.rhs);  // r0 = pointer value
                emit("mov", "r2,r0");
                loadOperand("r2", e.type);
            } else if (e.op == "~") {
                genExpr(*e.rhs);
                emit("mvn", "r0,r0");
                if (isWide(e.rhs->type)) emit("mvn", "r1,r1");
            } else {  // "&"
                genAddrOf(*e.rhs);
            }
            return;
        }
        case ir::ExprKind::Call: {
            // Arguments push last-declared first, so the first argument ends
            // up closest to the return address (matching paramIndex 0 = r7+8).
            // The caller cleans up afterward with one dummy pop per pushed
            // word (no add sp,#imm here). A wide argument pushes its high word
            // first so its low word ends up closer to r7.
            int pushedWords = 0;
            for (size_t i = e.args.size(); i-- > 0;) {
                genExpr(*e.args[i]);
                if (isWide(e.args[i]->type)) {
                    emit("push", "r1");
                    emit("push", "r0");
                    pushedWords += 2;
                } else {
                    emit("push", "r0");
                    pushedWords += 1;
                }
            }
            emit("bl", label(e.func));
            for (int i = 0; i < pushedWords; i++) emit("pop", "r1");
            return;
        }
        case ir::ExprKind::CallPtr: {
            // e.args is always empty: the callee is unknown at compile time, so
            // there's no p_<fn>_<i> cell to write arguments into.
            // blx r2 calls through whatever address r2 holds; r2's bit 0 must
            // already be 1 (ARMv7-M interworking), which genAddrOf()'s Func
            // case guarantees since nothing else here is callable.
            genExpr(*e.lhs);  // r0 = the callee's own entry address
            emit("mov", "r2,r0");
            emit("blx", "r2");
            return;
        }
        case ir::ExprKind::Binary: {
            genBinary(e);
            return;
        }
        case ir::ExprKind::Intrinsic: {
            // No NVIC/exception setup here, so interrupt enable/disable is a no-op.
            (void)e;
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

// arraySize > 0 means slot is the array itself (base = its first word);
// otherwise slot is a pointer variable, so the base is read from its stored
// value. Either array's element addresses increase with the index.
void ArmMCodegen::genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
    genExpr(idxExpr);  // r0 = index
    int shift = elemShift(slot->type);
    if (shift > 0) {
        emit("lsli", cat("r0,r0,", shift));  // r0 *= element width
    } else if (shift < 0) {
        // Non-power-of-two element width (a struct's): `muls` is two-operand (r0 *= r1), so
        // the width has to go through a register first.
        emit("movs", cat("r1,", widthOfType(slot->type)));
        emit("muls", "r0,r1");
    }
    if (slot->arraySize > 0) {
        if (slot->kind == ir::SlotKind::Global) {
            loadAddress("r2", label(slot));
            emit("adds", "r2,r2,r0");
        } else {
            loadFrameAddr("r2", slot);
            emit("adds", "r2,r2,r0");
        }
        return;
    }
    loadSlotAddr("r1", slot);
    emit("ldind", "r1,r1");  // r1 = the pointer variable's stored value
    emit("adds", "r2,r1,r0");
}

void ArmMCodegen::genIndex(const ir::Expr &e) {
    genIndexAddr(e.slot, *e.lhs);
    loadOperand("r2", e.slot->type);
}

// Unlike a Global's static trailer, a Local's frame storage is fresh per
// activation, so arrayInit must be re-stored on every call. Element i's
// address is i*widthOfType(slot->type) bytes above element 0's.
void ArmMCodegen::genLocalArrayInit(const ir::Slot *slot) {
    int w = widthOfType(slot->type);
    for (size_t i = 0; i < slot->arrayInit.size(); i++) {
        const ir::ArrayInitElem &elem = slot->arrayInit[i];
        loadArrayInitElem("r0", elem);
        if (w == 8 && !elem.isSymbol) {
            auto v = static_cast<uint64_t>(elem.value);
            loadImm32("r1", static_cast<uint32_t>(v >> 32));
        }
        loadFrameByteAddr("r2", -ir::localLowOffset(*slot, typeWidths()) + w * static_cast<int>(i));
        storeOperand("r2", slot->type);
    }
}

void ArmMCodegen::narrowR0To(Type to) {
    int w = widthOfType(to);
    // Shifts the unwanted high bits out and back in: lsr (zero-fill) for
    // unsigned, asr (sign-fill) for signed - no dedicated extend instruction here.
    if (w == 1) {
        emit("lsli", "r0,r0,24");
        emit("movs", "r1,24");
        emit((to.isUnsigned ? "lsr" : "asr"), "r0,r1");
    } else if (w == 2) {
        emit("lsli", "r0,r0,16");
        emit("movs", "r1,16");
        emit((to.isUnsigned ? "lsr" : "asr"), "r0,r1");
    }
}

// ldindb/ldindh always zero-extend; a signed source needs re-sign-extending
// via narrowR0To() afterward.
void ArmMCodegen::loadOperand(const std::string &addrReg, Type type) {
    int w = widthOfType(type);
    if (w == 1) {
        emit("ldindb", cat(addrReg, ",r0"));
        if (!type.isUnsigned) narrowR0To(type);
    } else if (w == 2) {
        emit("ldindh", cat(addrReg, ",r0"));
        if (!type.isUnsigned) narrowR0To(type);
    } else if (w == 4) {
        emit("ldind", cat(addrReg, ",r0"));
    } else {
        // subs has no immediate form here, so the high word's address is
        // computed into scratch r6 instead of mutating addrReg itself.
        emit("ldind", cat(addrReg, ",r0"));
        emit("adds", cat("r6,", addrReg, ",4"));
        emit("ldind", "r6,r1");
    }
}

void ArmMCodegen::storeOperand(const std::string &addrReg, Type type) {
    int w = widthOfType(type);
    if (w == 1) {
        emit("stindb", cat(addrReg, ",r0"));
    } else if (w == 2) {
        emit("stindh", cat(addrReg, ",r0"));
    } else if (w == 4) {
        emit("stind", cat(addrReg, ",r0"));
    } else {
        emit("stind", cat(addrReg, ",r0"));
        emit("adds", cat("r6,", addrReg, ",4"));
        emit("stind", "r6,r1");
    }
}

void ArmMCodegen::genConvert(const ir::Expr &e) {
    Type from = e.lhs->type;
    Type to = e.type;
    bool fromWide = isWide(from);
    bool toWide = isWide(to);
    genExpr(*e.lhs);
    if (fromWide && !toWide) {
        narrowR0To(to);  // r1 is simply discarded
    } else if (!fromWide && toWide) {
        if (from.isUnsigned) {
            emit("movs", "r1,0");
        } else {
            emit("mov", "r1,r0");
            emit("movs", "r4,31");
            emit("asr", "r1,r4");
        }
    } else if (!fromWide && !toWide) {
        narrowR0To(to);
    }
    // fromWide && toWide: the bit pattern is already correct, nothing
    // to do.
}

// Two's complement negate of r0:r1 in place.
void ArmMCodegen::genNegate64() {
    emit("mvn", "r0,r0");
    emit("mvn", "r1,r1");
    emit("movs", "r4,1");
    emit("adds", "r0,r0,r4");
    emit("movs", "r4,0");
    emit("adc", "r1,r4");
}

void ArmMCodegen::genWideAdd(const ir::Expr &lhs, const ir::Expr &rhs, bool isAdd) {
    genExpr(lhs);
    emit("push", "r1");
    emit("push", "r0");  // stack (top->bottom): lhs_lo, lhs_hi
    genExpr(rhs);
    emit("mov", "r4,r0");
    emit("mov", "r5,r1");  // r4=rhs_lo, r5=rhs_hi
    emit("pop", "r0");
    emit("pop", "r1");  // r0:r1 = lhs restored
    if (isAdd) {
        emit("adds", "r0,r0,r4");
        emit("adc", "r1,r5");
    } else {
        emit("subs", "r0,r0,r4");
        emit("sbc", "r1,r5");
    }
}

// 32x32->64 unsigned widening multiply: {r1:r0} = r6(a) * r4(b). No real UMULL
// here, so this is bit-serial shift-and-add (32 iterations): a (r6) is
// consumed one bit at a time via lsr, testing the carry bit it leaves behind
// (bhs/blo) directly; b's doubling value (r4:r5, grown via the lsl+adc
// carry-chain trick) is added into the accumulator (r0:r1) whenever that bit
// is 1. Clobbers r0-r6.
void ArmMCodegen::genMul64() {
    emit("movs", "r5,0");  // r5 = b's high word
    emit("movs", "r0,0");
    emit("movs", "r1,0");
    emit("movs", "r2,0");  // r2 = iteration count-up (0..32)
    emit("movs", "r3,1");
    std::string Lloop = newLabel(), LdoAdd = newLabel(), LskipAdd = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("cmp", "r2,32");
    emit("beq", Ldone);
    emit("adds", "r2,r2,r3");
    emit("lsr", "r6,r3");
    emit("bhs", LdoAdd);
    emit("b", LskipAdd);
    emitLabel(LdoAdd);
    emit("adds", "r0,r0,r4");
    emit("adc", "r1,r5");
    emitLabel(LskipAdd);
    emit("adds", "r4,r4,r4");
    emit("adc", "r5,r5");
    emit("b", Lloop);
    emitLabel(Ldone);
}

// Compares high words first (sign lives there); the low word's comparison,
// reached only when the high words are equal, is always unsigned.
void ArmMCodegen::genWideCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned) {
    genExpr(lhs);
    emit("push", "r1");
    emit("push", "r0");
    genExpr(rhs);
    emit("mov", "r4,r0");
    emit("mov", "r5,r1");  // r4:r5 = rhs
    emit("pop", "r0");
    emit("pop", "r1");        // r0:r1 = lhs
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    if (op == "==" || op == "!=") {
        const char *onEqual = op == "==" ? "1" : "0";
        const char *onDiffer = op == "==" ? "0" : "1";
        emit("cmp", "r0,r4");
        emit("bne", Lfalse);
        emit("cmp", "r1,r5");
        emit("bne", Lfalse);
        emit("movs", cat("r0,", onEqual));
        emit("b", Ldone);
        emitLabel(Lfalse);
        emit("movs", cat("r0,", onDiffer));
        emitLabel(Ldone);
        return;
    }
    std::string signedCC = op == "<" ? "blt" : op == "<=" ? "ble" : op == ">" ? "bgt" : "bge";
    std::string unsignedCC = op == "<" ? "blo" : op == "<=" ? "bls" : op == ">" ? "bhi" : "bhs";
    std::string hiCC = isUnsigned ? unsignedCC : signedCC;
    std::string LhiDiff = newLabel();
    emit("cmp", "r1,r5");
    emit("bne", LhiDiff);
    emit("cmp", "r0,r4");
    emit(unsignedCC, Ltrue);
    emit("b", Lfalse);
    emitLabel(LhiDiff);
    emit(hiCC, Ltrue);
    emit("b", Lfalse);
    emitLabel(Lfalse);
    emit("movs", "r0,0");
    emit("b", Ldone);
    emitLabel(Ltrue);
    emit("movs", "r0,1");
    emitLabel(Ldone);
}

// result_lo:result_hi = lo(lhs)*lo(rhs) (full 64-bit, via genMul64()) plus the
// two cross terms lhs_hi*rhs_lo/lhs_lo*rhs_hi folded into the high word only
// (truncating muls is correct there since anything past bit 64 is discarded).
void ArmMCodegen::genWideMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    emit("push", "r1");
    emit("push", "r0");  // stash lhs: top->bottom = lhs_lo, lhs_hi
    genExpr(rhs);
    emit("mov", "r6,r0");
    emit("mov", "r5,r1");  // r6=rhs_lo, r5=rhs_hi
    emit("pop", "r4");
    emit("pop", "r2");        // r4=lhs_lo, r2=lhs_hi
    // Cross terms computed before genMul64() clobbers r4/r6.
    emit("mov", "r3,r2");
    emit("muls", "r3,r6");  // r3 = lhs_hi*rhs_lo
    emit("mov", "r2,r5");
    emit("muls", "r2,r4");  // r2 = rhs_hi*lhs_lo
    emit("adds", "r2,r2,r3");
    emit("push", "r2");  // stash the cross-term sum across genMul64()'s clobber
    genMul64();                 // {r1:r0} = r6(rhs_lo) * r4(lhs_lo)
    emit("pop", "r2");
    emit("adds", "r1,r1,r2");
}

// No instruction shifts more than 32 bits, so this is a counted loop of 1-bit
// shifts (r6 = count). << doubles r0:r1 via self-add carry-chaining; >> has no
// such chain for the downward direction, so the bit crossing the r1/r0
// boundary is captured by hand and OR'd into r0's top bit via a 0x80000000
// mask. isUnsigned picks asr (sign-preserving) or lsr (zero-filling) for r1.
void ArmMCodegen::genWideShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned) {
    genExpr(lhs);
    emit("push", "r1");
    emit("push", "r0");
    genExpr(rhs);  // r0 = shift count
    emit("mov", "r6,r0");
    emit("pop", "r0");
    emit("pop", "r1");  // r0:r1 restored = the value being shifted
    emit("movs", "r5,1");
    std::string Lloop = newLabel(), Ldone = newLabel();
    if (!isLeft) {
        emit("movs", "r3,1");
        emit("lsli", "r3,r3,31");  // r3 = 0x80000000, for the >> path
    }
    emitLabel(Lloop);
    emit("cmp", "r6,0");
    emit("beq", Ldone);
    emit("subs", "r6,r6,r5");
    if (isLeft) {
        emit("adds", "r0,r0,r0");
        emit("adc", "r1,r1");
    } else {
        std::string LskipOr = newLabel();
        emit("mov", "r4,r1");
        emit("and", "r4,r5");  // r4 = r1's old bit 0, crossing into r0
        emit((isUnsigned ? "lsr" : "asr"), "r1,r5");
        emit("lsr", "r0,r5");
        emit("cmp", "r4,0");
        emit("beq", LskipOr);
        emit("orr", "r0,r3");
        emitLabel(LskipOr);
    }
    emit("b", Lloop);
    emitLabel(Ldone);
}

// Bit-serial restoring long division: r0:r1 start as the dividend and finish
// as the quotient (built into the bits vacated as it shifts left); r4:r5 is
// the remainder (starts 0); r2:r3 is the divisor. Signed division works on
// magnitudes, with each operand's original sign stashed on the stack and
// reapplied to the result afterward per C's truncating-division rule.
void ArmMCodegen::genWideDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    emit("push", "r1");
    emit("push", "r0");
    genExpr(rhs);
    emit("mov", "r2,r0");
    emit("mov", "r3,r1");  // r2:r3 = divisor
    emit("pop", "r0");
    emit("pop", "r1");        // r0:r1 = dividend

    if (!isUnsigned) {
        emit("movs", "r4,31");
        emit("mov", "r5,r1");
        emit("lsr", "r5,r4");  // r5 = dividend's sign (0/1)
        emit("mov", "r6,r3");
        emit("lsr", "r6,r4");  // r6 = divisor's sign (0/1)
        emit("push", "r5");
        emit("push", "r6");       // stack (top->bottom): divisorSign, dividendSign
        std::string LskipNegN = newLabel(), LskipNegD = newLabel();
        emit("cmp", "r5,0");
        emit("beq", LskipNegN);
        emit("mvn", "r0,r0");
        emit("mvn", "r1,r1");
        emit("adds", "r0,r0,1");
        emit("movs", "r4,0");
        emit("adc", "r1,r4");
        emitLabel(LskipNegN);
        emit("cmp", "r6,0");
        emit("beq", LskipNegD);
        emit("mvn", "r2,r2");
        emit("mvn", "r3,r3");
        emit("adds", "r2,r2,1");
        emit("movs", "r4,0");
        emit("adc", "r3,r4");
        emitLabel(LskipNegD);
    }

    emit("movs", "r4,0");
    emit("movs", "r5,0");  // remainder = 0
    emit("movs", "r6,0");                  // iteration count-up (0..64)

    std::string Lloop = newLabel(), Ldone = newLabel();
    std::string LhiDiff = newLabel(), LsubYes = newLabel(), LsubNo = newLabel();
    emitLabel(Lloop);
    emit("cmp", "r6,64");
    emit("beq", Ldone);
    emit("adds", "r6,r6,1");

    // Shift {remainder:dividend} left by 1 as one 128-bit value via the
    // carry-chain; the dividend's old top bit lands in the remainder's bit 0.
    emit("adds", "r0,r0,r0");
    emit("adc", "r1,r1");
    emit("adc", "r4,r4");
    emit("adc", "r5,r5");

    // if (remainder >= divisor) { remainder -= divisor; quotient bit = 1 }
    // (always an unsigned magnitude compare, since signed operands were
    // already negated to non-negative above).
    emit("cmp", "r5,r3");
    emit("bne", LhiDiff);
    emit("cmp", "r4,r2");
    emit("bhs", LsubYes);
    emit("b", LsubNo);
    emitLabel(LhiDiff);
    emit("bhi", LsubYes);
    emit("b", LsubNo);
    emitLabel(LsubYes);
    emit("subs", "r4,r4,r2");
    emit("sbc", "r5,r3");
    emit("adds", "r0,r0,1");  // set the quotient bit the shift above vacated at bit 0
    emitLabel(LsubNo);
    emit("b", Lloop);

    emitLabel(Ldone);
    if (!isUnsigned) {
        emit("pop", "r6");  // r6 = divisorSign
        emit("pop", "r2");  // r2 = dividendSign
        std::string LskipNegQ = newLabel(), LskipNegR = newLabel();
        emit("mov", "r3,r6");
        emit("eor", "r3,r2");
        emit("cmp", "r3,0");
        emit("beq", LskipNegQ);
        emit("mvn", "r0,r0");
        emit("mvn", "r1,r1");
        emit("adds", "r0,r0,1");
        emit("movs", "r3,0");
        emit("adc", "r1,r3");
        emitLabel(LskipNegQ);
        emit("cmp", "r2,0");
        emit("beq", LskipNegR);
        emit("mvn", "r4,r4");
        emit("mvn", "r5,r5");
        emit("adds", "r4,r4,1");
        emit("movs", "r3,0");
        emit("adc", "r5,r3");
        emitLabel(LskipNegR);
    }
    if (wantRemainder) {
        emit("mov", "r0,r4");
        emit("mov", "r1,r5");
    }
}

void ArmMCodegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        loadSlotAddr("r2", operand.slot);
        emit("mov", "r0,r2");
        return;
    }
    if (operand.kind == ir::ExprKind::Index) {
        genIndexAddr(operand.slot, *operand.lhs);
        emit("mov", "r0,r2");
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        // BLX (register) loads the target's bit 0 into EPSR.T and faults if
        // it isn't 1 (M-profile has no ARM state); the label is always even,
        // so +1 sets that bit.
        loadAddress("r2", label(operand.func));
        emit("adds", "r2,r2,1");
        emit("mov", "r0,r2");
        return;
    }
    err(operand.line, "internal: unhandled addressable-operand kind");
}

// Entry: r0 = old, r1 = rhs (both at type's width/signedness).
void ArmMCodegen::genCompoundOp(const std::string &op, Type type) {
    bool isUnsigned = type.isUnsigned;
    if (op == "+") { emit("adds", "r0,r0,r1"); return; }
    if (op == "-") { emit("subs", "r0,r0,r1"); return; }
    if (op == "*") { emit("muls", "r0,r1"); return; }
    if (op == "&") { emit("and", "r0,r1"); return; }
    if (op == "|") { emit("orr", "r0,r1"); return; }
    if (op == "^") { emit("eor", "r0,r1"); return; }
    if (op == "<<" || op == ">>") {
        const char *insn = op == "<<" ? "lsl" : (isUnsigned ? "lsr" : "asr");
        emit(insn, "r0,r1");
        return;
    }
    // "/" or "%": genDivModCore() expects r1=dividend,r0=divisor, so swap
    // from this function's r0=old,r1=rhs entry via scratch r3.
    emit("mov", "r3,r0");
    emit("mov", "r0,r1");
    emit("mov", "r1,r3");
    genDivModCore(/*wantRemainder=*/op == "%", isUnsigned);
}

// r2 must already hold the resolved address on entry; it's stashed on the
// stack (never recomputed) across evaluating e.rhs, since genExpr() may
// clobber r0-r7, and again across genCompoundOp() since its "/"/"%" path
// uses r2 as scratch.
void ArmMCodegen::genCompoundThroughR2(const ir::Expr &e) {
    emit("push", "r2");   // stash address
    genExpr(*e.rhs);              // r0 = rhs
    emit("push", "r0");      // stash rhs
    emit("pop", "r3");        // r3 = rhs (loadOperand() below never touches r3)
    emit("pop", "r2");         // r2 = address (restored)
    loadOperand("r2", e.type);          // r0 = old
    emit("mov", "r1,r3");           // r1 = rhs
    emit("push", "r2");              // stash address across genCompoundOp()
    if (e.yieldOld) emit("push", "r0");  // stash old
    genCompoundOp(e.op, e.type);                // r0 = new (raw)
    narrowR0To(e.type);
    if (e.yieldOld) {
        emit("mov", "r1,r0");               // r1 = new
        emit("pop", "r0");                    // r0 = old
        emit("pop", "r2");                      // r2 = address (restored)
        emit("mov", "r3,r0");                     // r3 = old, preserved across the store below
        emit("mov", "r0,r1");                       // r0 = new - storeOperand always stores from r0
        storeOperand("r2", e.type);
        emit("mov", "r0,r3");                         // r0 = old (final result)
    } else {
        emit("pop", "r2");  // r2 = address (restored)
        storeOperand("r2", e.type);  // r0 already = new
    }
}

void ArmMCodegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    // lhs/rhs share one common type (usual arithmetic conversions), except
    // shift (only lhs's type matters) and &&/|| (width-independent, below).
    Type operandType = e.lhs->type;
    bool wide = isWide(operandType);
    bool isUnsigned = operandType.isUnsigned;
    if (op == "+" || op == "-") {
        if (wide) {
            genWideAdd(*e.lhs, *e.rhs, op == "+");
            return;
        }
        genExpr(*e.lhs);
        emit("push", "r0");
        genExpr(*e.rhs);
        emit("pop", "r1");  // r1 = lhs, r0 = rhs
        if (op == "+") {
            emit("adds", "r0,r1,r0");  // r0 = lhs + rhs
        } else {
            emit("subs", "r0,r1,r0");  // r0 = lhs - rhs
        }
        return;
    }
    if (op == "*") {
        if (wide) {
            genWideMul(*e.lhs, *e.rhs);
        } else {
            genMul(*e.lhs, *e.rhs);
        }
        return;
    }
    if (op == "/" || op == "%") {
        if (wide) {
            genWideDivMod(*e.lhs, *e.rhs, /*wantRemainder=*/op == "%", isUnsigned);
        } else {
            genDivMod(*e.lhs, *e.rhs, /*wantRemainder=*/op == "%", isUnsigned);
        }
        return;
    }
    if (op == "&" || op == "|" || op == "^") {
        const char *insn = (op == "&") ? "and" : (op == "|") ? "orr" : "eor";
        if (wide) {
            genExpr(*e.lhs);
            emit("push", "r1");
            emit("push", "r0");
            genExpr(*e.rhs);
            emit("mov", "r4,r0");
            emit("mov", "r5,r1");
            emit("pop", "r0");
            emit("pop", "r1");  // r0:r1 = lhs
            emit(insn, "r0,r4");
            emit(insn, "r1,r5");
            return;
        }
        genExpr(*e.lhs);
        emit("push", "r0");
        genExpr(*e.rhs);
        emit("pop", "r1");  // r1 = lhs, r0 = rhs
        emit(insn, "r0,r1");
        return;
    }
    if (op == "<<" || op == ">>") {
        if (wide) {
            genWideShift(*e.lhs, *e.rhs, op == "<<", isUnsigned);
            return;
        }
        genExpr(*e.lhs);
        emit("push", "r0");
        genExpr(*e.rhs);               // r0 = shift count
        emit("mov", "r1,r0");    // r1 = shift count
        emit("pop", "r0");       // r0 = value to shift
        const char *insn = op == "<<" ? "lsl" : (isUnsigned ? "lsr" : "asr");
        emit(insn, "r0,r1");
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

void ArmMCodegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    emit("push", "r0");
    genExpr(rhs);
    emit("pop", "r1");  // r1 = lhs, r0 = rhs
    emit("muls", "r0,r1");
}

// Division is restoring shift-subtract, one quotient bit per
// iteration (genDivModCore()), so its cost is fixed at 32 steps.
void ArmMCodegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    emit("push", "r0");
    genExpr(rhs);
    emit("pop", "r1");  // r1 = lhs (dividend), r0 = rhs (divisor)
    genDivModCore(wantRemainder, isUnsigned);
}

// Entry: r1=dividend, r0=divisor; result in r0. Clobbers r1-r5.
// Signed operands are divided as magnitudes; the quotient is negated
// when the signs differ and the remainder takes the dividend's sign.
// Each step doubles the remainder with the dividend's outgoing bit
// carried in (`adds`/`adc`); a carry out of the remainder means it
// already exceeds any 32-bit divisor.
void ArmMCodegen::genDivModCore(bool wantRemainder, bool isUnsigned) {
    if (!isUnsigned) {
        std::string LnPos = newLabel(), LdPos = newLabel();
        emit("movs", "r5,0");  // 1 if the result needs negating
        emit("cmp", "r1,0");
        emit("bge", LnPos);
        emit("movs", "r5,1");
        emit("neg", "r1,r1");
        emitLabel(LnPos);
        emit("cmp", "r0,0");
        emit("bge", LdPos);
        emit("neg", "r0,r0");
        if (!wantRemainder) {
            emit("movs", "r4,1");
            emit("eor", "r5,r4");
        }
        emitLabel(LdPos);
    }

    std::string Lloop = newLabel(), Lsub = newLabel(), Lnext = newLabel();
    emit("movs", "r2,0");
    emit("movs", "r3,0");
    emit("movs", "r4,0");  // r2 = quotient, r3 = remainder, r4 = step
    emitLabel(Lloop);
    emit("adds", "r2,r2,r2");
    emit("adds", "r1,r1,r1");  // carry = dividend's outgoing bit
    emit("adc", "r3,r3");       // carry = remainder's outgoing bit
    emit("bhs", Lsub);
    emit("cmp", "r3,r0");
    emit("blo", Lnext);
    emitLabel(Lsub);
    emit("subs", "r3,r3,r0");
    emit("adds", "r2,r2,1");
    emitLabel(Lnext);
    emit("adds", "r4,r4,1");
    emit("cmp", "r4,32");
    emit("bne", Lloop);
    emit("mov", cat("r0,", (wantRemainder ? "r3" : "r2")));

    if (!isUnsigned) {
        std::string Ldone = newLabel();
        emit("cmp", "r5,0");
        emit("beq", Ldone);
        emit("neg", "r0,r0");
        emitLabel(Ldone);
    }
}

// CMP sets condition codes for Rn-Rm in hardware; isUnsigned picks the
// unsigned branch mnemonic (blo/bls/bhi/bhs) over the signed one.
void ArmMCodegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs) {
    if (isWide(lhs.type)) {
        genWideCompare(op, lhs, rhs, lhs.type.isUnsigned);
        return;
    }
    bool isUnsigned = lhs.type.isUnsigned;
    genExpr(lhs);
    emit("push", "r0");
    genExpr(rhs);
    emit("pop", "r1");      // r1 = lhs, r0 = rhs
    emit("cmp", "r1,r0");   // flags = r1 - r0 = lhs - rhs
    std::string branchIfTrue;
    if (op == "==") branchIfTrue = "beq";
    else if (op == "!=") branchIfTrue = "bne";
    else if (op == "<") branchIfTrue = isUnsigned ? "blo" : "blt";
    else if (op == "<=") branchIfTrue = isUnsigned ? "bls" : "ble";
    else if (op == ">") branchIfTrue = isUnsigned ? "bhi" : "bgt";
    else if (op == ">=") branchIfTrue = isUnsigned ? "bhs" : "bge";
    else err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit(branchIfTrue, Ltrue);
    emit("movs", "r0,0");
    emit("b", Ldone);
    emitLabel(Ltrue);
    emit("movs", "r0,1");
    emitLabel(Ldone);
}

// A wide operand's truthiness needs both words: orr r0,r1 folds them
// together before the usual cmp r0,0.
void ArmMCodegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    if (isWide(lhs.type)) emit("orr", "r0,r1");
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    emit("cmp", "r0,0");
    emit("beq", Lfalse);
    genExpr(rhs);
    if (isWide(rhs.type)) emit("orr", "r0,r1");
    emit("cmp", "r0,0");
    emit("beq", Lfalse);
    emit("b", Ltrue);
    emitLabel(Lfalse);
    emit("movs", "r0,0");
    emit("b", Ldone);
    emitLabel(Ltrue);
    emit("movs", "r0,1");
    emitLabel(Ldone);
}

void ArmMCodegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    if (isWide(lhs.type)) emit("orr", "r0,r1");
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    emit("cmp", "r0,0");
    emit("bne", Ltrue);
    genExpr(rhs);
    if (isWide(rhs.type)) emit("orr", "r0,r1");
    emit("cmp", "r0,0");
    emit("bne", Ltrue);
    emit("b", Lfalse);
    emitLabel(Ltrue);
    emit("movs", "r0,1");
    emit("b", Ldone);
    emitLabel(Lfalse);
    emit("movs", "r0,0");
    emitLabel(Ldone);
}

void ArmMCodegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    if (isWide(rhs.type)) emit("orr", "r0,r1");
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit("cmp", "r0,0");
    emit("beq", Ltrue);
    emit("movs", "r0,0");
    emit("b", Ldone);
    emitLabel(Ltrue);
    emit("movs", "r0,1");
    emitLabel(Ldone);
}

std::optional<std::string> ArmMCodegen::assembleInProcess(const std::string &asmText,
                                                            const std::string &format) const {
    if (format == "raw") return assembleArmM(asmText);
    // coff/elf/macho/aout/objraw: build one format-neutral ObjectFile, then
    // serialize it with that format's FileFormatWriter.
    if (auto kind = fileFormatKindFromName(format)) {
        ObjectFile obj = assembleArmMObject(asmText);
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

}  // namespace rcc
