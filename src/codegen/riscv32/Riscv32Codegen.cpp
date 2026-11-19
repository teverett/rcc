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

// A frame offset or `-frameSize` can exceed `addi`'s own 12-bit signed
// range for a large local array, unlike every other (always small,
// literal) stack adjustment in this backend - so this one spot routes
// through `s1` (this dialect's scratch register, transiently reused the
// same way MipsCodegen.h's `$at` is) when the plain immediate wouldn't fit.
void addImm(std::ostringstream &code, const std::string &dst, const std::string &src, int32_t v) {
    if (v >= -2048 && v <= 2047) {
        code << "    addi " << dst << "," << src << "," << v << "\n";
        return;
    }
    code << "    li s1," << v << "\n";
    code << "    add " << dst << "," << src << ",s1\n";
}
}  // namespace

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
    code_ << "    lui " << reg << "," << lbl << "\n";
    code_ << "    addi " << reg << "," << reg << "," << lbl << "\n";
}

void Riscv32Codegen::loadImm32(const std::string &reg, int32_t v) { code_ << "    li " << reg << "," << v << "\n"; }

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
    addImm(code_, reg, "fp", offset);
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
    code_ << "org " << (objectMode ? 0 : kBaseAddress) << "\n\n";
    if (!objectMode) {
        code_ << "_start:\n";
        code_ << "    li sp," << static_cast<int32_t>(kInitialSP) << "\n";
        code_ << "    call " << label(mainFn) << "\n";
        // main's result sits in t0 once execution reaches halt.
        code_ << "halt:\n    j halt\n\n";
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
    code_ << label(fn.func) << ":\n";
    code_ << "    addi sp,sp,-4\n    sw ra,sp,0\n";
    code_ << "    addi sp,sp,-4\n    sw fp,sp,0\n";
    code_ << "    add fp,sp,zero\n";
    if (fn.func->frameSize > 0) addImm(code_, "sp", "sp", -fn.func->frameSize);

    genStmt(*fn.body);

    genEpilogue();
    code_ << "\n";
}

// Deallocates locals (`sp` back up to `fp`), restores the caller's
// `fp`/`ra` from the two words `fp` points at, then returns. `s1` briefly
// holds the saved `fp` across restoring `ra`.
void Riscv32Codegen::genEpilogue() {
    code_ << "    add sp,fp,zero\n";
    code_ << "    lw s1,sp,0\n    addi sp,sp,4\n";
    code_ << "    lw ra,sp,0\n    addi sp,sp,4\n";
    code_ << "    add fp,s1,zero\n";
    code_ << "    ret\n";
}

