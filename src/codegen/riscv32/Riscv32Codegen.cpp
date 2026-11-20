#include "Riscv32Codegen.h"

#include <cstdint>
#include <stdexcept>

#include "Riscv32Assembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

// Where the whole program loads; the test harness points the simulated
// CPU's PC here directly - the RISC-V `virt` QEMU machine's RAM starts
// exactly here.
constexpr uint32_t kBaseAddress = 0x80000000u;

// Initial sp, comfortably above the loaded program and its data.
constexpr uint32_t kInitialSP = 0x80100000u;

}  // namespace

void Riscv32Codegen::addImm(const std::string &dst, const std::string &src, int32_t v) {
    if (v >= -2048 && v <= 2047) {
        emit("addi", cat(dst, ",", src, ",", v));
        return;
    }
    emit("li", cat("s1,", v));
    emit("add", cat(dst, ",", src, ",s1"));
}

// A literal element's decimal text, or a symbol element's referenced
// Slot's/Function's label written out bare.
std::string Riscv32Codegen::arrayElemText(const ir::ArrayInitElem &e) {
    if (!e.isSymbol) return std::to_string(static_cast<int32_t>(static_cast<uint32_t>(e.value)));
    return e.slot ? label(e.slot) : label(e.func);
}

void Riscv32Codegen::declareArray(const std::string &lbl, const std::vector<ir::ArrayInitElem> &values,
                                   Type elemType) {
    if (values.empty()) return;
    int w = widthOfType(elemType);
    // No 64-bit data directive; a `long long` element is two "dw"s
    // (low word, then high) instead.
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

void Riscv32Codegen::loadAddress(const std::string &reg, const std::string &lbl) {
    emit("lui", cat(reg, ",", lbl));
    emit("addi", cat(reg, ",", reg, ",", lbl));
}

void Riscv32Codegen::loadImm32(const std::string &reg, int32_t v) { emit("li", cat(reg, ",", v)); }

// A Param lives at fp+frameOffset (caller's pushed argument space); a
// Local's address is its lowest one (see ir::localLowOffset()), its later
// bytes above it. No bracketed memory operand exists in this dialect, so
// the address is computed with a single add.
void Riscv32Codegen::loadSlotAddr(const std::string &reg, const ir::Slot *slot) {
    if (slot->kind == ir::SlotKind::Global) {
        loadAddress(reg, label(slot));
        return;
    }
    int offset = slot->kind == ir::SlotKind::Param ? slot->frameOffset : -ir::localLowOffset(*slot, typeWidths());
    addImm(reg, "fp", offset);
}

std::string Riscv32Codegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    // In objectMode, `prog` may not define `main`; the entry sequence
    // below is skipped since Riscv32LinkerFactory.cpp synthesizes it once
    // every object is linked. An object's base is `org 0`; the linker
    // places it.
    emitRaw(cat("org ", (objectMode ? 0 : kBaseAddress)));
    emitRaw("");
    if (!objectMode) {
        emitLabel("_start");
        emit("li", cat("sp,", static_cast<int32_t>(kInitialSP)));
        emit("call", label(mainFn));
        // main's result sits in t0 once execution reaches halt.
        emitLabel("halt");
        emit("j", "halt");
        emitRaw("");
    }

    for (const auto &fb : prog.functionBodies) genFunction(fb);

    // RV32I raises a misaligned-access exception on some implementations
    // for a halfword/word access, so each dh/dw global needs padding
    // bytes first to reach its own natural alignment, same as MIPS.
    size_t globalByteOffset = 0;
    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        // An external global isn't defined in this file; skip reserving
        // storage so references stay as undefined symbols/relocations.
        if (slot->isExternal) continue;
        const std::string lbl = label(slot.get());
        int elemW = widthOfType(slot->type);
        size_t totalBytes = slot->arraySize > 0 ? static_cast<size_t>(slot->arraySize) * elemW
                                                  : static_cast<size_t>(elemW);
        size_t align = elemW >= 4 ? 4 : static_cast<size_t>(elemW);
        while (align > 1 && (globalByteOffset % align) != 0) {
            trailer_.push_back("db 0  ; alignment padding");
            globalByteOffset += 1;
        }
        globalByteOffset += totalBytes;
        if (slot->arraySize > 0) {
            declareArray(lbl, slot->arrayInit, slot->type);
        } else if (slot->type.base == BaseType::Struct) {
            // No struct initializer exists in this subset, so this is
            // always zero-filled; reuses declareArray's own element-list
            // emission (one zero "db" per byte) rather than a new
            // directive, since this assembler dialect has no "reserve N
            // zero bytes" form of its own.
            declareArray(lbl, std::vector<ir::ArrayInitElem>(slot->type.structSize), Type{BaseType::Char, false});
        } else {
            long v = slot->hasInit ? slot->initValue : 0;
            int w = widthOfType(slot->type);
            if (w == 8) {
                auto uv = static_cast<uint64_t>(v);
                trailer_.push_back(lbl + ": dw " + std::to_string(static_cast<uint32_t>(uv)) + ", " +
                                    std::to_string(static_cast<uint32_t>(uv >> 32)) + "  ; " + slot->sourceName);
            } else {
                const char *directive = w == 1 ? "db" : w == 2 ? "dh" : "dw";
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

std::map<std::string, std::string> Riscv32Codegen::perFileAsm() const { return perFileAsm_; }

// Every function saves/restores `ra`, leaf or not: RV32 has exactly one
// link register, so a non-leaf function's own call would otherwise
// overwrite it before `ret` returns. The prologue also pushes the
// caller's `fp` and points `fp` at that saved word, giving this call
// (including recursive ones) its own frame; locals are then reserved by
// lowering `sp` another frameSize bytes.
void Riscv32Codegen::genFunction(const ir::FunctionBody &fn) {
    emitLabel(label(fn.func));
    emit("addi", "sp,sp,-4");
    emit("sw", "ra,sp,0");
    emit("addi", "sp,sp,-4");
    emit("sw", "fp,sp,0");
    emit("add", "fp,sp,zero");
    if (fn.func->frameSize > 0) addImm("sp", "sp", -fn.func->frameSize);

    genStmt(*fn.body);

    genEpilogue();
    emitRaw("");
}

// Deallocates locals (`sp` back up to `fp`), restores the caller's
// `fp`/`ra` from the two words `fp` points at, then returns. `s1` briefly
// holds the saved `fp` across restoring `ra`.
void Riscv32Codegen::genEpilogue() {
    emit("add", "sp,fp,zero");
    emit("lw", "s1,sp,0");
    emit("addi", "sp,sp,4");
    emit("lw", "ra,sp,0");
    emit("addi", "sp,sp,4");
    emit("add", "fp,s1,zero");
    emit("ret");
}

void Riscv32Codegen::genStmt(const ir::Stmt &s) {
    switch (s.kind) {
        case ir::StmtKind::Block: {
            for (const auto &child : s.body) genStmt(*child);
            return;
        }
        case ir::StmtKind::If: {
            genExpr(*s.cond);
            if (isWide(s.cond->type)) emit("or", "t0,t0,t1");
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            emit("beq", cat("t0,zero,", (s.elseStmt ? Lelse : Ldone)));
            genStmt(*s.thenStmt);
            if (s.elseStmt) {
                emit("j", Ldone);
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
            if (isWide(s.cond->type)) emit("or", "t0,t0,t1");
            emit("beq", cat("t0,zero,", Lend));
            loopLabels_.push_back({Lcont, Lend});
            genStmt(*s.loopBody);
            loopLabels_.pop_back();
            if (s.postStmt) {
                emitLabel(Lcont);
                genStmt(*s.postStmt);
            }
            emit("j", Lstart);
            emitLabel(Lend);
            return;
        }
        case ir::StmtKind::Break: {
            emit("j", loopLabels_.back().second);
            return;
        }
        case ir::StmtKind::Continue: {
            emit("j", loopLabels_.back().first);
            return;
        }
        case ir::StmtKind::Return: {
            if (s.initOrReturnOrExpr) genExpr(*s.initOrReturnOrExpr);
            // Early exit, bypassing the fall-through epilogue; genEpilogue()
            // doesn't touch t0, where the return value already sits.
            genEpilogue();
            return;
        }
        case ir::StmtKind::LocalDecl: {
            if (s.declSlot->arraySize > 0) {
                genLocalArrayInit(s.declSlot);
            } else if (s.initOrReturnOrExpr) {
                genExpr(*s.initOrReturnOrExpr);
                loadSlotAddr("t2", s.declSlot);
                storeOperand("t2", s.declSlot->type);
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
            if (!s.asmText.empty() && s.asmText.back() != '\n') emitRaw("");
            return;
        }
        case ir::StmtKind::Label: {
            emitLabel(irLabel(s.labelId));
            return;
        }
        case ir::StmtKind::Goto: {
            emit("j", irLabel(s.labelId));
            return;
        }
    }
}

void Riscv32Codegen::genExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit: {
            if (isWide(e.type)) {
                auto v = static_cast<uint64_t>(e.intValue);
                loadImm32("t0", static_cast<int32_t>(static_cast<uint32_t>(v)));
                loadImm32("t1", static_cast<int32_t>(static_cast<uint32_t>(v >> 32)));
                return;
            }
            loadImm32("t0", static_cast<int32_t>(static_cast<uint32_t>(e.intValue)));
            return;
        }
        case ir::ExprKind::Var: {
            loadSlotAddr("t2", e.slot);
            loadOperand("t2", e.slot->type);
            return;
        }
        case ir::ExprKind::Index: {
            genIndex(e);
            return;
        }
        case ir::ExprKind::Assign: {
            if (e.lhs) {  // slot[idx] = rhs
                genIndexAddr(e.slot, *e.lhs);  // t2 = target address
                emit("addi", "sp,sp,-4");
                emit("sw", "t2,sp,0");
                genExpr(*e.rhs);
                emit("lw", "t2,sp,0");
                emit("addi", "sp,sp,4");
                storeOperand("t2", e.slot->type);
                return;
            }
            genExpr(*e.rhs);
            loadSlotAddr("t2", e.slot);
            storeOperand("t2", e.slot->type);
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            if (e.lhs) {  // slot[idx] op= rhs (including slot[idx]++/--)
                genIndexAddr(e.slot, *e.lhs);  // t2 = target address
                genCompoundThroughT2(e);
                return;
            }
            // RV32I has no absolute-addressing memory operand, so even a
            // scalar target's address is computed first.
            loadSlotAddr("t2", e.slot);
            genCompoundThroughT2(e);
            return;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            genExpr(*e.lhs);  // t0 = pointer value
            emit("add", "t2,t0,zero");  // t2 = address
            genCompoundThroughT2(e);
            return;
        }
        case ir::ExprKind::Ternary: {
            genExpr(*e.lhs);
            // The condition's type can still be wide even though the
            // ternary's result type isn't, hence the OR-then-test.
            if (isWide(e.lhs->type)) emit("or", "t0,t0,t1");
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            emit("beq", cat("t0,zero,", Lelse));
            genExpr(*e.rhs);
            emit("j", Ldone);
            emitLabel(Lelse);
            genExpr(*e.elseExpr);
            emitLabel(Ldone);
            return;
        }
        case ir::ExprKind::DerefAssign: {
            // Stores at e.type's width/signedness (the pointee type, or
            // native-int-width for untyped-int-as-address).
            genExpr(*e.lhs);  // t0 = pointer value
            emit("addi", "sp,sp,-4");
            emit("sw", "t0,sp,0");
            genExpr(*e.rhs);  // t0, or t1:t0 if wide
            emit("lw", "t2,sp,0");
            emit("addi", "sp,sp,4");
            storeOperand("t2", e.type);
            return;
        }
        case ir::ExprKind::Unary: {
            if (e.op == "-") {
                genExpr(*e.rhs);
                if (isWide(e.rhs->type)) {
                    genNegate64();
                } else {
                    emit("sub", "t0,zero,t0");
                }
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                // Loads at e.type's width/signedness.
                genExpr(*e.rhs);  // t0 = pointer value
                emit("add", "t2,t0,zero");
                loadOperand("t2", e.type);
            } else if (e.op == "~") {
                genExpr(*e.rhs);
                emit("xori", "t0,t0,-1");
                if (isWide(e.rhs->type)) emit("xori", "t1,t1,-1");
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
            // cdecl-style: push args right-to-left onto sp (arg 0 lands at
            // fp+8+4*i once the callee's prologue runs), caller cleans up
            // after the call returns. A wide argument's low word lands at
            // the lower address, matching loadOperand()'s own convention
            // for reading a wide Param slot back (low word at +0, high at
            // +4), so the push order here must put the low word first.
            int totalBytes = 0;
            for (size_t i = e.args.size(); i-- > 0;) {
                genExpr(*e.args[i]);
                if (isWide(e.args[i]->type)) {
                    emit("addi", "sp,sp,-8");
                    emit("sw", "t0,sp,0");
                    emit("sw", "t1,sp,4");
                    totalBytes += 8;
                } else {
                    emit("addi", "sp,sp,-4");
                    emit("sw", "t0,sp,0");
                    totalBytes += 4;
                }
            }
            emit("call", label(e.func));
            if (totalBytes > 0) addImm("sp", "sp", totalBytes);
            return;
        }
        case ir::ExprKind::CallPtr: {
            // `jalr ra,t2` jumps to the address in t2 and leaves the
            // return address in ra, like `call` for a runtime-known target.
            genExpr(*e.lhs);  // t0 = the callee's own entry address
            emit("add", "t2,t0,zero");
            emit("jalr", "ra,t2");
            return;
        }
        case ir::ExprKind::Binary: {
            genBinary(e);
            return;
        }
        case ir::ExprKind::Intrinsic: {
            // No exception/interrupt setup on this bare-metal target; no-op.
            (void)e;
            return;
        }
        case ir::ExprKind::Func:
            err(e.line, "internal: Func reached genExpr - should only ever appear as '&'s operand");
    }
}

// t2 ends up holding the computed address. arraySize > 0 means a
// compile-time-known array base (Global label or Local fp-relative);
// otherwise `slot` is a pointer variable, so the base is read from its
// stored value first.
//
// Both a Global and a Local array's element addresses increase with
// the index (base+4*i).
void Riscv32Codegen::genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
    genExpr(idxExpr);  // t0 = index
    int shift = elemShift(slot->type);
    if (shift > 0) {
        emit("slli", cat("t0,t0,", shift));
    } else if (shift < 0) {
        // Non-power-of-two element width (a struct's): scale by a real multiply instead.
        loadImm32("t3", widthOfType(slot->type));
        emit("mul", "t0,t0,t3");
    }
    if (slot->arraySize > 0) {
        if (slot->kind == ir::SlotKind::Global) {
            loadAddress("t2", label(slot));
            emit("add", "t2,t2,t0");
        } else {
            loadSlotAddr("t2", slot);
            emit("add", "t2,t2,t0");
        }
    } else {
        loadSlotAddr("t1", slot);
        emit("lw", "t1,t1,0");  // t1 = the pointer variable's own stored value
        emit("add", "t2,t1,t0");
    }
}

void Riscv32Codegen::genIndex(const ir::Expr &e) {
    genIndexAddr(e.slot, *e.lhs);
    loadOperand("t2", e.slot->type);
}

// Stores a Local array's compile-time initializer values into its frame
// slots, re-run on every call since the storage is freshly reserved each
// time. Element i lives w*i bytes above element 0.
void Riscv32Codegen::genLocalArrayInit(const ir::Slot *slot) {
    int w = widthOfType(slot->type);
    for (size_t i = 0; i < slot->arrayInit.size(); i++) {
        const ir::ArrayInitElem &elem = slot->arrayInit[i];
        int offset = -ir::localLowOffset(*slot, typeWidths()) + w * static_cast<int>(i);
        if (w == 8 && !elem.isSymbol) {
            // A long long element is split across both words.
            auto v = static_cast<uint64_t>(elem.value);
            loadImm32("t0", static_cast<int32_t>(static_cast<uint32_t>(v)));
            addImm("t2", "fp", offset);
            emit("sw", "t0,t2,0");
            loadImm32("t0", static_cast<int32_t>(static_cast<uint32_t>(v >> 32)));
            addImm("t2", "fp", offset + 4);
            emit("sw", "t0,t2,0");
            continue;
        }
        if (elem.isSymbol) {
            loadAddress("t0", elem.slot ? label(elem.slot) : label(elem.func));
        } else {
            loadImm32("t0", static_cast<int32_t>(static_cast<uint32_t>(elem.value)));
        }
        addImm("t2", "fp", offset);
        if (w == 1) {
            emit("sb", "t0,t2,0");
        } else if (w == 2) {
            emit("sh", "t0,t2,0");
        } else {
            emit("sw", "t0,t2,0");
        }
    }
}

void Riscv32Codegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        loadSlotAddr("t2", operand.slot);
        emit("add", "t0,t2,zero");
        return;
    }
    if (operand.kind == ir::ExprKind::Index) {
        genIndexAddr(operand.slot, *operand.lhs);
        emit("add", "t0,t2,zero");
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        loadAddress("t2", label(operand.func));
        emit("add", "t0,t2,zero");
        return;
    }
    err(operand.line, "internal: unhandled addressable-operand kind");
}

void Riscv32Codegen::loadOperand(const std::string &addrReg, Type type) {
    int w = widthOfType(type);
    if (w == 1) {
        emit((type.isUnsigned ? "lbu" : "lb"), cat("t0,", addrReg, ",0"));
    } else if (w == 2) {
        emit((type.isUnsigned ? "lhu" : "lh"), cat("t0,", addrReg, ",0"));
    } else if (w == 4) {
        emit("lw", cat("t0,", addrReg, ",0"));
    } else {  // 8 - long long: t1:t0 (hi:lo), low word first
        emit("lw", cat("t0,", addrReg, ",0"));
        emit("lw", cat("t1,", addrReg, ",4"));
    }
}

void Riscv32Codegen::storeOperand(const std::string &addrReg, Type type) {
    int w = widthOfType(type);
    if (w == 1) {
        emit("sb", cat("t0,", addrReg, ",0"));
    } else if (w == 2) {
        emit("sh", cat("t0,", addrReg, ",0"));
    } else if (w == 4) {
        emit("sw", cat("t0,", addrReg, ",0"));
    } else {
        emit("sw", cat("t0,", addrReg, ",0"));
        emit("sw", cat("t1,", addrReg, ",4"));
    }
}

void Riscv32Codegen::narrowT0To(Type to) {
    int w = widthOfType(to);
    if (w == 1) {
        emit("slli", "t0,t0,24");
        emit((to.isUnsigned ? "srli" : "srai"), "t0,t0,24");
    } else if (w == 2) {
        emit("slli", "t0,t0,16");
        emit((to.isUnsigned ? "srli" : "srai"), "t0,t0,16");
    }
    // 4 bytes (Int/Long): t0 already holds the full value.
}

// Crosses the native-int/`long long` boundary (sign/zero-extending t0
// into t1, or dropping t1), or re-truncates t0 for a narrower target.
void Riscv32Codegen::genConvert(const ir::Expr &e) {
    Type from = e.lhs->type;
    Type to = e.type;
    bool fromWide = isWide(from);
    bool toWide = isWide(to);
    genExpr(*e.lhs);  // t0, or t1:t0 if fromWide
    if (fromWide && !toWide) {
        narrowT0To(to);  // t1 is simply discarded
    } else if (!fromWide && toWide) {
        if (from.isUnsigned) {
            emit("add", "t1,zero,zero");
        } else {
            emit("srai", "t1,t0,31");  // sign-extend t0 into t1
        }
    } else if (!fromWide && !toWide) {
        narrowT0To(to);
    }
    // fromWide && toWide: the bit pattern is already correct.
}

// `long long` add/sub: stash lhs on the stack while rhs evaluates, then
// ripple carry/borrow across the word pair. RV32I has no carry flag, so
// the carry out of the low-word add is recovered via sltu.
void Riscv32Codegen::genWideAdd(const ir::Expr &lhs, const ir::Expr &rhs, bool isAdd) {
    genExpr(lhs);  // t1:t0 = lhs
    emit("addi", "sp,sp,-8");
    emit("sw", "t0,sp,0");
    emit("sw", "t1,sp,4");
    genExpr(rhs);  // t1:t0 = rhs
    emit("add", "t3,t0,zero");
    emit("add", "t4,t1,zero");  // t3:t4 = rhs (lo:hi)
    emit("lw", "t0,sp,0");
    emit("lw", "t1,sp,4");
    emit("addi", "sp,sp,8");  // t1:t0 = lhs
    if (isAdd) {
        emit("add", "t5,t0,t3");           // t5 = lo sum
        emit("sltu", "t6,t5,t0");           // t6 = carry out
        emit("add", "t1,t1,t4");
        emit("add", "t1,t1,t6");
        emit("add", "t0,t5,zero");
    } else {
        emit("sltu", "t6,t0,t3");           // t6 = borrow
        emit("sub", "t5,t0,t3");
        emit("sub", "t1,t1,t4");
        emit("sub", "t1,t1,t6");
        emit("add", "t0,t5,zero");
    }
}

// `long long` multiply, keeping the low 64 bits of the 128-bit product
// via the standard 32x32 cross-multiply decomposition; each cross term's
// own high half only affects bit 64+ and is discarded, so a plain `mul`
// (low 32 bits only) is all either cross term needs.
void Riscv32Codegen::genWideMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);  // t1:t0 = lhs
    emit("addi", "sp,sp,-8");
    emit("sw", "t0,sp,0");
    emit("sw", "t1,sp,4");
    genExpr(rhs);  // t1:t0 = rhs
    emit("add", "t5,t0,zero");
    emit("add", "t6,t1,zero");  // t5:t6 = rhs (lo:hi)
    emit("lw", "t3,sp,0");
    emit("lw", "t4,sp,4");
    emit("addi", "sp,sp,8");  // t3:t4 = lhs (lo:hi)
    emit("mul", "t0,t3,t6");  // lhs_lo*rhs_hi
    emit("addi", "sp,sp,-4");
    emit("sw", "t0,sp,0");
    emit("mul", "t0,t4,t5");  // lhs_hi*rhs_lo
    emit("addi", "sp,sp,-4");
    emit("sw", "t0,sp,0");
    emit("mul", "t0,t3,t5");
    emit("mulhu", "t1,t3,t5");  // lhs_lo*rhs_lo (final result_lo/carry)
    emit("lw", "t6,sp,0");
    emit("addi", "sp,sp,4");   // crossB_lo (lhs_hi*rhs_lo)
    emit("lw", "t5,sp,0");
    emit("addi", "sp,sp,4");   // crossA_lo (lhs_lo*rhs_hi)
    emit("add", "t1,t1,t6");
    emit("add", "t1,t1,t5");
}

