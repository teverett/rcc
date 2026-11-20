#include "M16cCodegen.h"

#include <cstdint>
#include <stdexcept>

#include "M16cAssembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

// Load/execution start address for a whole-program raw binary; chosen clear
// of the M16C's fixed low-memory special-function-register block
// (0x0000-0x03FF on real silicon).
constexpr uint32_t kBaseAddress = 0x0400;
// Initial stack pointer, comfortably above the loaded program and its data.
constexpr uint32_t kInitialSp = 0xE000;
}  // namespace

void M16cCodegen::declareTemp(const std::string &lbl) { trailer_.push_back(lbl + ": dw 0"); }

// A literal element's decimal text, or a symbol element's label text.
std::string M16cCodegen::arrayElemText(const ir::ArrayInitElem &e) {
    if (!e.isSymbol) return std::to_string(static_cast<int>(static_cast<uint16_t>(e.value)));
    return e.slot ? label(e.slot) : label(e.func);
}

// `elemType`'s width picks `db` (`char` array, one byte per element) vs `dw`
// (every other array - a `long`/`long long` array element still only
// occupies one native word, see genIndex()).
void M16cCodegen::declareArray(const std::string &lbl, const std::vector<ir::ArrayInitElem> &values, Type elemType) {
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

// Stores arrayInit into `slot`'s frame slots; re-run on every call since a
// Local's storage (unlike a Global's static trailer) isn't pre-set. An
// element's address is `i` strides above element 0's, the array's lowest.
void M16cCodegen::genLocalArrayInit(const ir::Slot *slot) {
    int stride = widthOfType(slot->type) == 1 ? 1 : 2;
    bool isChar = stride == 1;
    for (size_t i = 0; i < slot->arrayInit.size(); i++) {
        emit("mov.w", cat(arrayElemText(slot->arrayInit[i]), ",r0"));
        int off = ir::localLowOffset(*slot, typeWidths()) - stride * static_cast<int>(i);
        emit((isChar ? "movb" : "mov.w"), cat("r0,[fb-", off, "]"));
    }
}

std::string M16cCodegen::generate(const ir::Program &prog, bool objectMode) {
    perFileAsm_.clear();
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    std::string full;
    // Hands the text generated so far to the whole-program output and to the
    // input file it belongs to, leaving code_ empty for the next piece.
    auto flush = [&](const std::string &file) {
        std::string chunk = code_.str();
        code_.str("");
        code_.clear();
        full += chunk;
        perFileAsm_[file] += chunk;
    };

    { emitRaw(cat("org ", (objectMode ? 0 : kBaseAddress))); emitRaw(""); }
    if (!objectMode) {
        emitLabel("_start");
        emit("mov.w", cat(kInitialSp, ",sp"));
        emit("jsr.w", label(mainFn));
        { emitLabel("halt"); emit("jmp", "halt"); emitRaw(""); }
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
            // No struct initializer exists in this subset, so this is
            // always zero-filled; reuses declareArray's own element-list
            // emission rather than the word-pair logic below, which
            // assumes an even byte count - a struct's size isn't
            // necessarily even.
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
                // No 32-/64-bit data directive in this dialect, so a
                // `long`/`long long` global's words are emitted individually
                // (least significant first, matching wideWordOperand()).
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

std::map<std::string, std::string> M16cCodegen::perFileAsm() const { return perFileAsm_; }

void M16cCodegen::genFunction(const ir::FunctionBody &fn) {
    emitLabel(label(fn.func));
    emit("push.w", "fb");
    emit("mov.w", "sp,fb");
    if (fn.func->frameSize > 0) emit("sub.w", cat(fn.func->frameSize, ",sp"));
    genStmt(*fn.body);
    emit("mov.w", "fb,sp");
    emit("pop.w", "fb");
    { emit("rts"); emitRaw(""); }
}

void M16cCodegen::genStmt(const ir::Stmt &s) {
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
                // A wide value sits on the stack (LSW on top); pop it into
                // r0(LSW)..r(words-1)(MSW) before the epilogue's `mov.w
                // fb,sp` discards this call's whole frame.
                if (isWide(s.initOrReturnOrExpr->type)) {
                    int words = wordCountOf(s.initOrReturnOrExpr->type);
                    for (int i = 0; i < words; i++) emit("pop.w", cat("r", i));
                }
            }
            // Inlined frame teardown, since genFunction()'s own epilogue
            // never runs for this early-exit path.
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
            emitRaw(s.asmText);
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

void M16cCodegen::genExpr(const ir::Expr &e) {
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
            if (e.lhs) {  // slot[idx] = rhs - never wide (see genIndex()'s comment)
                genIndexAddr(e.slot, *e.lhs);  // a0 = target address
                emit("push.w", "a0");
                genExpr(*e.rhs);
                emit("pop.w", "a0");
                emit((widthOfType(e.slot->type) == 1 ? "stb" : "st"), "r0,a0");
                return;
            }
            if (isWide(e.slot->type)) {
                genExpr(*e.rhs);  // pushes `words` words
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
            // Stores through at e.type's width. A wide pointee has no
            // `[a0+N]` offset addressing (this dialect's `ld`/`st` only
            // address through a0 with no displacement), so each word is
            // stored through a0 one at a time, advancing it 2 bytes between
            // words.
            genExpr(*e.lhs);  // r0 = pointer value
            emit("mov.w", "r0,a0");
            emit("push.w", "a0");  // save the address below rhs's own pushed words
            genExpr(*e.rhs);
            if (isWide(e.type)) {
                int words = wordCountOf(e.type);
                emit("mov.w", cat("[sp+", (2 * words), "],a0"));
                for (int i = 0; i < words; i++) {
                    emit("pop.w", "r0");
                    emit("st", "r0,a0");
                    if (i + 1 < words) emit("add.w", "2,a0");
                }
                emit("add.w", "2,sp");  // discard the now-read saved address word
                return;
            }
            emit("pop.w", "a0");
            emit((widthOfType(e.type) == 1 ? "stb" : "st"), "r0,a0");
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            if (e.lhs) {  // slot[idx] op= rhs - never wide, same limitation as indexed Assign above
                genIndexAddr(e.slot, *e.lhs);  // a0 = target address
                genCompoundThroughA0(e);
                return;
            }
            genExpr(*e.rhs);              // r0 = rhs
            emit("mov.w", "r0,r1");  // r1 = rhs
            loadOperand(operandOf(e.slot), e.type);  // r0 = old
            if (e.yieldOld) emit("push.w", "r0");
            genCompoundOp(e.op, e.type);
            narrowR0To(e.type);
            storeOperand(operandOf(e.slot), e.type);
            if (e.yieldOld) emit("pop.w", "r0");
            return;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            genExpr(*e.lhs);  // r0 = pointer value
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
                genExpr(*e.rhs);  // r0 = pointer value
                emit("mov.w", "r0,a0");
                if (isWide(e.type)) {
                    // Pushes MSW first .. LSW last (ending on top); each word
                    // read through a0 one at a time (no `[a0+N]` form exists).
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
            } else {  // "&"
                genAddrOf(*e.rhs);
            }
            return;
        }
        case ir::ExprKind::Call: {
            // Arguments are pushed last-declared first, so argument 0 ends
            // up closest to the return address `jsr.w` is about to push
            // (matching frameOperand()'s `fb+4` for paramIndex 0). A wide
            // argument pushes its own `words` words directly; a narrow one
            // is pushed here from r0. The caller cleans the pushed words
            // back off afterward.
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
                // The callee left its wide return value in r0(LSW)..
                // r(words-1)(MSW) (see genStmt()'s Return case); push it
                // back as this expression's result, LSW on top.
                int words = wordCountOf(e.type);
                for (int i = words - 1; i >= 0; i--) {
                    { emit("mov.w", cat("r", i, ",r0")); emit("push.w", "r0"); }
                }
            }
            return;
        }
        case ir::ExprKind::CallPtr: {
            // The real `JSRI.W` indirect-call instruction fetches the entry
            // address from the WORD stored at its operand (not the operand
            // address itself), so the callee's address (just computed into
            // r0) is spilled to a temp cell first, then `jsri.w` points at
            // that cell.
            genExpr(*e.lhs);  // r0 = the callee's own entry address
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

// Leaves slot[idxExpr]'s effective address in `A0`. `slot->arraySize > 0`
// means a compile-time-known base (loaded via `lea`); otherwise `slot` is a
// plain pointer variable, so the base comes from reading its stored value.
//
// Both a Global and a Local array's elements sit at increasing addresses as
// the index increases (frameOperand() gives a Local array's element 0, its
// lowest address), so indexing is always base-plus-scaled-index.
void M16cCodegen::genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
    genExpr(idxExpr);  // r0 = index
    int shift = elemShift(slot->type);
    if (shift > 0) emit("shl.w", cat(shift, ",r0"));  // r0 *= widthOf(slot->type) (a power of two)
    if (slot->arraySize > 0) {
        emit("lea", cat(operandOf(slot), ",a0"));
        emit("add.w", "r0,a0");
    } else {
        emit("mov.w", cat(operandOf(slot), ",a0"));
        emit("add.w", "r0,a0");
    }
}

// A `char` array element is one byte (`ldb`); every other array element
// still only reads one native word (`ld`) regardless of its real width - a
// known gap for `long`/`long long` array elements (genIndexAddr() scales the
// index correctly, but a multi-word element's remaining words are never
// read/written); scalars of every width work fully.
void M16cCodegen::genIndex(const ir::Expr &e) {
    genIndexAddr(e.slot, *e.lhs);
    emit((widthOfType(e.slot->type) == 1 ? "ldb" : "ld"), "a0,r0");
    if (widthOfType(e.slot->type) == 1 && e.slot->type.isUnsigned) emit("and.w", "0x00FF,r0");
}

void M16cCodegen::genAddrOf(const ir::Expr &operand) {
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

void M16cCodegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    // Every operand here shares one common type via `e.lhs->type`, except
    // shift (only the LHS's type matters) and &&/|| (width-independent).
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
        emit("push.w", "r0");
        genExpr(*e.rhs);
        emit("pop.w", "r1");  // r1 = lhs, r0 = rhs
        if (op == "+") {
            emit("add.w", "r1,r0");  // r0 = r0 + r1 = rhs + lhs
        } else {
            emit("sub.w", "r0,r1");  // r1 = r1 - r0 = lhs - rhs
            emit("mov.w", "r1,r0");
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
        emit("push.w", "r0");
        genExpr(*e.rhs);
        emit("pop.w", "r1");  // r1 = lhs, r0 = rhs
        const char *insn = (op == "&") ? "and.w" : (op == "|") ? "or.w" : "xor.w";
        emit(insn, "r1,r0");  // r0 = r0 OP r1 (commutative)
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

// Real variable-count hardware shift: `SHL.W` for a left shift or unsigned
// right shift, `SHA.W` (sign-preserving) for a signed right shift - its
// signed count convention (negative = right) is why `isLeft` only decides
// whether to `NEG.W` the count, not which instruction to use.
void M16cCodegen::genShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned) {
    genExpr(lhs);
    emit("push.w", "r0");
    genExpr(rhs);
    emit("mov.w", "r0,r1");  // r1 = shift count
    emit("pop.w", "r0");     // r0 = value to shift
    if (!isLeft) emit("neg.w", "r1");
    emit(((isLeft || isUnsigned) ? "shl.w" : "sha.w"), "r1,r0");
}

// `MUL.W`'s low result word is identical whether the operands are signed or
// unsigned, so this one routine serves both.
void M16cCodegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    emit("push.w", "r0");
    genExpr(rhs);
    emit("pop.w", "r1");      // r1 = lhs, r0 = rhs
    emit("mul.w", "r1,r0");   // r2:r0 = r0 * r1 = rhs * lhs (high word:low word)
}

// `DIVU.W` only divides unsigned magnitudes, so a signed division/modulo
// records each operand's sign, negates to a non-negative magnitude, divides,
// then restores the quotient's sign (negative iff the signs disagreed) and
// the remainder's sign (always the dividend's, matching C's
// truncate-toward-zero convention).
void M16cCodegen::genDivModCore(bool wantRemainder, bool isUnsigned) {
    if (isUnsigned) {
        emit("mov.w", "0,r2");
        emit("divu.w", "r1,r0");  // r0 = quotient, r2 = remainder
        if (wantRemainder) emit("mov.w", "r2,r0");
        return;
    }
    std::string Ld = newLabel(), Lr = newLabel();
    emit("mov.w", "0,r3");  // r3 = dividend-negative flag
    { emit("cmp.w", "0,r0"); emit("jge", Ld); }
    { emit("neg.w", "r0"); emit("mov.w", "1,r3"); }
    emitLabel(Ld);
    emit("push.w", "r3");   // stash the dividend-negative flag across the divisor's own sign check
    emit("mov.w", "0,r3");  // r3 = divisor-negative flag
    { emit("cmp.w", "0,r1"); emit("jge", Lr); }
    { emit("neg.w", "r1"); emit("mov.w", "1,r3"); }
    emitLabel(Lr);
    emit("mov.w", "0,r2");
    emit("divu.w", "r1,r0");  // r0 = |quotient|, r2 = |remainder|
    std::string Lskip = newLabel();
    if (wantRemainder) {
        emit("mov.w", "r2,r0");
        emit("pop.w", "r3");  // r3 = dividend-negative flag (divisor's sign doesn't affect the remainder)
        { emit("cmp.w", "0,r3"); emit("jeq", Lskip); }
        emit("neg.w", "r0");
        emitLabel(Lskip);
        return;
    }
    emit("xor.w", "r3,[sp]");  // [sp] (dividend-negative) ^= r3 (divisor-negative) -> quotient-negative
    emit("pop.w", "r3");
    { emit("cmp.w", "0,r3"); emit("jeq", Lskip); }
    emit("neg.w", "r0");
    emitLabel(Lskip);
}

void M16cCodegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    emit("push.w", "r0");
    genExpr(rhs);
    emit("mov.w", "r0,r1");  // r1 = divisor
    emit("pop.w", "r0");     // r0 = dividend
    genDivModCore(wantRemainder, isUnsigned);
}

void M16cCodegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned) {
    if (op == "==") return emitCompare(lhs, rhs, "jeq");
    if (op == "!=") return emitCompare(lhs, rhs, "jne");
    if (op == "<") return emitCompare(lhs, rhs, isUnsigned ? "jltu" : "jlt");
    if (op == "<=") return emitCompare(lhs, rhs, isUnsigned ? "jleu" : "jle");
    if (op == ">") return emitCompare(lhs, rhs, isUnsigned ? "jgtu" : "jgt");
    if (op == ">=") return emitCompare(lhs, rhs, isUnsigned ? "jgeu" : "jge");
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

// `CMP.W src,dst` sets flags for `dst - src`, so `cmp.w r0,r1` (r0=rhs,
// r1=lhs) computes `lhs - rhs` with no further sign/overflow fixup needed.
void M16cCodegen::emitCompare(const ir::Expr &lhs, const ir::Expr &rhs, const std::string &jumpIfTrue) {
    genExpr(lhs);
    emit("push.w", "r0");
    genExpr(rhs);
    emit("pop.w", "r1");      // r1 = lhs, r0 = rhs
    emit("cmp.w", "r0,r1");   // flags = r1 - r0 = lhs - rhs
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit(jumpIfTrue, Ltrue);
    { emit("mov.w", "0,r0"); emit("jmp", Ldone); }
    { emitLabel(Ltrue); emit("mov.w", "1,r0"); }
    emitLabel(Ldone);
}

void M16cCodegen::testTruthy(const ir::Expr &e) {
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

void M16cCodegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    genExpr(lhs);
    testTruthy(lhs);
    emit("jeq", Lfalse);
    genExpr(rhs);
    testTruthy(rhs);
    emit("jeq", Lfalse);
    { emitLabel(Ltrue); emit("mov.w", "1,r0"); emit("jmp", Ldone); }
    { emitLabel(Lfalse); emit("mov.w", "0,r0"); }
    emitLabel(Ldone);
}

void M16cCodegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    genExpr(lhs);
    testTruthy(lhs);
    emit("jne", Ltrue);
    genExpr(rhs);
    testTruthy(rhs);
    emit("jne", Ltrue);
    { emitLabel(Lfalse); emit("mov.w", "0,r0"); emit("jmp", Ldone); }
    { emitLabel(Ltrue); emit("mov.w", "1,r0"); }
    emitLabel(Ldone);
}

void M16cCodegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    testTruthy(rhs);
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit("jeq", Ltrue);
    { emit("mov.w", "0,r0"); emit("jmp", Ldone); }
    { emitLabel(Ltrue); emit("mov.w", "1,r0"); }
    emitLabel(Ldone);
}