void Riscv32Codegen::genStmt(const ir::Stmt &s) {
    switch (s.kind) {
        case ir::StmtKind::Block: {
            for (const auto &child : s.body) genStmt(*child);
            return;
        }
        case ir::StmtKind::If: {
            genExpr(*s.cond);
            if (isWide(s.cond->type)) code_ << "    or t0,t0,t1\n";
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            code_ << "    beq t0,zero," << (s.elseStmt ? Lelse : Ldone) << "\n";
            genStmt(*s.thenStmt);
            if (s.elseStmt) {
                code_ << "    j " << Ldone << "\n";
                code_ << Lelse << ":\n";
                genStmt(*s.elseStmt);
            }
            code_ << Ldone << ":\n";
            return;
        }
        case ir::StmtKind::While: {
            std::string Lstart = newLabel();
            std::string Lend = newLabel();
            std::string Lcont = s.postStmt ? newLabel() : Lstart;
            code_ << Lstart << ":\n";
            genExpr(*s.cond);
            if (isWide(s.cond->type)) code_ << "    or t0,t0,t1\n";
            code_ << "    beq t0,zero," << Lend << "\n";
            loopLabels_.push_back({Lcont, Lend});
            genStmt(*s.loopBody);
            loopLabels_.pop_back();
            if (s.postStmt) {
                code_ << Lcont << ":\n";
                genStmt(*s.postStmt);
            }
            code_ << "    j " << Lstart << "\n";
            code_ << Lend << ":\n";
            return;
        }
        case ir::StmtKind::Break: {
            code_ << "    j " << loopLabels_.back().second << "\n";
            return;
        }
        case ir::StmtKind::Continue: {
            code_ << "    j " << loopLabels_.back().first << "\n";
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
            code_ << s.asmText;
            if (!s.asmText.empty() && s.asmText.back() != '\n') code_ << "\n";
            return;
        }
        case ir::StmtKind::Label: {
            code_ << irLabel(s.labelId) << ":\n";
            return;
        }
        case ir::StmtKind::Goto: {
            code_ << "    j " << irLabel(s.labelId) << "\n";
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
                code_ << "    addi sp,sp,-4\n    sw t2,sp,0\n";
                genExpr(*e.rhs);
                code_ << "    lw t2,sp,0\n    addi sp,sp,4\n";
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
            code_ << "    add t2,t0,zero\n";  // t2 = address
            genCompoundThroughT2(e);
            return;
        }
        case ir::ExprKind::Ternary: {
            genExpr(*e.lhs);
            // The condition's type can still be wide even though the
            // ternary's result type isn't, hence the OR-then-test.
            if (isWide(e.lhs->type)) code_ << "    or t0,t0,t1\n";
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            code_ << "    beq t0,zero," << Lelse << "\n";
            genExpr(*e.rhs);
            code_ << "    j " << Ldone << "\n";
            code_ << Lelse << ":\n";
            genExpr(*e.elseExpr);
            code_ << Ldone << ":\n";
            return;
        }
        case ir::ExprKind::DerefAssign: {
            // Stores at e.type's width/signedness (the pointee type, or
            // native-int-width for untyped-int-as-address).
            genExpr(*e.lhs);  // t0 = pointer value
            code_ << "    addi sp,sp,-4\n    sw t0,sp,0\n";
            genExpr(*e.rhs);  // t0, or t1:t0 if wide
            code_ << "    lw t2,sp,0\n    addi sp,sp,4\n";
            storeOperand("t2", e.type);
            return;
        }
        case ir::ExprKind::Unary: {
            if (e.op == "-") {
                genExpr(*e.rhs);
                if (isWide(e.rhs->type)) {
                    genNegate64();
                } else {
                    code_ << "    sub t0,zero,t0\n";
                }
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                // Loads at e.type's width/signedness.
                genExpr(*e.rhs);  // t0 = pointer value
                code_ << "    add t2,t0,zero\n";
                loadOperand("t2", e.type);
            } else if (e.op == "~") {
                genExpr(*e.rhs);
                code_ << "    xori t0,t0,-1\n";
                if (isWide(e.rhs->type)) code_ << "    xori t1,t1,-1\n";
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
                    code_ << "    addi sp,sp,-8\n    sw t0,sp,0\n    sw t1,sp,4\n";
                    totalBytes += 8;
                } else {
                    code_ << "    addi sp,sp,-4\n    sw t0,sp,0\n";
                    totalBytes += 4;
                }
            }
            code_ << "    call " << label(e.func) << "\n";
            if (totalBytes > 0) addImm(code_, "sp", "sp", totalBytes);
            return;
        }
        case ir::ExprKind::CallPtr: {
            // `jalr ra,t2` jumps to the address in t2 and leaves the
            // return address in ra, like `call` for a runtime-known target.
            genExpr(*e.lhs);  // t0 = the callee's own entry address
            code_ << "    add t2,t0,zero\n";
            code_ << "    jalr ra,t2\n";
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
        code_ << "    slli t0,t0," << shift << "\n";
    } else if (shift < 0) {
        // Non-power-of-two element width (a struct's): scale by a real multiply instead.
        loadImm32("t3", widthOfType(slot->type));
        code_ << "    mul t0,t0,t3\n";
    }
    if (slot->arraySize > 0) {
        if (slot->kind == ir::SlotKind::Global) {
            loadAddress("t2", label(slot));
            code_ << "    add t2,t2,t0\n";
        } else {
            loadSlotAddr("t2", slot);
            code_ << "    add t2,t2,t0\n";
        }
    } else {
        loadSlotAddr("t1", slot);
        code_ << "    lw t1,t1,0\n";  // t1 = the pointer variable's own stored value
        code_ << "    add t2,t1,t0\n";
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
            addImm(code_, "t2", "fp", offset);
            code_ << "    sw t0,t2,0\n";
            loadImm32("t0", static_cast<int32_t>(static_cast<uint32_t>(v >> 32)));
            addImm(code_, "t2", "fp", offset + 4);
            code_ << "    sw t0,t2,0\n";
            continue;
        }
        if (elem.isSymbol) {
            loadAddress("t0", elem.slot ? label(elem.slot) : label(elem.func));
        } else {
            loadImm32("t0", static_cast<int32_t>(static_cast<uint32_t>(elem.value)));
        }
        addImm(code_, "t2", "fp", offset);
        if (w == 1) {
            code_ << "    sb t0,t2,0\n";
        } else if (w == 2) {
            code_ << "    sh t0,t2,0\n";
        } else {
            code_ << "    sw t0,t2,0\n";
        }
    }
}

