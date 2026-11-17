#include "Z8000Codegen.h"

#include <cstdint>
#include <stdexcept>

#include "Z8000Assembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

// Where the whole program loads and starts executing.
constexpr uint32_t kBaseAddress = 0x1000;

// Where `r15` (the real hardware stack pointer) is set before calling main(), comfortably
// above the loaded program and its data.
constexpr uint32_t kInitialSp = 0xE000;
}  // namespace

std::string Z8000Codegen::arrayElemText(const ir::ArrayInitElem &e) {
    if (!e.isSymbol) return std::to_string(static_cast<int>(static_cast<uint16_t>(e.value)));
    return e.slot ? label(e.slot) : label(e.func);
}

void Z8000Codegen::declareArray(const std::string &lbl, const std::vector<ir::ArrayInitElem> &values, Type elemType) {
    if (values.empty()) return;
    int w = widthOfType(elemType);
    const char *directive = w == 1 ? "db" : "dw";
    std::ostringstream line;
    line << lbl << ": " << directive << " ";
    bool first = true;
    for (const auto &e : values) {
        int words = w <= 2 ? 1 : w / 2;
        if (words > 1 && !e.isSymbol) {
            auto uv = static_cast<unsigned long>(e.value);
            for (int i = 0; i < words; i++) {
                if (!first) line << ", ";
                line << static_cast<uint16_t>((uv >> (16 * i)) & 0xFFFF);
                first = false;
            }
            continue;
        }
        if (!first) line << ", ";
        line << arrayElemText(e);
        first = false;
    }
    trailer_.push_back(line.str());
}

std::string Z8000Codegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    code_ << "org " << (objectMode ? 0 : kBaseAddress) << "\n\n";
    if (!objectMode) {
        code_ << "_start:\n";
        code_ << "    ld r15," << kInitialSp << "\n";
        code_ << "    call " << label(mainFn) << "\n";
        // The result sits in r0 once execution reaches `halt`.
        code_ << "halt:\n    halt\n\n";
    }

    for (const auto &fb : prog.functionBodies) genFunction(fb);

    size_t globalByteOffset = 0;
    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        if (slot->isExternal) continue;
        const std::string lbl = label(slot.get());
        int elemW = widthOfType(slot->type);
        size_t totalBytes =
            slot->arraySize > 0 ? static_cast<size_t>(slot->arraySize) * elemW : static_cast<size_t>(elemW);
        if (elemW >= 2 && (globalByteOffset % 2) != 0) {
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
            int w = widthOfType(slot->type);
            if (w <= 2) {
                const char *directive = w == 1 ? "db" : "dw";
                trailer_.push_back(lbl + ": " + directive + " " +
                                    std::to_string(static_cast<int>(static_cast<uint16_t>(v))) + "  ; " +
                                    slot->sourceName);
            } else {
                auto uv = static_cast<unsigned long>(v);
                std::ostringstream line;
                line << lbl << ": dw ";
                for (int i = 0; i < w / 2; i++) {
                    if (i > 0) line << ", ";
                    line << static_cast<uint16_t>((uv >> (16 * i)) & 0xFFFF);
                }
                line << "  ; " << slot->sourceName;
                trailer_.push_back(line.str());
            }
        }
    }

    std::ostringstream out;
    out << code_.str();
    for (const auto &line : trailer_) out << line << "\n";
    return out.str();
}

std::map<std::string, std::string> Z8000Codegen::perFileAsm() const { return perFileAsm_; }

// Prologue: `push r13` saves the caller's frame pointer, `ld r13,r15` makes `r13` this call's
// frame pointer (pointing at that saved word, with the return address above it and the caller's
// pushed arguments above that - see addr()), then `sub` reserves Locals below `r13`. Epilogue
// (here and every `Return`) undoes it.
void Z8000Codegen::genFunction(const ir::FunctionBody &fn) {
    code_ << label(fn.func) << ":\n";
    code_ << "    push r13\n";
    code_ << "    ld r13,r15\n";
    if (fn.func->frameSize > 0) code_ << "    sub r15," << fn.func->frameSize << "\n";
    genStmt(*fn.body);
    code_ << "    ld r15,r13\n";
    code_ << "    pop r13\n";
    code_ << "    ret\n\n";
}

