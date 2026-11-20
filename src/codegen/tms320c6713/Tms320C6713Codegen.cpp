#include "Tms320C6713Codegen.h"

#include <cstdint>
#include <stdexcept>

#include "Tms320C6713Assembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

// Register names - see Tms320C6713Codegen.h's own header comment for the convention these
// follow.
constexpr const char *AC = "B0";
constexpr const char *T1 = "B1";
constexpr const char *T2 = "B2";
constexpr const char *RA = "B3";
constexpr const char *T3 = "B4";
constexpr const char *T4 = "B5";
constexpr const char *T5 = "B6";
constexpr const char *T6 = "B7";
constexpr const char *T7 = "B8";
constexpr const char *T8 = "B9";
constexpr const char *T9 = "B10";
constexpr const char *T10 = "B11";
constexpr const char *T11 = "B12";
constexpr const char *ZERO = "B13";
constexpr const char *FP = "B14";
constexpr const char *SP = "B15";

// Where the whole program loads; the test harness sets the simulated CPU's PC here directly.
constexpr uint32_t kBaseAddress = 0x00020000u;
// Initial sp, comfortably above the loaded program and its data.
constexpr uint32_t kInitialSP = 0x00100000u;
}  // namespace

std::string Tms320C6713Codegen::arrayElemText(const ir::ArrayInitElem &e) {
    if (!e.isSymbol) return std::to_string(static_cast<int32_t>(static_cast<uint32_t>(e.value)));
    return e.slot ? label(e.slot) : label(e.func);
}

void Tms320C6713Codegen::declareArray(const std::string &lbl, const std::vector<ir::ArrayInitElem> &values,
                                       Type elemType) {
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
        line << arrayElemText(e);
        first = false;
    }
    trailer_.push_back(line.str());
}

// --- low-level emission helpers -------------------------------------------

void Tms320C6713Codegen::loadImm32(const std::string &reg, int32_t v) {
    int32_t lo16 = static_cast<int32_t>(static_cast<int16_t>(v & 0xFFFF));
    if (v == lo16) {
        emit("MVK", cat(reg, ",", lo16));
        return;
    }
    emit("MVK", cat(reg, ",", lo16));
    uint32_t hi16 = (static_cast<uint32_t>(v) >> 16) & 0xFFFFu;
    emit("MVKH", cat(reg, ",", hi16));
}

void Tms320C6713Codegen::loadAddress(const std::string &reg, const std::string &lbl) {
    emit("MVK", reg + "," + lbl);
    emit("MVKH", reg + "," + lbl);
}

void Tms320C6713Codegen::adjustReg(const std::string &reg, int32_t delta) {
    if (delta == 0) return;
    loadImm32(T10, delta);
    emit("ADD", reg + "," + reg + "," + T10);
}

void Tms320C6713Codegen::computeAddr(const std::string &dst, const std::string &baseReg, int32_t byteOffset) {
    loadImm32(dst, byteOffset);
    emit("ADD", dst + "," + dst + "," + baseReg);
}

void Tms320C6713Codegen::loadSlotAddr(const std::string &reg, const ir::Slot *slot) {
    if (slot->kind == ir::SlotKind::Global) {
        loadAddress(reg, label(slot));
        return;
    }
    int offset = slot->kind == ir::SlotKind::Param ? slot->frameOffset : -ir::localLowOffset(*slot, typeWidths());
    computeAddr(reg, FP, offset);
}

void Tms320C6713Codegen::loadStack(const std::string &reg, int32_t byteOffset) {
    computeAddr(T2, SP, byteOffset);
    emitLoad("LDW", T2, reg);
}

void Tms320C6713Codegen::storeStack(const std::string &reg, int32_t byteOffset) {
    computeAddr(T2, SP, byteOffset);
    emitStore("STW", T2, reg);
}

void Tms320C6713Codegen::genPush(const std::string &reg) {
    adjustReg(SP, -4);
    emitStore("STW", SP, reg);
}

void Tms320C6713Codegen::genPop(const std::string &reg) {
    emitLoad("LDW", SP, reg);
    adjustReg(SP, 4);
}

void Tms320C6713Codegen::genPushWide(const std::string &lo, const std::string &hi) {
    adjustReg(SP, -8);
    emitStore("STW", SP, lo);
    computeAddr(T2, SP, 4);
    emitStore("STW", T2, hi);
}

void Tms320C6713Codegen::genPopWide(const std::string &lo, const std::string &hi) {
    emitLoad("LDW", SP, lo);
    computeAddr(T2, SP, 4);
    emitLoad("LDW", T2, hi);
    adjustReg(SP, 8);
}

void Tms320C6713Codegen::emitLoad(const std::string &mnemonic, const std::string &base, const std::string &reg) {
    emit(mnemonic, base + "," + reg);
    for (int i = 0; i < 4; i++) emit("NOP");
}

void Tms320C6713Codegen::emitStore(const std::string &mnemonic, const std::string &base, const std::string &reg) {
    emit(mnemonic, base + "," + reg);
}

void Tms320C6713Codegen::emitMul(const std::string &mnemonic, const std::string &dst, const std::string &src1,
                                  const std::string &src2) {
    emit(mnemonic, dst + "," + src1 + "," + src2);
    emit("NOP");
}

void Tms320C6713Codegen::emitBranch(const std::string &label) {
    emit("B", label);
    for (int i = 0; i < 5; i++) emit("NOP");
}

void Tms320C6713Codegen::emitBranchZ(bool branchIfZero, const std::string &label) {
    emit(branchIfZero ? "BZ" : "BNZ", label);
    for (int i = 0; i < 5; i++) emit("NOP");
}

void Tms320C6713Codegen::emitBranchReg(const std::string &reg) {
    emit("BR", "(" + reg + ")");
    for (int i = 0; i < 5; i++) emit("NOP");
}

// --- program/function structure --------------------------------------------

