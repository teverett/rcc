#include "Riscv64Codegen.h"

#include <cstdint>
#include <stdexcept>

#include "Riscv64Assembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

// Where the whole program loads; the test harness points the simulated
// CPU's PC here directly - the RISC-V `virt` QEMU machine's RAM starts
// exactly here, same as riscv32's own kBaseAddress.
constexpr uint64_t kBaseAddress = kRiscv64BaseAddress;

// Initial sp, comfortably above the loaded program and its data; matches
// Riscv64LinkerFactory.cpp's own kEntryAsm.
constexpr uint32_t kInitialSP = 0x80100000u;

// A frame offset or `-frameSize` can exceed `addi`'s own 12-bit signed
// range for a large local array, so this one spot routes through `s1`
// (this dialect's scratch register, same role as riscv32's own) when the
// plain immediate wouldn't fit.
void addImm(std::ostringstream &code, const std::string &dst, const std::string &src, int32_t v) {
    if (v >= -2048 && v <= 2047) {
        code << "    addi " << dst << "," << src << "," << v << "\n";
        return;
    }
    code << "    li s1," << v << "\n";
    code << "    add " << dst << "," << src << ",s1\n";
}
}  // namespace

std::string Riscv64Codegen::arrayElemText(const ir::ArrayInitElem &e) {
    if (!e.isSymbol) return std::to_string(e.value);
    return e.slot ? label(e.slot) : label(e.func);
}

// Unlike riscv32/AlphaCodegen.cpp (whose assembler dialects have no data
// directive wider than 32 bits, so a `long long`/pointer-width literal
// element needs splitting into a low/high "dw" pair), this target's own
// `dq` directive holds a full 64-bit value directly - literal or symbol -
// so every element width gets one data item each, with no special-casing.
void Riscv64Codegen::declareArray(const std::string &lbl, const std::vector<ir::ArrayInitElem> &values,
                                   Type elemType) {
    if (values.empty()) return;
    int w = widthOfType(elemType);
    const char *directive = w == 1 ? "db" : w == 2 ? "dh" : w == 4 ? "dw" : "dq";
    std::ostringstream line;
    line << lbl << ": " << directive << " ";
    bool first = true;
    for (const auto &e : values) {
        if (!first) line << ", ";
        line << arrayElemText(e);
        first = false;
    }
    trailer_.push_back(line.str());
}

// See this class's own header comment for why the zero-extend (`slli`/
// `srli` by 32) is needed here but not for a plain signed-int literal
// (loadImm32()).
void Riscv64Codegen::loadAddress(const std::string &reg, const std::string &lbl) {
    code_ << "    lui " << reg << "," << lbl << "\n";
    code_ << "    addi " << reg << "," << reg << "," << lbl << "\n";
    code_ << "    slli " << reg << "," << reg << ",32\n";
    code_ << "    srli " << reg << "," << reg << ",32\n";
}

void Riscv64Codegen::loadImm32(const std::string &reg, int32_t v) { code_ << "    li " << reg << "," << v << "\n"; }

void Riscv64Codegen::canonicalizeAddress(const std::string &reg) {
    code_ << "    slli " << reg << "," << reg << ",32\n";
    code_ << "    srli " << reg << "," << reg << ",32\n";
}

// Builds the low 32 bits (cleaned to a genuine zero-extended quantity) and the high 32 bits
// separately, then combines them - mirrors AlphaCodegen.cpp's own `loadImm64()`.
void Riscv64Codegen::loadImm64(const std::string &reg, uint64_t v) {
    int64_t sv = static_cast<int64_t>(v);
    if (sv == static_cast<int64_t>(static_cast<int32_t>(sv))) {
        loadImm32(reg, static_cast<int32_t>(sv));
        return;
    }
    loadImm32(reg, static_cast<int32_t>(static_cast<uint32_t>(v & 0xFFFFFFFFu)));
    code_ << "    slli " << reg << "," << reg << ",32\n    srli " << reg << "," << reg << ",32\n";
    loadImm32("t4", static_cast<int32_t>(static_cast<uint32_t>((v >> 32) & 0xFFFFFFFFu)));
    code_ << "    slli t4,t4,32\n";
    code_ << "    or " << reg << "," << reg << ",t4\n";
}

void Riscv64Codegen::loadSlotAddr(const std::string &reg, const ir::Slot *slot) {
    if (slot->kind == ir::SlotKind::Global) {
        loadAddress(reg, label(slot));
        return;
    }
    int offset = slot->kind == ir::SlotKind::Param ? slot->frameOffset : -ir::localLowOffset(*slot, typeWidths());
    addImm(code_, reg, "fp", offset);
}