// `movb` into a register destination always sign-extends to 16 bits, so an
// unsigned `char` has to mask the high byte back off afterward.
void M16cCodegen::loadOperand(const std::string &mem, Type type) {
    if (widthOfType(type) == 1) {
        emit("movb", cat(mem, ",r0"));
        if (type.isUnsigned) emit("and.w", "0x00FF,r0");
    } else {
        emit("mov.w", cat(mem, ",r0"));
    }
}

void M16cCodegen::storeOperand(const std::string &mem, Type type) {
    if (widthOfType(type) == 1) {
        emit("movb", cat("r0,", mem));
    } else {
        emit("mov.w", cat("r0,", mem));
    }
}

void M16cCodegen::narrowR0To(Type to) {
    if (widthOfType(to) != 1) return;  // a native word: r0 already holds the full value
    if (to.isUnsigned) {
        emit("and.w", "0x00FF,r0");
        return;
    }
    emit("movb", "r0,r0");  // narrow to the low byte, then (register dest) sign-extend back up
}

void M16cCodegen::genCompoundOp(const std::string &op, Type type) {
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
        emit("mul.w", "r1,r0");  // r2:r0 = r0*r1 = old*rhs (high word:low word)
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
    // "<<" or ">>" - same signed-count convention as genShift().
    if (op != "<<") emit("neg.w", "r1");
    emit(((op == "<<" || isUnsigned) ? "shl.w" : "sha.w"), "r1,r0");
}