void Riscv32Codegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        loadSlotAddr("t2", operand.slot);
        code_ << "    add t0,t2,zero\n";
        return;
    }
    if (operand.kind == ir::ExprKind::Index) {
        genIndexAddr(operand.slot, *operand.lhs);
        code_ << "    add t0,t2,zero\n";
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        loadAddress("t2", label(operand.func));
        code_ << "    add t0,t2,zero\n";
        return;
    }
    err(operand.line, "internal: unhandled addressable-operand kind");
}

void Riscv32Codegen::loadOperand(const std::string &addrReg, Type type) {
    int w = widthOfType(type);
    if (w == 1) {
        code_ << "    " << (type.isUnsigned ? "lbu" : "lb") << " t0," << addrReg << ",0\n";
    } else if (w == 2) {
        code_ << "    " << (type.isUnsigned ? "lhu" : "lh") << " t0," << addrReg << ",0\n";
    } else if (w == 4) {
        code_ << "    lw t0," << addrReg << ",0\n";
    } else {  // 8 - long long: t1:t0 (hi:lo), low word first
        code_ << "    lw t0," << addrReg << ",0\n";
        code_ << "    lw t1," << addrReg << ",4\n";
    }
}

void Riscv32Codegen::storeOperand(const std::string &addrReg, Type type) {
    int w = widthOfType(type);
    if (w == 1) {
        code_ << "    sb t0," << addrReg << ",0\n";
    } else if (w == 2) {
        code_ << "    sh t0," << addrReg << ",0\n";
    } else if (w == 4) {
        code_ << "    sw t0," << addrReg << ",0\n";
    } else {
        code_ << "    sw t0," << addrReg << ",0\n";
        code_ << "    sw t1," << addrReg << ",4\n";
    }
}

void Riscv32Codegen::narrowT0To(Type to) {
    int w = widthOfType(to);
    if (w == 1) {
        code_ << "    slli t0,t0,24\n";
        code_ << "    " << (to.isUnsigned ? "srli" : "srai") << " t0,t0,24\n";
    } else if (w == 2) {
        code_ << "    slli t0,t0,16\n";
        code_ << "    " << (to.isUnsigned ? "srli" : "srai") << " t0,t0,16\n";
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
            code_ << "    add t1,zero,zero\n";
        } else {
            code_ << "    srai t1,t0,31\n";  // sign-extend t0 into t1
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
    code_ << "    addi sp,sp,-8\n    sw t0,sp,0\n    sw t1,sp,4\n";
    genExpr(rhs);  // t1:t0 = rhs
    code_ << "    add t3,t0,zero\n    add t4,t1,zero\n";  // t3:t4 = rhs (lo:hi)
    code_ << "    lw t0,sp,0\n    lw t1,sp,4\n    addi sp,sp,8\n";  // t1:t0 = lhs
    if (isAdd) {
        code_ << "    add t5,t0,t3\n";           // t5 = lo sum
        code_ << "    sltu t6,t5,t0\n";           // t6 = carry out
        code_ << "    add t1,t1,t4\n    add t1,t1,t6\n";
        code_ << "    add t0,t5,zero\n";
    } else {
        code_ << "    sltu t6,t0,t3\n";           // t6 = borrow
        code_ << "    sub t5,t0,t3\n";
        code_ << "    sub t1,t1,t4\n    sub t1,t1,t6\n";
        code_ << "    add t0,t5,zero\n";
    }
}

// `long long` multiply, keeping the low 64 bits of the 128-bit product
// via the standard 32x32 cross-multiply decomposition; each cross term's
// own high half only affects bit 64+ and is discarded, so a plain `mul`
// (low 32 bits only) is all either cross term needs.
void Riscv32Codegen::genWideMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);  // t1:t0 = lhs
    code_ << "    addi sp,sp,-8\n    sw t0,sp,0\n    sw t1,sp,4\n";
    genExpr(rhs);  // t1:t0 = rhs
    code_ << "    add t5,t0,zero\n    add t6,t1,zero\n";  // t5:t6 = rhs (lo:hi)
    code_ << "    lw t3,sp,0\n    lw t4,sp,4\n    addi sp,sp,8\n";  // t3:t4 = lhs (lo:hi)
    code_ << "    mul t0,t3,t6\n";  // lhs_lo*rhs_hi
    code_ << "    addi sp,sp,-4\n    sw t0,sp,0\n";
    code_ << "    mul t0,t4,t5\n";  // lhs_hi*rhs_lo
    code_ << "    addi sp,sp,-4\n    sw t0,sp,0\n";
    code_ << "    mul t0,t3,t5\n    mulhu t1,t3,t5\n";  // lhs_lo*rhs_lo (final result_lo/carry)
    code_ << "    lw t6,sp,0\n    addi sp,sp,4\n";   // crossB_lo (lhs_hi*rhs_lo)
    code_ << "    lw t5,sp,0\n    addi sp,sp,4\n";   // crossA_lo (lhs_lo*rhs_hi)
    code_ << "    add t1,t1,t6\n    add t1,t1,t5\n";
}