std::string Tms320C6713Codegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    emitRaw("org " + std::to_string(objectMode ? 0 : kBaseAddress));
    emitRaw("");
    if (!objectMode) {
        emitLabel("_start");
        // Establishes the permanent zero register (see this backend's own header comment);
        // never written again anywhere else in generated code.
        emit("MVK", std::string(ZERO) + ",0");
        loadImm32(SP, static_cast<int32_t>(kInitialSP));
        loadAddress(RA, "halt");
        emitBranch(label(mainFn));
        emitLabel("halt");
        emitBranch("halt");
        emitRaw("");
    }

    for (const auto &fb : prog.functionBodies) genFunction(fb);

    size_t dataOffset = 0;
    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        if (slot->isExternal) continue;
        const std::string lbl = label(slot.get());
        int elemW = widthOfType(slot->type);
        size_t totalBytes = slot->arraySize > 0 ? static_cast<size_t>(slot->arraySize) * elemW
                                                  : static_cast<size_t>(elemW);
        // LDW/STW need 4-byte alignment; a `long long` is always accessed as two separate
        // 4-byte words (never one 8-byte transfer), so 4 bytes is this target's own widest
        // alignment requirement, matching MipsCodegen.cpp's own 32-bit-native data layout.
        size_t align = elemW >= 4 ? 4 : static_cast<size_t>(elemW);
        while (align > 1 && (dataOffset % align) != 0) {
            trailer_.push_back("db 0  ; alignment padding");
            dataOffset += 1;
        }
        dataOffset += totalBytes;
        if (slot->arraySize > 0) {
            declareArray(lbl, slot->arrayInit, slot->type);
        } else if (slot->type.base == BaseType::Struct) {
            // No struct initializer exists in this subset, so this is
            // always zero-filled; reuses declareArray's own element-list
            // emission rather than the width-bucketed logic below, which
            // only handles 1/2/4/8-byte widths.
            declareArray(lbl, std::vector<ir::ArrayInitElem>(slot->type.structSize), Type{BaseType::Char, false});
        } else {
            long v = slot->hasInit ? slot->initValue : 0;
            if (elemW == 8) {
                auto uv = static_cast<uint64_t>(v);
                trailer_.push_back(lbl + ": dw " + std::to_string(static_cast<uint32_t>(uv)) + ", " +
                                    std::to_string(static_cast<uint32_t>(uv >> 32)) + "  ; " + slot->sourceName);
            } else {
                const char *directive = elemW == 1 ? "db" : elemW == 2 ? "dh" : "dw";
                trailer_.push_back(lbl + ": " + directive + " " +
                                    std::to_string(static_cast<int32_t>(static_cast<uint32_t>(v))) + "  ; " +
                                    slot->sourceName);
            }
        }
    }

    std::ostringstream out;
    out << code_.str();
    for (const auto &line : trailer_) out << line << "\n";
    return out.str();
}

std::map<std::string, std::string> Tms320C6713Codegen::perFileAsm() const { return perFileAsm_; }

// B14/B3 are saved before B14/B15 move, the same chicken-and-egg care AlphaCodegen.cpp's own
// genFunction() comment describes: the caller's fp/ra must land in the NEW frame (computed off
// the already-adjusted sp) before fp itself is repointed there.
void Tms320C6713Codegen::genFunction(const ir::FunctionBody &fn) {
    emitLabel(label(fn.func));
    adjustReg(SP, -8);
    emitStore("STW", SP, FP);  // caller's fp at new sp+0
    computeAddr(T2, SP, 4);
    emitStore("STW", T2, RA);  // this call's own return address at new sp+4
    emit("ADD", std::string(FP) + "," + SP + "," + ZERO);  // fp = sp
    if (fn.func->frameSize > 0) adjustReg(SP, -fn.func->frameSize);

    genStmt(*fn.body);

    genEpilogue();
    emitRaw("");
}

void Tms320C6713Codegen::genEpilogue() {
    emit("ADD", std::string(SP) + "," + FP + "," + ZERO);  // sp = fp (drop locals)
    emitLoad("LDW", SP, T9);                                // T9 = saved caller's fp
    computeAddr(T2, SP, 4);
    emitLoad("LDW", T2, RA);                                // ra = this call's own saved ra
    adjustReg(SP, 8);
    emit("ADD", std::string(FP) + "," + T9 + "," + ZERO);  // fp = caller's fp
    emitBranchReg(RA);
}

void Tms320C6713Codegen::genStmt(const ir::Stmt &s) {
    switch (s.kind) {
        case ir::StmtKind::Block: {
            for (const auto &child : s.body) genStmt(*child);
            return;
        }
        case ir::StmtKind::If: {
            genExpr(*s.cond);
            if (isWide(s.cond->type)) emit("OR", std::string(AC) + "," + AC + "," + T1);
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            emitBranchZ(true, s.elseStmt ? Lelse : Ldone);
            genStmt(*s.thenStmt);
            if (s.elseStmt) {
                emitBranch(Ldone);
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
            if (isWide(s.cond->type)) emit("OR", std::string(AC) + "," + AC + "," + T1);
            emitBranchZ(true, Lend);
            loopLabels_.push_back({Lcont, Lend});
            genStmt(*s.loopBody);
            loopLabels_.pop_back();
            if (s.postStmt) {
                emitLabel(Lcont);
                genStmt(*s.postStmt);
            }
            emitBranch(Lstart);
            emitLabel(Lend);
            return;
        }
        case ir::StmtKind::Break: {
            emitBranch(loopLabels_.back().second);
            return;
        }
        case ir::StmtKind::Continue: {
            emitBranch(loopLabels_.back().first);
            return;
        }
        case ir::StmtKind::Return: {
            if (s.initOrReturnOrExpr) genExpr(*s.initOrReturnOrExpr);
            genEpilogue();
            return;
        }
        case ir::StmtKind::LocalDecl: {
            if (s.declSlot->arraySize > 0) {
                genLocalArrayInit(s.declSlot);
            } else if (s.initOrReturnOrExpr) {
                genExpr(*s.initOrReturnOrExpr);
                loadSlotAddr(T2, s.declSlot);
                storeOperand(T2, s.declSlot->type);
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
            emitBranch(irLabel(s.labelId));
            return;
        }
    }
}

void Tms320C6713Codegen::genExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit: {
            if (isWide(e.type)) {
                auto v = static_cast<uint64_t>(e.intValue);
                loadImm32(AC, static_cast<int32_t>(static_cast<uint32_t>(v)));
                loadImm32(T1, static_cast<int32_t>(static_cast<uint32_t>(v >> 32)));
                return;
            }
            loadImm32(AC, static_cast<int32_t>(static_cast<uint32_t>(e.intValue)));
            narrowB0To(e.type);
            return;
        }
        case ir::ExprKind::Var: {
            loadSlotAddr(T2, e.slot);
            loadOperand(T2, e.slot->type);
            return;
        }
        case ir::ExprKind::Index: {
            genIndex(e);
            return;
        }
        case ir::ExprKind::Assign: {
            if (e.lhs) {
                genIndexAddr(e.slot, *e.lhs);  // T2 = target address
                genPush(T2);
                genExpr(*e.rhs);
                genPop(T2);
                storeOperand(T2, e.slot->type);
                return;
            }
            genExpr(*e.rhs);
            loadSlotAddr(T2, e.slot);
            storeOperand(T2, e.slot->type);
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            if (e.lhs) {
                genIndexAddr(e.slot, *e.lhs);
                genCompoundThroughT2(e);
                return;
            }
            loadSlotAddr(T2, e.slot);
            genCompoundThroughT2(e);
            return;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            genExpr(*e.lhs);  // AC = pointer value
            emit("ADD", cat(T2, ",", AC, ",", ZERO));
            genCompoundThroughT2(e);
            return;
        }
        case ir::ExprKind::Ternary: {
            genExpr(*e.lhs);
            if (isWide(e.lhs->type)) emit("OR", cat(AC, ",", AC, ",", T1));
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            emitBranchZ(true, Lelse);
            genExpr(*e.rhs);
            emitBranch(Ldone);
            emitLabel(Lelse);
            genExpr(*e.elseExpr);
            emitLabel(Ldone);
            return;
        }
        case ir::ExprKind::DerefAssign: {
            genExpr(*e.lhs);  // AC = pointer value
            genPush(AC);
            genExpr(*e.rhs);
            genPop(T2);
            storeOperand(T2, e.type);
            return;
        }
        case ir::ExprKind::Unary: {
            if (e.op == "-") {
                genExpr(*e.rhs);
                if (isWide(e.rhs->type)) {
                    genNegate64();
                } else {
                    emit("SUB", cat(AC, ",", ZERO, ",", AC));
                    narrowB0To(e.rhs->type);
                }
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                genExpr(*e.rhs);  // AC = pointer value
                emit("ADD", cat(T2, ",", AC, ",", ZERO));
                loadOperand(T2, e.type);
            } else if (e.op == "~") {
                genExpr(*e.rhs);
                loadImm32(T10, -1);
                emit("XOR", cat(AC, ",", AC, ",", T10));
                if (isWide(e.rhs->type)) {
                    emit("XOR", cat(T1, ",", T1, ",", T10));
                } else {
                    narrowB0To(e.rhs->type);
                }
            } else {  // "&"
                genAddrOf(*e.rhs);
            }
            return;
        }
        case ir::ExprKind::Convert: {
            genConvert(e);
            return;
        }
        case ir::ExprKind::Call: {
            int totalBytes = 0;
            for (size_t i = e.args.size(); i-- > 0;) {
                genExpr(*e.args[i]);
                if (isWide(e.args[i]->type)) {
                    genPushWide(AC, T1);
                    totalBytes += 8;
                } else {
                    genPush(AC);
                    totalBytes += 4;
                }
            }
            std::string retLbl = newLabel();
            loadAddress(RA, retLbl);
            emitBranch(label(e.func));
            emitLabel(retLbl);
            if (totalBytes > 0) adjustReg(SP, totalBytes);
            return;
        }
        case ir::ExprKind::CallPtr: {
            genExpr(*e.lhs);  // AC = the callee's own entry address
            emit("ADD", cat(T2, ",", AC, ",", ZERO));
            std::string retLbl = newLabel();
            loadAddress(RA, retLbl);
            emitBranchReg(T2);
            emitLabel(retLbl);
            return;
        }
        case ir::ExprKind::Binary: {
            genBinary(e);
            return;
        }
        case ir::ExprKind::Intrinsic: {
            (void)e;  // no exception/interrupt setup on this bare-metal target; no-op.
            return;
        }
        case ir::ExprKind::Func:
            err(e.line, "internal: Func reached genExpr - should only ever appear as '&'s operand");
    }
}