// `e.rhs`'s evaluation can recursively reach codegen that clobbers r0-r3, so
// `a0` is stashed on the stack across it; genCompoundOp() itself never
// touches `a0`.
void M16cCodegen::genCompoundThroughA0(const ir::Expr &e) {
    emit("push.w", "a0");    // stash address across rhs's own evaluation
    genExpr(*e.rhs);               // r0 = rhs
    emit("mov.w", "r0,r1");  // r1 = rhs
    emit("pop.w", "a0");     // a0 restored
    emit((widthOfType(e.type) == 1 ? "ldb" : "ld"), "a0,r0");  // r0 = old
    if (widthOfType(e.type) == 1 && e.type.isUnsigned) emit("and.w", "0x00FF,r0");
    if (e.yieldOld) emit("push.w", "r0");  // stash old - this expr's own result
    genCompoundOp(e.op, e.type);  // r0 = new (raw), using r0=old, r1=rhs
    narrowR0To(e.type);
    emit((widthOfType(e.type) == 1 ? "stb" : "st"), "r0,a0");
    if (e.yieldOld) emit("pop.w", "r0");  // r0 = old (final result)
}

std::string M16cCodegen::wideWordOperand(const ir::Slot *slot, int wordIndex) const {
    int off = 2 * wordIndex;
    if (slot->kind == ir::SlotKind::Global) {
        return off == 0 ? label(slot) : (label(slot) + "+" + std::to_string(off));
    }
    if (slot->kind == ir::SlotKind::Param) {
        return "[fb+" + std::to_string(slot->frameOffset + off) + "]";
    }
    return "[fb-" + std::to_string(slot->frameOffset + off) + "]";
}