void Riscv32Codegen::genNegate64() {
    emit("xori", "t0,t0,-1");
    emit("xori", "t1,t1,-1");
    emit("addi", "t0,t0,1");
    emit("sltiu", "t3,t0,1");  // t3 = 1 if t0 wrapped to 0 (i.e. the +1 carried out)
    emit("add", "t1,t1,t3");
}

// Bit-serial shift/compare/subtract long division, using sp-relative
// scratch (lw/sw already take a displacement). Signed division operates
// on magnitudes: each negative operand is negated in place first, then
// the sign is restored on the result per C's truncating rule.
void Riscv32Codegen::genWideDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    emit("addi", "sp,sp,-8");
    emit("sw", "t1,sp,0");
    emit("sw", "t0,sp,4");  // [sp]=lhs_hi,[sp+4]=lhs_lo
    genExpr(rhs);
    emit("addi", "sp,sp,-8");
    emit("sw", "t1,sp,0");
    emit("sw", "t0,sp,4");  // [sp]=rhs_hi,[sp+4]=rhs_lo
    emit("addi", "sp,sp,-36");
    // Scratch layout at sp+0: n_lo,n_hi,d_lo,d_hi,r_lo,r_hi,q_lo,q_hi,cnt (9 words, 36 bytes).
    // The two operands were stashed lhs-then-rhs (so rhs, pushed last, sits closest to sp):
    // rhs_hi/rhs_lo at +36/+40, lhs_hi/lhs_lo at +44/+48 - offsets 40/44 become dead space
    // once this copy-in finishes and are reused below for lhsSign/rhsSign.
    emit("lw", "t0,sp,48");
    emit("sw", "t0,sp,0");   // n_lo = lhs_lo
    emit("lw", "t0,sp,44");
    emit("sw", "t0,sp,4");   // n_hi = lhs_hi
    emit("lw", "t0,sp,40");
    emit("sw", "t0,sp,8");   // d_lo = rhs_lo
    emit("lw", "t0,sp,36");
    emit("sw", "t0,sp,12");  // d_hi = rhs_hi
    emit("sw", "zero,sp,16");
    emit("sw", "zero,sp,20");  // r = 0
    emit("sw", "zero,sp,24");
    emit("sw", "zero,sp,28");  // q = 0
    emit("addi", "t0,zero,64");
    emit("sw", "t0,sp,32");  // cnt = 64: one 1-bit shift per bit of the 64-bit operand

    if (!isUnsigned) {
        emit("lw", "t0,sp,4");
        emit("srli", "t0,t0,31");
        emit("sw", "t0,sp,40");    // [sp+40] = lhsSign
        emit("lw", "t0,sp,12");
        emit("srli", "t0,t0,31");
        emit("sw", "t0,sp,44");   // [sp+44] = rhsSign
        std::string LskipNegN = newLabel(), LskipNegD = newLabel();
        emit("lw", "t0,sp,40");
        emit("beq", cat("t0,zero,", LskipNegN));
        emit("lw", "t0,sp,0");
        emit("lw", "t1,sp,4");
        genNegate64();
        emit("sw", "t0,sp,0");
        emit("sw", "t1,sp,4");
        emitLabel(LskipNegN);
        emit("lw", "t0,sp,44");
        emit("beq", cat("t0,zero,", LskipNegD));
        emit("lw", "t0,sp,8");
        emit("lw", "t1,sp,12");
        genNegate64();
        emit("sw", "t0,sp,8");
        emit("sw", "t1,sp,12");
        emitLabel(LskipNegD);
    }

    std::string Lloop = newLabel(), Ldone = newLabel();
    std::string LhiDiff = newLabel(), LsubYes = newLabel(), LsubNo = newLabel(), LshiftQ = newLabel();
    emitLabel(Lloop);
    emit("lw", "t0,sp,32");
    emit("beq", cat("t0,zero,", Ldone));
    emit("addi", "t0,t0,-1");
    emit("sw", "t0,sp,32");

    // n <<= 1 (0-filled), capturing its own old top bit into t3.
    emit("lw", "t0,sp,4");
    emit("srli", "t0,t0,31");
    emit("add", "t3,t0,zero");
    emit("lw", "t0,sp,0");
    emit("srli", "t0,t0,31");
    emit("add", "t4,t0,zero");
    emit("lw", "t0,sp,0");
    emit("slli", "t0,t0,1");
    emit("sw", "t0,sp,0");
    emit("lw", "t0,sp,4");
    emit("slli", "t0,t0,1");
    emit("or", "t0,t0,t4");
    emit("sw", "t0,sp,4");

    // r = (r << 1) | (n's old top bit, in t3).
    emit("lw", "t0,sp,16");
    emit("srli", "t0,t0,31");
    emit("add", "t5,t0,zero");
    emit("lw", "t0,sp,16");
    emit("slli", "t0,t0,1");
    emit("or", "t0,t0,t3");
    emit("sw", "t0,sp,16");
    emit("lw", "t0,sp,20");
    emit("slli", "t0,t0,1");
    emit("or", "t0,t0,t5");
    emit("sw", "t0,sp,20");

    // if (r >= d) { r -= d; quotient bit = 1 } else { quotient bit = 0 }
    emit("lw", "t0,sp,20");
    emit("lw", "t1,sp,12");
    emit("bne", cat("t0,t1,", LhiDiff));
    emit("lw", "t0,sp,16");
    emit("lw", "t1,sp,8");
    emit("sltu", "t3,t0,t1");
    emit("beq", cat("t3,zero,", LsubYes));
    emit("j", LsubNo);
    emitLabel(LhiDiff);
    emit("sltu", "t3,t0,t1");
    emit("beq", cat("t3,zero,", LsubYes));
    emit("j", LsubNo);
    emitLabel(LsubYes);
    emit("lw", "t0,sp,16");
    emit("lw", "t1,sp,8");
    emit("sltu", "t6,t0,t1");
    emit("sub", "t0,t0,t1");
    emit("sw", "t0,sp,16");
    emit("lw", "t0,sp,20");
    emit("lw", "t1,sp,12");
    emit("sub", "t0,t0,t1");
    emit("sub", "t0,t0,t6");
    emit("sw", "t0,sp,20");
    emit("addi", "t3,zero,1");
    emit("j", LshiftQ);
    emitLabel(LsubNo);
    emit("add", "t3,zero,zero");
    emitLabel(LshiftQ);

    // q = (q << 1) | quotient bit (in t3).
    emit("lw", "t0,sp,24");
    emit("srli", "t0,t0,31");
    emit("add", "t5,t0,zero");
    emit("lw", "t0,sp,24");
    emit("slli", "t0,t0,1");
    emit("or", "t0,t0,t3");
    emit("sw", "t0,sp,24");
    emit("lw", "t0,sp,28");
    emit("slli", "t0,t0,1");
    emit("or", "t0,t0,t5");
    emit("sw", "t0,sp,28");
    emit("j", Lloop);

    emitLabel(Ldone);
    if (!isUnsigned) {
        std::string LskipNegQ = newLabel(), LskipNegR = newLabel();
        emit("lw", "t3,sp,40");
        emit("lw", "t0,sp,44");
        emit("xor", "t3,t3,t0");
        emit("beq", cat("t3,zero,", LskipNegQ));
        emit("lw", "t0,sp,24");
        emit("lw", "t1,sp,28");
        genNegate64();
        emit("sw", "t0,sp,24");
        emit("sw", "t1,sp,28");
        emitLabel(LskipNegQ);
        emit("lw", "t3,sp,40");
        emit("beq", cat("t3,zero,", LskipNegR));
        emit("lw", "t0,sp,16");
        emit("lw", "t1,sp,20");
        genNegate64();
        emit("sw", "t0,sp,16");
        emit("sw", "t1,sp,20");
        emitLabel(LskipNegR);
    }
    emit("lw", "t0,sp,24");
    emit("lw", "t1,sp,28");  // quotient
    emit("lw", "t4,sp,16");
    emit("lw", "t5,sp,20");  // remainder (saved past the scratch release)
    emit("addi", "sp,sp,52");  // release the 9-word scratch + the 4 earlier stashed operand words
    if (wantRemainder) {
        emit("add", "t0,t4,zero");
        emit("add", "t1,t5,zero");
    }
}

