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
    emitRaw(cat("org ", (objectMode ? 0 : kBaseAddress)));
    emitRaw("");
    if (!objectMode) {
        emitLabel("_start");
        emit("ld", cat("r15,", kInitialSp));
        emit("call", label(mainFn));
        // The result sits in r0 once execution reaches `halt`.
        emitLabel("halt");
        emit("halt");
        emitRaw("");
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
    emitLabel(label(fn.func));
    emit("push", "r13");
    emit("ld", "r13,r15");
    if (fn.func->frameSize > 0) emit("sub", cat("r15,", fn.func->frameSize));
    genStmt(*fn.body);
    emit("ld", "r15,r13");
    emit("pop", "r13");
    emit("ret");
    emitRaw("");
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
            emit("beq", (s.elseStmt ? Lelse : Ldone));
            genStmt(*s.thenStmt);
            if (s.elseStmt) {
                emit("br", Ldone);
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
            emit("beq", Lend);
            loopLabels_.push_back({Lcont, Lend});
            genStmt(*s.loopBody);
            loopLabels_.pop_back();
            if (s.postStmt) {
                emitLabel(Lcont);
                genStmt(*s.postStmt);
            }
            emit("br", Lstart);
            emitLabel(Lend);
            return;
        }
        case ir::StmtKind::Break: {
            emit("br", loopLabels_.back().second);
            return;
        }
        case ir::StmtKind::Continue: {
            emit("br", loopLabels_.back().first);
            return;
        }
        case ir::StmtKind::Return: {
            if (s.initOrReturnOrExpr) {
                genExpr(*s.initOrReturnOrExpr);
                if (isWide(s.initOrReturnOrExpr->type)) {
                    int words = wordCountOf(s.initOrReturnOrExpr->type);
                    for (int i = 0; i < words; i++) emit("pop", cat("r", i));
                }
            }
            emit("ld", "r15,r13");
            emit("pop", "r13");
            emit("ret");
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
            emitVerbatim(s.asmText);
            if (!s.asmText.empty() && s.asmText.back() != '\n') emitRaw("");
            return;
        }
        case ir::StmtKind::Label: {
            emitLabel(irLabel(s.labelId));
            return;
        }
        case ir::StmtKind::Goto: {
            emit("br", irLabel(s.labelId));
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
            emit("ld", cat("r0,", static_cast<int>(static_cast<uint16_t>(e.intValue))));
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
                emit("push", "r2");
                genExpr(*e.rhs);
                emit("pop", "r2");
                if (widthOfType(e.slot->type) == 1) {
                    emit("ldb", "[r2+0],rl0");
                } else {
                    emit("ld", "[r2+0],r0");
                }
                return;
            }
            if (isWide(e.slot->type)) {
                genExpr(*e.rhs);  // pushes `words` words
                int words = wordCountOf(e.slot->type);
                for (int i = 0; i < words; i++) {
                    emit("ld", cat("r0,[r15+", (2 * i), "]"));
                    emit("ld", cat(wideWordOperand(e.slot, i), ",r0"));
                }
                emit("add", cat("r15,", (2 * words)));
                return;
            }
            genExpr(*e.rhs);
            storeOperand(operandOf(e.slot), e.slot->type);
            return;
        }
        case ir::ExprKind::DerefAssign: {
            genExpr(*e.lhs);  // r0 = pointer value
            emit("ld", "r2,r0");
            emit("push", "r2");
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
            emit("ld", "r1,r0");
            loadOperand(mem, e.type);  // r0 = old
            if (e.yieldOld) emit("push", "r0");
            genCompoundOp(e.op, e.type);
            narrowR0To(e.type);
            storeOperand(mem, e.type);
            if (e.yieldOld) emit("pop", "r0");
            return;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            genExpr(*e.lhs);  // r0 = pointer value
            emit("ld", "r2,r0");
            genCompoundThroughR2(e);
            return;
        }
        case ir::ExprKind::Ternary: {
            genExpr(*e.lhs);
            testTruthy(*e.lhs);
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            emit("beq", Lelse);
            genExpr(*e.rhs);
            emit("br", Ldone);
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
                    emit("neg", "r0");
                }
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                genExpr(*e.rhs);  // r0 = pointer value
                emit("ld", "r2,r0");
                loadIndirect("r2", e.type);
            } else if (e.op == "~") {
                genExpr(*e.rhs);
                if (isWide(e.rhs->type)) {
                    int words = wordCountOf(e.rhs->type);
                    for (int i = 0; i < words; i++) {
                        std::string w = "[r15+" + std::to_string(2 * i) + "]";
                        emit("ld", cat("r0,", w));
                        emit("com", "r0");
                        emit("ld", cat(w, ",r0"));
                    }
                } else {
                    emit("com", "r0");
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
                    emit("push", "r0");
                    totalWords += 1;
                }
            }
            emit("call", label(e.func));
            if (totalWords > 0) emit("add", cat("r15,", (2 * totalWords)));
            if (isWide(e.type)) {
                int words = wordCountOf(e.type);
                for (int i = words - 1; i >= 0; i--) emit("push", cat("r", i));
            }
            return;
        }
        case ir::ExprKind::CallPtr: {
            genExpr(*e.lhs);  // r0 = the callee's own entry address
            emit("ld", "r2,r0");
            emit("callind", "r2");
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
        emit("ldb", cat("rl0,", mem));
        if (type.isUnsigned) {
            emit("and", "r0,0x00FF");
        } else {
            emit("extsb", "r0");
        }
    } else {
        emit("ld", cat("r0,", mem));
    }
}

