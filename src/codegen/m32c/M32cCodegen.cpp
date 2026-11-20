#include "M32cCodegen.h"

#include <cstdint>
#include <stdexcept>

#include "M32cAssembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

constexpr uint32_t kBaseAddress = 0x0400;
constexpr uint32_t kInitialSp = 0xE000;
}  // namespace

void M32cCodegen::declareTemp(const std::string &lbl) { trailer_.push_back(lbl + ": dw 0"); }

std::string M32cCodegen::arrayElemText(const ir::ArrayInitElem &e) {
    if (!e.isSymbol) return std::to_string(static_cast<int>(static_cast<uint16_t>(e.value)));
    return e.slot ? label(e.slot) : label(e.func);
}

void M32cCodegen::declareArray(const std::string &lbl, const std::vector<ir::ArrayInitElem> &values, Type elemType) {
    if (values.empty()) return;
    const char *directive = widthOfType(elemType) == 1 ? "db" : "dw";
    std::ostringstream line;
    line << lbl << ": " << directive << " ";
    for (size_t i = 0; i < values.size(); i++) {
        if (i > 0) line << ", ";
        line << arrayElemText(values[i]);
    }
    trailer_.push_back(line.str());
}

void M32cCodegen::genLocalArrayInit(const ir::Slot *slot) {
    int stride = widthOfType(slot->type) == 1 ? 1 : 2;
    bool isChar = stride == 1;
    for (size_t i = 0; i < slot->arrayInit.size(); i++) {
        code_ << "    mov.w " << arrayElemText(slot->arrayInit[i]) << ",r0\n";
        int off = ir::localLowOffset(*slot, typeWidths()) - stride * static_cast<int>(i);
        code_ << "    " << (isChar ? "movb" : "mov.w") << " r0,[fb-" << off << "]\n";
    }
}

std::string M32cCodegen::generate(const ir::Program &prog, bool objectMode) {
    perFileAsm_.clear();
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    std::string full;
    auto flush = [&](const std::string &file) {
        std::string chunk = code_.str();
        code_.str("");
        code_.clear();
        full += chunk;
        perFileAsm_[file] += chunk;
    };

    code_ << "org " << (objectMode ? 0 : kBaseAddress) << "\n\n";
    if (!objectMode) {
        code_ << "_start:\n";
        code_ << "    mov.w " << kInitialSp << ",sp\n";
        code_ << "    jsr.w " << label(mainFn) << "\n";
        code_ << "halt:\n    jmp halt\n\n";
    }
    flush(mainFn ? mainFn->sourceFile : std::string());

    for (const auto &fb : prog.functionBodies) {
        genFunction(fb);
        flush(fb.func->sourceFile);
    }

    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        if (slot->isExternal) continue;
        const size_t before = trailer_.size();
        const std::string lbl = label(slot.get());
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
        for (size_t i = before; i < trailer_.size(); i++) perFileAsm_[slot->sourceFile] += trailer_[i] + "\n";
    }

    std::ostringstream out;
    out << full;
    for (const auto &line : trailer_) out << line << "\n";
    return out.str();
}

std::map<std::string, std::string> M32cCodegen::perFileAsm() const { return perFileAsm_; }

void M32cCodegen::genFunction(const ir::FunctionBody &fn) {
    code_ << label(fn.func) << ":\n";
    code_ << "    push.w fb\n";
    code_ << "    mov.w sp,fb\n";
    if (fn.func->frameSize > 0) code_ << "    sub.w " << fn.func->frameSize << ",sp\n";
    genStmt(*fn.body);
    code_ << "    mov.w fb,sp\n";
    code_ << "    pop.w fb\n";
    code_ << "    rts\n\n";
}

void M32cCodegen::genStmt(const ir::Stmt &s) {
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
            code_ << "    jeq " << (s.elseStmt ? Lelse : Ldone) << "\n";
            genStmt(*s.thenStmt);
            if (s.elseStmt) {
                code_ << "    jmp " << Ldone << "\n";
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
            code_ << "    jeq " << Lend << "\n";
            loopLabels_.push_back({Lcont, Lend});
            genStmt(*s.loopBody);
            loopLabels_.pop_back();
            if (s.postStmt) {
                code_ << Lcont << ":\n";
                genStmt(*s.postStmt);
            }
            code_ << "    jmp " << Lstart << "\n";
            code_ << Lend << ":\n";
            return;
        }
        case ir::StmtKind::Break: {
            code_ << "    jmp " << loopLabels_.back().second << "\n";
            return;
        }
        case ir::StmtKind::Continue: {
            code_ << "    jmp " << loopLabels_.back().first << "\n";
            return;
        }
        case ir::StmtKind::Return: {
            if (s.initOrReturnOrExpr) {
                genExpr(*s.initOrReturnOrExpr);
                if (isWide(s.initOrReturnOrExpr->type)) {
                    int words = wordCountOf(s.initOrReturnOrExpr->type);
                    for (int i = 0; i < words; i++) code_ << "    pop.w r" << i << "\n";
                }
            }
            code_ << "    mov.w fb,sp\n";
            code_ << "    pop.w fb\n";
            code_ << "    rts\n";
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
                    storeOperand(frameOperand(s.declSlot), s.declSlot->type);
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
            code_ << "    jmp " << irLabel(s.labelId) << "\n";
            return;
        }
    }
}