void Riscv32Codegen::genNegate64() {
    code_ << "    xori t0,t0,-1\n    xori t1,t1,-1\n";
    code_ << "    addi t0,t0,1\n";
    code_ << "    sltiu t3,t0,1\n";  // t3 = 1 if t0 wrapped to 0 (i.e. the +1 carried out)
    code_ << "    add t1,t1,t3\n";
}

// Bit-serial shift/compare/subtract long division, using sp-relative
// scratch (lw/sw already take a displacement). Signed division operates
// on magnitudes: each negative operand is negated in place first, then
// the sign is restored on the result per C's truncating rule.
void Riscv32Codegen::genWideDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    code_ << "    addi sp,sp,-8\n    sw t1,sp,0\n    sw t0,sp,4\n";  // [sp]=lhs_hi,[sp+4]=lhs_lo
    genExpr(rhs);
    code_ << "    addi sp,sp,-8\n    sw t1,sp,0\n    sw t0,sp,4\n";  // [sp]=rhs_hi,[sp+4]=rhs_lo
    code_ << "    addi sp,sp,-36\n";
    // Scratch layout at sp+0: n_lo,n_hi,d_lo,d_hi,r_lo,r_hi,q_lo,q_hi,cnt (9 words, 36 bytes).
    // The two operands were stashed lhs-then-rhs (so rhs, pushed last, sits closest to sp):
    // rhs_hi/rhs_lo at +36/+40, lhs_hi/lhs_lo at +44/+48 - offsets 40/44 become dead space
    // once this copy-in finishes and are reused below for lhsSign/rhsSign.
    code_ << "    lw t0,sp,48\n    sw t0,sp,0\n";   // n_lo = lhs_lo
    code_ << "    lw t0,sp,44\n    sw t0,sp,4\n";   // n_hi = lhs_hi
    code_ << "    lw t0,sp,40\n    sw t0,sp,8\n";   // d_lo = rhs_lo
    code_ << "    lw t0,sp,36\n    sw t0,sp,12\n";  // d_hi = rhs_hi
    code_ << "    sw zero,sp,16\n    sw zero,sp,20\n";  // r = 0
    code_ << "    sw zero,sp,24\n    sw zero,sp,28\n";  // q = 0
    code_ << "    addi t0,zero,64\n    sw t0,sp,32\n";  // cnt = 64: one 1-bit shift per bit of the 64-bit operand

    if (!isUnsigned) {
        code_ << "    lw t0,sp,4\n    srli t0,t0,31\n    sw t0,sp,40\n";    // [sp+40] = lhsSign
        code_ << "    lw t0,sp,12\n    srli t0,t0,31\n    sw t0,sp,44\n";   // [sp+44] = rhsSign
        std::string LskipNegN = newLabel(), LskipNegD = newLabel();
        code_ << "    lw t0,sp,40\n    beq t0,zero," << LskipNegN << "\n";
        code_ << "    lw t0,sp,0\n    lw t1,sp,4\n";
        genNegate64();
        code_ << "    sw t0,sp,0\n    sw t1,sp,4\n";
        code_ << LskipNegN << ":\n";
        code_ << "    lw t0,sp,44\n    beq t0,zero," << LskipNegD << "\n";
        code_ << "    lw t0,sp,8\n    lw t1,sp,12\n";
        genNegate64();
        code_ << "    sw t0,sp,8\n    sw t1,sp,12\n";
        code_ << LskipNegD << ":\n";
    }

    std::string Lloop = newLabel(), Ldone = newLabel();
    std::string LhiDiff = newLabel(), LsubYes = newLabel(), LsubNo = newLabel(), LshiftQ = newLabel();
    code_ << Lloop << ":\n";
    code_ << "    lw t0,sp,32\n    beq t0,zero," << Ldone << "\n";
    code_ << "    addi t0,t0,-1\n    sw t0,sp,32\n";

    // n <<= 1 (0-filled), capturing its own old top bit into t3.
    code_ << "    lw t0,sp,4\n    srli t0,t0,31\n    add t3,t0,zero\n";
    code_ << "    lw t0,sp,0\n    srli t0,t0,31\n    add t4,t0,zero\n";
    code_ << "    lw t0,sp,0\n    slli t0,t0,1\n    sw t0,sp,0\n";
    code_ << "    lw t0,sp,4\n    slli t0,t0,1\n    or t0,t0,t4\n    sw t0,sp,4\n";

    // r = (r << 1) | (n's old top bit, in t3).
    code_ << "    lw t0,sp,16\n    srli t0,t0,31\n    add t5,t0,zero\n";
    code_ << "    lw t0,sp,16\n    slli t0,t0,1\n    or t0,t0,t3\n    sw t0,sp,16\n";
    code_ << "    lw t0,sp,20\n    slli t0,t0,1\n    or t0,t0,t5\n    sw t0,sp,20\n";

    // if (r >= d) { r -= d; quotient bit = 1 } else { quotient bit = 0 }
    code_ << "    lw t0,sp,20\n    lw t1,sp,12\n    bne t0,t1," << LhiDiff << "\n";
    code_ << "    lw t0,sp,16\n    lw t1,sp,8\n    sltu t3,t0,t1\n";
    code_ << "    beq t3,zero," << LsubYes << "\n";
    code_ << "    j " << LsubNo << "\n";
    code_ << LhiDiff << ":\n";
    code_ << "    sltu t3,t0,t1\n    beq t3,zero," << LsubYes << "\n";
    code_ << "    j " << LsubNo << "\n";
    code_ << LsubYes << ":\n";
    code_ << "    lw t0,sp,16\n    lw t1,sp,8\n    sltu t6,t0,t1\n    sub t0,t0,t1\n    sw t0,sp,16\n";
    code_ << "    lw t0,sp,20\n    lw t1,sp,12\n    sub t0,t0,t1\n    sub t0,t0,t6\n    sw t0,sp,20\n";
    code_ << "    addi t3,zero,1\n    j " << LshiftQ << "\n";
    code_ << LsubNo << ":\n    add t3,zero,zero\n";
    code_ << LshiftQ << ":\n";

    // q = (q << 1) | quotient bit (in t3).
    code_ << "    lw t0,sp,24\n    srli t0,t0,31\n    add t5,t0,zero\n";
    code_ << "    lw t0,sp,24\n    slli t0,t0,1\n    or t0,t0,t3\n    sw t0,sp,24\n";
    code_ << "    lw t0,sp,28\n    slli t0,t0,1\n    or t0,t0,t5\n    sw t0,sp,28\n";
    code_ << "    j " << Lloop << "\n";

    code_ << Ldone << ":\n";
    if (!isUnsigned) {
        std::string LskipNegQ = newLabel(), LskipNegR = newLabel();
        code_ << "    lw t3,sp,40\n    lw t0,sp,44\n    xor t3,t3,t0\n";
        code_ << "    beq t3,zero," << LskipNegQ << "\n";
        code_ << "    lw t0,sp,24\n    lw t1,sp,28\n";
        genNegate64();
        code_ << "    sw t0,sp,24\n    sw t1,sp,28\n";
        code_ << LskipNegQ << ":\n";
        code_ << "    lw t3,sp,40\n    beq t3,zero," << LskipNegR << "\n";
        code_ << "    lw t0,sp,16\n    lw t1,sp,20\n";
        genNegate64();
        code_ << "    sw t0,sp,16\n    sw t1,sp,20\n";
        code_ << LskipNegR << ":\n";
    }
    code_ << "    lw t0,sp,24\n    lw t1,sp,28\n";  // quotient
    code_ << "    lw t4,sp,16\n    lw t5,sp,20\n";  // remainder (saved past the scratch release)
    code_ << "    addi sp,sp,52\n";  // release the 9-word scratch + the 4 earlier stashed operand words
    if (wantRemainder) {
        code_ << "    add t0,t4,zero\n    add t1,t5,zero\n";
    }
}

