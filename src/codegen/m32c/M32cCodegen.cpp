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
        emit("mov.w", cat(arrayElemText(slot->arrayInit[i]), ",r0"));
        int off = ir::localLowOffset(*slot, typeWidths()) - stride * static_cast<int>(i);
        emit((isChar ? "movb" : "mov.w"), cat("r0,[fb-", off, "]"));
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

    emitRaw(cat("org ", (objectMode ? 0 : kBaseAddress)));
    emitRaw("");
    if (!objectMode) {
        emitLabel("_start");
        emit("mov.w", cat(kInitialSp, ",sp"));
        emit("jsr.w", label(mainFn));
        emitLabel("halt");
        emit("jmp", "halt");
        emitRaw("");
    }
    flush(mainFn ? mainFn->sourceFile : std::string());

    for (const auto &fb : prog.functionBodies) {
        genFunction(fb);
        flush(fb.func->sourceFile);
    }

    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        if (slot->isExternal) continue;
        if (debugMapEnabled_) debugSymbols_.push_back({label(slot.get()), /*isFunc=*/false});
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
    lastDebugLine_ = 0;
    if (debugMapEnabled_) debugSymbols_.push_back({label(fn.func), /*isFunc=*/true});
    emitLabel(label(fn.func));
    emit("push.w", "fb");
    emit("mov.w", "sp,fb");
    if (fn.func->frameSize > 0) emit("sub.w", cat(fn.func->frameSize, ",sp"));
    genStmt(*fn.body);
    emit("mov.w", "fb,sp");
    emit("pop.w", "fb");
    emit("rts");
    emitRaw("");
}