void M32cCodegen::genExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit: {
            if (isWide(e.type)) {
                genWideLoadLiteral(e.intValue, wordCountOf(e.type));
                return;
            }
            code_ << "    mov.w " << static_cast<int>(static_cast<uint16_t>(e.intValue)) << ",r0\n";
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
            if (e.lhs) {
                genIndexAddr(e.slot, *e.lhs);
                code_ << "    push.w a0\n";
                genExpr(*e.rhs);
                code_ << "    pop.w a0\n";
                code_ << "    " << (widthOfType(e.slot->type) == 1 ? "stb" : "st") << " r0,a0\n";
                return;
            }
            if (isWide(e.slot->type)) {
                genExpr(*e.rhs);
                int words = wordCountOf(e.slot->type);
                for (int i = 0; i < words; i++) {
                    code_ << "    mov.w [sp+" << (2 * i) << "],r0\n";
                    code_ << "    mov.w r0," << wideWordOperand(e.slot, i) << "\n";
                }
                code_ << "    add.w " << (2 * words) << ",sp\n";
                return;
            }
            genExpr(*e.rhs);
            storeOperand(operandOf(e.slot), e.slot->type);
            return;
        }
        case ir::ExprKind::DerefAssign: {
            genExpr(*e.lhs);
            code_ << "    mov.w r0,a0\n";
            code_ << "    push.w a0\n";
            genExpr(*e.rhs);
            if (isWide(e.type)) {
                int words = wordCountOf(e.type);
                code_ << "    mov.w [sp+" << (2 * words) << "],a0\n";
                for (int i = 0; i < words; i++) {
                    code_ << "    pop.w r0\n";
                    code_ << "    st r0,a0\n";
                    if (i + 1 < words) code_ << "    add.w 2,a0\n";
                }
                code_ << "    add.w 2,sp\n";
                return;
            }
            code_ << "    pop.w a0\n";
            code_ << "    " << (widthOfType(e.type) == 1 ? "stb" : "st") << " r0,a0\n";
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            if (e.lhs) {
                genIndexAddr(e.slot, *e.lhs);
                genCompoundThroughA0(e);
                return;
            }
            if (isWide(e.type)) {
                // Only the plain (non-indexed) slot form is supported for a
                // wide type - same known limitation `genIndex()` already
                // documents for a wide array element (see
                // docs/known-bugs.md).
                int words = wordCountOf(e.type);
                bool isUnsigned = e.type.isUnsigned;
                if (e.yieldOld) genWideLoad(e.slot, words);  // kept copy, stays underneath
                genWideLoad(e.slot, words);                  // working copy, consumed by the op below
                if (e.op == "<<" || e.op == ">>") {
                    genWideShift(nullptr, *e.rhs, e.op == "<<", isUnsigned, words, /*lhsAlreadyPushed=*/true);
                } else {
                    genWideAdd(nullptr, *e.rhs, e.op == "+", words, /*lhsAlreadyPushed=*/true, e.op, isUnsigned);
                }
                if (!e.yieldOld) {
                    // Pushes a second copy of the new value (so it survives
                    // genWideStore()'s pop below) - word `2*(words-1)` is a
                    // constant offset here, not the same word every
                    // iteration: each push shifts the original block one
                    // word further from `sp`, so this slides across it one
                    // word at a time, most-significant word first.
                    for (int k = 0; k < words; k++) {
                        code_ << "    mov.w [sp+" << (2 * (words - 1)) << "],r0\n";
                        code_ << "    push.w r0\n";
                    }
                }
                genWideStore(e.slot, words);
                return;
            }
            genExpr(*e.rhs);
            code_ << "    mov.w r0,r1\n";
            loadOperand(operandOf(e.slot), e.type);
            if (e.yieldOld) code_ << "    push.w r0\n";
            genCompoundOp(e.op, e.type);
            narrowR0To(e.type);
            storeOperand(operandOf(e.slot), e.type);
            if (e.yieldOld) code_ << "    pop.w r0\n";
            return;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            genExpr(*e.lhs);
            code_ << "    mov.w r0,a0\n";
            genCompoundThroughA0(e);
            return;
        }
        case ir::ExprKind::Ternary: {
            genExpr(*e.lhs);
            testTruthy(*e.lhs);
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            code_ << "    jeq " << Lelse << "\n";
            genExpr(*e.rhs);
            code_ << "    jmp " << Ldone << "\n";
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
                    code_ << "    neg.w r0\n";
                }
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                genExpr(*e.rhs);
                code_ << "    mov.w r0,a0\n";
                if (isWide(e.type)) {
                    int words = wordCountOf(e.type);
                    if (words > 1) code_ << "    add.w " << (2 * (words - 1)) << ",a0\n";
                    for (int i = words - 1; i >= 0; i--) {
                        code_ << "    ld a0,r0\n";
                        code_ << "    push.w r0\n";
                        if (i > 0) code_ << "    sub.w 2,a0\n";
                    }
                    return;
                }
                code_ << "    " << (widthOfType(e.type) == 1 ? "ldb" : "ld") << " a0,r0\n";
                if (widthOfType(e.type) == 1 && e.type.isUnsigned) code_ << "    and.w 0x00FF,r0\n";
            } else if (e.op == "~") {
                genExpr(*e.rhs);
                code_ << "    not.w r0\n";
                if (isWide(e.rhs->type)) {
                    for (int i = 1; i < wordCountOf(e.rhs->type); i++) code_ << "    not.w [sp+" << (2 * i) << "]\n";
                }
            } else {
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
                    code_ << "    push.w r0\n";
                    totalWords += 1;
                }
            }
            code_ << "    jsr.w " << label(e.func) << "\n";
            if (totalWords > 0) code_ << "    add.w " << (2 * totalWords) << ",sp\n";
            if (isWide(e.type)) {
                int words = wordCountOf(e.type);
                for (int i = words - 1; i >= 0; i--) {
                    code_ << "    mov.w r" << i << ",r0\n    push.w r0\n";
                }
            }
            return;
        }
        case ir::ExprKind::CallPtr: {
            genExpr(*e.lhs);
            std::string T = allocTemp();
            declareTemp(T);
            code_ << "    mov.w r0," << T << "\n";
            code_ << "    jsri.w " << T << "\n";
            return;
        }
        case ir::ExprKind::Binary: {
            genBinary(e);
            return;
        }
        case ir::ExprKind::Intrinsic: {
            code_ << "    " << (e.op == "ion" ? "fset i" : "fclr i") << "\n";
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

void M32cCodegen::genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
    genExpr(idxExpr);
    int shift = elemShift(slot->type);
    if (shift > 0) code_ << "    shl.w " << shift << ",r0\n";
    if (slot->arraySize > 0) {
        code_ << "    lea " << operandOf(slot) << ",a0\n";
        code_ << "    add.w r0,a0\n";
    } else {
        code_ << "    mov.w " << operandOf(slot) << ",a0\n";
        code_ << "    add.w r0,a0\n";
    }
}