void Tms320C6713Codegen::genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
    genExpr(idxExpr);  // AC = index
    int shift = elemShift(slot->type);
    if (shift > 0) {
        loadImm32(T10, shift);
        emit("SHL", cat(AC, ",", AC, ",", T10));
    } else if (shift < 0) {
        // Non-power-of-two element width (a struct's): scale by a real multiply instead.
        loadImm32(T10, widthOfType(slot->type));
        genMulLow32(AC, AC, T10);
    }
    if (slot->arraySize > 0) {
        loadSlotAddr(T2, slot);
        emit("ADD", cat(T2, ",", T2, ",", AC));
    } else {
        loadSlotAddr(T1, slot);
        emitLoad("LDW", T1, T1);  // T1 = the pointer variable's own stored value
        emit("ADD", cat(T2, ",", T1, ",", AC));
    }
}

void Tms320C6713Codegen::genIndex(const ir::Expr &e) {
    genIndexAddr(e.slot, *e.lhs);
    loadOperand(T2, e.slot->type);
}

void Tms320C6713Codegen::genLocalArrayInit(const ir::Slot *slot) {
    int w = widthOfType(slot->type);
    for (size_t i = 0; i < slot->arrayInit.size(); i++) {
        const ir::ArrayInitElem &elem = slot->arrayInit[i];
        int offset = -ir::localLowOffset(*slot, typeWidths()) + w * static_cast<int>(i);
        if (elem.isSymbol) {
            loadAddress(AC, elem.slot ? label(elem.slot) : label(elem.func));
            computeAddr(T2, FP, offset);
            emitStore("STW", T2, AC);
            continue;
        }
        if (w == 8) {
            auto v = static_cast<uint64_t>(elem.value);
            loadImm32(AC, static_cast<int32_t>(static_cast<uint32_t>(v)));
            loadImm32(T1, static_cast<int32_t>(static_cast<uint32_t>(v >> 32)));
            computeAddr(T2, FP, offset);
            emitStore("STW", T2, AC);
            computeAddr(T2, FP, offset + 4);
            emitStore("STW", T2, T1);
            continue;
        }
        loadImm32(AC, static_cast<int32_t>(static_cast<uint32_t>(elem.value)));
        computeAddr(T2, FP, offset);
        emitStore(w == 1 ? "STB" : w == 2 ? "STH" : "STW", T2, AC);
    }
}

void Tms320C6713Codegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        loadSlotAddr(T2, operand.slot);
        emit("ADD", cat(AC, ",", T2, ",", ZERO));
        return;
    }
    if (operand.kind == ir::ExprKind::Index) {
        genIndexAddr(operand.slot, *operand.lhs);
        emit("ADD", cat(AC, ",", T2, ",", ZERO));
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        loadAddress(T2, label(operand.func));
        emit("ADD", cat(AC, ",", T2, ",", ZERO));
        return;
    }
    err(operand.line, "internal: unhandled addressable-operand kind");
}

void Tms320C6713Codegen::loadOperand(const std::string &addrReg, Type type) {
    int w = widthOfType(type);
    if (w == 1) {
        emitLoad("LDB", addrReg, AC);
        narrowB0To(type);
        return;
    }
    if (w == 2) {
        emitLoad("LDH", addrReg, AC);
        narrowB0To(type);
        return;
    }
    if (w == 4) {
        emitLoad("LDW", addrReg, AC);
        return;
    }
    // w == 8
    emitLoad("LDW", addrReg, AC);     // lo
    computeAddr(T9, addrReg, 4);       // T9 = addrReg+4 (never disturbs addrReg itself)
    emitLoad("LDW", T9, T1);            // hi
}

void Tms320C6713Codegen::storeOperand(const std::string &addrReg, Type type) {
    int w = widthOfType(type);
    if (w == 1) {
        emitStore("STB", addrReg, AC);
        return;
    }
    if (w == 2) {
        emitStore("STH", addrReg, AC);
        return;
    }
    if (w == 4) {
        emitStore("STW", addrReg, AC);
        return;
    }
    emitStore("STW", addrReg, AC);
    computeAddr(T9, addrReg, 4);
    emitStore("STW", T9, T1);
}

void Tms320C6713Codegen::narrowB0To(Type to) {
    int w = widthOfType(to);
    if (w >= 4) return;
    int shiftAmt = 32 - 8 * w;
    loadImm32(T10, shiftAmt);
    emit("SHL", cat(AC, ",", AC, ",", T10));
    emit((to.isUnsigned ? "SHRU" : "SHR"), cat(AC, ",", AC, ",", T10));
}