void M16cCodegen::genWideLoad(const ir::Slot *slot, int words) {
    for (int i = words - 1; i >= 0; i--) {
        emit("mov.w", cat(wideWordOperand(slot, i), ",r0"));
        emit("push.w", "r0");
    }
}

void M16cCodegen::genWideLoadLiteral(long value, int words) {
    auto uv = static_cast<unsigned long>(value);
    for (int i = words - 1; i >= 0; i--) {
        int shiftedWord = static_cast<int>((uv >> (16 * i)) & 0xFFFF);
        emit("mov.w", cat(shiftedWord, ",r0"));
        emit("push.w", "r0");
    }
}

void M16cCodegen::genWideStore(const ir::Slot *slot, int words) {
    for (int i = 0; i < words; i++) {
        emit("pop.w", "r0");
        emit("mov.w", cat("r0,", wideWordOperand(slot, i)));
    }
}

// Word 0 is a plain `add.w`/`sub.w` (sets the carry/borrow flag), each later
// word folds that flag in via `adc.w`/`sbb.w`. Writing every result word
// over lhs's slot, then discarding rhs's now-stale block, leaves lhs's
// block - holding the result - as the new stack top.
void M16cCodegen::genWideAdd(const ir::Expr &lhs, const ir::Expr &rhs, bool isAdd, int words) {
    genExpr(lhs);
    genExpr(rhs);
    for (int i = 0; i < words; i++) {
        std::string rhsOperand = "[sp+" + std::to_string(2 * i) + "]";
        std::string lhsOperand = "[sp+" + std::to_string(2 * words + 2 * i) + "]";
        const char *mnemonic = i == 0 ? (isAdd ? "add.w" : "sub.w") : (isAdd ? "adc.w" : "sbb.w");
        emit(mnemonic, cat(rhsOperand, ",", lhsOperand));
    }
    emit("add.w", cat((2 * words), ",sp"));
}