void M32cCodegen::genIndex(const ir::Expr &e) {
    genIndexAddr(e.slot, *e.lhs);
    code_ << "    " << (widthOfType(e.slot->type) == 1 ? "ldb" : "ld") << " a0,r0\n";
    if (widthOfType(e.slot->type) == 1 && e.slot->type.isUnsigned) code_ << "    and.w 0x00FF,r0\n";
}

void M32cCodegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        code_ << "    lea " << operandOf(operand.slot) << ",r0\n";
        return;
    }
    if (operand.kind == ir::ExprKind::Index) {
        genIndexAddr(operand.slot, *operand.lhs);
        code_ << "    mov.w a0,r0\n";
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        code_ << "    lea " << label(operand.func) << ",r0\n";
        return;
    }
    err(operand.line, "internal: unhandled addressable-operand kind");
}

void M32cCodegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    Type operandType = e.lhs->type;
    bool wide = isWide(operandType);
    bool isUnsigned = operandType.isUnsigned;
    int words = wide ? wordCountOf(operandType) : 1;
    if (op == "+" || op == "-") {
        if (wide) {
            genWideAdd(e.lhs.get(), *e.rhs, op == "+", words);
            return;
        }
        genExpr(*e.lhs);
        code_ << "    push.w r0\n";
        genExpr(*e.rhs);
        code_ << "    pop.w r1\n";
        if (op == "+") {
            code_ << "    add.w r1,r0\n";
        } else {
            code_ << "    sub.w r0,r1\n";
            code_ << "    mov.w r1,r0\n";
        }
        return;
    }
    if (op == "*") {
        if (wide) {
            genWideMul(e.lhs.get(), *e.rhs, words);
        } else {
            genMul(*e.lhs, *e.rhs);
        }
        return;
    }
    if (op == "/" || op == "%") {
        if (wide) {
            genWideDivMod(e.lhs.get(), *e.rhs, op == "%", isUnsigned, words);
        } else {
            genDivMod(*e.lhs, *e.rhs, op == "%", isUnsigned);
        }
        return;
    }
    if (op == "&" || op == "|" || op == "^") {
        if (wide) {
            genWideBitwise(e.lhs.get(), *e.rhs, op, words);
            return;
        }
        genExpr(*e.lhs);
        code_ << "    push.w r0\n";
        genExpr(*e.rhs);
        code_ << "    pop.w r1\n";
        const char *insn = (op == "&") ? "and.w" : (op == "|") ? "or.w" : "xor.w";
        code_ << "    " << insn << " r1,r0\n";
        return;
    }
    if (op == "<<" || op == ">>") {
        if (wide) {
            genWideShift(e.lhs.get(), *e.rhs, op == "<<", isUnsigned, words);
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

void M32cCodegen::genShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned) {
    genExpr(lhs);
    code_ << "    push.w r0\n";
    genExpr(rhs);
    code_ << "    mov.w r0,r1\n";
    code_ << "    pop.w r0\n";
    if (!isLeft) code_ << "    neg.w r1\n";
    code_ << "    " << ((isLeft || isUnsigned) ? "shl.w" : "sha.w") << " r1,r0\n";
}

void M32cCodegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    code_ << "    push.w r0\n";
    genExpr(rhs);
    code_ << "    pop.w r1\n";
    code_ << "    mul.w r1,r0\n";
}