void Z8000Codegen::storeOperand(const std::string &mem, Type type) {
    if (widthOfType(type) == 1) {
        emit("ldb", cat(mem, ",rl0"));
    } else {
        emit("ld", cat(mem, ",r0"));
    }
}

void Z8000Codegen::loadIndirect(const std::string &addrReg, Type type) {
    if (!isWide(type)) {
        loadOperand("[" + addrReg + "+0]", type);
        return;
    }
    int words = wordCountOf(type);
    for (int i = words - 1; i >= 0; i--) {
        emit("ld", cat("r0,[", addrReg, "+", (2 * i), "]"));
        emit("push", "r0");
    }
}

void Z8000Codegen::storeIndirect(Type type) {
    if (!isWide(type)) {
        emit("pop", "r2");
        storeOperand("[r2+0]", type);
        return;
    }
    int words = wordCountOf(type);
    static const char *const tempRegs[] = {"r0", "r1", "r3", "r4"};
    for (int i = 0; i < words; i++) emit("pop", tempRegs[i]);
    emit("pop", "r2");
    for (int i = 0; i < words; i++) emit("ld", cat("[r2+", (2 * i), "],", tempRegs[i]));
}

void Z8000Codegen::genWideLoad(const ir::Slot *slot, int words) {
    for (int i = words - 1; i >= 0; i--) {
        emit("ld", cat("r0,", wideWordOperand(slot, i)));
        emit("push", "r0");
    }
}

void Z8000Codegen::genWideLoadLiteral(long value, int words) {
    auto uv = static_cast<unsigned long>(value);
    for (int i = words - 1; i >= 0; i--) {
        int shiftedWord = static_cast<int>((uv >> (16 * i)) & 0xFFFF);
        emit("ld", cat("r0,", shiftedWord));
        emit("push", "r0");
    }
}

void Z8000Codegen::genWideStore(const ir::Slot *slot, int words) {
    for (int i = 0; i < words; i++) {
        emit("pop", "r0");
        emit("ld", cat(wideWordOperand(slot, i), ",r0"));
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
        emit("ld", cat("r0,", lhsOperand));
        emit("ld", cat("r1,", rhsOperand));
        emit(op, "r0,r1");
        emit("ld", cat(lhsOperand, ",r0"));
    }
    emit("add", cat("r15,", (2 * words)));
}