std::string Riscv64Codegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    code_ << "org " << (objectMode ? 0 : kBaseAddress) << "\n\n";
    if (!objectMode) {
        code_ << "_start:\n";
        // `kInitialSP`'s bit 31 is set, so `li`'s own sign-extending
        // `lui`/`addi` must be zero-extended afterward, same as
        // loadAddress()'s own sequence above.
        code_ << "    li sp," << static_cast<int32_t>(kInitialSP) << "\n";
        code_ << "    slli sp,sp,32\n    srli sp,sp,32\n";
        code_ << "    call " << label(mainFn) << "\n";
        code_ << "halt:\n    j halt\n\n";
    }

    for (const auto &fb : prog.functionBodies) genFunction(fb);

    // A misaligned `ld`/`sd`/`lw`/`sw`/`lh`/`sh` faults on some
    // implementations, so each global needs padding bytes first to reach
    // its own natural alignment (capped at 8, the widest transfer this
    // backend ever makes), same reasoning as riscv32's.
    size_t globalByteOffset = 0;
    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        if (slot->isExternal) continue;
        const std::string lbl = label(slot.get());
        int elemW = widthOfType(slot->type);
        size_t totalBytes = slot->arraySize > 0 ? static_cast<size_t>(slot->arraySize) * elemW
                                                  : static_cast<size_t>(elemW);
        size_t align = static_cast<size_t>(elemW) < 8 ? static_cast<size_t>(elemW) : 8;
        while (align > 1 && (globalByteOffset % align) != 0) {
            trailer_.push_back("db 0  ; alignment padding");
            globalByteOffset += 1;
        }
        globalByteOffset += totalBytes;
        if (slot->arraySize > 0) {
            declareArray(lbl, slot->arrayInit, slot->type);
        } else if (slot->type.base == BaseType::Struct) {
            declareArray(lbl, std::vector<ir::ArrayInitElem>(slot->type.structSize), Type{BaseType::Char, false});
        } else {
            long v = slot->hasInit ? slot->initValue : 0;
            const char *directive = elemW == 1 ? "db" : elemW == 2 ? "dh" : elemW == 4 ? "dw" : "dq";
            trailer_.push_back(lbl + ": " + directive + " " + std::to_string(v) + "  ; " + slot->sourceName);
        }
    }

    std::ostringstream out;
    out << code_.str();
    for (const auto &line : trailer_) out << line << "\n";
    return out.str();
}

std::map<std::string, std::string> Riscv64Codegen::perFileAsm() const { return perFileAsm_; }

// Every function saves/restores `ra`/`fp` as plain 32-bit words (`sw`/
// `lwu`), not full 64-bit quadwords (`sd`/`ld`): every address in this
// backend's universe fits in 32 bits (kBaseAddress plus whatever this
// program's own code+data+stack need), so the low 32 bits already
// reconstruct the exact original value, and `lwu` (RV64's zero-extending
// 32-bit load) recovers it correctly rather than `lw`'s sign-extension
// mis-reconstructing an address whose bit 31 happens to be set (every
// address here, starting at 0x80000000, has it set). This keeps this
// reservation at exactly IR.h's own assumed "two native ints" (8 bytes)
// ahead of a Param's frameOffset=0 point, with no extra per-target
// adjustment needed (see AlphaCodegen.cpp's own near-identical reasoning,
// and Arm64Codegen.h's `currentParamBase_`/`kParamExtra` for what that
// adjustment looks like when it can't be avoided this way).
void Riscv64Codegen::genFunction(const ir::FunctionBody &fn) {
    code_ << label(fn.func) << ":\n";
    code_ << "    addi sp,sp,-4\n    sw ra,sp,0\n";
    code_ << "    addi sp,sp,-4\n    sw fp,sp,0\n";
    code_ << "    add fp,sp,zero\n";
    if (fn.func->frameSize > 0) addImm(code_, "sp", "sp", -fn.func->frameSize);

    genStmt(*fn.body);

    genEpilogue();
    code_ << "\n";
}

void Riscv64Codegen::genEpilogue() {
    code_ << "    add sp,fp,zero\n";
    code_ << "    lwu s1,sp,0\n    addi sp,sp,4\n";
    code_ << "    lwu ra,sp,0\n    addi sp,sp,4\n";
    code_ << "    add fp,s1,zero\n";
    code_ << "    ret\n";
}