void M32cCodegen::genDivModCore(bool wantRemainder, bool isUnsigned) {
    if (isUnsigned) {
        code_ << "    mov.w 0,r2\n";
        code_ << "    divu.w r1,r0\n";
        if (wantRemainder) code_ << "    mov.w r2,r0\n";
        return;
    }
    std::string Ld = newLabel(), Lr = newLabel();
    code_ << "    mov.w 0,r3\n";
    code_ << "    cmp.w 0,r0\n    jge " << Ld << "\n";
    code_ << "    neg.w r0\n    mov.w 1,r3\n";
    code_ << Ld << ":\n";
    code_ << "    push.w r3\n";
    code_ << "    mov.w 0,r3\n";
    code_ << "    cmp.w 0,r1\n    jge " << Lr << "\n";
    code_ << "    neg.w r1\n    mov.w 1,r3\n";
    code_ << Lr << ":\n";
    code_ << "    mov.w 0,r2\n";
    code_ << "    divu.w r1,r0\n";
    std::string Lskip = newLabel();
    if (wantRemainder) {
        code_ << "    mov.w r2,r0\n";
        code_ << "    pop.w r3\n";
        code_ << "    cmp.w 0,r3\n    jeq " << Lskip << "\n";
        code_ << "    neg.w r0\n";
        code_ << Lskip << ":\n";
        return;
    }
    code_ << "    xor.w r3,[sp]\n";
    code_ << "    pop.w r3\n";
    code_ << "    cmp.w 0,r3\n    jeq " << Lskip << "\n";
    code_ << "    neg.w r0\n";
    code_ << Lskip << ":\n";
}

// The `long`-only counterpart of genDivModCore(), operating on the real
// 32-bit pair registers. Precondition: `r2r0` holds the dividend, `r3r1`
// the divisor (both 32-bit) - unlike the word-level routine above, no
// zero-/sign-extend step is needed first, since both operands are already
// the full 32 bits `DIVUL`/`DIVXL` expect (the word-level routine's
// dividend is twice its divisor's width, since `DIVU.W` widens a native
// 16-bit division the way plain C integer division never needs to for
// `long`).
//
// No free plain register survives once both pairs are loaded (`r0`-`r3`
// are all pinned down as halves of `r2r0`/`r3r1`), so the sign flags this
// needs for a signed divide/modulo live in two scratch memory cells
// instead of a register, unlike genDivModCore()'s own `r3`.
void M32cCodegen::genDivModCoreLong(bool wantRemainder, bool isUnsigned) {
    if (isUnsigned) {
        code_ << "    divul r3r1,r2r0\n";  // r2r0 = quotient, r3r1 = remainder
        if (wantRemainder) code_ << "    movl r3r1,r2r0\n";
        return;
    }
    std::string flagD = allocTemp();
    declareTemp(flagD);
    std::string flagS = allocTemp();
    declareTemp(flagS);
    std::string Ld = newLabel(), Lr = newLabel(), Lskip = newLabel();
    code_ << "    mov.w 0," << flagD << "\n";
    code_ << "    cmpl 0,r2r0\n    jge " << Ld << "\n";
    code_ << "    negl r2r0\n    mov.w 1," << flagD << "\n";
    code_ << Ld << ":\n";
    code_ << "    mov.w 0," << flagS << "\n";
    code_ << "    cmpl 0,r3r1\n    jge " << Lr << "\n";
    code_ << "    negl r3r1\n    mov.w 1," << flagS << "\n";
    code_ << Lr << ":\n";
    code_ << "    divxl r3r1,r2r0\n";  // |quotient| in r2r0, |remainder| in r3r1
    if (wantRemainder) {
        code_ << "    movl r3r1,r2r0\n";
        code_ << "    mov.w " << flagD << ",r1\n";  // r1 is free again: r3r1's own value is no longer needed
        code_ << "    cmp.w 0,r1\n    jeq " << Lskip << "\n";
        code_ << "    negl r2r0\n";
        code_ << Lskip << ":\n";
        return;
    }
    code_ << "    mov.w " << flagD << ",r1\n";
    code_ << "    xor.w " << flagS << ",r1\n";
    code_ << "    cmp.w 0,r1\n    jeq " << Lskip << "\n";
    code_ << "    negl r2r0\n";
    code_ << Lskip << ":\n";
}

void M32cCodegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    code_ << "    push.w r0\n";
    genExpr(rhs);
    code_ << "    mov.w r0,r1\n";
    code_ << "    pop.w r0\n";
    genDivModCore(wantRemainder, isUnsigned);
}