void Z8000Codegen::genWideNegate(int words) {
    // Two's-complement negation in place: `com` every word first, then `add` 1 into the least
    // significant word and `adc` the carry up through the rest. `adc dst,src` needs a real
    // second operand (unlike the PDP-11's single-operand carry-only `ADC`), so each later word
    // adds 0 with the carry folded in.
    for (int i = 0; i < words; i++) {
        std::string w = "[r15+" + std::to_string(2 * i) + "]";
        emit("ld", cat("r0,", w));
        emit("com", "r0");
        emit("ld", cat(w, ",r0"));
    }
    emit("ld", "r0,[r15+0]");
    emit("add", "r0,1");
    emit("ld", "[r15+0],r0");
    for (int i = 1; i < words; i++) {
        std::string w = "[r15+" + std::to_string(2 * i) + "]";
        emit("ld", cat("r0,", w));
        emit("ld", "r1,0");
        emit("adc", "r0,r1");
        emit("ld", cat(w, ",r0"));
    }
}

void Z8000Codegen::genWideBitwise(const ir::Expr &lhs, const ir::Expr &rhs, const std::string &op, int words) {
    genExpr(lhs);
    genExpr(rhs);
    for (int i = 0; i < words; i++) {
        std::string rhsOperand = "[r15+" + std::to_string(2 * i) + "]";
        std::string lhsOperand = "[r15+" + std::to_string(2 * words + 2 * i) + "]";
        emit("ld", cat("r0,", lhsOperand));
        emit("ld", cat("r1,", rhsOperand));
        if (op == "&") {
            emit("and", "r0,r1");
        } else if (op == "|") {
            emit("or", "r0,r1");
        } else {
            emit("xor", "r0,r1");
        }
        emit("ld", cat(lhsOperand, ",r0"));
    }
    emit("add", cat("r15,", (2 * words)));
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
            emit("ld", cat("r0,", lhsOperand));
            emit("ld", cat("r1,", rhsOperand));
            emit("cp", "r0,r1");
            std::string Lcontinue = newLabel();
            emit("beq", Lcontinue);
            emit("br", (op == "==" ? Lfalse : Ltrue));
            emitLabel(Lcontinue);
        }
        emit("br", (op == "==" ? Ltrue : Lfalse));
    } else {
        std::string signedBranch = op == "<" ? "blt" : op == "<=" ? "ble" : op == ">" ? "bgt" : "bge";
        std::string unsignedBranch = op == "<" ? "blo" : op == "<=" ? "blos" : op == ">" ? "bhi" : "bhis";
        for (int i = words - 1; i >= 0; i--) {
            std::string lhsOperand = "[r15+" + std::to_string(2 * words + 2 * i) + "]";
            std::string rhsOperand = "[r15+" + std::to_string(2 * i) + "]";
            emit("ld", cat("r0,", lhsOperand));
            emit("ld", cat("r1,", rhsOperand));
            emit("cp", "r0,r1");
            std::string Lcontinue = newLabel();
            emit("beq", Lcontinue);
            bool useSigned = (i == words - 1) && !isUnsigned;
            emit((useSigned ? signedBranch : unsignedBranch), Ltrue);
            emit("br", Lfalse);
            emitLabel(Lcontinue);
        }
        emit("br", Lfalse);
    }
    emitLabel(Lfalse);
    emit("ld", "r0,0");
    emit("br", Ldone);
    emitLabel(Ltrue);
    emit("ld", "r0,1");
    emitLabel(Ldone);
    emit("add", cat("r15,", (4 * words)));
}