void M32cCodegen::genStmt(const ir::Stmt &s) {
    emitSourceLine(s.line);
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
            emit("jeq", (s.elseStmt ? Lelse : Ldone));
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
            std::string Lstart = newLabel(), Lend = newLabel();
            std::string Lcont = s.postStmt ? newLabel() : Lstart;
            emitLabel(Lstart);
            genExpr(*s.cond);
            testTruthy(*s.cond);
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
            if (s.initOrReturnOrExpr) {
                genExpr(*s.initOrReturnOrExpr);
                if (isWide(s.initOrReturnOrExpr->type)) {
                    int words = wordCountOf(s.initOrReturnOrExpr->type);
                    for (int i = 0; i < words; i++) emit("pop.w", cat("r", i));
                }
            }
            emit("mov.w", "fb,sp");
            emit("pop.w", "fb");
            emit("rts");
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
            emitVerbatim(s.asmText);
            if (!s.asmText.empty() && s.asmText.back() != '\n') emitRaw("");
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

void M32cCodegen::genExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit: {
            if (isWide(e.type)) {
                genWideLoadLiteral(e.intValue, wordCountOf(e.type));
                return;
            }
            emit("mov.w", cat(static_cast<int>(static_cast<uint16_t>(e.intValue)), ",r0"));
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
                emit("push.w", "a0");
                genExpr(*e.rhs);
                emit("pop.w", "a0");
                emit((widthOfType(e.slot->type) == 1 ? "stb" : "st"), "r0,a0");
                return;
            }
            if (isWide(e.slot->type)) {
                genExpr(*e.rhs);
                int words = wordCountOf(e.slot->type);
                for (int i = 0; i < words; i++) {
                    emit("mov.w", cat("[sp+", (2 * i), "],r0"));
                    emit("mov.w", cat("r0,", wideWordOperand(e.slot, i)));
                }
                emit("add.w", cat((2 * words), ",sp"));
                return;
            }
            genExpr(*e.rhs);
            storeOperand(operandOf(e.slot), e.slot->type);
            return;
        }
        case ir::ExprKind::DerefAssign: {
            genExpr(*e.lhs);
            emit("mov.w", "r0,a0");
            emit("push.w", "a0");
            genExpr(*e.rhs);
            if (isWide(e.type)) {
                int words = wordCountOf(e.type);
                emit("mov.w", cat("[sp+", (2 * words), "],a0"));
                for (int i = 0; i < words; i++) {
                    emit("pop.w", "r0");
                    emit("st", "r0,a0");
                    if (i + 1 < words) emit("add.w", "2,a0");
                }
                emit("add.w", "2,sp");
                return;
            }
            emit("pop.w", "a0");
            emit((widthOfType(e.type) == 1 ? "stb" : "st"), "r0,a0");
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
                        emit("mov.w", cat("[sp+", (2 * (words - 1)), "],r0"));
                        emit("push.w", "r0");
                    }
                }
                genWideStore(e.slot, words);
                return;
            }
            genExpr(*e.rhs);
            emit("mov.w", "r0,r1");
            loadOperand(operandOf(e.slot), e.type);
            if (e.yieldOld) emit("push.w", "r0");
            genCompoundOp(e.op, e.type);
            narrowR0To(e.type);
            storeOperand(operandOf(e.slot), e.type);
            if (e.yieldOld) emit("pop.w", "r0");
            return;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            genExpr(*e.lhs);
            emit("mov.w", "r0,a0");
            genCompoundThroughA0(e);
            return;
        }
        case ir::ExprKind::Ternary: {
            genExpr(*e.lhs);
            testTruthy(*e.lhs);
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
                if (isWide(e.rhs->type)) {
                    genWideNegate(wordCountOf(e.rhs->type));
                } else {
                    emit("neg.w", "r0");
                }
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                genExpr(*e.rhs);
                emit("mov.w", "r0,a0");
                if (isWide(e.type)) {
                    int words = wordCountOf(e.type);
                    if (words > 1) emit("add.w", cat((2 * (words - 1)), ",a0"));
                    for (int i = words - 1; i >= 0; i--) {
                        emit("ld", "a0,r0");
                        emit("push.w", "r0");
                        if (i > 0) emit("sub.w", "2,a0");
                    }
                    return;
                }
                emit((widthOfType(e.type) == 1 ? "ldb" : "ld"), "a0,r0");
                if (widthOfType(e.type) == 1 && e.type.isUnsigned) emit("and.w", "0x00FF,r0");
            } else if (e.op == "~") {
                genExpr(*e.rhs);
                emit("not.w", "r0");
                if (isWide(e.rhs->type)) {
                    for (int i = 1; i < wordCountOf(e.rhs->type); i++) emit("not.w", cat("[sp+", (2 * i), "]"));
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
                    emit("push.w", "r0");
                    totalWords += 1;
                }
            }
            emit("jsr.w", label(e.func));
            if (totalWords > 0) emit("add.w", cat((2 * totalWords), ",sp"));
            if (isWide(e.type)) {
                int words = wordCountOf(e.type);
                for (int i = words - 1; i >= 0; i--) {
                    emit("mov.w", cat("r", i, ",r0"));
                    emit("push.w", "r0");
                }
            }
            return;
        }
        case ir::ExprKind::CallPtr: {
            genExpr(*e.lhs);
            std::string T = allocTemp();
            declareTemp(T);
            emit("mov.w", cat("r0,", T));
            emit("jsri.w", T);
            return;
        }
        case ir::ExprKind::Binary: {
            genBinary(e);
            return;
        }
        case ir::ExprKind::Intrinsic: {
            emit((e.op == "ion" ? "fset i" : "fclr i"));
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
    if (shift > 0) emit("shl.w", cat(shift, ",r0"));
    if (slot->arraySize > 0) {
        emit("lea", cat(operandOf(slot), ",a0"));
        emit("add.w", "r0,a0");
    } else {
        emit("mov.w", cat(operandOf(slot), ",a0"));
        emit("add.w", "r0,a0");
    }
}

void M32cCodegen::genIndex(const ir::Expr &e) {
    genIndexAddr(e.slot, *e.lhs);
    emit((widthOfType(e.slot->type) == 1 ? "ldb" : "ld"), "a0,r0");
    if (widthOfType(e.slot->type) == 1 && e.slot->type.isUnsigned) emit("and.w", "0x00FF,r0");
}

void M32cCodegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        emit("lea", cat(operandOf(operand.slot), ",r0"));
        return;
    }
    if (operand.kind == ir::ExprKind::Index) {
        genIndexAddr(operand.slot, *operand.lhs);
        emit("mov.w", "a0,r0");
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        emit("lea", cat(label(operand.func), ",r0"));
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
        emit("push.w", "r0");
        genExpr(*e.rhs);
        emit("pop.w", "r1");
        if (op == "+") {
            emit("add.w", "r1,r0");
        } else {
            emit("sub.w", "r0,r1");
            emit("mov.w", "r1,r0");
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
        emit("push.w", "r0");
        genExpr(*e.rhs);
        emit("pop.w", "r1");
        const char *insn = (op == "&") ? "and.w" : (op == "|") ? "or.w" : "xor.w";
        emit(insn, "r1,r0");
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
    emit("push.w", "r0");
    genExpr(rhs);
    emit("mov.w", "r0,r1");
    emit("pop.w", "r0");
    if (!isLeft) emit("neg.w", "r1");
    emit(((isLeft || isUnsigned) ? "shl.w" : "sha.w"), "r1,r0");
}

void M32cCodegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    emit("push.w", "r0");
    genExpr(rhs);
    emit("pop.w", "r1");
    emit("mul.w", "r1,r0");
}