void M32cCodegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned) {
    if (op == "==") return emitCompare(lhs, rhs, "jeq");
    if (op == "!=") return emitCompare(lhs, rhs, "jne");
    if (op == "<") return emitCompare(lhs, rhs, isUnsigned ? "jltu" : "jlt");
    if (op == "<=") return emitCompare(lhs, rhs, isUnsigned ? "jleu" : "jle");
    if (op == ">") return emitCompare(lhs, rhs, isUnsigned ? "jgtu" : "jgt");
    if (op == ">=") return emitCompare(lhs, rhs, isUnsigned ? "jgeu" : "jge");
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

void M32cCodegen::emitCompare(const ir::Expr &lhs, const ir::Expr &rhs, const std::string &jumpIfTrue) {
    genExpr(lhs);
    code_ << "    push.w r0\n";
    genExpr(rhs);
    code_ << "    pop.w r1\n";
    code_ << "    cmp.w r0,r1\n";
    std::string Ltrue = newLabel(), Ldone = newLabel();
    code_ << "    " << jumpIfTrue << " " << Ltrue << "\n";
    code_ << "    mov.w 0,r0\n    jmp " << Ldone << "\n";
    code_ << Ltrue << ":\n    mov.w 1,r0\n";
    code_ << Ldone << ":\n";
}

void M32cCodegen::testTruthy(const ir::Expr &e) {
    if (!isWide(e.type)) {
        code_ << "    cmp.w 0,r0\n";
        return;
    }
    int words = wordCountOf(e.type);
    code_ << "    mov.w [sp],r0\n";
    for (int i = 1; i < words; i++) code_ << "    or.w [sp+" << (2 * i) << "],r0\n";
    code_ << "    add.w " << (2 * words) << ",sp\n";
    code_ << "    cmp.w 0,r0\n";
}

void M32cCodegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    genExpr(lhs);
    testTruthy(lhs);
    code_ << "    jeq " << Lfalse << "\n";
    genExpr(rhs);
    testTruthy(rhs);
    code_ << "    jeq " << Lfalse << "\n";
    code_ << Ltrue << ":\n    mov.w 1,r0\n    jmp " << Ldone << "\n";
    code_ << Lfalse << ":\n    mov.w 0,r0\n";
    code_ << Ldone << ":\n";
}

void M32cCodegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    genExpr(lhs);
    testTruthy(lhs);
    code_ << "    jne " << Ltrue << "\n";
    genExpr(rhs);
    testTruthy(rhs);
    code_ << "    jne " << Ltrue << "\n";
    code_ << Lfalse << ":\n    mov.w 0,r0\n    jmp " << Ldone << "\n";
    code_ << Ltrue << ":\n    mov.w 1,r0\n";
    code_ << Ldone << ":\n";
}

void M32cCodegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    testTruthy(rhs);
    std::string Ltrue = newLabel(), Ldone = newLabel();
    code_ << "    jeq " << Ltrue << "\n";
    code_ << "    mov.w 0,r0\n    jmp " << Ldone << "\n";
    code_ << Ltrue << ":\n    mov.w 1,r0\n";
    code_ << Ldone << ":\n";
}

void M32cCodegen::loadOperand(const std::string &mem, Type type) {
    if (widthOfType(type) == 1) {
        code_ << "    movb " << mem << ",r0\n";
        if (type.isUnsigned) code_ << "    and.w 0x00FF,r0\n";
    } else {
        code_ << "    mov.w " << mem << ",r0\n";
    }
}

void M32cCodegen::storeOperand(const std::string &mem, Type type) {
    if (widthOfType(type) == 1) {
        code_ << "    movb r0," << mem << "\n";
    } else {
        code_ << "    mov.w r0," << mem << "\n";
    }
}

void M32cCodegen::narrowR0To(Type to) {
    if (widthOfType(to) != 1) return;
    if (to.isUnsigned) {
        code_ << "    and.w 0x00FF,r0\n";
        return;
    }
    code_ << "    movb r0,r0\n";
}

void M32cCodegen::genCompoundOp(const std::string &op, Type type) {
    bool isUnsigned = type.isUnsigned;
    if (op == "+") {
        code_ << "    add.w r1,r0\n";
        return;
    }
    if (op == "-") {
        code_ << "    sub.w r1,r0\n";
        return;
    }
    if (op == "*") {
        code_ << "    mul.w r1,r0\n";
        return;
    }
    if (op == "/" || op == "%") {
        genDivModCore(op == "%", isUnsigned);
        return;
    }
    if (op == "&" || op == "|" || op == "^") {
        const char *insn = (op == "&") ? "and.w" : (op == "|") ? "or.w" : "xor.w";
        code_ << "    " << insn << " r1,r0\n";
        return;
    }
    if (op != "<<") code_ << "    neg.w r1\n";
    code_ << "    " << ((op == "<<" || isUnsigned) ? "shl.w" : "sha.w") << " r1,r0\n";
}

void M32cCodegen::genCompoundThroughA0(const ir::Expr &e) {
    code_ << "    push.w a0\n";
    genExpr(*e.rhs);
    code_ << "    mov.w r0,r1\n";
    code_ << "    pop.w a0\n";
    code_ << "    " << (widthOfType(e.type) == 1 ? "ldb" : "ld") << " a0,r0\n";
    if (widthOfType(e.type) == 1 && e.type.isUnsigned) code_ << "    and.w 0x00FF,r0\n";
    if (e.yieldOld) code_ << "    push.w r0\n";
    genCompoundOp(e.op, e.type);
    narrowR0To(e.type);
    code_ << "    " << (widthOfType(e.type) == 1 ? "stb" : "st") << " r0,a0\n";
    if (e.yieldOld) code_ << "    pop.w r0\n";
}