// A `long long` shifted by a runtime count: a loop of 1-bit shifts, each
// carrying the bit across the word boundary by hand (`sll`/`srl` only
// shift one 32-bit word).
void Riscv32Codegen::genWideShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned) {
    genExpr(lhs);
    code_ << "    addi sp,sp,-8\n    sw t0,sp,0\n    sw t1,sp,4\n";
    genExpr(rhs);  // t0 = shift count (already promoted to a plain int)
    code_ << "    add t3,t0,zero\n";
    code_ << "    lw t0,sp,0\n    lw t1,sp,4\n    addi sp,sp,8\n";  // t1:t0 restored = the value being shifted
    std::string Lloop = newLabel(), Ldone = newLabel();
    code_ << Lloop << ":\n    beq t3,zero," << Ldone << "\n    addi t3,t3,-1\n";
    if (isLeft) {
        code_ << "    srli t4,t0,31\n";       // t4 = t0's old top bit, carries into t1
        code_ << "    slli t0,t0,1\n";
        code_ << "    slli t1,t1,1\n    or t1,t1,t4\n";
    } else {
        code_ << "    slli t4,t1,31\n";       // t4 = (t1&1)<<31, carries into t0's top
        code_ << "    srli t0,t0,1\n    or t0,t0,t4\n";
        code_ << "    " << (isUnsigned ? "srli" : "srai") << " t1,t1,1\n";
    }
    code_ << "    j " << Lloop << "\n";
    code_ << Ldone << ":\n";
}