void Z8000Codegen::genStmt(const ir::Stmt &s) {
    switch (s.kind) {
        case ir::StmtKind::Block: {
            for (const auto &child : s.body) genStmt(*child);
            return;
        }
        case ir::StmtKind::If: {
            genExpr(*s.cond);
            testTruthy(*s.cond);
            std::string Lelse = s.elseStmt ? newLabel() : "";
            std::string Ldone = newLabel();
            code_ << "    beq " << (s.elseStmt ? Lelse : Ldone) << "\n";
            genStmt(*s.thenStmt);
            if (s.elseStmt) {
                code_ << "    br " << Ldone << "\n";
                code_ << Lelse << ":\n";
                genStmt(*s.elseStmt);
            }
            code_ << Ldone << ":\n";
            return;
        }
        case ir::StmtKind::While: {
            std::string Lstart = newLabel(), Lend = newLabel();
            std::string Lcont = s.postStmt ? newLabel() : Lstart;
            code_ << Lstart << ":\n";
            genExpr(*s.cond);
            testTruthy(*s.cond);
            code_ << "    beq " << Lend << "\n";
            loopLabels_.push_back({Lcont, Lend});
            genStmt(*s.loopBody);
            loopLabels_.pop_back();
            if (s.postStmt) {
                code_ << Lcont << ":\n";
                genStmt(*s.postStmt);
            }
            code_ << "    br " << Lstart << "\n";
            code_ << Lend << ":\n";
            return;
        }
        case ir::StmtKind::Break: {
            code_ << "    br " << loopLabels_.back().second << "\n";
            return;
        }
        case ir::StmtKind::Continue: {
            code_ << "    br " << loopLabels_.back().first << "\n";
            return;
        }
        case ir::StmtKind::Return: {
            if (s.initOrReturnOrExpr) {
                genExpr(*s.initOrReturnOrExpr);
                if (isWide(s.initOrReturnOrExpr->type)) {
                    int words = wordCountOf(s.initOrReturnOrExpr->type);
                    for (int i = 0; i < words; i++) code_ << "    pop r" << i << "\n";
                }
            }
            code_ << "    ld r15,r13\n";
            code_ << "    pop r13\n";
            code_ << "    ret\n";
            return;
        }
        case ir::StmtKind::LocalDecl: {
            if (s.declSlot->arraySize > 0) {
                genLocalArrayInit(s.declSlot);
            } else if (s.initOrReturnOrExpr) {
                genExpr(*s.initOrReturnOrExpr);
                if (isWide(s.declSlot->type)) {
                    genWideStore(s.declSlot, wordCountOf(s.declSlot->type));
                } else {
                    storeOperand(addr(s.declSlot), s.declSlot->type);
                }
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
            code_ << "    br " << irLabel(s.labelId) << "\n";
            return;
        }
    }
}

void Z8000Codegen::genExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit: {
            if (isWide(e.type)) {
                genWideLoadLiteral(e.intValue, wordCountOf(e.type));
                return;
            }
            code_ << "    ld r0," << static_cast<int>(static_cast<uint16_t>(e.intValue)) << "\n";
            return;
        }
        case ir::ExprKind::Var: {
            if (isWide(e.slot->type)) {
                genWideLoad(e.slot, wordCountOf(e.slot->type));
                return;
            }
            loadOperand(operandOf(e.slot), e.slot->type);
            return;
        }
        case ir::ExprKind::Index: {
            genIndex(e);
            return;
        }
        case ir::ExprKind::Assign: {
            if (e.lhs) {  // slot[idx] = rhs (never wide).
                genIndexAddr(e.slot, *e.lhs);  // r2 = target address
                code_ << "    push r2\n";
                genExpr(*e.rhs);
                code_ << "    pop r2\n";
                if (widthOfType(e.slot->type) == 1) {
                    code_ << "    ldb [r2+0],rl0\n";
                } else {
                    code_ << "    ld [r2+0],r0\n";
                }
                return;
            }
            if (isWide(e.slot->type)) {
                genExpr(*e.rhs);  // pushes `words` words
                int words = wordCountOf(e.slot->type);
                for (int i = 0; i < words; i++) {
                    code_ << "    ld r0,[r15+" << (2 * i) << "]\n    ld " << wideWordOperand(e.slot, i) << ",r0\n";
                }
                code_ << "    add r15," << (2 * words) << "\n";
                return;
            }
            genExpr(*e.rhs);
            storeOperand(operandOf(e.slot), e.slot->type);
            return;
        }
        case ir::ExprKind::DerefAssign: {
            genExpr(*e.lhs);  // r0 = pointer value
            code_ << "    ld r2,r0\n";
            code_ << "    push r2\n";
            genExpr(*e.rhs);  // r0, or pushes `words` words (LSW on top) if wide
            storeIndirect(e.type);
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            if (e.lhs) {  // slot[idx] op= rhs (including slot[idx]++/--) - never wide.
                genIndexAddr(e.slot, *e.lhs);  // r2 = address
                genCompoundThroughR2(e);
                return;
            }
            std::string mem = operandOf(e.slot);
            genExpr(*e.rhs);  // r0 = rhs
            code_ << "    ld r1,r0\n";
            loadOperand(mem, e.type);  // r0 = old
            if (e.yieldOld) code_ << "    push r0\n";
            genCompoundOp(e.op, e.type);
            narrowR0To(e.type);
            storeOperand(mem, e.type);
            if (e.yieldOld) code_ << "    pop r0\n";
            return;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            genExpr(*e.lhs);  // r0 = pointer value
            code_ << "    ld r2,r0\n";
            genCompoundThroughR2(e);
            return;
        }
        case ir::ExprKind::Ternary: {
            genExpr(*e.lhs);
            testTruthy(*e.lhs);
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            code_ << "    beq " << Lelse << "\n";
            genExpr(*e.rhs);
            code_ << "    br " << Ldone << "\n";
            code_ << Lelse << ":\n";
            genExpr(*e.elseExpr);
            code_ << Ldone << ":\n";
            return;
        }
        case ir::ExprKind::Unary: {
            if (e.op == "-") {
                genExpr(*e.rhs);
                if (isWide(e.rhs->type)) {
                    genWideNegate(wordCountOf(e.rhs->type));
                } else {
                    code_ << "    neg r0\n";
                }
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                genExpr(*e.rhs);  // r0 = pointer value
                code_ << "    ld r2,r0\n";
                loadIndirect("r2", e.type);
            } else if (e.op == "~") {
                genExpr(*e.rhs);
                if (isWide(e.rhs->type)) {
                    int words = wordCountOf(e.rhs->type);
                    for (int i = 0; i < words; i++) {
                        std::string w = "[r15+" + std::to_string(2 * i) + "]";
                        code_ << "    ld r0," << w << "\n    com r0\n    ld " << w << ",r0\n";
                    }
                } else {
                    code_ << "    com r0\n";
                }
            } else {  // "&"
                genAddrOf(*e.rhs);
            }
            return;
        }
        case ir::ExprKind::Call: {
            int totalWords = 0;
            for (size_t i = e.args.size(); i-- > 0;) {
                genExpr(*e.args[i]);
                if (isWide(e.args[i]->type)) {
                    totalWords += wordCountOf(e.args[i]->type);
                } else {
                    code_ << "    push r0\n";
                    totalWords += 1;
                }
            }
            code_ << "    call " << label(e.func) << "\n";
            if (totalWords > 0) code_ << "    add r15," << (2 * totalWords) << "\n";
            if (isWide(e.type)) {
                int words = wordCountOf(e.type);
                for (int i = words - 1; i >= 0; i--) code_ << "    push r" << i << "\n";
            }
            return;
        }
        case ir::ExprKind::CallPtr: {
            genExpr(*e.lhs);  // r0 = the callee's own entry address
            code_ << "    ld r2,r0\n";
            code_ << "    callind r2\n";
            return;
        }
        case ir::ExprKind::Binary: {
            genBinary(e);
            return;
        }
        case ir::ExprKind::Intrinsic: {
            (void)e;  // no interrupt vector is set up (see generate()); treated as a no-op
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

std::string Z8000Codegen::wideWordOperand(const ir::Slot *slot, int wordIndex) const {
    int off = 2 * wordIndex;
    if (slot->kind == ir::SlotKind::Global) {
        return off == 0 ? label(slot) : (label(slot) + "+" + std::to_string(off));
    }
    if (slot->kind == ir::SlotKind::Param) {
        return "[r13+" + std::to_string(slot->frameOffset + off) + "]";
    }
    return "[r13-" + std::to_string(slot->frameOffset + off) + "]";
}

// `ldb` into a register's byte half never sign-extends the rest of that register, unlike the
// PDP-11's `movb` - so a signed `char` load needs an explicit `extsb` afterward.
void Z8000Codegen::loadOperand(const std::string &mem, Type type) {
    if (widthOfType(type) == 1) {
        code_ << "    ldb rl0," << mem << "\n";
        if (type.isUnsigned) {
            code_ << "    and r0,0x00FF\n";
        } else {
            code_ << "    extsb r0\n";
        }
    } else {
        code_ << "    ld r0," << mem << "\n";
    }
}

void Z8000Codegen::storeOperand(const std::string &mem, Type type) {
    if (widthOfType(type) == 1) {
        code_ << "    ldb " << mem << ",rl0\n";
    } else {
        code_ << "    ld " << mem << ",r0\n";
    }
}

void Z8000Codegen::loadIndirect(const std::string &addrReg, Type type) {
    if (!isWide(type)) {
        loadOperand("[" + addrReg + "+0]", type);
        return;
    }
    int words = wordCountOf(type);
    for (int i = words - 1; i >= 0; i--) {
        code_ << "    ld r0,[" << addrReg << "+" << (2 * i) << "]\n";
        code_ << "    push r0\n";
    }
}

void Z8000Codegen::storeIndirect(Type type) {
    if (!isWide(type)) {
        code_ << "    pop r2\n";
        storeOperand("[r2+0]", type);
        return;
    }
    int words = wordCountOf(type);
    static const char *const tempRegs[] = {"r0", "r1", "r3", "r4"};
    for (int i = 0; i < words; i++) code_ << "    pop " << tempRegs[i] << "\n";
    code_ << "    pop r2\n";
    for (int i = 0; i < words; i++) code_ << "    ld [r2+" << (2 * i) << "]," << tempRegs[i] << "\n";
}

void Z8000Codegen::genWideLoad(const ir::Slot *slot, int words) {
    for (int i = words - 1; i >= 0; i--) {
        code_ << "    ld r0," << wideWordOperand(slot, i) << "\n";
        code_ << "    push r0\n";
    }
}

void Z8000Codegen::genWideLoadLiteral(long value, int words) {
    auto uv = static_cast<unsigned long>(value);
    for (int i = words - 1; i >= 0; i--) {
        int shiftedWord = static_cast<int>((uv >> (16 * i)) & 0xFFFF);
        code_ << "    ld r0," << shiftedWord << "\n";
        code_ << "    push r0\n";
    }
}

void Z8000Codegen::genWideStore(const ir::Slot *slot, int words) {
    for (int i = 0; i < words; i++) {
        code_ << "    pop r0\n";
        code_ << "    ld " << wideWordOperand(slot, i) << ",r0\n";
    }
}

// Standard multi-word ripple-carry idiom, simpler than Pdp11Codegen.cpp's own genWideAdd()
// (see its comment) since the Z8000's `adc`/`sbc` are real two-operand add/subtract-with-carry
// instructions (`adc dst,src`: dst = dst + src + carry), unlike the PDP-11's single-operand
// `ADC`/`SBC` (carry-only). Word 0 is a plain `add`/`sub` (which also sets the carry flag the
// next word's `adc`/`sbc` reads); every later word folds the previous word's carry/borrow in
// directly.
void Z8000Codegen::genWideAdd(const ir::Expr &lhs, const ir::Expr &rhs, bool isAdd, int words) {
    genExpr(lhs);
    genExpr(rhs);
    for (int i = 0; i < words; i++) {
        std::string rhsOperand = "[r15+" + std::to_string(2 * i) + "]";
        std::string lhsOperand = "[r15+" + std::to_string(2 * words + 2 * i) + "]";
        const char *op = i == 0 ? (isAdd ? "add" : "sub") : (isAdd ? "adc" : "sbc");
        code_ << "    ld r0," << lhsOperand << "\n";
        code_ << "    ld r1," << rhsOperand << "\n";
        code_ << "    " << op << " r0,r1\n";
        code_ << "    ld " << lhsOperand << ",r0\n";
    }
    code_ << "    add r15," << (2 * words) << "\n";
}

void Z8000Codegen::genWideNegate(int words) {
    // Two's-complement negation in place: `com` every word first, then `add` 1 into the least
    // significant word and `adc` the carry up through the rest. `adc dst,src` needs a real
    // second operand (unlike the PDP-11's single-operand carry-only `ADC`), so each later word
    // adds 0 with the carry folded in.
    for (int i = 0; i < words; i++) {
        std::string w = "[r15+" + std::to_string(2 * i) + "]";
        code_ << "    ld r0," << w << "\n    com r0\n    ld " << w << ",r0\n";
    }
    code_ << "    ld r0,[r15+0]\n    add r0,1\n    ld [r15+0],r0\n";
    for (int i = 1; i < words; i++) {
        std::string w = "[r15+" + std::to_string(2 * i) + "]";
        code_ << "    ld r0," << w << "\n    ld r1,0\n    adc r0,r1\n    ld " << w << ",r0\n";
    }
}

void Z8000Codegen::genWideBitwise(const ir::Expr &lhs, const ir::Expr &rhs, const std::string &op, int words) {
    genExpr(lhs);
    genExpr(rhs);
    for (int i = 0; i < words; i++) {
        std::string rhsOperand = "[r15+" + std::to_string(2 * i) + "]";
        std::string lhsOperand = "[r15+" + std::to_string(2 * words + 2 * i) + "]";
        code_ << "    ld r0," << lhsOperand << "\n";
        code_ << "    ld r1," << rhsOperand << "\n";
        if (op == "&") {
            code_ << "    and r0,r1\n";
        } else if (op == "|") {
            code_ << "    or r0,r1\n";
        } else {
            code_ << "    xor r0,r1\n";
        }
        code_ << "    ld " << lhsOperand << ",r0\n";
    }
    code_ << "    add r15," << (2 * words) << "\n";
}

void Z8000Codegen::genWideCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned,
                                   int words) {
    genExpr(lhs);
    genExpr(rhs);
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    if (op == "==" || op == "!=") {
        for (int i = words - 1; i >= 0; i--) {
            std::string lhsOperand = "[r15+" + std::to_string(2 * words + 2 * i) + "]";
            std::string rhsOperand = "[r15+" + std::to_string(2 * i) + "]";
            code_ << "    ld r0," << lhsOperand << "\n    ld r1," << rhsOperand << "\n    cp r0,r1\n";
            std::string Lcontinue = newLabel();
            code_ << "    beq " << Lcontinue << "\n";
            code_ << "    br " << (op == "==" ? Lfalse : Ltrue) << "\n";
            code_ << Lcontinue << ":\n";
        }
        code_ << "    br " << (op == "==" ? Ltrue : Lfalse) << "\n";
    } else {
        std::string signedBranch = op == "<" ? "blt" : op == "<=" ? "ble" : op == ">" ? "bgt" : "bge";
        std::string unsignedBranch = op == "<" ? "blo" : op == "<=" ? "blos" : op == ">" ? "bhi" : "bhis";
        for (int i = words - 1; i >= 0; i--) {
            std::string lhsOperand = "[r15+" + std::to_string(2 * words + 2 * i) + "]";
            std::string rhsOperand = "[r15+" + std::to_string(2 * i) + "]";
            code_ << "    ld r0," << lhsOperand << "\n    ld r1," << rhsOperand << "\n    cp r0,r1\n";
            std::string Lcontinue = newLabel();
            code_ << "    beq " << Lcontinue << "\n";
            bool useSigned = (i == words - 1) && !isUnsigned;
            code_ << "    " << (useSigned ? signedBranch : unsignedBranch) << " " << Ltrue << "\n";
            code_ << "    br " << Lfalse << "\n";
            code_ << Lcontinue << ":\n";
        }
        code_ << "    br " << Lfalse << "\n";
    }
    code_ << Lfalse << ":\n    ld r0,0\n    br " << Ldone << "\n";
    code_ << Ltrue << ":\n    ld r0,1\n";
    code_ << Ldone << ":\n";
    code_ << "    add r15," << (4 * words) << "\n";
}

// A `long`/`long long` shifted by a runtime count loops one bit at a time via `rlc`/`rrc`
// (rotate-through-carry), folding the bit crossing a word boundary by hand, chained across
// `words` words - the Z8000's native dynamic shift (`sdl`/`sda`) isn't used here (see
// Z8000Codegen.h's own header comment, and docs/known-bugs.md).
void Z8000Codegen::genWideShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned, int words) {
    genExpr(lhs);  // pushes `words` words
    code_ << "    push r1\n";
    genExpr(rhs);  // r0 = shift count
    code_ << "    ld [r15+0],r0\n";
    std::string Lloop = newLabel(), Ldone = newLabel();
    code_ << Lloop << ":\n";
    code_ << "    ld r0,[r15+0]\n    test r0\n";
    code_ << "    beq " << Ldone << "\n";
    if (isLeft) {
        code_ << "    clc\n";
        for (int i = 0; i < words; i++) {
            std::string w = "[r15+" + std::to_string(2 + 2 * i) + "]";
            code_ << "    ld r0," << w << "\n    rlc r0\n    ld " << w << ",r0\n";
        }
    } else {
        code_ << "    clc\n";
        for (int i = words - 1; i >= 0; i--) {
            std::string w = "[r15+" + std::to_string(2 + 2 * i) + "]";
            // The top word's sign bit, not a cleared carry, feeds back in for a signed shift.
            if (i == words - 1 && !isUnsigned) {
                code_ << "    ld r0," << w << "\n    test r0\n    rrc r0\n    ld " << w << ",r0\n";
            } else {
                code_ << "    ld r0," << w << "\n    rrc r0\n    ld " << w << ",r0\n";
            }
        }
    }
    code_ << "    ld r0,[r15+0]\n    sub r0,1\n    ld [r15+0],r0\n";
    code_ << "    br " << Lloop << "\n";
    code_ << Ldone << ":\n";
    code_ << "    pop r1\n";
}