void Tms320C6713Codegen::genConvert(const ir::Expr &e) {
    Type from = e.lhs->type;
    Type to = e.type;
    bool fromWide = isWide(from);
    bool toWide = isWide(to);
    genExpr(*e.lhs);
    if (fromWide && !toWide) {
        narrowB0To(to);  // T1 simply discarded
    } else if (!fromWide && toWide) {
        if (from.isUnsigned) {
            loadImm32(T1, 0);
        } else {
            loadImm32(T10, 31);
            emit("SHR", cat(T1, ",", AC, ",", T10));  // sign-fill T1
        }
    } else if (!fromWide && !toWide) {
        narrowB0To(to);
    }
    // fromWide && toWide: nothing to do.
}

// --- multiply ---------------------------------------------------------------

// Standard TI 16x16-decomposition sequence (see this backend's header comment): the low 32
// bits of src1*src2, correct regardless of either operand's sign interpretation since
// multiplication mod 2^32 only depends on each operand's own bit pattern mod 2^32. `dst` is
// only ever written by the final instruction, so it may safely alias `src1`/`src2`.
// The plain MPY+MPYHL+MPYLH sum (as above, shifted and added) is only *almost* low32(A*B):
// MPY itself sign-extends each operand's low halfword before multiplying, so whenever A's (or
// B's) low 16 bits have their own bit 15 set, MPY's result is short by exactly 0x10000*B (resp.
// 0x10000*A) relative to the unsigned-low16*unsigned-low16 product the textbook decomposition
// actually needs - an error that does NOT cancel out the way the equivalent error in
// MPYHL/MPYLH's own cross terms does once shifted left 16 (that one lands exactly on a multiple
// of 2^32 and vanishes mod 2^32; this one doesn't). Correct for it explicitly below via a
// branch-free mask (0 or 0xFFFFFFFF from each operand's own low-halfword sign bit) rather than
// trust the sequence to be exact as a bare sum.
void Tms320C6713Codegen::genMulLow32(const std::string &dst, const std::string &src1, const std::string &src2) {
    emit("ADD", cat(T6, ",", src1, ",", ZERO));  // T6 = A (safe copy)
    emit("ADD", cat(T7, ",", src2, ",", ZERO));  // T7 = B (safe copy)

    emitMul("MPY", T3, T6, T7);     // T3 = lo*lo partial product
    emitMul("MPYHL", T4, T6, T7);    // T4 = A_hi * B_lo
    emitMul("MPYLH", T5, T6, T7);     // T5 = A_lo * B_hi
    emit("ADD", cat(T4, ",", T4, ",", T5));
    loadImm32(T8, 16);
    emit("SHL", cat(T4, ",", T4, ",", T8));
    emit("ADD", cat(T3, ",", T3, ",", T4));

    // If A's low halfword's own sign bit is set, add B's raw low halfword (unshifted sign
    // error) back in, shifted into position.
    loadImm32(T8, 0x8000);
    emit("AND", cat(T9, ",", T6, ",", T8));
    loadImm32(T8, 15);
    emit("SHRU", cat(T9, ",", T9, ",", T8));  // clean 0/1
    emit("SUB", cat(T9, ",", ZERO, ",", T9));  // mask: 0 or all-ones
    loadImm32(T8, 0xFFFF);
    emit("AND", cat(T10, ",", T7, ",", T8));
    emit("AND", cat(T10, ",", T10, ",", T9));
    loadImm32(T8, 16);
    emit("SHL", cat(T10, ",", T10, ",", T8));
    emit("ADD", cat(T3, ",", T3, ",", T10));

    // Symmetric correction if B's low halfword's own sign bit is set.
    loadImm32(T8, 0x8000);
    emit("AND", cat(T9, ",", T7, ",", T8));
    loadImm32(T8, 15);
    emit("SHRU", cat(T9, ",", T9, ",", T8));
    emit("SUB", cat(T9, ",", ZERO, ",", T9));
    loadImm32(T8, 0xFFFF);
    emit("AND", cat(T10, ",", T6, ",", T8));
    emit("AND", cat(T10, ",", T10, ",", T9));
    loadImm32(T8, 16);
    emit("SHL", cat(T10, ",", T10, ",", T8));
    emit("ADD", cat(T3, ",", T3, ",", T10));

    emit("ADD", cat(dst, ",", T3, ",", ZERO));
}

// Full unsigned 64-bit product of two 32-bit registers via a 32-iteration shift-and-add loop
// (see this backend's own header comment on why the sign-extending MPY family can't give this
// directly). `aReg`/`bReg` are copied out before anything else is touched, so `outLo`/`outHi`
// may safely alias either of them.
void Tms320C6713Codegen::genMulFull32(const std::string &aReg, const std::string &bReg, const std::string &outLo,
                                       const std::string &outHi) {
    emit("ADD", cat(T6, ",", aReg, ",", ZERO));  // addend lo = a
    loadImm32(T7, 0);                                                  // addend hi = 0
    emit("ADD", cat(T8, ",", bReg, ",", ZERO));  // multiplier = b
    loadImm32(T3, 0);                                                    // result lo = 0
    loadImm32(T4, 0);                                                     // result hi = 0
    loadImm32(T9, 32);                                                      // counter = 32

    std::string Lloop = newLabel(), Ldone = newLabel(), Lskip = newLabel();
    emitLabel(Lloop);
    emit("CMPEQ", cat(AC, ",", T9, ",", ZERO));
    emitBranchZ(false, Ldone);  // counter == 0 -> done
    loadImm32(T10, -1);
    emit("ADD", cat(T9, ",", T9, ",", T10));

    loadImm32(T10, 1);
    emit("AND", cat(AC, ",", T8, ",", T10));  // bit = multiplier & 1
    emitBranchZ(true, Lskip);
    // result += addend (64-bit add with carry)
    emit("ADD", cat(T11, ",", T3, ",", T6));  // sum = resultLo+addendLo
    emit("CMPLTU", cat(AC, ",", T11, ",", T3));  // carry
    emit("ADD", cat(T4, ",", T4, ",", T7));
    emit("ADD", cat(T4, ",", T4, ",", AC));
    emit("ADD", cat(T3, ",", T11, ",", ZERO));
    emitLabel(Lskip);
    // addend <<= 1 (64-bit)
    loadImm32(T10, 31);
    emit("SHRU", cat(AC, ",", T6, ",", T10));  // top bit of addend lo
    loadImm32(T10, 1);
    emit("SHL", cat(T6, ",", T6, ",", T10));
    emit("SHL", cat(T7, ",", T7, ",", T10));
    emit("OR", cat(T7, ",", T7, ",", AC));
    // multiplier >>>= 1
    loadImm32(T10, 1);
    emit("SHRU", cat(T8, ",", T8, ",", T10));
    emitBranch(Lloop);
    emitLabel(Ldone);

    emit("ADD", cat(outLo, ",", T3, ",", ZERO));
    emit("ADD", cat(outHi, ",", T4, ",", ZERO));
}

void Tms320C6713Codegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    genPush(AC);
    genExpr(rhs);
    emit("ADD", cat(T1, ",", AC, ",", ZERO));  // T1 = rhs
    genPop(AC);                                                      // AC = lhs
    genMulLow32(AC, AC, T1);
}