void M16cCodegen::genWideNegate(int words) {
    for (int i = 0; i < words; i++) emit("not.w", cat("[sp+", (2 * i), "]"));
    emit("add.w", "1,[sp+0]");
    for (int i = 1; i < words; i++) emit("adc.w", cat("0,[sp+", (2 * i), "]"));
}

void M16cCodegen::genWideBitwise(const ir::Expr &lhs, const ir::Expr &rhs, const std::string &op, int words) {
    genExpr(lhs);
    genExpr(rhs);
    const char *mnemonic = op == "&" ? "and.w" : op == "|" ? "or.w" : "xor.w";
    for (int i = 0; i < words; i++) {
        std::string rhsOperand = "[sp+" + std::to_string(2 * i) + "]";
        std::string lhsOperand = "[sp+" + std::to_string(2 * words + 2 * i) + "]";
        emit(mnemonic, cat(rhsOperand, ",", lhsOperand));
    }
    emit("add.w", cat((2 * words), ",sp"));
}

// Compares most-significant word first; once a word differs, that
// comparison alone decides the result. Only the most significant word's
// comparison uses the signed jump when the type is signed - lower words,
// once reached, compare a pure magnitude.
void M16cCodegen::genWideCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned,
                                   int words) {
    genExpr(lhs);
    genExpr(rhs);
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
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
        emit("jmp", Lfalse);  // every word equal
    }
    { emitLabel(Lfalse); emit("mov.w", "0,r0"); emit("jmp", Ldone); }
    { emitLabel(Ltrue); emit("mov.w", "1,r0"); }
    emitLabel(Ldone);
    emit("add.w", cat((4 * words), ",sp"));  // release both operand blocks
}