// A `long long` shifted by a runtime count: a loop of 1-bit shifts, each
// carrying the bit across the word boundary by hand (`sll`/`srl` only
// shift one 32-bit word).
void Riscv32Codegen::genWideShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned) {
    genExpr(lhs);
    emit("addi", "sp,sp,-8");
    emit("sw", "t0,sp,0");
    emit("sw", "t1,sp,4");
    genExpr(rhs);  // t0 = shift count (already promoted to a plain int)
    emit("add", "t3,t0,zero");
    emit("lw", "t0,sp,0");
    emit("lw", "t1,sp,4");
    emit("addi", "sp,sp,8");  // t1:t0 restored = the value being shifted
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("beq", cat("t3,zero,", Ldone));
    emit("addi", "t3,t3,-1");
    if (isLeft) {
        emit("srli", "t4,t0,31");       // t4 = t0's old top bit, carries into t1
        emit("slli", "t0,t0,1");
        emit("slli", "t1,t1,1");
        emit("or", "t1,t1,t4");
    } else {
        emit("slli", "t4,t1,31");       // t4 = (t1&1)<<31, carries into t0's top
        emit("srli", "t0,t0,1");
        emit("or", "t0,t0,t4");
        emit((isUnsigned ? "srli" : "srai"), "t1,t1,1");
    }
    emit("j", Lloop);
    emitLabel(Ldone);
}