// `long long` multiply, low 64 bits of the 128-bit product: result = lo64(lhsLo*rhsLo) +
// (low32(lhsLo*rhsHi + lhsHi*rhsLo) << 32), the standard cross-term decomposition - see this
// backend's own header comment.
void Tms320C6713Codegen::genWideMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    genPushWide(AC, T1);
    genExpr(rhs);
    genPushWide(AC, T1);
    // stack layout (relative to current sp): +0 rhs_lo, +4 rhs_hi, +8 lhs_lo, +12 lhs_hi
    loadStack(AC, 8);
    loadStack(T1, 4);
    genMulLow32(T5, AC, T1);  // T5 = crossA = lhsLo*rhsHi (low32)
    loadStack(AC, 12);
    loadStack(T1, 0);
    genMulLow32(T2, AC, T1);  // T2 = crossB = lhsHi*rhsLo (low32)
    emit("ADD", cat(T5, ",", T5, ",", T2));  // T5 = crossSum

    loadStack(AC, 8);
    loadStack(T1, 0);
    genMulFull32(AC, T1, AC, T1);  // AC:T1 = full lhsLo*rhsLo (lo:hi)
    emit("ADD", cat(T1, ",", T1, ",", T5));

    adjustReg(SP, 16);
}

// `long long` add/sub: stash lhs on the stack while rhs evaluates, then ripple carry/borrow
// across the word pair via CMPLTU (there's no carry flag on this ISA either).
void Tms320C6713Codegen::genWideAdd(const ir::Expr &lhs, const ir::Expr &rhs, bool isAdd) {
    genExpr(lhs);
    genPushWide(AC, T1);
    genExpr(rhs);  // AC:T1 = rhs
    emit("ADD", cat(T3, ",", AC, ",", ZERO));
    emit("ADD", cat(T4, ",", T1, ",", ZERO));
    genPopWide(AC, T1);  // AC:T1 = lhs
    if (isAdd) {
        emit("ADD", cat(T5, ",", AC, ",", T3));
        emit("CMPLTU", cat(T6, ",", T5, ",", AC));
        emit("ADD", cat(T1, ",", T1, ",", T4));
        emit("ADD", cat(T1, ",", T1, ",", T6));
        emit("ADD", cat(AC, ",", T5, ",", ZERO));
    } else {
        emit("CMPLTU", cat(T6, ",", AC, ",", T3));
        emit("SUB", cat(T5, ",", AC, ",", T3));
        emit("SUB", cat(T1, ",", T1, ",", T4));
        emit("SUB", cat(T1, ",", T1, ",", T6));
        emit("ADD", cat(AC, ",", T5, ",", ZERO));
    }
}

// A `long long` shifted by a runtime count: a loop of 1-bit shifts, each carrying the bit
// across the word boundary by hand (SHL/SHR/SHRU only ever shift one 32-bit word). The value
// being shifted lives in T4:T5 throughout (never AC:T1), since the loop-exit test needs AC free
// to hold the predicate each iteration.
void Tms320C6713Codegen::genWideShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned) {
    genExpr(lhs);
    genPushWide(AC, T1);
    genExpr(rhs);  // AC = shift count
    emit("ADD", cat(T3, ",", AC, ",", ZERO));
    genPopWide(T4, T5);  // T4:T5 = value (lo:hi)

    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("CMPEQ", cat(AC, ",", T3, ",", ZERO));
    emitBranchZ(false, Ldone);
    loadImm32(T10, -1);
    emit("ADD", cat(T3, ",", T3, ",", T10));
    if (isLeft) {
        loadImm32(T10, 31);
        emit("SHRU", cat(T6, ",", T4, ",", T10));  // carry from lo's top bit
        loadImm32(T10, 1);
        emit("SHL", cat(T4, ",", T4, ",", T10));
        emit("SHL", cat(T5, ",", T5, ",", T10));
        emit("OR", cat(T5, ",", T5, ",", T6));
    } else {
        loadImm32(T10, 1);
        emit("AND", cat(T6, ",", T5, ",", T10));  // hi's bit0
        loadImm32(T10, 31);
        emit("SHL", cat(T6, ",", T6, ",", T10));  // move into lo's top-bit position
        loadImm32(T10, 1);
        emit((isUnsigned ? "SHRU" : "SHR"), cat(T5, ",", T5, ",", T10));
        emit("SHRU", cat(T4, ",", T4, ",", T10));
        emit("OR", cat(T4, ",", T4, ",", T6));
    }
    emitBranch(Lloop);
    emitLabel(Ldone);
    emit("ADD", cat(AC, ",", T4, ",", ZERO));
    emit("ADD", cat(T1, ",", T5, ",", ZERO));
}

// Negates AC:T1 (two's complement) in place.
void Tms320C6713Codegen::genNegate64() {
    loadImm32(T10, -1);
    emit("XOR", cat(AC, ",", AC, ",", T10));
    emit("XOR", cat(T1, ",", T1, ",", T10));
    loadImm32(T10, 1);
    emit("ADD", cat(AC, ",", AC, ",", T10));
    emit("CMPEQ", cat(T11, ",", AC, ",", ZERO));  // AC wrapped to 0 -> carry
    emit("ADD", cat(T1, ",", T1, ",", T11));
}

// --- compare/logical ---------------------------------------------------------