// A `long`/`long long` shifted by a runtime count loops one bit at a time via `rlc`/`rrc`
// (rotate-through-carry), folding the bit crossing a word boundary by hand, chained across
// `words` words - the Z8000's native dynamic shift (`sdl`/`sda`) isn't used here (see
// Z8000Codegen.h's own header comment, and docs/known-bugs.md).
void Z8000Codegen::genWideShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned, int words) {
    genExpr(lhs);  // pushes `words` words
    emit("push", "r1");
    genExpr(rhs);  // r0 = shift count
    emit("ld", "[r15+0],r0");
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("ld", "r0,[r15+0]");
    emit("test", "r0");
    emit("beq", Ldone);
    if (isLeft) {
        emit("clc");
        for (int i = 0; i < words; i++) {
            std::string w = "[r15+" + std::to_string(2 + 2 * i) + "]";
            emit("ld", cat("r0,", w));
            emit("rlc", "r0");
            emit("ld", cat(w, ",r0"));
        }
    } else {
        emit("clc");
        for (int i = words - 1; i >= 0; i--) {
            std::string w = "[r15+" + std::to_string(2 + 2 * i) + "]";
            // The top word's sign bit, not a cleared carry, feeds back in for a signed shift.
            if (i == words - 1 && !isUnsigned) {
                emit("ld", cat("r0,", w));
                emit("test", "r0");
                emit("rrc", "r0");
                emit("ld", cat(w, ",r0"));
            } else {
                emit("ld", cat("r0,", w));
                emit("rrc", "r0");
                emit("ld", cat(w, ",r0"));
            }
        }
    }
    emit("ld", "r0,[r15+0]");
    emit("sub", "r0,1");
    emit("ld", "[r15+0],r0");
    emit("br", Lloop);
    emitLabel(Ldone);
    emit("pop", "r1");
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
                emit("and", "r0,0x00FF");
            } else {
                emit("extsb", "r0");
            }
        }
        return;
    }
    if (fromWide && !toWide) {
        int words = wordCountOf(from);
        genExpr(*e.lhs);
        emit("ld", "r0,[r15+0]");
        emit("add", cat("r15,", (2 * words)));
        if (widthOfType(to) == 1) {
            emit((to.isUnsigned ? "and r0,0x00FF\n" : "extsb r0\n"));
        }
        return;
    }
    if (!fromWide && toWide) {
        int toWords = wordCountOf(to);
        genExpr(*e.lhs);  // r0 = the native-width value, already correctly sign/zero-extended to 16 bits
        emit("ld", "r1,r0");
        if (from.isUnsigned) {
            emit("ld", "r0,0");
        } else {
            emit("exts", "r0");  // sign-extends r1 into r0 (the pair r0:r1)
        }
        for (int i = 1; i < toWords; i++) emit("push", "r0");
        emit("push", "r1");
        return;
    }
    int fromWords = wordCountOf(from);
    int toWords = wordCountOf(to);
    genExpr(*e.lhs);
    if (toWords > fromWords) {
        emit("ld", cat("r1,[r15+", (2 * (fromWords - 1)), "]"));
        emit("ld", "r0,r1");
        emit("exts", "r0");
        static const char *const tempRegs[] = {"r1", "r3", "r4"};
        for (int i = 0; i < fromWords; i++) emit("pop", tempRegs[i]);
        for (int i = 0; i < toWords - fromWords; i++) emit("push", "r0");
        for (int i = fromWords - 1; i >= 0; i--) emit("push", tempRegs[i]);
    } else if (toWords < fromWords) {
        emit("add", cat("r15,", (2 * (fromWords - toWords))));
    }
}

void Z8000Codegen::narrowR0To(Type to) {
    if (widthOfType(to) != 1) return;
    if (to.isUnsigned) {
        emit("and", "r0,0x00FF");
    } else {
        emit("extsb", "r0");
    }
}

int Z8000Codegen::localArrayBase(const ir::Slot *slot) { return ir::localLowOffset(*slot, typeWidths()); }