void M32cCodegen::genDivModCore(bool wantRemainder, bool isUnsigned) {
    if (isUnsigned) {
        emit("mov.w", "0,r2");
        emit("divu.w", "r1,r0");
        if (wantRemainder) emit("mov.w", "r2,r0");
        return;
    }
    std::string Ld = newLabel(), Lr = newLabel();
    emit("mov.w", "0,r3");
    emit("cmp.w", "0,r0");
    emit("jge", Ld);
    emit("neg.w", "r0");
    emit("mov.w", "1,r3");
    emitLabel(Ld);
    emit("push.w", "r3");
    emit("mov.w", "0,r3");
    emit("cmp.w", "0,r1");
    emit("jge", Lr);
    emit("neg.w", "r1");
    emit("mov.w", "1,r3");
    emitLabel(Lr);
    emit("mov.w", "0,r2");
    emit("divu.w", "r1,r0");
    std::string Lskip = newLabel();
    if (wantRemainder) {
        emit("mov.w", "r2,r0");
        emit("pop.w", "r3");
        emit("cmp.w", "0,r3");
        emit("jeq", Lskip);
        emit("neg.w", "r0");
        emitLabel(Lskip);
        return;
    }
    emit("xor.w", "r3,[sp]");
    emit("pop.w", "r3");
    emit("cmp.w", "0,r3");
    emit("jeq", Lskip);
    emit("neg.w", "r0");
    emitLabel(Lskip);
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
        emit("divul", "r3r1,r2r0");  // r2r0 = quotient, r3r1 = remainder
        if (wantRemainder) emit("movl", "r3r1,r2r0");
        return;
    }
    std::string flagD = allocTemp();
    declareTemp(flagD);
    std::string flagS = allocTemp();
    declareTemp(flagS);
    std::string Ld = newLabel(), Lr = newLabel(), Lskip = newLabel();
    emit("mov.w", cat("0,", flagD));
    emit("cmpl", "0,r2r0");
    emit("jge", Ld);
    emit("negl", "r2r0");
    emit("mov.w", cat("1,", flagD));
    emitLabel(Ld);
    emit("mov.w", cat("0,", flagS));
    emit("cmpl", "0,r3r1");
    emit("jge", Lr);
    emit("negl", "r3r1");
    emit("mov.w", cat("1,", flagS));
    emitLabel(Lr);
    emit("divxl", "r3r1,r2r0");  // |quotient| in r2r0, |remainder| in r3r1
    if (wantRemainder) {
        emit("movl", "r3r1,r2r0");
        emit("mov.w", cat(flagD, ",r1"));  // r1 is free again: r3r1's own value is no longer needed
        emit("cmp.w", "0,r1");
        emit("jeq", Lskip);
        emit("negl", "r2r0");
        emitLabel(Lskip);
        return;
    }
    emit("mov.w", cat(flagD, ",r1"));
    emit("xor.w", cat(flagS, ",r1"));
    emit("cmp.w", "0,r1");
    emit("jeq", Lskip);
    emit("negl", "r2r0");
    emitLabel(Lskip);
}

void M32cCodegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    emit("push.w", "r0");
    genExpr(rhs);
    emit("mov.w", "r0,r1");
    emit("pop.w", "r0");
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
    emit("push.w", "r0");
    genExpr(rhs);
    emit("pop.w", "r1");
    emit("cmp.w", "r0,r1");
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit(jumpIfTrue, Ltrue);
    emit("mov.w", "0,r0");
    emit("jmp", Ldone);
    emitLabel(Ltrue);
    emit("mov.w", "1,r0");
    emitLabel(Ldone);
}

void M32cCodegen::testTruthy(const ir::Expr &e) {
    if (!isWide(e.type)) {
        emit("cmp.w", "0,r0");
        return;
    }
    int words = wordCountOf(e.type);
    emit("mov.w", "[sp],r0");
    for (int i = 1; i < words; i++) emit("or.w", cat("[sp+", (2 * i), "],r0"));
    emit("add.w", cat((2 * words), ",sp"));
    emit("cmp.w", "0,r0");
}