// `SHL.W`/`SHA.W` only shift one native word at a time, so this loops one
// bit at a time, chaining the bit that crosses a word boundary through the
// carry flag via `ROL.W`/`ROR.W`.
void M16cCodegen::genWideShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned, int words) {
    genExpr(lhs);                // pushes `words` words
    emit("push.w", "r1");  // reserve a slot to stash the shift count
    genExpr(rhs);                // r0 = shift count (already promoted to a plain int)
    emit("mov.w", "r0,[sp]");
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    { emit("cmp.w", "0,[sp]"); emit("jeq", Ldone); }
    if (isLeft) {
        emit("clc");
        for (int i = 0; i < words; i++) emit("rol.w", cat("[sp+", (2 + 2 * i), "]"));
    } else {
        if (isUnsigned) {
            emit("clc");
        } else {
            // Seed the carry from the top word's current sign bit instead of
            // clearing it, so the first `ror.w` rotates that sign bit back
            // into the vacated top bit.
            std::string topWord = "[sp+" + std::to_string(2 + 2 * (words - 1)) + "]";
            std::string Lneg = newLabel(), Lcdone = newLabel();
            { emit("cmp.w", cat("0,", topWord)); emit("jlt", Lneg); }
            { emit("clc"); emit("jmp", Lcdone); }
            { emitLabel(Lneg); emit("stc"); }
            emitLabel(Lcdone);
        }
        for (int i = words - 1; i >= 0; i--) emit("ror.w", cat("[sp+", (2 + 2 * i), "]"));
    }
    emit("sub.w", "1,[sp]");
    emit("jmp", Lloop);
    emitLabel(Ldone);
    emit("pop.w", "r1");  // discard the shift-count slot; the `words`-word result stays on top
}