// A `long long` comparison: compare high words first (signed/unsigned
// per isUnsigned), falling back to an unsigned low-word compare only
// when the high words are equal.
void Riscv32Codegen::genWideCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs,
                                     bool isUnsigned) {
    genExpr(lhs);
    emit("addi", "sp,sp,-8");
    emit("sw", "t0,sp,0");
    emit("sw", "t1,sp,4");
    genExpr(rhs);
    emit("add", "t5,t0,zero");
    emit("add", "t6,t1,zero");  // t5:t6 = rhs (lo:hi)
    emit("lw", "t3,sp,0");
    emit("lw", "t4,sp,4");
    emit("addi", "sp,sp,8");  // t3:t4 = lhs (lo:hi)
    if (op == "==" || op == "!=") {
        std::string Lfalse = newLabel(), Ldone = newLabel();
        const char *onEqual = op == "==" ? "1" : "0";
        const char *onDiffer = op == "==" ? "0" : "1";
        emit("bne", cat("t3,t5,", Lfalse));
        emit("bne", cat("t4,t6,", Lfalse));
        emit("addi", cat("t0,zero,", onEqual));
        emit("j", Ldone);
        emitLabel(Lfalse);
        emit("addi", cat("t0,zero,", onDiffer));
        emitLabel(Ldone);
        return;
    }
    // Once the high words are equal, the low 32 bits are an unsigned
    // magnitude regardless of the overall type's signedness.
    std::string LhiDiff = newLabel(), Ldone = newLabel();
    emit("bne", cat("t4,t6,", LhiDiff));
    if (op == "<") {
        emit("sltu", "t0,t3,t5");
    } else if (op == ">") {
        emit("sltu", "t0,t5,t3");
    } else if (op == "<=") {
        emit("sltu", "t0,t5,t3");
        emit("addi", "t1,zero,1");
        emit("xor", "t0,t1,t0");  // !(rhs<lhs)
    } else {  // ">="
        emit("sltu", "t0,t3,t5");
        emit("addi", "t1,zero,1");
        emit("xor", "t0,t1,t0");  // !(lhs<rhs)
    }
    emit("j", Ldone);
    emitLabel(LhiDiff);
    const char *hiInsn = isUnsigned ? "sltu" : "slt";
    if (op == "<") {
        emit(hiInsn, "t0,t4,t6");
    } else if (op == ">") {
        emit(hiInsn, "t0,t6,t4");
    } else if (op == "<=") {
        emit(hiInsn, "t0,t6,t4");
        emit("addi", "t1,zero,1");
        emit("xor", "t0,t1,t0");
    } else {  // ">="
        emit(hiInsn, "t0,t4,t6");
        emit("addi", "t1,zero,1");
        emit("xor", "t0,t1,t0");
    }
    emitLabel(Ldone);
}