// Deliberately not implemented - see docs/known-bugs.md and Pdp11Codegen.cpp's own
// genWideMul()/genWideDivMod() comment, which this backend follows for the same reason (no
// simulator exists to verify a from-scratch wide multiply/divide algorithm against).
void Z8000Codegen::genWideMul(const ir::Expr &lhs, const ir::Expr &rhs, int) {
    err(lhs.line, "multiplying two 'long'/'long long' values isn't supported on this target yet - "
                   "see Z8000Codegen.cpp's own genWideMul() comment");
    (void)rhs;
}

void Z8000Codegen::genWideDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool, bool, int) {
    err(lhs.line, "dividing two 'long'/'long long' values isn't supported on this target yet - "
                   "see Z8000Codegen.cpp's own genWideDivMod() comment");
    (void)rhs;
}

void Z8000Codegen::genConvert(const ir::Expr &e) {
    Type from = e.lhs->type;
    Type to = e.type;
    bool fromWide = isWide(from);
    bool toWide = isWide(to);
    if (!fromWide && !toWide) {
        genExpr(*e.lhs);
        if (widthOfType(to) == 1) {
            if (to.isUnsigned) {
                code_ << "    and r0,0x00FF\n";
            } else {
                code_ << "    extsb r0\n";
            }
        }
        return;
    }
    if (fromWide && !toWide) {
        int words = wordCountOf(from);
        genExpr(*e.lhs);
        code_ << "    ld r0,[r15+0]\n";
        code_ << "    add r15," << (2 * words) << "\n";
        if (widthOfType(to) == 1) {
            code_ << "    " << (to.isUnsigned ? "and r0,0x00FF\n" : "extsb r0\n");
        }
        return;
    }
    if (!fromWide && toWide) {
        int toWords = wordCountOf(to);
        genExpr(*e.lhs);  // r0 = the native-width value, already correctly sign/zero-extended to 16 bits
        code_ << "    ld r1,r0\n";
        if (from.isUnsigned) {
            code_ << "    ld r0,0\n";
        } else {
            code_ << "    exts r0\n";  // sign-extends r1 into r0 (the pair r0:r1)
        }
        for (int i = 1; i < toWords; i++) code_ << "    push r0\n";
        code_ << "    push r1\n";
        return;
    }
    int fromWords = wordCountOf(from);
    int toWords = wordCountOf(to);
    genExpr(*e.lhs);
    if (toWords > fromWords) {
        code_ << "    ld r1,[r15+" << (2 * (fromWords - 1)) << "]\n    ld r0,r1\n    exts r0\n";
        static const char *const tempRegs[] = {"r1", "r3", "r4"};
        for (int i = 0; i < fromWords; i++) code_ << "    pop " << tempRegs[i] << "\n";
        for (int i = 0; i < toWords - fromWords; i++) code_ << "    push r0\n";
        for (int i = fromWords - 1; i >= 0; i--) code_ << "    push " << tempRegs[i] << "\n";
    } else if (toWords < fromWords) {
        code_ << "    add r15," << (2 * (fromWords - toWords)) << "\n";
    }
}