void Riscv64Codegen::genStmt(const ir::Stmt &s) {
    switch (s.kind) {
        case ir::StmtKind::Block: {
            for (const auto &child : s.body) genStmt(*child);
            return;
        }
        case ir::StmtKind::If: {
            genExpr(*s.cond);
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

void Riscv64Codegen::genExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit: {
            if (isWide(e.type)) {
                loadImm64("t0", static_cast<uint64_t>(e.intValue));
            } else {
                loadImm32("t0", static_cast<int32_t>(static_cast<uint32_t>(e.intValue)));
                narrowT0To(e.type);
            }
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
                code_ << "    addi sp,sp,-8\n    sd t2,sp,0\n";
                genExpr(*e.rhs);
                code_ << "    ld t2,sp,0\n    addi sp,sp,8\n";
                storeOperand("t2", e.slot->type);
                return;
            }
            genExpr(*e.rhs);
            loadSlotAddr("t2", e.slot);
            storeOperand("t2", e.slot->type);
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            if (e.lhs) {
                genIndexAddr(e.slot, *e.lhs);
                genCompoundThroughT2(e);
                return;
            }
            loadSlotAddr("t2", e.slot);
            genCompoundThroughT2(e);
            return;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            genExpr(*e.lhs);  // t0 = pointer value
            code_ << "    add t2,t0,zero\n";
            canonicalizeAddress("t2");
            genCompoundThroughT2(e);
            return;
        }
        case ir::ExprKind::Ternary: {
            genExpr(*e.lhs);
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
            genExpr(*e.lhs);  // t0 = pointer value
            code_ << "    addi sp,sp,-8\n    sd t0,sp,0\n";
            genExpr(*e.rhs);
            code_ << "    ld t2,sp,0\n    addi sp,sp,8\n";
            canonicalizeAddress("t2");
            storeOperand("t2", e.type);
            return;
        }
        case ir::ExprKind::Unary: {
            if (e.op == "-") {
                genExpr(*e.rhs);
                code_ << "    sub t0,zero,t0\n";
                narrowT0To(e.rhs->type);
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                genExpr(*e.rhs);  // t0 = pointer value
                code_ << "    add t2,t0,zero\n";
                canonicalizeAddress("t2");
                loadOperand("t2", e.type);
            } else if (e.op == "~") {
                genExpr(*e.rhs);
                code_ << "    xori t0,t0,-1\n";
                narrowT0To(e.rhs->type);
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
            // Caller pushes right to left, caller cleans up afterward - same convention as
            // Riscv32Codegen.cpp's own `Call`. A wide (8-byte: long/long long/pointer)
            // argument's own single `sd` replaces riscv32's split `sw`/`sw` pair, since this
            // register is already 64 bits wide.
            int totalBytes = 0;
            for (size_t i = e.args.size(); i-- > 0;) {
                genExpr(*e.args[i]);
                if (isWide(e.args[i]->type)) {
                    code_ << "    addi sp,sp,-8\n    sd t0,sp,0\n";
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
            genExpr(*e.lhs);  // t0 = the callee's own entry address
            code_ << "    add t2,t0,zero\n";
            canonicalizeAddress("t2");
            code_ << "    jalr ra,t2\n";
            return;
        }
        case ir::ExprKind::Binary: {
            genBinary(e);
            return;
        }
        case ir::ExprKind::Intrinsic: {
            (void)e;
            return;
        }
        case ir::ExprKind::Func:
            err(e.line, "internal: Func reached genExpr - should only ever appear as '&'s operand");
    }
}

// t2 ends up holding the computed address. Both a Global and a Local array's element
// addresses increase with the index (base + elemWidth*i); a pointer variable's own stored
// value is read at *its own* storage width (`slot->type`'s, not necessarily 8 bytes - this C
// subset lets a plain `int` stand in for an address, as `tests/pointers.c`'s `sum3(int p)`
// does, so a hardcoded 8-byte `ld` would both overread adjacent storage and, on reload,
// leave the wrong (sign- rather than zero-extended) value - the same `lwu`-over-`lw` fix
// canonicalizeAddress() applies elsewhere, done here directly since the load's own width
// already has to be chosen per slot anyway).
void Riscv64Codegen::genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
    genExpr(idxExpr);  // t0 = index
    int shift = elemShift(slot->type);
    if (shift > 0) {
        code_ << "    slli t0,t0," << shift << "\n";
    } else if (shift < 0) {
        loadImm32("t3", widthOfType(slot->type));
        code_ << "    mul t0,t0,t3\n";
    }
    if (slot->arraySize > 0) {
        loadSlotAddr("t2", slot);
        code_ << "    add t2,t2,t0\n";
    } else {
        loadSlotAddr("t1", slot);
        int baseW = widthOfType(slot->type);
        const char *loadInsn = baseW == 1 ? "lbu" : baseW == 2 ? "lhu" : baseW == 4 ? "lwu" : "ld";
        code_ << "    " << loadInsn << " t1,t1,0\n";  // t1 = the pointer variable's own stored value
        code_ << "    add t2,t1,t0\n";
    }
}

void Riscv64Codegen::genIndex(const ir::Expr &e) {
    genIndexAddr(e.slot, *e.lhs);
    loadOperand("t2", e.slot->type);
}

void Riscv64Codegen::genLocalArrayInit(const ir::Slot *slot) {
    int w = widthOfType(slot->type);
    for (size_t i = 0; i < slot->arrayInit.size(); i++) {
        const ir::ArrayInitElem &elem = slot->arrayInit[i];
        int offset = -ir::localLowOffset(*slot, typeWidths()) + w * static_cast<int>(i);
        if (elem.isSymbol) {
            loadAddress("t0", elem.slot ? label(elem.slot) : label(elem.func));
        } else if (w == 8) {
            loadImm64("t0", static_cast<uint64_t>(elem.value));
        } else {
            loadImm32("t0", static_cast<int32_t>(static_cast<uint32_t>(elem.value)));
        }
        addImm(code_, "t2", "fp", offset);
        if (w == 1) {
            code_ << "    sb t0,t2,0\n";
        } else if (w == 2) {
            code_ << "    sh t0,t2,0\n";
        } else if (w == 4) {
            code_ << "    sw t0,t2,0\n";
        } else {
            code_ << "    sd t0,t2,0\n";
        }
    }
}

void Riscv64Codegen::genAddrOf(const ir::Expr &operand) {
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

void Riscv64Codegen::loadOperand(const std::string &addrReg, Type type) {
    int w = widthOfType(type);
    if (w == 1) {
        code_ << "    " << (type.isUnsigned ? "lbu" : "lb") << " t0," << addrReg << ",0\n";
    } else if (w == 2) {
        code_ << "    " << (type.isUnsigned ? "lhu" : "lh") << " t0," << addrReg << ",0\n";
    } else if (w == 4) {
        code_ << "    " << (type.isUnsigned ? "lwu" : "lw") << " t0," << addrReg << ",0\n";
    } else {
        code_ << "    ld t0," << addrReg << ",0\n";
    }
}

void Riscv64Codegen::storeOperand(const std::string &addrReg, Type type) {
    int w = widthOfType(type);
    if (w == 1) {
        code_ << "    sb t0," << addrReg << ",0\n";
    } else if (w == 2) {
        code_ << "    sh t0," << addrReg << ",0\n";
    } else if (w == 4) {
        code_ << "    sw t0," << addrReg << ",0\n";
    } else {
        code_ << "    sd t0," << addrReg << ",0\n";
    }
}

void Riscv64Codegen::narrowT0To(Type to) {
    int w = widthOfType(to);
    if (w == 8) return;
    int shiftAmt = 64 - 8 * w;
    code_ << "    slli t0,t0," << shiftAmt << "\n";
    code_ << "    " << (to.isUnsigned ? "srli" : "srai") << " t0,t0," << shiftAmt << "\n";
}

// Crosses the native-`int`/`long` (or pointer) boundary, or re-narrows a same-side Convert.
// Like AlphaCodegen.cpp's own version (another native-64-bit-register target), the "wide &&
// wide" case is a genuine no-op: there's only ever one register, and every wide C type here
// shares the same 8-byte canonical representation.
void Riscv64Codegen::genConvert(const ir::Expr &e) {
    Type from = e.lhs->type;
    Type to = e.type;
    bool fromWide = isWide(from);
    bool toWide = isWide(to);
    genExpr(*e.lhs);
    if (fromWide && !toWide) {
        narrowT0To(to);
    } else if (!fromWide && toWide) {
        code_ << "    slli t0,t0,32\n";
        code_ << "    " << (from.isUnsigned ? "srli" : "srai") << " t0,t0,32\n";
    } else if (!fromWide && !toWide) {
        narrowT0To(to);
    }
}

void Riscv64Codegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    Type operandType = e.lhs->type;
    bool isUnsigned = operandType.isUnsigned;
    if (op == "+" || op == "-") {
        genExpr(*e.lhs);
        code_ << "    addi sp,sp,-8\n    sd t0,sp,0\n";
        genExpr(*e.rhs);
        code_ << "    ld t1,sp,0\n    addi sp,sp,8\n";  // t1 = lhs, t0 = rhs
        code_ << "    " << (op == "+" ? "add t0,t1,t0\n" : "sub t0,t1,t0\n");
        narrowT0To(operandType);
        return;
    }
    if (op == "*") {
        genMul(*e.lhs, *e.rhs);
        narrowT0To(operandType);
        return;
    }
    if (op == "/" || op == "%") {
        genDivMod(*e.lhs, *e.rhs, /*wantRemainder=*/op == "%", isUnsigned);
        return;
    }
    if (op == "&" || op == "|" || op == "^") {
        const char *insn = op == "&" ? "and" : op == "|" ? "or" : "xor";
        genExpr(*e.lhs);
        code_ << "    addi sp,sp,-8\n    sd t0,sp,0\n";
        genExpr(*e.rhs);
        code_ << "    ld t1,sp,0\n    addi sp,sp,8\n";
        code_ << "    " << insn << " t0,t1,t0\n";
        narrowT0To(operandType);
        return;
    }
    if (op == "<<" || op == ">>") {
        genExpr(*e.lhs);
        code_ << "    addi sp,sp,-8\n    sd t0,sp,0\n";
        genExpr(*e.rhs);  // t0 = shift count
        code_ << "    add t1,t0,zero\n";
        code_ << "    ld t0,sp,0\n    addi sp,sp,8\n";  // t0 = value to shift
        const char *insn = op == "<<" ? "sll" : (isUnsigned ? "srl" : "sra");
        code_ << "    " << insn << " t0,t0,t1\n";
        narrowT0To(operandType);
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

// Real RV64M hardware multiply (`mul`), a full 64x64->64 product - the same instruction
// regardless of operand width/signedness, since the low bits of a two's-complement product
// never depend on it; the caller (genBinary()) re-trims the result to `int` width when needed.
void Riscv64Codegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    code_ << "    addi sp,sp,-8\n    sd t0,sp,0\n";
    genExpr(rhs);
    code_ << "    ld t1,sp,0\n    addi sp,sp,8\n";  // t1 = lhs, t0 = rhs
    code_ << "    mul t0,t1,t0\n";
}

// Real RV64M hardware divide/remainder (`div`/`divu`/`rem`/`remu`) directly, at full register
// width - unlike AlphaCodegen.cpp's own bit-serial `genDivModCore()` (a genuine architectural
// gap in real Alpha), RV64 always has a hardware divide, so no software fallback is ever
// needed here, for `int` or `long`/`long long` alike. Both operands already carry their type's
// own canonical sign/zero-extended 64-bit representation (narrowT0To()'s own invariant), so
// the 64-bit division's result is already exactly the narrower type's correct result - no
// post-narrow needed, matching AlphaCodegen.cpp's own "/" case in genBinary().
void Riscv64Codegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    code_ << "    addi sp,sp,-8\n    sd t0,sp,0\n";
    genExpr(rhs);
    code_ << "    ld t1,sp,0\n    addi sp,sp,8\n";  // t1 = lhs (dividend), t0 = rhs (divisor)
    const char *insn = wantRemainder ? (isUnsigned ? "remu" : "rem") : (isUnsigned ? "divu" : "div");
    code_ << "    " << insn << " t0,t1,t0\n";
}

// `slt`/`sltu` directly produce the 0/1 boolean needed, no branch required - same scheme as
// Riscv32Codegen.cpp's own `genCompare()`, simplified since there's only ever one register
// width to compare regardless of the C operand type.
void Riscv64Codegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs) {
    bool isUnsigned = lhs.type.isUnsigned;
    const char *sltInsn = isUnsigned ? "sltu" : "slt";
    genExpr(lhs);
    code_ << "    addi sp,sp,-8\n    sd t0,sp,0\n";
    genExpr(rhs);
    code_ << "    ld t1,sp,0\n    addi sp,sp,8\n";  // t1 = lhs, t0 = rhs
    if (op == "<") {
        code_ << "    " << sltInsn << " t0,t1,t0\n";
        return;
    }
    if (op == ">") {
        code_ << "    " << sltInsn << " t0,t0,t1\n";
        return;
    }
    if (op == "<=") {
        code_ << "    " << sltInsn << " s1,t0,t1\n";
        code_ << "    addi t0,zero,1\n    xor t0,t0,s1\n";
        return;
    }
    if (op == ">=") {
        code_ << "    " << sltInsn << " s1,t1,t0\n";
        code_ << "    addi t0,zero,1\n    xor t0,t0,s1\n";
        return;
    }
    if (op == "==") {
        code_ << "    sub s1,t1,t0\n";
        code_ << "    sltiu t0,s1,1\n";
        return;
    }
    if (op == "!=") {
        code_ << "    sub s1,t1,t0\n";
        code_ << "    sltu t0,zero,s1\n";
        return;
    }
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

void Riscv64Codegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    code_ << "    beq t0,zero," << Lfalse << "\n";
    genExpr(rhs);
    code_ << "    beq t0,zero," << Lfalse << "\n";
    code_ << "    j " << Ltrue << "\n";
    code_ << Lfalse << ":\n    addi t0,zero,0\n    j " << Ldone << "\n";
    code_ << Ltrue << ":\n    addi t0,zero,1\n";
    code_ << Ldone << ":\n";
}

void Riscv64Codegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    code_ << "    bne t0,zero," << Ltrue << "\n";
    genExpr(rhs);
    code_ << "    bne t0,zero," << Ltrue << "\n";
    code_ << "    j " << Lfalse << "\n";
    code_ << Ltrue << ":\n    addi t0,zero,1\n    j " << Ldone << "\n";
    code_ << Lfalse << ":\n    addi t0,zero,0\n";
    code_ << Ldone << ":\n";
}