// Entry: t0 = old, t1 = rhs (both at `type`'s width/signedness). Neither
// `mul` nor `div`/`rem` touch t2, so the address genCompoundThroughT2()
// holds there survives this call.
void Riscv32Codegen::genCompoundOp(const std::string &op, Type type) {
    bool isUnsigned = type.isUnsigned;
    if (op == "+") { emit("add", "t0,t0,t1"); return; }
    if (op == "-") { emit("sub", "t0,t0,t1"); return; }
    if (op == "*") {
        emit("mul", "t0,t0,t1");
        return;
    }
    if (op == "&") { emit("and", "t0,t0,t1"); return; }
    if (op == "|") { emit("or", "t0,t0,t1"); return; }
    if (op == "^") { emit("xor", "t0,t0,t1"); return; }
    if (op == "<<" || op == ">>") {
        const char *insn = op == "<<" ? "sll" : (isUnsigned ? "srl" : "sra");
        emit(insn, "t0,t0,t1");
        return;
    }
    // "/" or "%"
    const char *insn = op == "%" ? (isUnsigned ? "remu" : "rem") : (isUnsigned ? "divu" : "div");
    emit(insn, "t0,t0,t1");
}

// t2 must already hold the resolved address on entry; it's stashed on
// the stack across evaluating e.rhs (which may clobber t0-t4) and never
// recomputed, so the target's address sub-expression runs exactly once.
void Riscv32Codegen::genCompoundThroughT2(const ir::Expr &e) {
    emit("addi", "sp,sp,-4");
    emit("sw", "t2,sp,0");  // stash address
    genExpr(*e.rhs);                                     // t0 = rhs
    emit("add", "t1,t0,zero");                      // t1 = rhs
    emit("lw", "t2,sp,0");
    emit("addi", "sp,sp,4");         // t2 = address (restored)
    loadOperand("t2", e.type);                                 // t0 = old
    if (e.yieldOld) {
        emit("addi", "sp,sp,-4");
        emit("sw", "t0,sp,0");  // stash old
    }
    genCompoundOp(e.op, e.type);                                 // t0 = new (raw)
    narrowT0To(e.type);
    if (e.yieldOld) {
        emit("add", "t1,t0,zero");                          // t1 = new
        emit("lw", "t0,sp,0");
        emit("addi", "sp,sp,4");             // t0 = old
        emit("add", "t3,t0,zero");                              // t3 = old, preserved across the store below
        emit("add", "t0,t1,zero");                                // t0 = new - storeOperand always stores from t0
        storeOperand("t2", e.type);
        emit("add", "t0,t3,zero");                                  // t0 = old (final result)
    } else {
        storeOperand("t2", e.type);  // t0 already = new
    }
}