void M32cCodegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    genExpr(lhs);
    testTruthy(lhs);
    emit("jeq", Lfalse);
    genExpr(rhs);
    testTruthy(rhs);
    emit("jeq", Lfalse);
    emitLabel(Ltrue);
    emit("mov.w", "1,r0");
    emit("jmp", Ldone);
    emitLabel(Lfalse);
    emit("mov.w", "0,r0");
    emitLabel(Ldone);
}

void M32cCodegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    genExpr(lhs);
    testTruthy(lhs);
    emit("jne", Ltrue);
    genExpr(rhs);
    testTruthy(rhs);
    emit("jne", Ltrue);
    emitLabel(Lfalse);
    emit("mov.w", "0,r0");
    emit("jmp", Ldone);
    emitLabel(Ltrue);
    emit("mov.w", "1,r0");
    emitLabel(Ldone);
}

void M32cCodegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    testTruthy(rhs);
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit("jeq", Ltrue);
    emit("mov.w", "0,r0");
    emit("jmp", Ldone);
    emitLabel(Ltrue);
    emit("mov.w", "1,r0");
    emitLabel(Ldone);
}

void M32cCodegen::loadOperand(const std::string &mem, Type type) {
    if (widthOfType(type) == 1) {
        emit("movb", cat(mem, ",r0"));
        if (type.isUnsigned) emit("and.w", "0x00FF,r0");
    } else {
        emit("mov.w", cat(mem, ",r0"));
    }
}

void M32cCodegen::storeOperand(const std::string &mem, Type type) {
    if (widthOfType(type) == 1) {
        emit("movb", cat("r0,", mem));
    } else {
        emit("mov.w", cat("r0,", mem));
    }
}

void M32cCodegen::narrowR0To(Type to) {
    if (widthOfType(to) != 1) return;
    if (to.isUnsigned) {
        emit("and.w", "0x00FF,r0");
        return;
    }
    emit("movb", "r0,r0");
}

void M32cCodegen::genCompoundOp(const std::string &op, Type type) {
    bool isUnsigned = type.isUnsigned;
    if (op == "+") {
        emit("add.w", "r1,r0");
        return;
    }
    if (op == "-") {
        emit("sub.w", "r1,r0");
        return;
    }
    if (op == "*") {
        emit("mul.w", "r1,r0");
        return;
    }
    if (op == "/" || op == "%") {
        genDivModCore(op == "%", isUnsigned);
        return;
    }
    if (op == "&" || op == "|" || op == "^") {
        const char *insn = (op == "&") ? "and.w" : (op == "|") ? "or.w" : "xor.w";
        emit(insn, "r1,r0");
        return;
    }
    if (op != "<<") emit("neg.w", "r1");
    emit(((op == "<<" || isUnsigned) ? "shl.w" : "sha.w"), "r1,r0");
}

void M32cCodegen::genCompoundThroughA0(const ir::Expr &e) {
    emit("push.w", "a0");
    genExpr(*e.rhs);
    emit("mov.w", "r0,r1");
    emit("pop.w", "a0");
    emit((widthOfType(e.type) == 1 ? "ldb" : "ld"), "a0,r0");
    if (widthOfType(e.type) == 1 && e.type.isUnsigned) emit("and.w", "0x00FF,r0");
    if (e.yieldOld) emit("push.w", "r0");
    genCompoundOp(e.op, e.type);
    narrowR0To(e.type);
    emit((widthOfType(e.type) == 1 ? "stb" : "st"), "r0,a0");
    if (e.yieldOld) emit("pop.w", "r0");
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
        emit("movl", cat(operandOf(slot), ",r2r0"));
        emit("pushl", "r2r0");
        return;
    }
    for (int i = words - 1; i >= 0; i--) {
        emit("mov.w", cat(wideWordOperand(slot, i), ",r0"));
        emit("push.w", "r0");
    }
}

void M32cCodegen::genWideLoadLiteral(long value, int words) {
    if (words == 2) {
        emit("movl", cat(static_cast<uint32_t>(value), ",r2r0"));
        emit("pushl", "r2r0");
        return;
    }
    auto uv = static_cast<unsigned long>(value);
    for (int i = words - 1; i >= 0; i--) {
        int shiftedWord = static_cast<int>((uv >> (16 * i)) & 0xFFFF);
        emit("mov.w", cat(shiftedWord, ",r0"));
        emit("push.w", "r0");
    }
}