// Not implemented: `MUL.W`/`DIVU.W` only widen one native word to two, and
// this project has no simulator to verify a hand-written multi-word
// multiply/divide against.
void M16cCodegen::genWideMul(const ir::Expr &lhs, const ir::Expr &rhs, int) {
    err(lhs.line, "multiplying two 'long'/'long long' values isn't supported on this target yet - "
                  "see M16cCodegen.cpp's own genWideMul() comment");
    (void)rhs;
}

void M16cCodegen::genWideDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool, bool, int) {
    err(lhs.line, "dividing two 'long'/'long long' values isn't supported on this target yet - "
                  "see M16cCodegen.cpp's own genWideDivMod() comment");
    (void)rhs;
}

void M16cCodegen::genConvert(const ir::Expr &e) {
    Type from = e.lhs->type;
    Type to = e.type;
    bool fromWide = isWide(from);
    bool toWide = isWide(to);
    if (!fromWide && !toWide) {
        genExpr(*e.lhs);  // r0
        if (widthOfType(to) == 1) {
            if (to.isUnsigned) {
                emit("and.w", "0x00FF,r0");
            } else {
                emit("movb", "r0,r0");  // narrow to the low byte, then (register dest) sign-extend back up
            }
        }
        return;
    }
    if (fromWide && !toWide) {
        int words = wordCountOf(from);
        genExpr(*e.lhs);                 // pushes `words` words
        emit("mov.w", "[sp],r0");  // the low word is this type's own truncated value
        emit("add.w", cat((2 * words), ",sp"));
        if (widthOfType(to) == 1) {
            emit((to.isUnsigned ? "and.w 0x00FF,r0\n" : "movb r0,r0\n"));
        }
        return;
    }
    if (!fromWide && toWide) {
        int toWords = wordCountOf(to);
        genExpr(*e.lhs);  // r0 = the native-width value, already correctly sign/zero-extended to 16 bits
        emit("push.w", "r0");
        if (from.isUnsigned) {
            for (int i = 1; i < toWords; i++) { emit("mov.w", "0,r0"); emit("push.w", "r0"); }
        } else {
            genSignFillWord();  // r1 = 0xFFFF if r0 is negative, else 0
            for (int i = 1; i < toWords; i++) emit("push.w", "r1");
        }
        return;
    }
    // fromWide && toWide: pad/truncate word count, preserving sign.
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

// No dedicated widen-with-sign instruction exists on this target, so this
// branches on the sign once and reuses the result for every extra word.
void M16cCodegen::genSignFillWord() {
    std::string Lneg = newLabel(), Ldone = newLabel();
    { emit("cmp.w", "0,r0"); emit("jlt", Lneg); }
    { emit("mov.w", "0,r1"); emit("jmp", Ldone); }
    { emitLabel(Lneg); emit("mov.w", "0xFFFF,r1"); }
    emitLabel(Ldone);
}

std::optional<std::string> M16cCodegen::assembleInProcess(const std::string &asmText,
                                                           const std::string &format) const {
    if (format == "raw") return assembleM16c(asmText);
    if (auto kind = fileFormatKindFromName(format)) {
        ObjectFile obj = assembleM16cObject(asmText);
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

}  // namespace rcc