void Tms320C6713Codegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs) {
    bool isUnsigned = lhs.type.isUnsigned;
    genExpr(lhs);
    genPush(AC);
    genExpr(rhs);  // AC = rhs
    genPop(T1);     // T1 = lhs
    if (op == "<") {
        emit((isUnsigned ? "CMPLTU" : "CMPLT"), cat(AC, ",", T1, ",", AC));
        return;
    }
    if (op == ">") {
        emit((isUnsigned ? "CMPGTU" : "CMPGT"), cat(AC, ",", T1, ",", AC));
        return;
    }
    if (op == "<=") {
        emit((isUnsigned ? "CMPGTU" : "CMPGT"), cat(T10, ",", T1, ",", AC));
        loadImm32(T11, 1);
        emit("XOR", cat(AC, ",", T10, ",", T11));
        return;
    }
    if (op == ">=") {
        emit((isUnsigned ? "CMPLTU" : "CMPLT"), cat(T10, ",", T1, ",", AC));
        loadImm32(T11, 1);
        emit("XOR", cat(AC, ",", T10, ",", T11));
        return;
    }
    if (op == "==") {
        emit("CMPEQ", cat(AC, ",", T1, ",", AC));
        return;
    }
    if (op == "!=") {
        emit("CMPEQ", cat(T10, ",", T1, ",", AC));
        loadImm32(T11, 1);
        emit("XOR", cat(AC, ",", T10, ",", T11));
        return;
    }
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

// Compares lo-then-hi for ==/!= (either word differing is enough), hi-then-lo (falling back to
// an unsigned lo compare only once the hi words match) for an ordered comparison - same shape
// as MipsCodegen.cpp's own genWideCompare(). lhs/rhs are moved out of AC/T1 immediately (into
// T6:T7/T3:T4) so AC stays free to hold each comparison's own predicate test.
void Tms320C6713Codegen::genWideCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs,
                                         bool isUnsigned) {
    genExpr(lhs);
    genPushWide(AC, T1);
    genExpr(rhs);  // AC:T1 = rhs
    emit("ADD", cat(T3, ",", AC, ",", ZERO));
    emit("ADD", cat(T4, ",", T1, ",", ZERO));
    genPopWide(AC, T1);  // AC:T1 = lhs
    emit("ADD", cat(T6, ",", AC, ",", ZERO));
    emit("ADD", cat(T7, ",", T1, ",", ZERO));

    if (op == "==" || op == "!=") {
        std::string Lfalse = newLabel(), Ldone = newLabel();
        const char *onEqual = op == "==" ? "1" : "0";
        const char *onDiffer = op == "==" ? "0" : "1";
        emit("CMPEQ", cat(AC, ",", T6, ",", T3));
        emitBranchZ(true, Lfalse);
        emit("CMPEQ", cat(AC, ",", T7, ",", T4));
        emitBranchZ(true, Lfalse);
        loadImm32(AC, atoi(onEqual));
        emitBranch(Ldone);
        emitLabel(Lfalse);
        loadImm32(AC, atoi(onDiffer));
        emitLabel(Ldone);
        return;
    }

    std::string LhiDiff = newLabel(), Ldone = newLabel();
    emit("CMPEQ", cat(AC, ",", T7, ",", T4));
    emitBranchZ(true, LhiDiff);
    // hi equal: unsigned lo compare
    if (op == "<") {
        emit("CMPLTU", cat(AC, ",", T6, ",", T3));
    } else if (op == ">") {
        emit("CMPGTU", cat(AC, ",", T6, ",", T3));
    } else if (op == "<=") {
        emit("CMPGTU", cat(T10, ",", T6, ",", T3));
        loadImm32(T11, 1);
        emit("XOR", cat(AC, ",", T10, ",", T11));
    } else {  // ">="
        emit("CMPLTU", cat(T10, ",", T6, ",", T3));
        loadImm32(T11, 1);
        emit("XOR", cat(AC, ",", T10, ",", T11));
    }
    emitBranch(Ldone);
    emitLabel(LhiDiff);
    const char *ltInsn = isUnsigned ? "CMPLTU" : "CMPLT";
    const char *gtInsn = isUnsigned ? "CMPGTU" : "CMPGT";
    if (op == "<") {
        emit(ltInsn, cat(AC, ",", T7, ",", T4));
    } else if (op == ">") {
        emit(gtInsn, cat(AC, ",", T7, ",", T4));
    } else if (op == "<=") {
        emit(gtInsn, cat(T10, ",", T7, ",", T4));
        loadImm32(T11, 1);
        emit("XOR", cat(AC, ",", T10, ",", T11));
    } else {  // ">="
        emit(ltInsn, cat(T10, ",", T7, ",", T4));
        loadImm32(T11, 1);
        emit("XOR", cat(AC, ",", T10, ",", T11));
    }
    emitLabel(Ldone);
}

void Tms320C6713Codegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    if (isWide(lhs.type)) emit("OR", cat(AC, ",", AC, ",", T1));
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    emitBranchZ(true, Lfalse);
    genExpr(rhs);
    if (isWide(rhs.type)) emit("OR", cat(AC, ",", AC, ",", T1));
    emitBranchZ(true, Lfalse);
    emitBranch(Ltrue);
    emitLabel(Lfalse);
    loadImm32(AC, 0);
    emitBranch(Ldone);
    emitLabel(Ltrue);
    loadImm32(AC, 1);
    emitLabel(Ldone);
}

void Tms320C6713Codegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    if (isWide(lhs.type)) emit("OR", cat(AC, ",", AC, ",", T1));
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    emitBranchZ(false, Ltrue);
    genExpr(rhs);
    if (isWide(rhs.type)) emit("OR", cat(AC, ",", AC, ",", T1));
    emitBranchZ(false, Ltrue);
    emitBranch(Lfalse);
    emitLabel(Ltrue);
    loadImm32(AC, 1);
    emitBranch(Ldone);
    emitLabel(Lfalse);
    loadImm32(AC, 0);
    emitLabel(Ldone);
}

void Tms320C6713Codegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    if (isWide(rhs.type)) emit("OR", cat(AC, ",", AC, ",", T1));
    emit("CMPEQ", cat(AC, ",", AC, ",", ZERO));
}

// --- division ---------------------------------------------------------------

void Tms320C6713Codegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    genPush(AC);
    genExpr(rhs);  // AC = divisor
    emit("ADD", cat(T1, ",", AC, ",", ZERO));
    genPop(AC);  // AC = dividend
    genDivModCore(wantRemainder, isUnsigned);
}

// Bit-serial restoring division (no hardware divide at all on this target - see this backend's
// header comment), mirroring AlphaCodegen.cpp's own genDivModCore() but for a fixed 32-bit
// width. Entry: AC=dividend, T1=divisor. The dividend/divisor/remainder/quotient/signs all live
// in T2..T9 throughout (never AC/T1), since the loop's own exit test and sign checks need AC
// free to hold each predicate in turn.
void Tms320C6713Codegen::genDivModCore(bool wantRemainder, bool isUnsigned) {
    emit("ADD", cat(T3, ",", AC, ",", ZERO));  // T3 = dividend (n)
    emit("ADD", cat(T4, ",", T1, ",", ZERO));  // T4 = divisor (d)

    std::string LskipNegN = newLabel(), LskipNegD = newLabel();
    if (!isUnsigned) {
        loadImm32(T10, 31);
        emit("SHRU", cat(T7, ",", T3, ",", T10));  // T7 = dividend sign
        emit("ADD", cat(AC, ",", T7, ",", ZERO));
        emitBranchZ(true, LskipNegN);
        emit("SUB", cat(T3, ",", ZERO, ",", T3));
        emitLabel(LskipNegN);
        emit("SHRU", cat(T8, ",", T4, ",", T10));  // T8 = divisor sign
        emit("ADD", cat(AC, ",", T8, ",", ZERO));
        emitBranchZ(true, LskipNegD);
        emit("SUB", cat(T4, ",", ZERO, ",", T4));
        emitLabel(LskipNegD);
    }

    loadImm32(T5, 0);   // r
    loadImm32(T6, 0);    // q
    loadImm32(T9, 32);    // cnt

    std::string Lloop = newLabel(), Ldone = newLabel();
    std::string LcheckCmp = newLabel(), LsubYes = newLabel(), LsubNo = newLabel(), Lcont = newLabel();
    emitLabel(Lloop);
    emit("CMPEQ", cat(AC, ",", T9, ",", ZERO));
    emitBranchZ(false, Ldone);
    loadImm32(T10, -1);
    emit("ADD", cat(T9, ",", T9, ",", T10));

    loadImm32(T10, 31);
    emit("SHRU", cat(T11, ",", T3, ",", T10));  // dividend's own top bit
    loadImm32(T10, 31);
    emit("SHRU", cat(T1, ",", T5, ",", T10));  // remainder's own top bit (overflow
                                                                        // flag) - T1 is free here: its own
                                                                        // entry value (the divisor) was
                                                                        // already copied into T4 above, and
                                                                        // T2 must NOT be used (genCompoundOp's
                                                                        // own caller, genCompoundThroughT2,
                                                                        // keeps the live target address there
                                                                        // across this whole call)
    loadImm32(T10, 1);
    emit("SHL", cat(T3, ",", T3, ",", T10));  // n <<= 1
    loadImm32(T10, 1);
    emit("SHL", cat(T5, ",", T5, ",", T10));  // r <<= 1
    emit("OR", cat(T5, ",", T5, ",", T11));  // r |= n's captured top bit

    emit("ADD", cat(AC, ",", T1, ",", ZERO));
    emitBranchZ(true, LcheckCmp);
    emitBranch(LsubYes);
    emitLabel(LcheckCmp);
    emit("CMPLTU", cat(AC, ",", T5, ",", T4));  // r < d ?
    emitBranchZ(false, LsubNo);
    emitLabel(LsubYes);
    emit("SUB", cat(T5, ",", T5, ",", T4));
    loadImm32(T10, 1);
    emit("SHL", cat(T6, ",", T6, ",", T10));
    emit("OR", cat(T6, ",", T6, ",", T10));
    emitBranch(Lcont);
    emitLabel(LsubNo);
    loadImm32(T10, 1);
    emit("SHL", cat(T6, ",", T6, ",", T10));
    emitLabel(Lcont);
    emitBranch(Lloop);
    emitLabel(Ldone);

    if (!isUnsigned) {
        std::string LnoNegQ = newLabel(), LnoNegR = newLabel();
        emit("XOR", cat(AC, ",", T7, ",", T8));
        emitBranchZ(true, LnoNegQ);
        emit("SUB", cat(T6, ",", ZERO, ",", T6));
        emitLabel(LnoNegQ);
        emit("ADD", cat(AC, ",", T7, ",", ZERO));
        emitBranchZ(true, LnoNegR);
        emit("SUB", cat(T5, ",", ZERO, ",", T5));
        emitLabel(LnoNegR);
    }
    emit("ADD", cat(AC, ",", (wantRemainder ? T5 : T6), ",", ZERO));
}