void Z8000Codegen::genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
    genExpr(idxExpr);  // r0 = index
    int shift = elemShift(slot->type);
    if (shift >= 0) {
        for (int i = 0; i < shift; i++) emit("add", "r0,r0");
    } else {
        // A non-power-of-two element width (e.g. a packed struct's) has no shift-based
        // scaling, so multiply by the real width via `mult` instead; only the low word of the
        // 32-bit product is kept, same convention as genMul().
        emit("ld", "r1,r0");                       // r1 = index (MULT's pre-loaded pair-low operand)
        emit("ld", cat("r2,", widthOfType(slot->type)));  // r2 = width (the source operand)
        emit("mult", "r0,r2");                      // r0:r1 = r2 * r1 = width * index
        emit("ld", "r0,r1");
    }
    if (slot->arraySize > 0) {
        if (slot->kind == ir::SlotKind::Global) {
            emit("lda", cat("r2,", label(slot)));
            emit("add", "r2,r0");
        } else {
            emit("lda", cat("r2,[r13-", localArrayBase(slot), "]"));
            emit("add", "r2,r0");
        }
        return;
    }
    emit("ld", cat("r2,", operandOf(slot)));
    emit("add", "r2,r0");
}

void Z8000Codegen::genIndex(const ir::Expr &e) {
    genIndexAddr(e.slot, *e.lhs);
    if (widthOfType(e.slot->type) == 1) {
        emit("ldb", "rl0,[r2+0]");
        if (e.slot->type.isUnsigned) {
            emit("and", "r0,0x00FF");
        } else {
            emit("extsb", "r0");
        }
    } else {
        emit("ld", "r0,[r2+0]");
    }
}

void Z8000Codegen::genLocalArrayInit(const ir::Slot *slot) {
    for (size_t i = 0; i < slot->arrayInit.size(); i++) {
        const ir::ArrayInitElem &elem = slot->arrayInit[i];
        if (elem.isSymbol) {
            emit("lda", cat("r0,", (elem.slot ? label(elem.slot) : label(elem.func))));
        } else {
            emit("ld", cat("r0,", static_cast<int>(static_cast<uint16_t>(elem.value))));
        }
        int elemW = widthOfType(slot->type);
        emit((elemW == 1 ? "ldb" : "ld"), cat("[r13-", (localArrayBase(slot) - elemW * static_cast<int>(i)), "],", (elemW == 1 ? "rl0" : "r0")));
    }
}

void Z8000Codegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        if (operand.slot->kind == ir::SlotKind::Global) {
            emit("lda", cat("r0,", label(operand.slot)));
            return;
        }
        if (operand.slot->kind == ir::SlotKind::Param) {
            emit("lda", cat("r0,[r13+", operand.slot->frameOffset, "]"));
        } else {
            int below = ir::localOffsetDescendingWide(*operand.slot, typeWidths());
            emit("lda", cat("r0,[r13-", below, "]"));
        }
        return;
    }
    if (operand.kind == ir::ExprKind::Index) {
        genIndexAddr(operand.slot, *operand.lhs);
        emit("ld", "r0,r2");
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        emit("lda", cat("r0,", label(operand.func)));
        return;
    }
    err(operand.line, "internal: unhandled addressable-operand kind");
}