void Z8000Codegen::narrowR0To(Type to) {
    if (widthOfType(to) != 1) return;
    if (to.isUnsigned) {
        code_ << "    and r0,0x00FF\n";
    } else {
        code_ << "    extsb r0\n";
    }
}

int Z8000Codegen::localArrayBase(const ir::Slot *slot) { return ir::localLowOffset(*slot, typeWidths()); }

void Z8000Codegen::genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
    genExpr(idxExpr);  // r0 = index
    int shift = elemShift(slot->type);
    if (shift >= 0) {
        for (int i = 0; i < shift; i++) code_ << "    add r0,r0\n";
    } else {
        // A non-power-of-two element width (e.g. a packed struct's) has no shift-based
        // scaling, so multiply by the real width via `mult` instead; only the low word of the
        // 32-bit product is kept, same convention as genMul().
        code_ << "    ld r1,r0\n";                       // r1 = index (MULT's pre-loaded pair-low operand)
        code_ << "    ld r2," << widthOfType(slot->type) << "\n";  // r2 = width (the source operand)
        code_ << "    mult r0,r2\n";                      // r0:r1 = r2 * r1 = width * index
        code_ << "    ld r0,r1\n";
    }
    if (slot->arraySize > 0) {
        if (slot->kind == ir::SlotKind::Global) {
            code_ << "    lda r2," << label(slot) << "\n";
            code_ << "    add r2,r0\n";
        } else {
            code_ << "    lda r2,[r13-" << localArrayBase(slot) << "]\n";
            code_ << "    add r2,r0\n";
        }
        return;
    }
    code_ << "    ld r2," << operandOf(slot) << "\n";
    code_ << "    add r2,r0\n";
}