void M32cCodegen::genWideStore(const ir::Slot *slot, int words) {
    if (words == 2) {
        emit("popl", "r2r0");
        emit("movl", cat("r2r0,", operandOf(slot)));
        return;
    }
    for (int i = 0; i < words; i++) {
        emit("pop.w", "r0");
        emit("mov.w", cat("r0,", wideWordOperand(slot, i)));
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
        emit("popl", "r3r1");  // r3r1 = rhs
        emit("popl", "r2r0");  // r2r0 = lhs
        if (op == "+") {
            emit("addl", "r3r1,r2r0");
        } else if (op == "-") {
            emit("subl", "r3r1,r2r0");
        } else if (op == "*") {
            emit("mull", "r3r1,r2r0");  // truncated low 32 bits - same result whether signed or unsigned
        } else if (op == "/" || op == "%") {
            genDivModCoreLong(op == "%", isUnsignedOverride);
        } else {  // "&"/"|"/"^"
            const char *insn = op == "&" ? "andl" : op == "|" ? "orl" : "xorl";
            emit(insn, "r3r1,r2r0");
        }
        emit("pushl", "r2r0");
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
        emit(mnemonic, cat(rhsOperand, ",", lhsOperand));
    }
    emit("add.w", cat((2 * words), ",sp"));
}

void M32cCodegen::genWideNegate(int words) {
    if (words == 2) {
        emit("popl", "r2r0");
        emit("negl", "r2r0");
        emit("pushl", "r2r0");
        return;
    }
    for (int i = 0; i < words; i++) emit("not.w", cat("[sp+", (2 * i), "]"));
    emit("add.w", "1,[sp+0]");
    for (int i = 1; i < words; i++) emit("adc.w", cat("0,[sp+", (2 * i), "]"));
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
        emit("popl", "r3r1");  // rhs
        emit("popl", "r2r0");  // lhs
        emit("cmpl", "r3r1,r2r0");
        std::string signedJ = op == "<" ? "jlt" : op == "<=" ? "jle" : op == ">" ? "jgt" : "jge";
        std::string unsignedJ = op == "<" ? "jltu" : op == "<=" ? "jleu" : op == ">" ? "jgtu" : "jgeu";
        std::string jump = op == "==" ? "jeq" : op == "!=" ? "jne" : (isUnsigned ? unsignedJ : signedJ);
        emit(jump, Ltrue);
        emit("mov.w", "0,r0");
        emit("jmp", Ldone);
        emitLabel(Ltrue);
        emit("mov.w", "1,r0");
        emitLabel(Ldone);
        return;
    }
    if (op == "==" || op == "!=") {
        for (int i = words - 1; i >= 0; i--) {
            std::string lhsOperand = "[sp+" + std::to_string(2 * words + 2 * i) + "]";
            std::string rhsOperand = "[sp+" + std::to_string(2 * i) + "]";
            emit("cmp.w", cat(rhsOperand, ",", lhsOperand));
            emit("jne", (op == "==" ? Lfalse : Ltrue));
        }
        emit("jmp", (op == "==" ? Ltrue : Lfalse));
    } else {
        std::string signedJ = op == "<" ? "jlt" : op == "<=" ? "jle" : op == ">" ? "jgt" : "jge";
        std::string unsignedJ = op == "<" ? "jltu" : op == "<=" ? "jleu" : op == ">" ? "jgtu" : "jgeu";
        for (int i = words - 1; i >= 0; i--) {
            std::string lhsOperand = "[sp+" + std::to_string(2 * words + 2 * i) + "]";
            std::string rhsOperand = "[sp+" + std::to_string(2 * i) + "]";
            std::string Lcontinue = newLabel();
            emit("cmp.w", cat(rhsOperand, ",", lhsOperand));
            emit("jeq", Lcontinue);
            bool useSigned = (i == words - 1) && !isUnsigned;
            emit((useSigned ? signedJ : unsignedJ), Ltrue);
            emit("jmp", Lfalse);
            emitLabel(Lcontinue);
        }
        emit("jmp", Lfalse);
    }
    emitLabel(Lfalse);
    emit("mov.w", "0,r0");
    emit("jmp", Ldone);
    emitLabel(Ltrue);
    emit("mov.w", "1,r0");
    emitLabel(Ldone);
    emit("add.w", cat((4 * words), ",sp"));
}