std::string M32cCodegen::wideWordOperand(const ir::Slot *slot, int wordIndex) const {
    int off = 2 * wordIndex;
    if (slot->kind == ir::SlotKind::Global) {
        return off == 0 ? label(slot) : (label(slot) + "+" + std::to_string(off));
    }
    if (slot->kind == ir::SlotKind::Param) {
        return "[fb+" + std::to_string(slot->frameOffset + off) + "]";
    }
    return "[fb-" + std::to_string(slot->frameOffset + off) + "]";
}

void M32cCodegen::genWideLoad(const ir::Slot *slot, int words) {
    if (words == 2) {
        code_ << "    movl " << operandOf(slot) << ",r2r0\n";
        code_ << "    pushl r2r0\n";
        return;
    }
    for (int i = words - 1; i >= 0; i--) {
        code_ << "    mov.w " << wideWordOperand(slot, i) << ",r0\n";
        code_ << "    push.w r0\n";
    }
}

void M32cCodegen::genWideLoadLiteral(long value, int words) {
    if (words == 2) {
        code_ << "    movl " << static_cast<uint32_t>(value) << ",r2r0\n";
        code_ << "    pushl r2r0\n";
        return;
    }
    auto uv = static_cast<unsigned long>(value);
    for (int i = words - 1; i >= 0; i--) {
        int shiftedWord = static_cast<int>((uv >> (16 * i)) & 0xFFFF);
        code_ << "    mov.w " << shiftedWord << ",r0\n";
        code_ << "    push.w r0\n";
    }
}

void M32cCodegen::genWideStore(const ir::Slot *slot, int words) {
    if (words == 2) {
        code_ << "    popl r2r0\n";
        code_ << "    movl r2r0," << operandOf(slot) << "\n";
        return;
    }
    for (int i = 0; i < words; i++) {
        code_ << "    pop.w r0\n";
        code_ << "    mov.w r0," << wideWordOperand(slot, i) << "\n";
    }
}

// `lhs == nullptr` means the caller (CompoundAssign's wide path) has
// already pushed the left operand's `words`-word block itself (e.g. via
// genWideLoad()); this lets that one caller reuse the exact same
// 32-bit-pair fast path/multi-word-loop split below instead of duplicating
// it.
void M32cCodegen::genWideAdd(const ir::Expr *lhs, const ir::Expr &rhs, bool isAdd, int words, bool lhsAlreadyPushed,
                              const std::string &opOverride, bool isUnsignedOverride) {
    if (!lhsAlreadyPushed) genExpr(*lhs);
    std::string op = opOverride.empty() ? (isAdd ? "+" : "-") : opOverride;
    if (words == 2) {
        genExpr(rhs);
        code_ << "    popl r3r1\n";  // r3r1 = rhs
        code_ << "    popl r2r0\n";  // r2r0 = lhs
        if (op == "+") {
            code_ << "    addl r3r1,r2r0\n";
        } else if (op == "-") {
            code_ << "    subl r3r1,r2r0\n";
        } else if (op == "*") {
            code_ << "    mull r3r1,r2r0\n";  // truncated low 32 bits - same result whether signed or unsigned
        } else if (op == "/" || op == "%") {
            genDivModCoreLong(op == "%", isUnsignedOverride);
        } else {  // "&"/"|"/"^"
            const char *insn = op == "&" ? "andl" : op == "|" ? "orl" : "xorl";
            code_ << "    " << insn << " r3r1,r2r0\n";
        }
        code_ << "    pushl r2r0\n";
        return;
    }
    // `long long` (words != 2): no 32-bit-pair fast path exists above this
    // width, and no 64-bit hardware multiply/divide exists either - same
    // known gap M16cCodegen.cpp's own genWideMul()/genWideDivMod() document.
    if (op == "*" || op == "/" || op == "%") {
        err(rhs.line, "multiplying/dividing two 'long long' values isn't supported on this target - "
                      "see M32cCodegen.cpp's own genWideAdd() comment");
    }
    genExpr(rhs);
    for (int i = 0; i < words; i++) {
        std::string rhsOperand = "[sp+" + std::to_string(2 * i) + "]";
        std::string lhsOperand = "[sp+" + std::to_string(2 * words + 2 * i) + "]";
        std::string mnemonic;
        if (op == "+" || op == "-") {
            mnemonic = i == 0 ? (op == "+" ? "add.w" : "sub.w") : (op == "+" ? "adc.w" : "sbb.w");
        } else {
            mnemonic = op == "&" ? "and.w" : op == "|" ? "or.w" : "xor.w";
        }
        code_ << "    " << mnemonic << " " << rhsOperand << "," << lhsOperand << "\n";
    }
    code_ << "    add.w " << (2 * words) << ",sp\n";
}