void Z8000Codegen::genIndex(const ir::Expr &e) {
    genIndexAddr(e.slot, *e.lhs);
    if (widthOfType(e.slot->type) == 1) {
        code_ << "    ldb rl0,[r2+0]\n";
        if (e.slot->type.isUnsigned) {
            code_ << "    and r0,0x00FF\n";
        } else {
            code_ << "    extsb r0\n";
        }
    } else {
        code_ << "    ld r0,[r2+0]\n";
    }
}

void Z8000Codegen::genLocalArrayInit(const ir::Slot *slot) {
    for (size_t i = 0; i < slot->arrayInit.size(); i++) {
        const ir::ArrayInitElem &elem = slot->arrayInit[i];
        if (elem.isSymbol) {
            code_ << "    lda r0," << (elem.slot ? label(elem.slot) : label(elem.func)) << "\n";
        } else {
            code_ << "    ld r0," << static_cast<int>(static_cast<uint16_t>(elem.value)) << "\n";
        }
        int elemW = widthOfType(slot->type);
        code_ << "    " << (elemW == 1 ? "ldb" : "ld") << " [r13-" << (localArrayBase(slot) - elemW * static_cast<int>(i))
              << "]," << (elemW == 1 ? "rl0" : "r0") << "\n";
    }
}