void Riscv32Codegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    // Both operands share one common type (IRBuilder's usual-
    // arithmetic-conversion pass), except shift (only LHS matters)
    // and &&/|| (width-independent, handled below).
    Type operandType = e.lhs->type;
    bool wide = isWide(operandType);
    bool isUnsigned = operandType.isUnsigned;
    if (op == "+" || op == "-") {
        if (wide) {
            genWideAdd(*e.lhs, *e.rhs, op == "+");
            return;
        }
        genExpr(*e.lhs);
        emit("addi", "sp,sp,-4");
        emit("sw", "t0,sp,0");
        genExpr(*e.rhs);
        emit("lw", "t1,sp,0");
        emit("addi", "sp,sp,4");  // t1 = lhs, t0 = rhs
        if (op == "+") {
            emit("add", "t0,t1,t0");
        } else {
            emit("sub", "t0,t1,t0");
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
        const char *insn = op == "&" ? "and" : op == "|" ? "or" : "xor";
        if (wide) {
            genExpr(*e.lhs);
            emit("addi", "sp,sp,-8");
            emit("sw", "t0,sp,0");
            emit("sw", "t1,sp,4");
            genExpr(*e.rhs);
            emit("add", "t3,t0,zero");
            emit("add", "t4,t1,zero");
            emit("lw", "t0,sp,0");
            emit("lw", "t1,sp,4");
            emit("addi", "sp,sp,8");
            emit(insn, "t0,t0,t3");
            emit(insn, "t1,t1,t4");
            return;
        }
        genExpr(*e.lhs);
        emit("addi", "sp,sp,-4");
        emit("sw", "t0,sp,0");
        genExpr(*e.rhs);
        emit("lw", "t1,sp,0");
        emit("addi", "sp,sp,4");
        emit(insn, "t0,t1,t0");
        return;
    }
    if (op == "<<" || op == ">>") {
        if (wide) {
            genWideShift(*e.lhs, *e.rhs, op == "<<", isUnsigned);
            return;
        }
        genExpr(*e.lhs);
        emit("addi", "sp,sp,-4");
        emit("sw", "t0,sp,0");
        genExpr(*e.rhs);  // t0 = shift count
        emit("add", "t1,t0,zero");
        emit("lw", "t0,sp,0");
        emit("addi", "sp,sp,4");  // t0 = value to shift
        const char *insn = op == "<<" ? "sll" : (isUnsigned ? "srl" : "sra");
        emit(insn, "t0,t0,t1");
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

// Real hardware multiply (`mul`), a full 32x32->32 product - the same
// instruction regardless of signedness, since the low 32 bits of a
// two's-complement product never depend on it.
void Riscv32Codegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    emit("addi", "sp,sp,-4");
    emit("sw", "t0,sp,0");
    genExpr(rhs);
    emit("lw", "t1,sp,0");
    emit("addi", "sp,sp,4");  // t1 = lhs, t0 = rhs
    emit("mul", "t0,t1,t0");
}

// Real hardware divide/remainder (`div`/`divu`/`rem`/`remu`) - no
// `mfhi`/`mflo` step, unlike MIPS.
void Riscv32Codegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    emit("addi", "sp,sp,-4");
    emit("sw", "t0,sp,0");
    genExpr(rhs);
    emit("lw", "t1,sp,0");
    emit("addi", "sp,sp,4");  // t1 = lhs (dividend), t0 = rhs (divisor)
    const char *insn = wantRemainder ? (isUnsigned ? "remu" : "rem") : (isUnsigned ? "divu" : "div");
    emit(insn, "t0,t1,t0");
}