void M32cCodegen::genWideNegate(int words) {
    if (words == 2) {
        code_ << "    popl r2r0\n";
        code_ << "    negl r2r0\n";
        code_ << "    pushl r2r0\n";
        return;
    }
    for (int i = 0; i < words; i++) code_ << "    not.w [sp+" << (2 * i) << "]\n";
    code_ << "    add.w 1,[sp+0]\n";
    for (int i = 1; i < words; i++) code_ << "    adc.w 0,[sp+" << (2 * i) << "]\n";
}

void M32cCodegen::genWideBitwise(const ir::Expr *lhs, const ir::Expr &rhs, const std::string &op, int words) {
    genWideAdd(lhs, rhs, /*isAdd=*/false, words, /*lhsAlreadyPushed=*/false, op, /*isUnsignedOverride=*/false);
}

void M32cCodegen::genWideCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned,
                                   int words) {
    genExpr(lhs);
    genExpr(rhs);
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    if (words == 2) {
        code_ << "    popl r3r1\n";  // rhs
        code_ << "    popl r2r0\n";  // lhs
        code_ << "    cmpl r3r1,r2r0\n";
        std::string signedJ = op == "<" ? "jlt" : op == "<=" ? "jle" : op == ">" ? "jgt" : "jge";
        std::string unsignedJ = op == "<" ? "jltu" : op == "<=" ? "jleu" : op == ">" ? "jgtu" : "jgeu";
        std::string jump = op == "==" ? "jeq" : op == "!=" ? "jne" : (isUnsigned ? unsignedJ : signedJ);
        code_ << "    " << jump << " " << Ltrue << "\n";
        code_ << "    mov.w 0,r0\n    jmp " << Ldone << "\n";
        code_ << Ltrue << ":\n    mov.w 1,r0\n";
        code_ << Ldone << ":\n";
        return;
    }
    if (op == "==" || op == "!=") {
        for (int i = words - 1; i >= 0; i--) {
            std::string lhsOperand = "[sp+" + std::to_string(2 * words + 2 * i) + "]";
            std::string rhsOperand = "[sp+" + std::to_string(2 * i) + "]";
            code_ << "    cmp.w " << rhsOperand << "," << lhsOperand << "\n";
            code_ << "    jne " << (op == "==" ? Lfalse : Ltrue) << "\n";
        }
        code_ << "    jmp " << (op == "==" ? Ltrue : Lfalse) << "\n";
    } else {
        std::string signedJ = op == "<" ? "jlt" : op == "<=" ? "jle" : op == ">" ? "jgt" : "jge";
        std::string unsignedJ = op == "<" ? "jltu" : op == "<=" ? "jleu" : op == ">" ? "jgtu" : "jgeu";
        for (int i = words - 1; i >= 0; i--) {
            std::string lhsOperand = "[sp+" + std::to_string(2 * words + 2 * i) + "]";
            std::string rhsOperand = "[sp+" + std::to_string(2 * i) + "]";
            std::string Lcontinue = newLabel();
            code_ << "    cmp.w " << rhsOperand << "," << lhsOperand << "\n";
            code_ << "    jeq " << Lcontinue << "\n";
            bool useSigned = (i == words - 1) && !isUnsigned;
            code_ << "    " << (useSigned ? signedJ : unsignedJ) << " " << Ltrue << "\n";
            code_ << "    jmp " << Lfalse << "\n";
            code_ << Lcontinue << ":\n";
        }
        code_ << "    jmp " << Lfalse << "\n";
    }
    code_ << Lfalse << ":\n    mov.w 0,r0\n    jmp " << Ldone << "\n";
    code_ << Ltrue << ":\n    mov.w 1,r0\n";
    code_ << Ldone << ":\n";
    code_ << "    add.w " << (4 * words) << ",sp\n";
}

void M32cCodegen::genWideShift(const ir::Expr *lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned, int words,
                                bool lhsAlreadyPushed) {
    if (!lhsAlreadyPushed) genExpr(*lhs);
    if (words == 2) {
        genExpr(rhs);                  // r0 = shift count
        code_ << "    mov.w r0,r1\n";  // stash count in r1 (free: r3r1's "pair" meaning isn't live yet)
        code_ << "    popl r2r0\n";    // r2r0 = value to shift
        if (!isLeft) code_ << "    neg.w r1\n";
        code_ << "    " << ((isLeft || isUnsigned) ? "shll" : "shal") << " r1,r2r0\n";
        code_ << "    pushl r2r0\n";
        return;
    }
    code_ << "    push.w r1\n";
    genExpr(rhs);
    code_ << "    mov.w r0,[sp]\n";
    std::string Lloop = newLabel(), Ldone = newLabel();
    code_ << Lloop << ":\n";
    code_ << "    cmp.w 0,[sp]\n    jeq " << Ldone << "\n";
    if (isLeft) {
        code_ << "    clc\n";
        for (int i = 0; i < words; i++) code_ << "    rol.w [sp+" << (2 + 2 * i) << "]\n";
    } else {
        if (isUnsigned) {
            code_ << "    clc\n";
        } else {
            std::string topWord = "[sp+" + std::to_string(2 + 2 * (words - 1)) + "]";
            std::string Lneg = newLabel(), Lcdone = newLabel();
            code_ << "    cmp.w 0," << topWord << "\n    jlt " << Lneg << "\n";
            code_ << "    clc\n    jmp " << Lcdone << "\n";
            code_ << Lneg << ":\n    stc\n";
            code_ << Lcdone << ":\n";
        }
        for (int i = words - 1; i >= 0; i--) code_ << "    ror.w [sp+" << (2 + 2 * i) << "]\n";
    }
    code_ << "    sub.w 1,[sp]\n";
    code_ << "    jmp " << Lloop << "\n";
    code_ << Ldone << ":\n";
    code_ << "    pop.w r1\n";
}

