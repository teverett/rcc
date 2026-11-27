#include "Riscv64Codegen.h"

#include <cstdint>
#include <tuple>
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

}  // namespace

// A frame offset or `-frameSize` can exceed `addi`'s own 12-bit signed
// range for a large local array, so this one spot routes through `s1`
// (this dialect's scratch register, same role as riscv32's own) when the
// plain immediate wouldn't fit.
void Riscv64Codegen::addImm(const std::string &dst, const std::string &src, int32_t v) {
    if (v >= -2048 && v <= 2047) {
        emit("addi", cat(dst, ",", src, ",", v));
        return;
    }
    emit("li", cat("s1,", v));
    emit("add", cat(dst, ",", src, ",s1"));
}

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
    emit("lui", cat(reg, ",", lbl));
    emit("addi", cat(reg, ",", reg, ",", lbl));
    emit("slli", cat(reg, ",", reg, ",32"));
    emit("srli", cat(reg, ",", reg, ",32"));
}

void Riscv64Codegen::loadImm32(const std::string &reg, int32_t v) { emit("li", cat(reg, ",", v)); }

void Riscv64Codegen::canonicalizeAddress(const std::string &reg) {
    emit("slli", cat(reg, ",", reg, ",32"));
    emit("srli", cat(reg, ",", reg, ",32"));
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
    emit("slli", cat(reg, ",", reg, ",32"));
    emit("srli", cat(reg, ",", reg, ",32"));
    loadImm32("t4", static_cast<int32_t>(static_cast<uint32_t>((v >> 32) & 0xFFFFFFFFu)));
    emit("slli", "t4,t4,32");
    emit("or", cat(reg, ",", reg, ",t4"));
}

void Riscv64Codegen::loadSlotAddr(const std::string &reg, const ir::Slot *slot) {
    if (slot->kind == ir::SlotKind::Global) {
        loadAddress(reg, label(slot));
        return;
    }
    int offset = slot->kind == ir::SlotKind::Param ? slot->frameOffset : -ir::localLowOffset(*slot, typeWidths());
    addImm(reg, "fp", offset);
}