// Entry convention: r0 = old value, r1 = rhs value, both at `type`'s width/signedness.
void Z8000Codegen::genCompoundOp(const std::string &op, Type type) {
    bool isUnsigned = type.isUnsigned;
    if (op == "+") { emit("add", "r0,r1"); return; }
    if (op == "-") { emit("sub", "r0,r1"); return; }
    if (op == "*") { emit("ld", "r2,r0");
emit("mult", "r0,r2");
emit("ld", "r0,r1"); return; }
    if (op == "&") { emit("and", "r0,r1"); return; }
    if (op == "|") { emit("or", "r0,r1"); return; }
    if (op == "^") { emit("xor", "r0,r1"); return; }
    if (op == "<<" || op == ">>") {
        std::string Lloop = newLabel(), Ldone = newLabel();
        emitLabel(Lloop);
        emit("test", "r1");
        emit("beq", Ldone);
        if (op == "<<") {
            emit("clc");
            emit("rlc", "r0");
        } else if (isUnsigned) {
            emit("clc");
            emit("rrc", "r0");
        } else {
            emit("test", "r0");
            emit("rrc", "r0");
        }
        emit("sub", "r1,1");
        emit("br", Lloop);
        emitLabel(Ldone);
        return;
    }
    // "/" or "%"
    emit("ld", "r2,r1");  // r2 = divisor
    emit("ld", "r1,r0");  // r1 = dividend
    if (isUnsigned) {
        emitUnsignedDiv();
    } else {
        emit("test", "r1");
        emit("exts", "r0");
        emit("div", "r0,r2");
    }
    if (op == "%") emit("ld", "r0,r1");
}