// Unlike M16cCodegen.cpp's own genWideMul() (which has no 32-bit hardware
// multiply to call and so is left unimplemented), `long`/`unsigned long`
// multiply on this target reuses genWideAdd()'s `r2r0`/`r3r1` fast path
// (see its `op == "*"` branch) - `MUL.L`'s low 32 result bits are the same
// whether the operands are signed or unsigned, so no separate signed path
// is needed, same as the word-level `MUL.W` genMul() already relies on.
// `long long` multiply still has no corresponding 64-bit hardware, so it
// stays unimplemented for `words != 2` (see docs/known-bugs.md).
void M32cCodegen::genWideMul(const ir::Expr *lhs, const ir::Expr &rhs, int words) {
    if (words == 2) {
        genWideAdd(lhs, rhs, /*isAdd=*/true, words, /*lhsAlreadyPushed=*/false, "*", /*isUnsignedOverride=*/false);
        return;
    }
    err(lhs->line, "multiplying two 'long long' values isn't supported on this target - "
                  "see M32cCodegen.cpp's own genWideMul() comment");
    (void)rhs;
}

// `long`/`unsigned long` divide/modulo reuses genDivModCoreLong() via
// genWideAdd()'s fast path. `long long` has no 64-bit divide hardware
// either, so it stays unimplemented, same gap M16C has (see
// docs/known-bugs.md).
void M32cCodegen::genWideDivMod(const ir::Expr *lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned,
                                 int words) {
    if (words == 2) {
        genWideAdd(lhs, rhs, /*isAdd=*/true, words, /*lhsAlreadyPushed=*/false, wantRemainder ? "%" : "/",
                   isUnsigned);
        return;
    }
    err(lhs->line, "dividing two 'long long' values isn't supported on this target - "
                  "see M32cCodegen.cpp's own genWideDivMod() comment");
    (void)rhs;
}

void M32cCodegen::genConvert(const ir::Expr &e) {
    Type from = e.lhs->type;
    Type to = e.type;
    bool fromWide = isWide(from);
    bool toWide = isWide(to);
    if (!fromWide && !toWide) {
        genExpr(*e.lhs);
        if (widthOfType(to) == 1) {
            if (to.isUnsigned) {
                code_ << "    and.w 0x00FF,r0\n";
            } else {
                code_ << "    movb r0,r0\n";
            }
        }
        return;
    }
    if (fromWide && !toWide) {
        int words = wordCountOf(from);
        genExpr(*e.lhs);
        code_ << "    mov.w [sp],r0\n";
        code_ << "    add.w " << (2 * words) << ",sp\n";
        if (widthOfType(to) == 1) {
            code_ << "    " << (to.isUnsigned ? "and.w 0x00FF,r0\n" : "movb r0,r0\n");
        }
        return;
    }
    if (!fromWide && toWide) {
        int toWords = wordCountOf(to);
        genExpr(*e.lhs);
        code_ << "    push.w r0\n";
        if (from.isUnsigned) {
            for (int i = 1; i < toWords; i++) code_ << "    mov.w 0,r0\n    push.w r0\n";
        } else {
            genSignFillWord();
            for (int i = 1; i < toWords; i++) code_ << "    push.w r1\n";
        }
        return;
    }
    int fromWords = wordCountOf(from);
    int toWords = wordCountOf(to);
    genExpr(*e.lhs);
    if (toWords > fromWords) {
        code_ << "    mov.w [sp+" << (2 * (fromWords - 1)) << "],r0\n";
        if (from.isUnsigned) {
            code_ << "    mov.w 0,r1\n";
        } else {
            genSignFillWord();
        }
        for (int i = 0; i < toWords - fromWords; i++) code_ << "    push.w r1\n";
    } else if (toWords < fromWords) {
        code_ << "    add.w " << (2 * (fromWords - toWords)) << ",sp\n";
    }
}

void M32cCodegen::genSignFillWord() {
    std::string Lneg = newLabel(), Ldone = newLabel();
    code_ << "    cmp.w 0,r0\n    jlt " << Lneg << "\n";
    code_ << "    mov.w 0,r1\n    jmp " << Ldone << "\n";
    code_ << Lneg << ":\n    mov.w 0xFFFF,r1\n";
    code_ << Ldone << ":\n";
}

std::optional<std::string> M32cCodegen::assembleInProcess(const std::string &asmText,
                                                           const std::string &format) const {
    if (format == "raw") return assembleM32c(asmText);
    if (auto kind = fileFormatKindFromName(format)) {
        ObjectFile obj = assembleM32cObject(asmText);
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

}  // namespace rcc