void M32cCodegen::genWideShift(const ir::Expr *lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned, int words,
                                bool lhsAlreadyPushed) {
    if (!lhsAlreadyPushed) genExpr(*lhs);
    if (words == 2) {
        genExpr(rhs);                  // r0 = shift count
        emit("mov.w", "r0,r1");  // stash count in r1 (free: r3r1's "pair" meaning isn't live yet)
        emit("popl", "r2r0");    // r2r0 = value to shift
        if (!isLeft) emit("neg.w", "r1");
        emit(((isLeft || isUnsigned) ? "shll" : "shal"), "r1,r2r0");
        emit("pushl", "r2r0");
        return;
    }
    emit("push.w", "r1");
    genExpr(rhs);
    emit("mov.w", "r0,[sp]");
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("cmp.w", "0,[sp]");
    emit("jeq", Ldone);
    if (isLeft) {
        emit("clc");
        for (int i = 0; i < words; i++) emit("rol.w", cat("[sp+", (2 + 2 * i), "]"));
    } else {
        if (isUnsigned) {
            emit("clc");
        } else {
            std::string topWord = "[sp+" + std::to_string(2 + 2 * (words - 1)) + "]";
            std::string Lneg = newLabel(), Lcdone = newLabel();
            emit("cmp.w", cat("0,", topWord));
            emit("jlt", Lneg);
            emit("clc");
            emit("jmp", Lcdone);
            emitLabel(Lneg);
            emit("stc");
            emitLabel(Lcdone);
        }
        for (int i = words - 1; i >= 0; i--) emit("ror.w", cat("[sp+", (2 + 2 * i), "]"));
    }
    emit("sub.w", "1,[sp]");
    emit("jmp", Lloop);
    emitLabel(Ldone);
    emit("pop.w", "r1");
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
                emit("and.w", "0x00FF,r0");
            } else {
                emit("movb", "r0,r0");
            }
        }
        return;
    }
    if (fromWide && !toWide) {
        int words = wordCountOf(from);
        genExpr(*e.lhs);
        emit("mov.w", "[sp],r0");
        emit("add.w", cat((2 * words), ",sp"));
        if (widthOfType(to) == 1) {
            emit((to.isUnsigned ? "and.w 0x00FF,r0\n" : "movb r0,r0\n"));
        }
        return;
    }
    if (!fromWide && toWide) {
        int toWords = wordCountOf(to);
        genExpr(*e.lhs);
        emit("push.w", "r0");
        if (from.isUnsigned) {
            for (int i = 1; i < toWords; i++) emit("mov.w", "0,r0");
emit("push.w", "r0");
        } else {
            genSignFillWord();
            for (int i = 1; i < toWords; i++) emit("push.w", "r1");
        }
        return;
    }
    int fromWords = wordCountOf(from);
    int toWords = wordCountOf(to);
    genExpr(*e.lhs);
    if (toWords > fromWords) {
        emit("mov.w", cat("[sp+", (2 * (fromWords - 1)), "],r0"));
        if (from.isUnsigned) {
            emit("mov.w", "0,r1");
        } else {
            genSignFillWord();
        }
        for (int i = 0; i < toWords - fromWords; i++) emit("push.w", "r1");
    } else if (toWords < fromWords) {
        emit("add.w", cat((2 * (fromWords - toWords)), ",sp"));
    }
}

void M32cCodegen::genSignFillWord() {
    std::string Lneg = newLabel(), Ldone = newLabel();
    emit("cmp.w", "0,r0");
    emit("jlt", Lneg);
    emit("mov.w", "0,r1");
    emit("jmp", Ldone);
    emitLabel(Lneg);
    emit("mov.w", "0xFFFF,r1");
    emitLabel(Ldone);
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

std::optional<std::string> M32cCodegen::debugMap(const std::string &asmText, const std::string &format,
        const std::string &sourcePath) const {
    (void)format;
    return assembleM32cDebugMap(asmText, sourcePath, debugSymbols_);
}

std::optional<std::string> M32cCodegen::debugMapPreLink(const std::string &asmText,
                                                    const std::string &sourcePath) const {
    return assembleM32cDebugMap(asmText, sourcePath, debugSymbols_, /*allowExternal=*/true);
}

// Only emits a marker when `debugMapEnabled_` (set via setDebugMapEnabled(),
// before generate() runs) and `line` actually differs from the last one
// emitted - see Pdp7Codegen.cpp's identical emitSourceLine(), which this
// mirrors exactly.
void M32cCodegen::emitSourceLine(int line) {
    if (!debugMapEnabled_ || line == 0 || line == lastDebugLine_) return;
    emitRaw(cat("__line ", line));
    lastDebugLine_ = line;
}

}  // namespace rcc