// A `long long` comparison: compare high words first (signed/unsigned
// per isUnsigned), falling back to an unsigned low-word compare only
// when the high words are equal.
void Riscv32Codegen::genWideCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs,
                                     bool isUnsigned) {
    genExpr(lhs);
    code_ << "    addi sp,sp,-8\n    sw t0,sp,0\n    sw t1,sp,4\n";
    genExpr(rhs);
    code_ << "    add t5,t0,zero\n    add t6,t1,zero\n";  // t5:t6 = rhs (lo:hi)
    code_ << "    lw t3,sp,0\n    lw t4,sp,4\n    addi sp,sp,8\n";  // t3:t4 = lhs (lo:hi)
    if (op == "==" || op == "!=") {
        std::string Lfalse = newLabel(), Ldone = newLabel();
        const char *onEqual = op == "==" ? "1" : "0";
        const char *onDiffer = op == "==" ? "0" : "1";
        code_ << "    bne t3,t5," << Lfalse << "\n";
        code_ << "    bne t4,t6," << Lfalse << "\n";
        code_ << "    addi t0,zero," << onEqual << "\n    j " << Ldone << "\n";
        code_ << Lfalse << ":\n    addi t0,zero," << onDiffer << "\n";
        code_ << Ldone << ":\n";
        return;
    }
    // Once the high words are equal, the low 32 bits are an unsigned
    // magnitude regardless of the overall type's signedness.
    std::string LhiDiff = newLabel(), Ldone = newLabel();
    code_ << "    bne t4,t6," << LhiDiff << "\n";
    if (op == "<") {
        code_ << "    sltu t0,t3,t5\n";
    } else if (op == ">") {
        code_ << "    sltu t0,t5,t3\n";
    } else if (op == "<=") {
        code_ << "    sltu t0,t5,t3\n    addi t1,zero,1\n    xor t0,t1,t0\n";  // !(rhs<lhs)
    } else {  // ">="
        code_ << "    sltu t0,t3,t5\n    addi t1,zero,1\n    xor t0,t1,t0\n";  // !(lhs<rhs)
    }
    code_ << "    j " << Ldone << "\n";
    code_ << LhiDiff << ":\n";
    const char *hiInsn = isUnsigned ? "sltu" : "slt";
    if (op == "<") {
        code_ << "    " << hiInsn << " t0,t4,t6\n";
    } else if (op == ">") {
        code_ << "    " << hiInsn << " t0,t6,t4\n";
    } else if (op == "<=") {
        code_ << "    " << hiInsn << " t0,t6,t4\n    addi t1,zero,1\n    xor t0,t1,t0\n";
    } else {  // ">="
        code_ << "    " << hiInsn << " t0,t4,t6\n    addi t1,zero,1\n    xor t0,t1,t0\n";
    }
    code_ << Ldone << ":\n";
}