void Z8000Codegen::genCompoundThroughR2(const ir::Expr &e) {
    emit("push", "r2");
    genExpr(*e.rhs);
    emit("push", "r0");
    emit("pop", "r1");
    emit("pop", "r2");
    loadOperand("[r2+0]", e.type);  // r0 = old
    emit("push", "r2");
    if (e.yieldOld) emit("push", "r0");
    genCompoundOp(e.op, e.type);  // r0 = new (raw)
    narrowR0To(e.type);
    if (e.yieldOld) {
        emit("pop", "r1");
        emit("pop", "r2");
        storeOperand("[r2+0]", e.type);
        emit("ld", "r0,r1");
    } else {
        emit("pop", "r2");
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
        emit("push", "r0");
        genExpr(*e.rhs);
        emit("pop", "r1");  // r1 = lhs, r0 = rhs
        if (op == "+") {
            emit("add", "r0,r1");
        } else {
            emit("sub", "r1,r0");  // r1 = r1 - r0 = lhs - rhs
            emit("ld", "r0,r1");
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
        emit("push", "r0");
        genExpr(*e.rhs);
        emit("pop", "r1");  // r1 = lhs, r0 = rhs
        if (op == "&") {
            emit("and", "r0,r1");
        } else if (op == "|") {
            emit("or", "r0,r1");
        } else {
            emit("xor", "r0,r1");
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
    emit("push", "r0");
    genExpr(rhs);
    emit("ld", "r1,r0");  // r1 = shift count
    emit("pop", "r0");    // r0 = value to shift
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("test", "r1");
    emit("beq", Ldone);
    if (isLeft) {
        emit("clc");
        emit("rlc", "r0");
    } else if (isUnsigned) {
        emit("clc");
        emit("rrc", "r0");
    } else {
        emit("test", "r0");
        emit("rrc", "r0");  // arithmetic: sign bit stays, rotated back into bit 15
    }
    emit("sub", "r1,1");
    emit("br", Lloop);
    emitLabel(Ldone);
}

// `mult`: the low word of its 32-bit product (`r1`) is the 16-bit truncated answer.
void Z8000Codegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    emit("push", "r0");
    genExpr(rhs);
    emit("pop", "r1");  // r1 = lhs, r0 = rhs
    emit("ld", "r2,r0");  // r2 = rhs - MULT's source operand must not be part of the r0:r1 pair
    emit("mult", "r0,r2");  // r0:r1 = r2 * r1 = rhs * lhs (high:low)
    emit("ld", "r0,r1");
}

// `div` needs a 32-bit dividend in `r0:r1`, so the 16-bit dividend is first sign-extended into
// that pair with `exts` (`test` first, to set the flags `exts` reads).
void Z8000Codegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    emit("push", "r0");
    genExpr(rhs);
    emit("ld", "r2,r0");  // r2 = divisor
    emit("pop", "r1");    // r1 = dividend (low word)
    if (isUnsigned) {
        emitUnsignedDiv();
    } else {
        emit("test", "r1");
        emit("exts", "r0");
        emit("div", "r0,r2");  // r0 = quotient, r1 = remainder
    }
    if (wantRemainder) emit("ld", "r0,r1");
}

// `div` is signed: the divisor is a signed 16-bit value and the quotient must fit in 15 bits.
// With the dividend zero-extended into r0:r1 it's exact for divisors 2..0x7FFF. A divisor of 1
// would give a quotient up to 0xFFFF, so it's handled directly; a divisor with its top bit set
// divides an unsigned 16-bit dividend at most once. Mirrors Pdp11Codegen.cpp's own
// emitUnsignedDiv() exactly (see its comment).
void Z8000Codegen::emitUnsignedDiv() {
    std::string Lbig = newLabel(), Lsmall = newLabel(), Lone = newLabel(), Lnorm = newLabel(), Lge = newLabel(),
                Llt = newLabel(), Ldone = newLabel();
    emit("test", "r2");
    emit("blt", Lbig);
    emit("br", Lsmall);
    emitLabel(Lsmall);
    emit("ld", "r0,r2");
    emit("sub", "r0,1");
    emit("beq", Lone);
    emit("br", Lnorm);
    emitLabel(Lnorm);
    emit("ld", "r0,0");
    emit("div", "r0,r2");
    emit("br", Ldone);
    emitLabel(Lone);
    emit("ld", "r0,r1");
    emit("ld", "r1,0");
    emit("br", Ldone);
    emitLabel(Lbig);
    emit("cp", "r1,r2");  // flags = dividend - divisor
    emit("bhis", Lge);
    emit("br", Llt);
    emitLabel(Llt);
    emit("ld", "r0,0");
    emit("br", Ldone);
    emitLabel(Lge);
    emit("sub", "r1,r2");
    emit("ld", "r0,1");
    emitLabel(Ldone);
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
    emit("push", "r0");
    genExpr(rhs);
    emit("pop", "r1");  // r1 = lhs, r0 = rhs
    emit("cp", "r1,r0");
    std::string Ldone = newLabel();
    std::string Ltrue = newLabel();
    emit(jpCondIfTrue, Ltrue);
    emit("ld", "r0,0");
    emit("br", Ldone);
    emitLabel(Ltrue);
    emit("ld", "r0,1");
    emitLabel(Ldone);
}

void Z8000Codegen::testTruthy(const ir::Expr &e) {
    if (!isWide(e.type)) {
        emit("test", "r0");
        return;
    }
    int words = wordCountOf(e.type);
    emit("ld", "r0,[r15+0]");
    for (int i = 1; i < words; i++) {
        emit("ld", cat("r1,[r15+", (2 * i), "]"));
        emit("or", "r0,r1");
    }
    emit("add", cat("r15,", (2 * words)));
    emit("test", "r0");
}

void Z8000Codegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    testTruthy(lhs);
    std::string Lfalse = newLabel(), Ldone = newLabel();
    emit("beq", Lfalse);
    genExpr(rhs);
    testTruthy(rhs);
    emit("beq", Lfalse);
    emit("ld", "r0,1");
    emit("br", Ldone);
    emitLabel(Lfalse);
    emit("ld", "r0,0");
    emitLabel(Ldone);
}

void Z8000Codegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    testTruthy(lhs);
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit("bne", Ltrue);
    genExpr(rhs);
    testTruthy(rhs);
    emit("bne", Ltrue);
    emit("ld", "r0,0");
    emit("br", Ldone);
    emitLabel(Ltrue);
    emit("ld", "r0,1");
    emitLabel(Ldone);
}

void Z8000Codegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    testTruthy(rhs);
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit("beq", Ltrue);
    emit("ld", "r0,0");
    emit("br", Ldone);
    emitLabel(Ltrue);
    emit("ld", "r0,1");
    emitLabel(Ldone);
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