// `slt`/`sltu` directly produce the 0/1 boolean needed, no branch
// required. isUnsigned picks `slt` vs `sltu` for the ordered forms;
// ==/!= need no such choice.
void Riscv32Codegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs) {
    if (isWide(lhs.type)) {
        genWideCompare(op, lhs, rhs, lhs.type.isUnsigned);
        return;
    }
    bool isUnsigned = lhs.type.isUnsigned;
    const char *sltInsn = isUnsigned ? "sltu" : "slt";
    genExpr(lhs);
    emit("addi", "sp,sp,-4");
    emit("sw", "t0,sp,0");
    genExpr(rhs);
    emit("lw", "t1,sp,0");
    emit("addi", "sp,sp,4");  // t1 = lhs, t0 = rhs
    if (op == "<") {
        emit(sltInsn, "t0,t1,t0");  // t0 = (lhs < rhs)
        return;
    }
    if (op == ">") {
        emit(sltInsn, "t0,t0,t1");  // t0 = (rhs < lhs) = (lhs > rhs)
        return;
    }
    if (op == "<=") {
        emit(sltInsn, "s1,t0,t1");           // s1 = (rhs < lhs)
        emit("addi", "t0,zero,1");
        emit("xor", "t0,t0,s1");  // t0 = !(rhs<lhs) = (lhs<=rhs)
        return;
    }
    if (op == ">=") {
        emit(sltInsn, "s1,t1,t0");   // s1 = (lhs < rhs)
        emit("addi", "t0,zero,1");
        emit("xor", "t0,t0,s1");  // t0 = !(lhs<rhs) = (lhs>=rhs)
        return;
    }
    if (op == "==") {
        emit("sub", "s1,t1,t0");    // s1 = lhs - rhs
        emit("sltiu", "t0,s1,1");    // t0 = (unsigned(s1) < 1) = (s1 == 0)
        return;
    }
    if (op == "!=") {
        emit("sub", "s1,t1,t0");    // s1 = lhs - rhs
        emit("sltu", "t0,zero,s1");  // t0 = (0 < unsigned(s1)) = (s1 != 0)
        return;
    }
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

// A wide operand's truthiness needs both words; `or t0,t0,t1` folds
// them together before the usual zero test.
void Riscv32Codegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    if (isWide(lhs.type)) emit("or", "t0,t0,t1");
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    emit("beq", cat("t0,zero,", Lfalse));
    genExpr(rhs);
    if (isWide(rhs.type)) emit("or", "t0,t0,t1");
    emit("beq", cat("t0,zero,", Lfalse));
    emit("j", Ltrue);
    emitLabel(Lfalse);
    emit("addi", "t0,zero,0");
    emit("j", Ldone);
    emitLabel(Ltrue);
    emit("addi", "t0,zero,1");
    emitLabel(Ldone);
}