// Entry: t0 = old, t1 = rhs (both at `type`'s width/signedness). Neither
// `mul` nor `div`/`rem` touch t2, so the address genCompoundThroughT2()
// holds there survives this call.
void Riscv32Codegen::genCompoundOp(const std::string &op, Type type) {
    bool isUnsigned = type.isUnsigned;
    if (op == "+") { code_ << "    add t0,t0,t1\n"; return; }
    if (op == "-") { code_ << "    sub t0,t0,t1\n"; return; }
    if (op == "*") {
        code_ << "    mul t0,t0,t1\n";
        return;
    }
    if (op == "&") { code_ << "    and t0,t0,t1\n"; return; }
    if (op == "|") { code_ << "    or t0,t0,t1\n"; return; }
    if (op == "^") { code_ << "    xor t0,t0,t1\n"; return; }
    if (op == "<<" || op == ">>") {
        const char *insn = op == "<<" ? "sll" : (isUnsigned ? "srl" : "sra");
        code_ << "    " << insn << " t0,t0,t1\n";
        return;
    }
    // "/" or "%"
    const char *insn = op == "%" ? (isUnsigned ? "remu" : "rem") : (isUnsigned ? "divu" : "div");
    code_ << "    " << insn << " t0,t0,t1\n";
}

// t2 must already hold the resolved address on entry; it's stashed on
// the stack across evaluating e.rhs (which may clobber t0-t4) and never
// recomputed, so the target's address sub-expression runs exactly once.
void Riscv32Codegen::genCompoundThroughT2(const ir::Expr &e) {
    code_ << "    addi sp,sp,-4\n    sw t2,sp,0\n";  // stash address
    genExpr(*e.rhs);                                     // t0 = rhs
    code_ << "    add t1,t0,zero\n";                      // t1 = rhs
    code_ << "    lw t2,sp,0\n    addi sp,sp,4\n";         // t2 = address (restored)
    loadOperand("t2", e.type);                                 // t0 = old
    if (e.yieldOld) code_ << "    addi sp,sp,-4\n    sw t0,sp,0\n";  // stash old
    genCompoundOp(e.op, e.type);                                 // t0 = new (raw)
    narrowT0To(e.type);
    if (e.yieldOld) {
        code_ << "    add t1,t0,zero\n";                          // t1 = new
        code_ << "    lw t0,sp,0\n    addi sp,sp,4\n";             // t0 = old
        code_ << "    add t3,t0,zero\n";                              // t3 = old, preserved across the store below
        code_ << "    add t0,t1,zero\n";                                // t0 = new - storeOperand always stores from t0
        storeOperand("t2", e.type);
        code_ << "    add t0,t3,zero\n";                                  // t0 = old (final result)
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
        code_ << "    addi sp,sp,-4\n    sw t0,sp,0\n";
        genExpr(*e.rhs);
        code_ << "    lw t1,sp,0\n    addi sp,sp,4\n";  // t1 = lhs, t0 = rhs
        if (op == "+") {
            code_ << "    add t0,t1,t0\n";
        } else {
            code_ << "    sub t0,t1,t0\n";
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
            code_ << "    addi sp,sp,-8\n    sw t0,sp,0\n    sw t1,sp,4\n";
            genExpr(*e.rhs);
            code_ << "    add t3,t0,zero\n    add t4,t1,zero\n";
            code_ << "    lw t0,sp,0\n    lw t1,sp,4\n    addi sp,sp,8\n";
            code_ << "    " << insn << " t0,t0,t3\n    " << insn << " t1,t1,t4\n";
            return;
        }
        genExpr(*e.lhs);
        code_ << "    addi sp,sp,-4\n    sw t0,sp,0\n";
        genExpr(*e.rhs);
        code_ << "    lw t1,sp,0\n    addi sp,sp,4\n";
        code_ << "    " << insn << " t0,t1,t0\n";
        return;
    }
    if (op == "<<" || op == ">>") {
        if (wide) {
            genWideShift(*e.lhs, *e.rhs, op == "<<", isUnsigned);
            return;
        }
        genExpr(*e.lhs);
        code_ << "    addi sp,sp,-4\n    sw t0,sp,0\n";
        genExpr(*e.rhs);  // t0 = shift count
        code_ << "    add t1,t0,zero\n";
        code_ << "    lw t0,sp,0\n    addi sp,sp,4\n";  // t0 = value to shift
        const char *insn = op == "<<" ? "sll" : (isUnsigned ? "srl" : "sra");
        code_ << "    " << insn << " t0,t0,t1\n";
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
    code_ << "    addi sp,sp,-4\n    sw t0,sp,0\n";
    genExpr(rhs);
    code_ << "    lw t1,sp,0\n    addi sp,sp,4\n";  // t1 = lhs, t0 = rhs
    code_ << "    mul t0,t1,t0\n";
}

// Real hardware divide/remainder (`div`/`divu`/`rem`/`remu`) - no
// `mfhi`/`mflo` step, unlike MIPS.
void Riscv32Codegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    code_ << "    addi sp,sp,-4\n    sw t0,sp,0\n";
    genExpr(rhs);
    code_ << "    lw t1,sp,0\n    addi sp,sp,4\n";  // t1 = lhs (dividend), t0 = rhs (divisor)
    const char *insn = wantRemainder ? (isUnsigned ? "remu" : "rem") : (isUnsigned ? "divu" : "div");
    code_ << "    " << insn << " t0,t1,t0\n";
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
    code_ << "    addi sp,sp,-4\n    sw t0,sp,0\n";
    genExpr(rhs);
    code_ << "    lw t1,sp,0\n    addi sp,sp,4\n";  // t1 = lhs, t0 = rhs
    if (op == "<") {
        code_ << "    " << sltInsn << " t0,t1,t0\n";  // t0 = (lhs < rhs)
        return;
    }
    if (op == ">") {
        code_ << "    " << sltInsn << " t0,t0,t1\n";  // t0 = (rhs < lhs) = (lhs > rhs)
        return;
    }
    if (op == "<=") {
        code_ << "    " << sltInsn << " s1,t0,t1\n";           // s1 = (rhs < lhs)
        code_ << "    addi t0,zero,1\n    xor t0,t0,s1\n";  // t0 = !(rhs<lhs) = (lhs<=rhs)
        return;
    }
    if (op == ">=") {
        code_ << "    " << sltInsn << " s1,t1,t0\n";   // s1 = (lhs < rhs)
        code_ << "    addi t0,zero,1\n    xor t0,t0,s1\n";  // t0 = !(lhs<rhs) = (lhs>=rhs)
        return;
    }
    if (op == "==") {
        code_ << "    sub s1,t1,t0\n";    // s1 = lhs - rhs
        code_ << "    sltiu t0,s1,1\n";    // t0 = (unsigned(s1) < 1) = (s1 == 0)
        return;
    }
    if (op == "!=") {
        code_ << "    sub s1,t1,t0\n";    // s1 = lhs - rhs
        code_ << "    sltu t0,zero,s1\n";  // t0 = (0 < unsigned(s1)) = (s1 != 0)
        return;
    }
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

// A wide operand's truthiness needs both words; `or t0,t0,t1` folds
// them together before the usual zero test.
void Riscv32Codegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    if (isWide(lhs.type)) code_ << "    or t0,t0,t1\n";
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    code_ << "    beq t0,zero," << Lfalse << "\n";
    genExpr(rhs);
    if (isWide(rhs.type)) code_ << "    or t0,t0,t1\n";
    code_ << "    beq t0,zero," << Lfalse << "\n";
    code_ << "    j " << Ltrue << "\n";
    code_ << Lfalse << ":\n    addi t0,zero,0\n    j " << Ldone << "\n";
    code_ << Ltrue << ":\n    addi t0,zero,1\n";
    code_ << Ldone << ":\n";
}

void Riscv32Codegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    if (isWide(lhs.type)) code_ << "    or t0,t0,t1\n";
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    code_ << "    bne t0,zero," << Ltrue << "\n";
    genExpr(rhs);
    if (isWide(rhs.type)) code_ << "    or t0,t0,t1\n";
    code_ << "    bne t0,zero," << Ltrue << "\n";
    code_ << "    j " << Lfalse << "\n";
    code_ << Ltrue << ":\n    addi t0,zero,1\n    j " << Ldone << "\n";
    code_ << Lfalse << ":\n    addi t0,zero,0\n";
    code_ << Ldone << ":\n";
}

// `sltiu t0,t0,1` computes (unsigned(t0) < 1), i.e. logical not, in one
// instruction.
void Riscv32Codegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    if (isWide(rhs.type)) code_ << "    or t0,t0,t1\n";
    code_ << "    sltiu t0,t0,1\n";
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