void Riscv64Codegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    code_ << "    sltiu t0,t0,1\n";
}

// Entry: t0 = old, t1 = rhs. Neither `mul` nor `div`/`rem` touch t2, so the address
// genCompoundThroughT2() holds there survives this call - same convention as
// Riscv32Codegen.cpp's own `genCompoundOp()`.
void Riscv64Codegen::genCompoundOp(const std::string &op, Type type) {
    bool isUnsigned = type.isUnsigned;
    if (op == "+") { code_ << "    add t0,t0,t1\n"; return; }
    if (op == "-") { code_ << "    sub t0,t0,t1\n"; return; }
    if (op == "*") { code_ << "    mul t0,t0,t1\n"; return; }
    if (op == "&") { code_ << "    and t0,t0,t1\n"; return; }
    if (op == "|") { code_ << "    or t0,t0,t1\n"; return; }
    if (op == "^") { code_ << "    xor t0,t0,t1\n"; return; }
    if (op == "<<" || op == ">>") {
        const char *insn = op == "<<" ? "sll" : (isUnsigned ? "srl" : "sra");
        code_ << "    " << insn << " t0,t0,t1\n";
        return;
    }
    const char *insn = op == "%" ? (isUnsigned ? "remu" : "rem") : (isUnsigned ? "divu" : "div");
    code_ << "    " << insn << " t0,t0,t1\n";
}