void Z8000Codegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        if (operand.slot->kind == ir::SlotKind::Global) {
            code_ << "    lda r0," << label(operand.slot) << "\n";
            return;
        }
        if (operand.slot->kind == ir::SlotKind::Param) {
            code_ << "    lda r0,[r13+" << operand.slot->frameOffset << "]\n";
        } else {
            int below = ir::localOffsetDescendingWide(*operand.slot, typeWidths());
            code_ << "    lda r0,[r13-" << below << "]\n";
        }
        return;
    }
    if (operand.kind == ir::ExprKind::Index) {
        genIndexAddr(operand.slot, *operand.lhs);
        code_ << "    ld r0,r2\n";
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        code_ << "    lda r0," << label(operand.func) << "\n";
        return;
    }
    err(operand.line, "internal: unhandled addressable-operand kind");
}

// Entry convention: r0 = old value, r1 = rhs value, both at `type`'s width/signedness.
void Z8000Codegen::genCompoundOp(const std::string &op, Type type) {
    bool isUnsigned = type.isUnsigned;
    if (op == "+") { code_ << "    add r0,r1\n"; return; }
    if (op == "-") { code_ << "    sub r0,r1\n"; return; }
    if (op == "*") { code_ << "    ld r2,r0\n    mult r0,r2\n    ld r0,r1\n"; return; }
    if (op == "&") { code_ << "    and r0,r1\n"; return; }
    if (op == "|") { code_ << "    or r0,r1\n"; return; }
    if (op == "^") { code_ << "    xor r0,r1\n"; return; }
    if (op == "<<" || op == ">>") {
        std::string Lloop = newLabel(), Ldone = newLabel();
        code_ << Lloop << ":\n    test r1\n";
        code_ << "    beq " << Ldone << "\n";
        if (op == "<<") {
            code_ << "    clc\n    rlc r0\n";
        } else if (isUnsigned) {
            code_ << "    clc\n    rrc r0\n";
        } else {
            code_ << "    test r0\n    rrc r0\n";
        }
        code_ << "    sub r1,1\n";
        code_ << "    br " << Lloop << "\n";
        code_ << Ldone << ":\n";
        return;
    }
    // "/" or "%"
    code_ << "    ld r2,r1\n";  // r2 = divisor
    code_ << "    ld r1,r0\n";  // r1 = dividend
    if (isUnsigned) {
        emitUnsignedDiv();
    } else {
        code_ << "    test r1\n    exts r0\n";
        code_ << "    div r0,r2\n";
    }
    if (op == "%") code_ << "    ld r0,r1\n";
}