// 64-bit counterpart of genDivModCore(), the same algorithm run on register pairs kept in a
// small stack-relative scratch block (same technique as MipsCodegen.cpp's own
// genWideDivMod()), since there aren't enough registers to hold n/d/r/q (8 32-bit words) plus
// loop temporaries at once. Field layout (byte offsets from the reserved block's own base):
// 0 n_lo, 4 n_hi, 8 d_lo, 12 d_hi, 16 r_lo, 20 r_hi, 24 q_lo, 28 q_hi, 32 cnt, 36 lhsSign,
// 40 rhsSign.
void Tms320C6713Codegen::genWideDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder,
                                        bool isUnsigned) {
    adjustReg(SP, -44);
    genExpr(lhs);
    storeStack(AC, 0);
    storeStack(T1, 4);
    genExpr(rhs);
    storeStack(AC, 8);
    storeStack(T1, 12);
    storeStack(ZERO, 16);
    storeStack(ZERO, 20);
    storeStack(ZERO, 24);
    storeStack(ZERO, 28);
    loadImm32(T3, 64);
    storeStack(T3, 32);

    std::string Lskip1 = newLabel(), Lskip2 = newLabel();
    if (!isUnsigned) {
        loadImm32(T10, 31);
        loadStack(AC, 4);
        emit("SHRU", cat(T3, ",", AC, ",", T10));
        storeStack(T3, 36);
        loadStack(AC, 12);
        emit("SHRU", cat(T4, ",", AC, ",", T10));
        storeStack(T4, 40);

        emit("ADD", cat(AC, ",", T3, ",", ZERO));
        emitBranchZ(true, Lskip1);
        loadStack(AC, 0);
        loadStack(T1, 4);
        genNegate64();
        storeStack(AC, 0);
        storeStack(T1, 4);
        emitLabel(Lskip1);

        emit("ADD", cat(AC, ",", T4, ",", ZERO));
        emitBranchZ(true, Lskip2);
        loadStack(AC, 8);
        loadStack(T1, 12);
        genNegate64();
        storeStack(AC, 8);
        storeStack(T1, 12);
        emitLabel(Lskip2);
    }

    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    loadStack(T3, 32);
    emit("CMPEQ", cat(AC, ",", T3, ",", ZERO));
    emitBranchZ(false, Ldone);
    loadImm32(T10, -1);
    emit("ADD", cat(T3, ",", T3, ",", T10));
    storeStack(T3, 32);

    // capture n's top bit, then n <<= 1 (64-bit)
    loadStack(T4, 4);
    loadImm32(T10, 31);
    emit("SHRU", cat(T5, ",", T4, ",", T10));  // n's top bit
    loadStack(T6, 0);
    loadImm32(T10, 31);
    emit("SHRU", cat(T7, ",", T6, ",", T10));  // n_lo's own top bit (carries into hi)
    loadImm32(T10, 1);
    emit("SHL", cat(T6, ",", T6, ",", T10));
    loadImm32(T10, 1);
    emit("SHL", cat(T4, ",", T4, ",", T10));
    emit("OR", cat(T4, ",", T4, ",", T7));
    storeStack(T6, 0);
    storeStack(T4, 4);

    // r = (r<<1)|T5, capturing r's own (pre-shift) top bit as the overflow flag T9
    loadStack(T8, 20);
    loadImm32(T10, 31);
    emit("SHRU", cat(T9, ",", T8, ",", T10));  // overflow flag
    loadStack(T11, 16);
    loadImm32(T10, 31);
    emit("SHRU", cat(T2, ",", T11, ",", T10));  // r_lo's own top bit (carries into hi)
    loadImm32(T10, 1);
    emit("SHL", cat(T11, ",", T11, ",", T10));
    emit("OR", cat(T11, ",", T11, ",", T5));
    loadImm32(T10, 1);
    emit("SHL", cat(T8, ",", T8, ",", T10));
    emit("OR", cat(T8, ",", T8, ",", T2));
    storeStack(T11, 16);
    storeStack(T8, 20);

    std::string LcheckCmp = newLabel(), LloCmp = newLabel(), LcmpDone = newLabel();
    std::string LsubYes = newLabel(), LsubNo = newLabel(), Lcont = newLabel();
    emit("ADD", cat(AC, ",", T9, ",", ZERO));
    emitBranchZ(true, LcheckCmp);
    emitBranch(LsubYes);
    emitLabel(LcheckCmp);
    // unsigned 64-bit compare: r (now at 16/20) >= d (at 8/12)
    loadStack(T4, 20);
    loadStack(T6, 12);
    emit("CMPEQ", cat(AC, ",", T4, ",", T6));
    emitBranchZ(false, LloCmp);
    emit("CMPGTU", cat(AC, ",", T4, ",", T6));
    emitBranch(LcmpDone);
    emitLabel(LloCmp);
    loadStack(T5, 16);  // r_lo
    loadStack(T7, 8);    // d_lo
    emit("CMPLTU", cat(T10, ",", T5, ",", T7));  // r_lo < d_lo ?
    loadImm32(T11, 1);
    emit("XOR", cat(AC, ",", T10, ",", T11));  // AC = r_lo >= d_lo
    emitLabel(LcmpDone);
    emitBranchZ(true, LsubNo);
    emitLabel(LsubYes);
    loadStack(T4, 16);
    loadStack(T6, 8);
    emit("CMPLTU", cat(T9, ",", T4, ",", T6));
    emit("SUB", cat(T5, ",", T4, ",", T6));
    loadStack(T4, 20);
    loadStack(T6, 12);
    emit("SUB", cat(T4, ",", T4, ",", T6));
    emit("SUB", cat(T4, ",", T4, ",", T9));
    storeStack(T5, 16);
    storeStack(T4, 20);
    loadStack(T6, 24);
    loadStack(T7, 28);
    loadImm32(T10, 31);
    emit("SHRU", cat(T11, ",", T6, ",", T10));
    loadImm32(T10, 1);
    emit("SHL", cat(T6, ",", T6, ",", T10));
    emit("OR", cat(T6, ",", T6, ",", T10));
    emit("SHL", cat(T7, ",", T7, ",", T10));
    emit("OR", cat(T7, ",", T7, ",", T11));
    storeStack(T6, 24);
    storeStack(T7, 28);
    emitBranch(Lcont);
    emitLabel(LsubNo);
    loadStack(T6, 24);
    loadStack(T7, 28);
    loadImm32(T10, 31);
    emit("SHRU", cat(T11, ",", T6, ",", T10));
    loadImm32(T10, 1);
    emit("SHL", cat(T6, ",", T6, ",", T10));
    emit("SHL", cat(T7, ",", T7, ",", T10));
    emit("OR", cat(T7, ",", T7, ",", T11));
    storeStack(T6, 24);
    storeStack(T7, 28);
    emitLabel(Lcont);
    emitBranch(Lloop);
    emitLabel(Ldone);

    if (!isUnsigned) {
        std::string LnoNegQ = newLabel(), LnoNegR = newLabel();
        loadStack(T3, 36);
        loadStack(T4, 40);
        emit("XOR", cat(AC, ",", T3, ",", T4));
        emitBranchZ(true, LnoNegQ);
        loadStack(AC, 24);
        loadStack(T1, 28);
        genNegate64();
        storeStack(AC, 24);
        storeStack(T1, 28);
        emitLabel(LnoNegQ);
        loadStack(T3, 36);
        emit("ADD", cat(AC, ",", T3, ",", ZERO));
        emitBranchZ(true, LnoNegR);
        loadStack(AC, 16);
        loadStack(T1, 20);
        genNegate64();
        storeStack(AC, 16);
        storeStack(T1, 20);
        emitLabel(LnoNegR);
    }

    if (wantRemainder) {
        loadStack(AC, 16);
        loadStack(T1, 20);
    } else {
        loadStack(AC, 24);
        loadStack(T1, 28);
    }
    adjustReg(SP, 44);
}