void Riscv32Codegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    if (isWide(lhs.type)) emit("or", "t0,t0,t1");
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    emit("bne", cat("t0,zero,", Ltrue));
    genExpr(rhs);
    if (isWide(rhs.type)) emit("or", "t0,t0,t1");
    emit("bne", cat("t0,zero,", Ltrue));
    emit("j", Lfalse);
    emitLabel(Ltrue);
    emit("addi", "t0,zero,1");
    emit("j", Ldone);
    emitLabel(Lfalse);
    emit("addi", "t0,zero,0");
    emitLabel(Ldone);
}

// `sltiu t0,t0,1` computes (unsigned(t0) < 1), i.e. logical not, in one
// instruction.
void Riscv32Codegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    if (isWide(rhs.type)) emit("or", "t0,t0,t1");
    emit("sltiu", "t0,t0,1");
}

std::optional<std::string> Riscv32Codegen::assembleInProcess(const std::string &asmText,
                                                               const std::string &format) const {
    if (format == "raw") return assembleRiscv32(asmText);
    // coff/elf/macho/aout/objraw: build one format-neutral ObjectFile,
    // then pick that format's FileFormatWriter to serialize it.
    if (auto kind = fileFormatKindFromName(format)) {
        ObjectFile obj = assembleRiscv32Object(asmText);
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

}  // namespace rcc