// t2 must already hold the resolved address on entry; it's stashed on the stack (as a full
// 8-byte quantity - see this class's own header comment on why every internal spill here uses
// `sd`/`ld` rather than a narrower store/load) across evaluating e.rhs, and never recomputed.
void Riscv64Codegen::genCompoundThroughT2(const ir::Expr &e) {
    code_ << "    addi sp,sp,-8\n    sd t2,sp,0\n";
    genExpr(*e.rhs);
    code_ << "    add t1,t0,zero\n";
    code_ << "    ld t2,sp,0\n    addi sp,sp,8\n";
    loadOperand("t2", e.type);
    if (e.yieldOld) code_ << "    addi sp,sp,-8\n    sd t0,sp,0\n";
    genCompoundOp(e.op, e.type);
    narrowT0To(e.type);
    if (e.yieldOld) {
        code_ << "    add t1,t0,zero\n";
        code_ << "    ld t0,sp,0\n    addi sp,sp,8\n";
        code_ << "    add t3,t0,zero\n";
        code_ << "    add t0,t1,zero\n";
        storeOperand("t2", e.type);
        code_ << "    add t0,t3,zero\n";
    } else {
        storeOperand("t2", e.type);
    }
}

std::optional<std::string> Riscv64Codegen::assembleInProcess(const std::string &asmText,
                                                               const std::string &format) const {
    if (format == "raw") return assembleRiscv64(asmText);
    if (auto kind = fileFormatKindFromName(format)) {
        ObjectFile obj = assembleRiscv64Object(asmText);
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

}  // namespace rcc