// --- binary dispatch --------------------------------------------------------

void Tms320C6713Codegen::genCompoundOp(const std::string &op, Type type) {
    bool isUnsigned = type.isUnsigned;
    if (op == "+") {
        emit("ADD", cat(AC, ",", AC, ",", T1));
        return;
    }
    if (op == "-") {
        emit("SUB", cat(AC, ",", AC, ",", T1));
        return;
    }
    if (op == "*") {
        genMulLow32(AC, AC, T1);
        return;
    }
    if (op == "&") {
        emit("AND", cat(AC, ",", AC, ",", T1));
        return;
    }
    if (op == "|") {
        emit("OR", cat(AC, ",", AC, ",", T1));
        return;
    }
    if (op == "^") {
        emit("XOR", cat(AC, ",", AC, ",", T1));
        return;
    }
    if (op == "<<" || op == ">>") {
        const char *insn = op == "<<" ? "SHL" : (isUnsigned ? "SHRU" : "SHR");
        emit(insn, cat(AC, ",", AC, ",", T1));
        return;
    }
    // "/" or "%"
    genDivModCore(op == "%", isUnsigned);
}

void Tms320C6713Codegen::genCompoundThroughT2(const ir::Expr &e) {
    genPush(T2);
    genExpr(*e.rhs);
    emit("ADD", cat(T1, ",", AC, ",", ZERO));
    genPop(T2);
    loadOperand(T2, e.type);
    if (e.yieldOld) genPush(AC);
    genCompoundOp(e.op, e.type);
    narrowB0To(e.type);
    if (e.yieldOld) {
        emit("ADD", cat(T1, ",", AC, ",", ZERO));
        genPop(AC);
        emit("ADD", cat(T3, ",", AC, ",", ZERO));
        emit("ADD", cat(AC, ",", T1, ",", ZERO));
        storeOperand(T2, e.type);
        emit("ADD", cat(AC, ",", T3, ",", ZERO));
    } else {
        storeOperand(T2, e.type);
    }
}

void Tms320C6713Codegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    Type operandType = e.lhs->type;
    bool wide = isWide(operandType);
    bool isUnsigned = operandType.isUnsigned;
    if (op == "+" || op == "-") {
        if (wide) {
            genWideAdd(*e.lhs, *e.rhs, op == "+");
            return;
        }
        genExpr(*e.lhs);
        genPush(AC);
        genExpr(*e.rhs);
        genPop(T1);  // T1 = lhs, AC = rhs
        emit((op == "+" ? "ADD" : "SUB"), cat(AC, ",", T1, ",", AC));
        narrowB0To(operandType);
        return;
    }
    if (op == "*") {
        if (wide) {
            genWideMul(*e.lhs, *e.rhs);
            return;
        }
        genMul(*e.lhs, *e.rhs);
        narrowB0To(operandType);
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
        const char *insn = op == "&" ? "AND" : op == "|" ? "OR" : "XOR";
        if (wide) {
            genExpr(*e.lhs);
            genPushWide(AC, T1);
            genExpr(*e.rhs);
            emit("ADD", cat(T3, ",", AC, ",", ZERO));
            emit("ADD", cat(T4, ",", T1, ",", ZERO));
            genPopWide(AC, T1);
            emit(insn, cat(AC, ",", AC, ",", T3));
            emit(insn, cat(T1, ",", T1, ",", T4));
            return;
        }
        genExpr(*e.lhs);
        genPush(AC);
        genExpr(*e.rhs);
        genPop(T1);
        emit(insn, cat(AC, ",", T1, ",", AC));
        return;
    }
    if (op == "<<" || op == ">>") {
        if (wide) {
            genWideShift(*e.lhs, *e.rhs, op == "<<", isUnsigned);
            return;
        }
        genExpr(*e.lhs);
        genPush(AC);
        genExpr(*e.rhs);  // AC = shift count
        emit("ADD", cat(T1, ",", AC, ",", ZERO));
        genPop(AC);  // AC = value
        const char *insn = op == "<<" ? "SHL" : (isUnsigned ? "SHRU" : "SHR");
        emit(insn, cat(AC, ",", AC, ",", T1));
        narrowB0To(operandType);
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
    if (wide) {
        genWideCompare(op, *e.lhs, *e.rhs, isUnsigned);
        return;
    }
    genCompare(op, *e.lhs, *e.rhs);
}

std::optional<std::string> Tms320C6713Codegen::assembleInProcess(const std::string &asmText,
                                                                   const std::string &format) const {
    if (format == "raw") return assembleTms320C6713(asmText);
    return std::nullopt;
}

}  // namespace rcc