void Z8000Codegen::genCompoundThroughR2(const ir::Expr &e) {
    code_ << "    push r2\n";
    genExpr(*e.rhs);
    code_ << "    push r0\n";
    code_ << "    pop r1\n";
    code_ << "    pop r2\n";
    loadOperand("[r2+0]", e.type);  // r0 = old
    code_ << "    push r2\n";
    if (e.yieldOld) code_ << "    push r0\n";
    genCompoundOp(e.op, e.type);  // r0 = new (raw)
    narrowR0To(e.type);
    if (e.yieldOld) {
        code_ << "    pop r1\n";
        code_ << "    pop r2\n";
        storeOperand("[r2+0]", e.type);
        code_ << "    ld r0,r1\n";
    } else {
        code_ << "    pop r2\n";
        storeOperand("[r2+0]", e.type);
    }
}

void Z8000Codegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    Type operandType = e.lhs->type;
    bool wide = isWide(operandType);
    bool isUnsigned = operandType.isUnsigned;
    int words = wide ? wordCountOf(operandType) : 1;
    if (op == "+" || op == "-") {
        if (wide) {
            genWideAdd(*e.lhs, *e.rhs, op == "+", words);
            return;
        }
        genExpr(*e.lhs);
        code_ << "    push r0\n";
        genExpr(*e.rhs);
        code_ << "    pop r1\n";  // r1 = lhs, r0 = rhs
        if (op == "+") {
            code_ << "    add r0,r1\n";
        } else {
            code_ << "    sub r1,r0\n";  // r1 = r1 - r0 = lhs - rhs
            code_ << "    ld r0,r1\n";
        }
        return;
    }
    if (op == "*") {
        if (wide) {
            genWideMul(*e.lhs, *e.rhs, words);
        } else {
            genMul(*e.lhs, *e.rhs);
        }
        return;
    }
    if (op == "/" || op == "%") {
        if (wide) {
            genWideDivMod(*e.lhs, *e.rhs, /*wantRemainder=*/op == "%", isUnsigned, words);
        } else {
            genDivMod(*e.lhs, *e.rhs, /*wantRemainder=*/op == "%", isUnsigned);
        }
        return;
    }
    if (op == "&" || op == "|" || op == "^") {
        if (wide) {
            genWideBitwise(*e.lhs, *e.rhs, op, words);
            return;
        }
        genExpr(*e.lhs);
        code_ << "    push r0\n";
        genExpr(*e.rhs);
        code_ << "    pop r1\n";  // r1 = lhs, r0 = rhs
        if (op == "&") {
            code_ << "    and r0,r1\n";
        } else if (op == "|") {
            code_ << "    or r0,r1\n";
        } else {
            code_ << "    xor r0,r1\n";
        }
        return;
    }
    if (op == "<<" || op == ">>") {
        if (wide) {
            genWideShift(*e.lhs, *e.rhs, op == "<<", isUnsigned, words);
            return;
        }
        genShift(*e.lhs, *e.rhs, op == "<<", isUnsigned);
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
        genWideCompare(op, *e.lhs, *e.rhs, isUnsigned, words);
        return;
    }
    genCompare(op, *e.lhs, *e.rhs, isUnsigned);
}

void Z8000Codegen::genShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned) {
    genExpr(lhs);
    code_ << "    push r0\n";
    genExpr(rhs);
    code_ << "    ld r1,r0\n";  // r1 = shift count
    code_ << "    pop r0\n";    // r0 = value to shift
    std::string Lloop = newLabel(), Ldone = newLabel();
    code_ << Lloop << ":\n    test r1\n";
    code_ << "    beq " << Ldone << "\n";
    if (isLeft) {
        code_ << "    clc\n    rlc r0\n";
    } else if (isUnsigned) {
        code_ << "    clc\n    rrc r0\n";
    } else {
        code_ << "    test r0\n    rrc r0\n";  // arithmetic: sign bit stays, rotated back into bit 15
    }
    code_ << "    sub r1,1\n";
    code_ << "    br " << Lloop << "\n";
    code_ << Ldone << ":\n";
}

// `mult`: the low word of its 32-bit product (`r1`) is the 16-bit truncated answer.
void Z8000Codegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    code_ << "    push r0\n";
    genExpr(rhs);
    code_ << "    pop r1\n";  // r1 = lhs, r0 = rhs
    code_ << "    ld r2,r0\n";  // r2 = rhs - MULT's source operand must not be part of the r0:r1 pair
    code_ << "    mult r0,r2\n";  // r0:r1 = r2 * r1 = rhs * lhs (high:low)
    code_ << "    ld r0,r1\n";
}

// `div` needs a 32-bit dividend in `r0:r1`, so the 16-bit dividend is first sign-extended into
// that pair with `exts` (`test` first, to set the flags `exts` reads).
void Z8000Codegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    code_ << "    push r0\n";
    genExpr(rhs);
    code_ << "    ld r2,r0\n";  // r2 = divisor
    code_ << "    pop r1\n";    // r1 = dividend (low word)
    if (isUnsigned) {
        emitUnsignedDiv();
    } else {
        code_ << "    test r1\n    exts r0\n";
        code_ << "    div r0,r2\n";  // r0 = quotient, r1 = remainder
    }
    if (wantRemainder) code_ << "    ld r0,r1\n";
}