std::string Riscv64Codegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    emitRaw(cat("org ", (objectMode ? 0 : kBaseAddress)));
    emitRaw("");
    if (!objectMode) {
        emitLabel("_start");
        // `kInitialSP`'s bit 31 is set, so `li`'s own sign-extending
        // `lui`/`addi` must be zero-extended afterward, same as
        // loadAddress()'s own sequence above.
        emit("li", cat("sp,", static_cast<int32_t>(kInitialSP)));
        emit("slli", "sp,sp,32");
        emit("srli", "sp,sp,32");
        emit("call", label(mainFn));
        emitLabel("halt");
        emit("j", "halt");
        emitRaw("");
    }

    // --dump-asm wants one file per input, not one combined dump - see
    // Pdp7Codegen.cpp's own generate() for the pattern this mirrors
    // (tellp()-position ranges into code_, index ranges into trailer_,
    // sliced into perFileAsm_ at the end).
    std::vector<std::tuple<std::string, size_t, size_t>> codeRanges;
    std::vector<std::tuple<std::string, size_t, size_t>> trailerRanges;

    for (const auto &fb : prog.functionBodies) {
        size_t fCodeStart = static_cast<size_t>(code_.tellp());
        genFunction(fb);
        codeRanges.push_back({fb.func->sourceFile, fCodeStart, static_cast<size_t>(code_.tellp())});
    }

    // A misaligned `ld`/`sd`/`lw`/`sw`/`lh`/`sh` faults on some
    // implementations, so each global needs padding bytes first to reach
    // its own natural alignment (capped at 8, the widest transfer this
    // backend ever makes), same reasoning as riscv32's.
    size_t globalByteOffset = 0;
    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        if (slot->isExternal) continue;
        if (debugMapEnabled_) debugSymbols_.push_back({label(slot.get()), /*isFunc=*/false});
        size_t gTrailerStart = trailer_.size();
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
        trailerRanges.push_back({slot->sourceFile, gTrailerStart, trailer_.size()});
    }

    std::string codeStr = code_.str();
    perFileAsm_.clear();
    for (const auto &[file, s, e] : codeRanges) perFileAsm_[file] += codeStr.substr(s, e - s);
    for (const auto &[file, s, e] : trailerRanges) {
        for (size_t i = s; i < e; i++) perFileAsm_[file] += trailer_[i] + "\n";
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
    lastDebugLine_ = 0;
    if (debugMapEnabled_) debugSymbols_.push_back({label(fn.func), /*isFunc=*/true});
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

void Riscv64Codegen::genEpilogue() {
    emit("add", "sp,fp,zero");
    emit("lwu", "s1,sp,0");
    emit("addi", "sp,sp,4");
    emit("lwu", "ra,sp,0");
    emit("addi", "sp,sp,4");
    emit("add", "fp,s1,zero");
    emit("ret");
}

void Riscv64Codegen::genStmt(const ir::Stmt &s) {
    emitSourceLine(s.line);
    switch (s.kind) {
        case ir::StmtKind::Block: {
            for (const auto &child : s.body) genStmt(*child);
            return;
        }
        case ir::StmtKind::If: {
            genExpr(*s.cond);
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            emit("beq", cat("t0,zero,", s.elseStmt ? Lelse : Ldone));
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
                emit("addi", "sp,sp,-8");
                emit("sd", "t2,sp,0");
                genExpr(*e.rhs);
                emit("ld", "t2,sp,0");
                emit("addi", "sp,sp,8");
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
            emit("add", "t2,t0,zero");
            canonicalizeAddress("t2");
            genCompoundThroughT2(e);
            return;
        }
        case ir::ExprKind::Ternary: {
            genExpr(*e.lhs);
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
            genExpr(*e.lhs);  // t0 = pointer value
            emit("addi", "sp,sp,-8");
            emit("sd", "t0,sp,0");
            genExpr(*e.rhs);
            emit("ld", "t2,sp,0");
            emit("addi", "sp,sp,8");
            canonicalizeAddress("t2");
            storeOperand("t2", e.type);
            return;
        }
        case ir::ExprKind::Unary: {
            if (e.op == "-") {
                genExpr(*e.rhs);
                emit("sub", "t0,zero,t0");
                narrowT0To(e.rhs->type);
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                genExpr(*e.rhs);  // t0 = pointer value
                emit("add", "t2,t0,zero");
                canonicalizeAddress("t2");
                loadOperand("t2", e.type);
            } else if (e.op == "~") {
                genExpr(*e.rhs);
                emit("xori", "t0,t0,-1");
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
                    emit("addi", "sp,sp,-8");
                    emit("sd", "t0,sp,0");
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
            genExpr(*e.lhs);  // t0 = the callee's own entry address
            emit("add", "t2,t0,zero");
            canonicalizeAddress("t2");
            emit("jalr", "ra,t2");
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
        emit("slli", cat("t0,t0,", shift));
    } else if (shift < 0) {
        loadImm32("t3", widthOfType(slot->type));
        emit("mul", "t0,t0,t3");
    }
    if (slot->arraySize > 0) {
        loadSlotAddr("t2", slot);
        emit("add", "t2,t2,t0");
    } else {
        loadSlotAddr("t1", slot);
        int baseW = widthOfType(slot->type);
        const char *loadInsn = baseW == 1 ? "lbu" : baseW == 2 ? "lhu" : baseW == 4 ? "lwu" : "ld";
        emit(loadInsn, "t1,t1,0");  // t1 = the pointer variable's own stored value
        emit("add", "t2,t1,t0");
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
        addImm("t2", "fp", offset);
        if (w == 1) {
            emit("sb", "t0,t2,0");
        } else if (w == 2) {
            emit("sh", "t0,t2,0");
        } else if (w == 4) {
            emit("sw", "t0,t2,0");
        } else {
            emit("sd", "t0,t2,0");
        }
    }
}

void Riscv64Codegen::genAddrOf(const ir::Expr &operand) {
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

void Riscv64Codegen::loadOperand(const std::string &addrReg, Type type) {
    int w = widthOfType(type);
    if (w == 1) {
        emit(type.isUnsigned ? "lbu" : "lb", cat("t0,", addrReg, ",0"));
    } else if (w == 2) {
        emit(type.isUnsigned ? "lhu" : "lh", cat("t0,", addrReg, ",0"));
    } else if (w == 4) {
        emit(type.isUnsigned ? "lwu" : "lw", cat("t0,", addrReg, ",0"));
    } else {
        emit("ld", cat("t0,", addrReg, ",0"));
    }
}

void Riscv64Codegen::storeOperand(const std::string &addrReg, Type type) {
    int w = widthOfType(type);
    if (w == 1) {
        emit("sb", cat("t0,", addrReg, ",0"));
    } else if (w == 2) {
        emit("sh", cat("t0,", addrReg, ",0"));
    } else if (w == 4) {
        emit("sw", cat("t0,", addrReg, ",0"));
    } else {
        emit("sd", cat("t0,", addrReg, ",0"));
    }
}

void Riscv64Codegen::narrowT0To(Type to) {
    int w = widthOfType(to);
    if (w == 8) return;
    int shiftAmt = 64 - 8 * w;
    emit("slli", cat("t0,t0,", shiftAmt));
    emit(to.isUnsigned ? "srli" : "srai", cat("t0,t0,", shiftAmt));
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
        emit("slli", "t0,t0,32");
        emit(from.isUnsigned ? "srli" : "srai", "t0,t0,32");
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
        emit("addi", "sp,sp,-8");
        emit("sd", "t0,sp,0");
        genExpr(*e.rhs);
        emit("ld", "t1,sp,0");
        emit("addi", "sp,sp,8");  // t1 = lhs, t0 = rhs
        emit(op == "+" ? "add" : "sub", "t0,t1,t0");
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
        emit("addi", "sp,sp,-8");
        emit("sd", "t0,sp,0");
        genExpr(*e.rhs);
        emit("ld", "t1,sp,0");
        emit("addi", "sp,sp,8");
        emit(insn, "t0,t1,t0");
        narrowT0To(operandType);
        return;
    }
    if (op == "<<" || op == ">>") {
        genExpr(*e.lhs);
        emit("addi", "sp,sp,-8");
        emit("sd", "t0,sp,0");
        genExpr(*e.rhs);  // t0 = shift count
        emit("add", "t1,t0,zero");
        emit("ld", "t0,sp,0");
        emit("addi", "sp,sp,8");  // t0 = value to shift
        const char *insn = op == "<<" ? "sll" : (isUnsigned ? "srl" : "sra");
        emit(insn, "t0,t0,t1");
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
    emit("addi", "sp,sp,-8");
    emit("sd", "t0,sp,0");
    genExpr(rhs);
    emit("ld", "t1,sp,0");
    emit("addi", "sp,sp,8");  // t1 = lhs, t0 = rhs
    emit("mul", "t0,t1,t0");
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
    emit("addi", "sp,sp,-8");
    emit("sd", "t0,sp,0");
    genExpr(rhs);
    emit("ld", "t1,sp,0");
    emit("addi", "sp,sp,8");  // t1 = lhs (dividend), t0 = rhs (divisor)
    const char *insn = wantRemainder ? (isUnsigned ? "remu" : "rem") : (isUnsigned ? "divu" : "div");
    emit(insn, "t0,t1,t0");
}

// `slt`/`sltu` directly produce the 0/1 boolean needed, no branch required - same scheme as
// Riscv32Codegen.cpp's own `genCompare()`, simplified since there's only ever one register
// width to compare regardless of the C operand type.
void Riscv64Codegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs) {
    bool isUnsigned = lhs.type.isUnsigned;
    const char *sltInsn = isUnsigned ? "sltu" : "slt";
    genExpr(lhs);
    emit("addi", "sp,sp,-8");
    emit("sd", "t0,sp,0");
    genExpr(rhs);
    emit("ld", "t1,sp,0");
    emit("addi", "sp,sp,8");  // t1 = lhs, t0 = rhs
    if (op == "<") {
        emit(sltInsn, "t0,t1,t0");
        return;
    }
    if (op == ">") {
        emit(sltInsn, "t0,t0,t1");
        return;
    }
    if (op == "<=") {
        emit(sltInsn, "s1,t0,t1");
        emit("addi", "t0,zero,1");
        emit("xor", "t0,t0,s1");
        return;
    }
    if (op == ">=") {
        emit(sltInsn, "s1,t1,t0");
        emit("addi", "t0,zero,1");
        emit("xor", "t0,t0,s1");
        return;
    }
    if (op == "==") {
        emit("sub", "s1,t1,t0");
        emit("sltiu", "t0,s1,1");
        return;
    }
    if (op == "!=") {
        emit("sub", "s1,t1,t0");
        emit("sltu", "t0,zero,s1");
        return;
    }
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

void Riscv64Codegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    emit("beq", cat("t0,zero,", Lfalse));
    genExpr(rhs);
    emit("beq", cat("t0,zero,", Lfalse));
    emit("j", Ltrue);
    emitLabel(Lfalse);
    emit("addi", "t0,zero,0");
    emit("j", Ldone);
    emitLabel(Ltrue);
    emit("addi", "t0,zero,1");
    emitLabel(Ldone);
}

void Riscv64Codegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    emit("bne", cat("t0,zero,", Ltrue));
    genExpr(rhs);
    emit("bne", cat("t0,zero,", Ltrue));
    emit("j", Lfalse);
    emitLabel(Ltrue);
    emit("addi", "t0,zero,1");
    emit("j", Ldone);
    emitLabel(Lfalse);
    emit("addi", "t0,zero,0");
    emitLabel(Ldone);
}

void Riscv64Codegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    emit("sltiu", "t0,t0,1");
}

// Entry: t0 = old, t1 = rhs. Neither `mul` nor `div`/`rem` touch t2, so the address
// genCompoundThroughT2() holds there survives this call - same convention as
// Riscv32Codegen.cpp's own `genCompoundOp()`.
void Riscv64Codegen::genCompoundOp(const std::string &op, Type type) {
    bool isUnsigned = type.isUnsigned;
    if (op == "+") { emit("add", "t0,t0,t1"); return; }
    if (op == "-") { emit("sub", "t0,t0,t1"); return; }
    if (op == "*") { emit("mul", "t0,t0,t1"); return; }
    if (op == "&") { emit("and", "t0,t0,t1"); return; }
    if (op == "|") { emit("or", "t0,t0,t1"); return; }
    if (op == "^") { emit("xor", "t0,t0,t1"); return; }
    if (op == "<<" || op == ">>") {
        const char *insn = op == "<<" ? "sll" : (isUnsigned ? "srl" : "sra");
        emit(insn, "t0,t0,t1");
        return;
    }
    const char *insn = op == "%" ? (isUnsigned ? "remu" : "rem") : (isUnsigned ? "divu" : "div");
    emit(insn, "t0,t0,t1");
}

// t2 must already hold the resolved address on entry; it's stashed on the stack (as a full
// 8-byte quantity - see this class's own header comment on why every internal spill here uses
// `sd`/`ld` rather than a narrower store/load) across evaluating e.rhs, and never recomputed.
void Riscv64Codegen::genCompoundThroughT2(const ir::Expr &e) {
    emit("addi", "sp,sp,-8");
    emit("sd", "t2,sp,0");
    genExpr(*e.rhs);
    emit("add", "t1,t0,zero");
    emit("ld", "t2,sp,0");
    emit("addi", "sp,sp,8");
    loadOperand("t2", e.type);
    if (e.yieldOld) {
        emit("addi", "sp,sp,-8");
        emit("sd", "t0,sp,0");
    }
    genCompoundOp(e.op, e.type);
    narrowT0To(e.type);
    if (e.yieldOld) {
        emit("add", "t1,t0,zero");
        emit("ld", "t0,sp,0");
        emit("addi", "sp,sp,8");
        emit("add", "t3,t0,zero");
        emit("add", "t0,t1,zero");
        storeOperand("t2", e.type);
        emit("add", "t0,t3,zero");
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

std::optional<std::string> Riscv64Codegen::debugMap(const std::string &asmText, const std::string &format,
        const std::string &sourcePath) const {
    (void)format;
    return assembleRiscv64DebugMap(asmText, sourcePath, debugSymbols_);
}

std::optional<std::string> Riscv64Codegen::debugMapPreLink(const std::string &asmText,
                                                    const std::string &sourcePath) const {
    return assembleRiscv64DebugMap(asmText, sourcePath, debugSymbols_, /*allowExternal=*/true);
}

// Only emits a marker when `debugMapEnabled_` (set via setDebugMapEnabled(),
// before generate() runs) and `line` actually differs from the last one
// emitted - see Pdp7Codegen.cpp's identical emitSourceLine(), which this
// mirrors exactly.
void Riscv64Codegen::emitSourceLine(int line) {
    if (!debugMapEnabled_ || line == 0 || line == lastDebugLine_) return;
    emitRaw(cat("__line ", line));
    lastDebugLine_ = line;
}

}  // namespace rcc