// `div` is signed: the divisor is a signed 16-bit value and the quotient must fit in 15 bits.
// With the dividend zero-extended into r0:r1 it's exact for divisors 2..0x7FFF. A divisor of 1
// would give a quotient up to 0xFFFF, so it's handled directly; a divisor with its top bit set
// divides an unsigned 16-bit dividend at most once. Mirrors Pdp11Codegen.cpp's own
// emitUnsignedDiv() exactly (see its comment).
void Z8000Codegen::emitUnsignedDiv() {
    std::string Lbig = newLabel(), Lsmall = newLabel(), Lone = newLabel(), Lnorm = newLabel(), Lge = newLabel(),
                Llt = newLabel(), Ldone = newLabel();
    code_ << "    test r2\n";
    code_ << "    blt " << Lbig << "\n    br " << Lsmall << "\n";
    code_ << Lsmall << ":\n";
    code_ << "    ld r0,r2\n    sub r0,1\n";
    code_ << "    beq " << Lone << "\n    br " << Lnorm << "\n";
    code_ << Lnorm << ":\n";
    code_ << "    ld r0,0\n    div r0,r2\n    br " << Ldone << "\n";
    code_ << Lone << ":\n";
    code_ << "    ld r0,r1\n    ld r1,0\n    br " << Ldone << "\n";
    code_ << Lbig << ":\n";
    code_ << "    cp r1,r2\n";  // flags = dividend - divisor
    code_ << "    bhis " << Lge << "\n    br " << Llt << "\n";
    code_ << Llt << ":\n";
    code_ << "    ld r0,0\n    br " << Ldone << "\n";
    code_ << Lge << ":\n";
    code_ << "    sub r1,r2\n    ld r0,1\n";
    code_ << Ldone << ":\n";
}

void Z8000Codegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned) {
    if (op == "==") return emitCompare(lhs, rhs, "beq");
    if (op == "!=") return emitCompare(lhs, rhs, "bne");
    if (op == "<") return emitCompare(lhs, rhs, isUnsigned ? "blo" : "blt");
    if (op == "<=") return emitCompare(lhs, rhs, isUnsigned ? "blos" : "ble");
    if (op == ">") return emitCompare(lhs, rhs, isUnsigned ? "bhi" : "bgt");
    if (op == ">=") return emitCompare(lhs, rhs, isUnsigned ? "bhis" : "bge");
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

// `cp dst,src` sets condition codes for `dst - src`, so `cp r1,r0` (lhs, rhs) is directly
// `lhs - rhs`, with `jpCondIfTrue` applying with no further sign/overflow fixup.
void Z8000Codegen::emitCompare(const ir::Expr &lhs, const ir::Expr &rhs, const std::string &jpCondIfTrue) {
    genExpr(lhs);
    code_ << "    push r0\n";
    genExpr(rhs);
    code_ << "    pop r1\n";  // r1 = lhs, r0 = rhs
    code_ << "    cp r1,r0\n";
    std::string Ldone = newLabel();
    std::string Ltrue = newLabel();
    code_ << "    " << jpCondIfTrue << " " << Ltrue << "\n";
    code_ << "    ld r0,0\n    br " << Ldone << "\n";
    code_ << Ltrue << ":\n    ld r0,1\n";
    code_ << Ldone << ":\n";
}

void Z8000Codegen::testTruthy(const ir::Expr &e) {
    if (!isWide(e.type)) {
        code_ << "    test r0\n";
        return;
    }
    int words = wordCountOf(e.type);
    code_ << "    ld r0,[r15+0]\n";
    for (int i = 1; i < words; i++) {
        code_ << "    ld r1,[r15+" << (2 * i) << "]\n    or r0,r1\n";
    }
    code_ << "    add r15," << (2 * words) << "\n";
    code_ << "    test r0\n";
}

void Z8000Codegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    testTruthy(lhs);
    std::string Lfalse = newLabel(), Ldone = newLabel();
    code_ << "    beq " << Lfalse << "\n";
    genExpr(rhs);
    testTruthy(rhs);
    code_ << "    beq " << Lfalse << "\n";
    code_ << "    ld r0,1\n    br " << Ldone << "\n";
    code_ << Lfalse << ":\n    ld r0,0\n";
    code_ << Ldone << ":\n";
}

void Z8000Codegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    testTruthy(lhs);
    std::string Ltrue = newLabel(), Ldone = newLabel();
    code_ << "    bne " << Ltrue << "\n";
    genExpr(rhs);
    testTruthy(rhs);
    code_ << "    bne " << Ltrue << "\n";
    code_ << "    ld r0,0\n    br " << Ldone << "\n";
    code_ << Ltrue << ":\n    ld r0,1\n";
    code_ << Ldone << ":\n";
}

void Z8000Codegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    testTruthy(rhs);
    std::string Ltrue = newLabel(), Ldone = newLabel();
    code_ << "    beq " << Ltrue << "\n";
    code_ << "    ld r0,0\n    br " << Ldone << "\n";
    code_ << Ltrue << ":\n    ld r0,1\n";
    code_ << Ldone << ":\n";
}

std::optional<std::string> Z8000Codegen::assembleInProcess(const std::string &asmText,
                                                             const std::string &format) const {
    if (format == "raw") return assembleZ8000(asmText);
    if (auto kind = fileFormatKindFromName(format)) {
        ObjectFile obj = assembleZ8000Object(asmText);
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

}  // namespace rcc
