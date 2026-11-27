#include "Pdp11Codegen.h"

#include <cstdint>
#include <tuple>
#include <stdexcept>

#include "Pdp11Assembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

// Where the whole program loads and starts executing; the test harness sets the simulated
// CPU's PC here directly. Must stay even - the PDP-11 can only fetch an instruction word
// from an even address. Chosen above the real reset/trap vector range (0-0376).
constexpr uint32_t kBaseAddress = 0x1000;

// Where SP is set before calling main(), comfortably above the loaded program and its data.
constexpr uint32_t kInitialSp = 0xE000;
}  // namespace

// A literal element's decimal text, truncated to 16 bits; a symbol element is the
// referenced Slot's or Function's label, written out bare.
std::string Pdp11Codegen::arrayElemText(const ir::ArrayInitElem &e) {
    if (!e.isSymbol) return std::to_string(static_cast<int>(static_cast<uint16_t>(e.value)));
    return e.slot ? label(e.slot) : label(e.func);
}

void Pdp11Codegen::declareArray(const std::string &lbl, const std::vector<ir::ArrayInitElem> &values, Type elemType) {
    if (values.empty()) return;
    int w = widthOfType(elemType);
    // No 32-/64-bit data directive: a `long`/`long long` literal element is emitted as 2/4
    // "dw"s (least significant word first); a symbol element is always a plain native-word
    // address, so it still gets exactly one "dw".
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

void Pdp11Codegen::safeBranch(const std::string &branchMnemonic, const std::string &targetIfTaken,
                               const std::string &targetIfNot) {
    std::string Lnear = newLabel();
    emit(branchMnemonic, Lnear);
    emit("jmp", targetIfNot);
    emitLabel(Lnear);
    emit("jmp", targetIfTaken);
}

std::string Pdp11Codegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    // Real separate compilation means `prog` may not define `main` at all; `objectMode`
    // skips the entry sequence below, since Pdp11LinkerFactory.cpp synthesizes it once
    // instead, after linking. An object file's own base is `org 0`; placing it is the
    // linker's job.
    emitRaw(cat("org ", (objectMode ? 0 : kBaseAddress)));
    emitRaw("");
    if (!objectMode) {
        emitLabel("_start");
        emit("mov", cat(kInitialSp, ",sp"));
        emit("jsr", label(mainFn));
        // The result sits in R0 once execution reaches `halt`. `halt` should stop the
        // simulated CPU; the `br halt` after it is a safety net in case a simulator's HALT
        // just traps to a console prompt and execution resumes at the next word.
        emitLabel("halt");
        emit("halt");
        emit("br", "halt");
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

    // Globals are laid out back-to-back in declaration order. PDP-11 hardware traps on an
    // odd-address word access, so a `dw`-or-wider global landing after an odd number of
    // accumulated `db` bytes needs one byte of padding first.
    size_t globalByteOffset = 0;
    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        // An external global isn't defined in this file: it must never get a storage word
        // here, only be referenced by name, letting the assembler defer it to a relocation.
        if (slot->isExternal) continue;
        if (debugMapEnabled_) debugSymbols_.push_back({label(slot.get()), /*isFunc=*/false});
        size_t gTrailerStart = trailer_.size();
        const std::string lbl = label(slot.get());
        int elemW = widthOfType(slot->type);
        size_t totalBytes = slot->arraySize > 0 ? static_cast<size_t>(slot->arraySize) * elemW
                                                  : static_cast<size_t>(elemW);
        if (elemW >= 2 && (globalByteOffset % 2) != 0) {
            trailer_.push_back("db 0  ; alignment padding");
            globalByteOffset += 1;
        }
        globalByteOffset += totalBytes;
        if (slot->arraySize > 0) {
            declareArray(lbl, slot->arrayInit, slot->type);
        } else if (slot->type.base == BaseType::Struct) {
            // No struct initializer exists in this subset, so this is
            // always zero-filled; reuses declareArray's own element-list
            // emission (one zero "db" per byte - byte-exact even when
            // structSize is odd, unlike the word-pair loop below) rather
            // than a new directive.
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

std::map<std::string, std::string> Pdp11Codegen::perFileAsm() const { return perFileAsm_; }

// Prologue: `push r5` saves the caller's frame pointer, `mov sp,r5` makes `r5` this call's
// frame pointer (pointing at that saved word, with the return address above it and the
// caller's pushed arguments above that - see addr()), then `sub N,sp` reserves Locals below
// `r5`. Epilogue (here and every `Return`) undoes it: `mov r5,sp` drops the Locals, `pop r5`
// restores the caller's frame pointer, `rts` pops the return address into `pc`.
void Pdp11Codegen::genFunction(const ir::FunctionBody &fn) {
    lastDebugLine_ = 0;
    if (debugMapEnabled_) debugSymbols_.push_back({label(fn.func), /*isFunc=*/true});
    emitLabel(label(fn.func));
    emit("push", "r5");
    emit("mov", "sp,r5");
    if (fn.func->frameSize > 0) emit("sub", cat(fn.func->frameSize, ",sp"));
    genStmt(*fn.body);
    emit("mov", "r5,sp");
    emit("pop", "r5");
    emit("rts");
    emitRaw("");
}

void Pdp11Codegen::genStmt(const ir::Stmt &s) {
    emitSourceLine(s.line);
    switch (s.kind) {
        case ir::StmtKind::Block: {
            for (const auto &child : s.body) genStmt(*child);
            return;
        }
        case ir::StmtKind::If: {
            genExpr(*s.cond);
            testTruthy(*s.cond);
            std::string Lthen = newLabel();
            std::string Lelse = s.elseStmt ? newLabel() : "";
            std::string Ldone = newLabel();
            safeBranch("beq", s.elseStmt ? Lelse : Ldone, Lthen);
            emitLabel(Lthen);
            genStmt(*s.thenStmt);
            emit("jmp", Ldone);
            if (s.elseStmt) {
                emitLabel(Lelse);
                genStmt(*s.elseStmt);
            }
            emitLabel(Ldone);
            return;
        }
        case ir::StmtKind::While: {
            std::string Lstart = newLabel(), Lbody = newLabel(), Lend = newLabel();
            std::string Lcont = s.postStmt ? newLabel() : Lstart;
            emitLabel(Lstart);
            genExpr(*s.cond);
            testTruthy(*s.cond);
            safeBranch("beq", Lend, Lbody);
            emitLabel(Lbody);
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
                // A wide value sits on the stack (LSW on top) after genExpr(); pop it into
                // r0(LSW)..r(words-1)(MSW) first, since the epilogue's `mov r5,sp` is about
                // to discard this call's whole frame before `rts` runs.
                if (isWide(s.initOrReturnOrExpr->type)) {
                    int words = wordCountOf(s.initOrReturnOrExpr->type);
                    for (int i = 0; i < words; i++) emit("pop", cat("r", i));
                }
            }
            emit("mov", "r5,sp");
            emit("pop", "r5");
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

void Pdp11Codegen::genExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit: {
            if (isWide(e.type)) {
                genWideLoadLiteral(e.intValue, wordCountOf(e.type));
                return;
            }
            emit("mov", cat(static_cast<int>(static_cast<uint16_t>(e.intValue)), ",r0"));
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
                    emit("stindb", "r0,r2");
                } else {
                    emit("stind", "r0,r2");
                }
                return;
            }
            if (isWide(e.slot->type)) {
                genExpr(*e.rhs);  // pushes `words` words
                int words = wordCountOf(e.slot->type);
                for (int i = 0; i < words; i++) {
                    emit("mov", cat("[sp+", 2 * i, "],r0"));
                    emit("mov", cat("r0,", wideWordOperand(e.slot, i)));
                }
                emit("add", cat(2 * words, ",sp"));
                return;
            }
            genExpr(*e.rhs);
            storeOperand(operandOf(e.slot), e.slot->type);
            return;
        }
        case ir::ExprKind::DerefAssign: {
            // Stores through at e.type's width/signedness. The pointer value (always one
            // word) has to ride the data stack, not a register, across evaluating rhs, since
            // rhs may clobber any register as scratch (e.g. genDivMod() uses r2); storeIndirect()
            // recovers it, underneath rhs's own pushed value if that's wide.
            genExpr(*e.lhs);  // r0 = pointer value
            emit("mov", "r0,r2");
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
            // Plain scalar target - operandOf(slot) is a stable memory operand, so no
            // address needs to be held across rhs's own evaluation.
            std::string mem = operandOf(e.slot);
            genExpr(*e.rhs);  // r0 = rhs
            emit("mov", "r0,r1");
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
            emit("mov", "r0,r2");  // r2 = address
            genCompoundThroughR2(e);
            return;
        }
        case ir::ExprKind::Ternary: {
            genExpr(*e.lhs);
            testTruthy(*e.lhs);
            std::string Lthen = newLabel();
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            safeBranch("beq", Lelse, Lthen);
            emitLabel(Lthen);
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
                    emit("neg", "r0");
                }
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                // Loads at e.type's width/signedness. Unlike DerefAssign, no competing stack
                // use happens between computing the address and dereferencing it, so r2 can
                // safely hold the address directly.
                genExpr(*e.rhs);  // r0 = pointer value
                emit("mov", "r0,r2");
                loadIndirect("r2", e.type);
            } else if (e.op == "~") {
                genExpr(*e.rhs);
                emit("com", "r0");
                if (isWide(e.rhs->type)) {
                    for (int i = 1; i < wordCountOf(e.rhs->type); i++) emit("com", cat("[sp+", 2 * i, "]"));
                }
            } else {  // "&"
                genAddrOf(*e.rhs);
            }
            return;
        }
        case ir::ExprKind::Call: {
            // Push arguments right-to-left so param 0 ends up closest to the return address;
            // caller cleans up with `add N,sp` after the call returns. A wide argument's
            // genExpr() already pushes its own `words` words; a narrow one leaves its value
            // in r0, which this loop pushes itself.
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
            emit("jsr", label(e.func));
            if (totalWords > 0) emit("add", cat(2 * totalWords, ",sp"));
            if (isWide(e.type)) {
                // The callee left its wide return value in r0(LSW)..r(words-1)(MSW); push it
                // back as this expression's result in LSW-on-top order. Pushes each register
                // directly rather than relaying through r0, since r0 is itself one of the
                // values being pushed.
                int words = wordCountOf(e.type);
                for (int i = words - 1; i >= 0; i--) {
                    emit("push", cat("r", i));
                }
            }
            return;
        }
        case ir::ExprKind::CallPtr: {
            // e.args is always empty here - IRBuilder.cpp rejects any argument on this kind,
            // since the callee is unknown at compile time. "jsrind r2" is the real `JSR
            // PC,@Rn` register-deferred call through whatever address r2 holds. No callee
            // signature is known, so the result stays plain int.
            genExpr(*e.lhs);  // r0 = the callee's own entry address
            emit("mov", "r0,r2");
            emit("jsrind", "r2");
            return;
        }
        case ir::ExprKind::Binary: {
            genBinary(e);
            return;
        }
        case ir::ExprKind::Intrinsic: {
            // No trap vector is set up (see generate()), so there's nothing for `ion`/`iof`
            // to meaningfully enable or disable; treated as a no-op.
            (void)e;
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

std::string Pdp11Codegen::wideWordOperand(const ir::Slot *slot, int wordIndex) const {
    int off = 2 * wordIndex;
    if (slot->kind == ir::SlotKind::Global) {
        return off == 0 ? label(slot) : (label(slot) + "+" + std::to_string(off));
    }
    if (slot->kind == ir::SlotKind::Param) {
        return "[r5+" + std::to_string(slot->frameOffset + off) + "]";
    }
    return "[r5-" + std::to_string(slot->frameOffset + off) + "]";
}

// `movb` into a register destination always sign-extends to 16 bits, so an unsigned `char`
// has to mask the high byte back off afterward.
void Pdp11Codegen::loadOperand(const std::string &mem, Type type) {
    if (widthOfType(type) == 1) {
        emit("movb", cat(mem, ",r0"));
        if (type.isUnsigned) emit("bic", "0xFF00,r0");
    } else {
        emit("mov", cat(mem, ",r0"));
    }
}

void Pdp11Codegen::storeOperand(const std::string &mem, Type type) {
    if (widthOfType(type) == 1) {
        emit("movb", cat("r0,", mem));
    } else {
        emit("mov", cat("r0,", mem));
    }
}

void Pdp11Codegen::loadIndirect(const std::string &addrReg, Type type) {
    if (!isWide(type)) {
        loadOperand("[" + addrReg + "+0]", type);
        return;
    }
    int words = wordCountOf(type);
    for (int i = words - 1; i >= 0; i--) {
        emit("mov", cat("[", addrReg, "+", 2 * i, "],r0"));
        emit("push", "r0");
    }
}

void Pdp11Codegen::storeIndirect(Type type) {
    if (!isWide(type)) {
        emit("pop", "r2");
        storeOperand("[r2+0]", type);
        return;
    }
    int words = wordCountOf(type);
    // r0/r1/r3/r4 are the only four registers free (r5 is the frame pointer, r2 holds the
    // address) - exactly enough to stash a `long long`'s 4 words while the address stays
    // buried underneath them on the stack.
    static const char *const tempRegs[] = {"r0", "r1", "r3", "r4"};
    for (int i = 0; i < words; i++) emit("pop", tempRegs[i]);
    emit("pop", "r2");
    for (int i = 0; i < words; i++) emit("mov", cat(tempRegs[i], ",[r2+", 2 * i, "]"));
}

// Pushes `slot`'s `words`-word value, least-significant word on top (pushed last).
void Pdp11Codegen::genWideLoad(const ir::Slot *slot, int words) {
    for (int i = words - 1; i >= 0; i--) {
        emit("mov", cat(wideWordOperand(slot, i), ",r0"));
        emit("push", "r0");
    }
}

void Pdp11Codegen::genWideLoadLiteral(long value, int words) {
    auto uv = static_cast<unsigned long>(value);
    for (int i = words - 1; i >= 0; i--) {
        int shiftedWord = static_cast<int>((uv >> (16 * i)) & 0xFFFF);
        emit("mov", cat(shiftedWord, ",r0"));
        emit("push", "r0");
    }
}

// Pops `words` words off the stack (least-significant on top) into `slot`'s storage.
void Pdp11Codegen::genWideStore(const ir::Slot *slot, int words) {
    for (int i = 0; i < words; i++) {
        emit("pop", "r0");
        emit("mov", cat("r0,", wideWordOperand(slot, i)));
    }
}

// Standard PDP-11 multi-word idiom: word 0 is a plain ADD/SUB (setting C for the next word);
// each later word first folds in the previous C via ADC/SBC (which only adds/subtracts the
// carry bit into one operand - there's no two-operand "add with carry-in" here), then does
// its own ADD/SUB. Result is written over lhs's stack block; rhs's now-stale block is
// discarded with one `add`, leaving lhs's block as the new top.
// Word i of the result is lhs_i +/- rhs_i +/- the carry (borrow) out of word i-1, folded in
// with ADC/SBC first. When that ADC/SBC itself carries, the word has just wrapped to 0 (or
// 0xFFFF), so the ADD/SUB of rhs_i that follows can't carry too; the carry out is then set
// again by hand (`cmp r1,r0` with r1=0, r0=1 borrows), since ADD/SUB would clear it.
void Pdp11Codegen::genWideAdd(const ir::Expr &lhs, const ir::Expr &rhs, bool isAdd, int words) {
    genExpr(lhs);
    genExpr(rhs);
    const char *op = isAdd ? "add" : "sub";
    for (int i = 0; i < words; i++) {
        std::string rhsOperand = "[sp+" + std::to_string(2 * i) + "]";
        std::string lhsOperand = "[sp+" + std::to_string(2 * words + 2 * i) + "]";
        if (i == 0) {
            emit(op, cat(rhsOperand, ",", lhsOperand));
            continue;
        }
        std::string Lcarried = newLabel(), Ldone = newLabel();
        emit(isAdd ? "adc" : "sbc", lhsOperand);
        emit("blo", Lcarried);
        emit(op, cat(rhsOperand, ",", lhsOperand));
        emit("br", Ldone);
        emitLabel(Lcarried);
        emit(op, cat(rhsOperand, ",", lhsOperand));
        emit("mov", "1,r0");
        emit("mov", "0,r1");
        emit("cmp", "r1,r0");
        emitLabel(Ldone);
    }
    emit("add", cat(2 * words, ",sp"));
}

// Two's-complement negation in place: COM every word first, then ADD 1 into the least
// significant word and ADC the carry up through the rest.
void Pdp11Codegen::genWideNegate(int words) {
    for (int i = 0; i < words; i++) emit("com", cat("[sp+", 2 * i, "]"));
    emit("add", "1,[sp+0]");
    for (int i = 1; i < words; i++) emit("adc", cat("[sp+", 2 * i, "]"));
}

void Pdp11Codegen::genWideBitwise(const ir::Expr &lhs, const ir::Expr &rhs, const std::string &op, int words) {
    genExpr(lhs);
    genExpr(rhs);
    for (int i = 0; i < words; i++) {
        std::string rhsOperand = "[sp+" + std::to_string(2 * i) + "]";
        std::string lhsOperand = "[sp+" + std::to_string(2 * words + 2 * i) + "]";
        if (op == "&") {
            // No real AND - BIC is "dst &= ~src", so COM the rhs word first (its stack slot
            // is dead afterward anyway).
            emit("com", rhsOperand);
            emit("bic", cat(rhsOperand, ",", lhsOperand));
        } else if (op == "|") {
            emit("bis", cat(rhsOperand, ",", lhsOperand));
        } else {
            emit("mov", cat(rhsOperand, ",r0"));
            emit("xor", cat("r0,", lhsOperand));
        }
    }
    emit("add", cat(2 * words, ",sp"));
}

// Compares two `words`-word values most-significant word first (only that compare needs the
// signed/unsigned distinction); once a word differs, that comparison alone decides the
// result. `CMP src,dst` sets flags for `src - dst`, so `cmp lhsWord,rhsWord` is directly
// `lhs - rhs`.
void Pdp11Codegen::genWideCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned,
                                    int words) {
    genExpr(lhs);
    genExpr(rhs);
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    if (op == "==" || op == "!=") {
        for (int i = words - 1; i >= 0; i--) {
            std::string lhsOperand = "[sp+" + std::to_string(2 * words + 2 * i) + "]";
            std::string rhsOperand = "[sp+" + std::to_string(2 * i) + "]";
            emit("cmp", cat(lhsOperand, ",", rhsOperand));
            std::string Lcontinue = newLabel();
            safeBranch("beq", Lcontinue, (op == "==" ? Lfalse : Ltrue));
            emitLabel(Lcontinue);
        }
        emit("jmp", op == "==" ? Ltrue : Lfalse);
    } else {
        std::string signedBranch = op == "<" ? "blt" : op == "<=" ? "ble" : op == ">" ? "bgt" : "bge";
        std::string unsignedBranch = op == "<" ? "blo" : op == "<=" ? "blos" : op == ">" ? "bhi" : "bhis";
        for (int i = words - 1; i >= 0; i--) {
            std::string lhsOperand = "[sp+" + std::to_string(2 * words + 2 * i) + "]";
            std::string rhsOperand = "[sp+" + std::to_string(2 * i) + "]";
            emit("cmp", cat(lhsOperand, ",", rhsOperand));
            std::string Lcontinue = newLabel(), Ldiffer = newLabel();
            safeBranch("beq", Lcontinue, Ldiffer);
            emitLabel(Ldiffer);
            // Only the most significant word uses the signed branch; lower words, once
            // reached, compare a pure magnitude (higher words already matched).
            bool useSigned = (i == words - 1) && !isUnsigned;
            safeBranch(useSigned ? signedBranch : unsignedBranch, Ltrue, Lfalse);
            emitLabel(Lcontinue);
        }
        emit("jmp", Lfalse);  // every word equal
    }
    emitLabel(Lfalse);
    emit("mov", "0,r0");
    emit("jmp", Ldone);
    emitLabel(Ltrue);
    emit("mov", "1,r0");
    emitLabel(Ldone);
    emit("add", cat(4 * words, ",sp"));  // release both operand blocks - the result is a plain int in r0
}

// A `long`/`long long` shifted by a runtime count: no hardware shift is wider than one word,
// so this loops one bit at a time, folding the bit crossing a word boundary by hand, chained
// across `words` words.
void Pdp11Codegen::genWideShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned, int words) {
    genExpr(lhs);     // pushes `words` words
    emit("push", "r1");  // reserve a slot to stash the shift count
    genExpr(rhs);              // r0 = shift count (already promoted to a plain int)
    emit("mov", "r0,[sp]");
    std::string Lloop = newLabel(), Lbody = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("tst", "[sp]");
    safeBranch("beq", Ldone, Lbody);
    emitLabel(Lbody);
    if (isLeft) {
        emit("clc");
        for (int i = 0; i < words; i++) {
            std::string w = "[sp+" + std::to_string(2 + 2 * i) + "]";
            emit("rol", w);
        }
    } else {
        emit("clc");
        for (int i = words - 1; i >= 0; i--) {
            std::string w = "[sp+" + std::to_string(2 + 2 * i) + "]";
            // A signed shift's top word sign-extends instead of filling with a cleared
            // carry; CLC/no-CLC before that word's `ror` is the only difference.
            if (i == words - 1 && !isUnsigned) {
                emit("asr", w);
            } else {
                emit("ror", w);
            }
        }
    }
    emit("sub", "1,[sp]");
    emit("jmp", Lloop);
    emitLabel(Ldone);
    emit("pop", "r1");  // discard the shift-count slot; the `words`-word result stays on top
}

// `long`/`long long` multiply/divide need a bit-serial software routine (EIS MUL/DIV only
// widen one native word to two, not `words` to `2*words`); deliberately not implemented
// since this backend has no simulator to verify such an algorithm against.
void Pdp11Codegen::genWideMul(const ir::Expr &lhs, const ir::Expr &rhs, int) {
    err(lhs.line, "multiplying two 'long'/'long long' values isn't supported on this target yet - "
                   "see Pdp11Codegen.cpp's own genWideMul() comment");
    (void)rhs;
}

void Pdp11Codegen::genWideDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool, bool, int) {
    err(lhs.line, "dividing two 'long'/'long long' values isn't supported on this target yet - "
                   "see Pdp11Codegen.cpp's own genWideDivMod() comment");
    (void)rhs;
}

// The real work here is crossing the native-word/`long`-or-`long long` boundary (pushing
// more words of sign/zero extension, or popping to just the low word) and re-narrowing a
// `char` result.
void Pdp11Codegen::genConvert(const ir::Expr &e) {
    Type from = e.lhs->type;
    Type to = e.type;
    bool fromWide = isWide(from);
    bool toWide = isWide(to);
    if (!fromWide && !toWide) {
        genExpr(*e.lhs);  // r0
        if (widthOfType(to) == 1) {
            if (to.isUnsigned) {
                emit("bic", "0xFF00,r0");
            } else {
                emit("movb", "r0,r0");  // narrow to the low byte, then (register dest) sign-extend back up
            }
        }
        return;
    }
    if (fromWide && !toWide) {
        int words = wordCountOf(from);
        genExpr(*e.lhs);               // pushes `words` words
        emit("mov", "[sp],r0");  // the low word is this type's own truncated value
        emit("add", cat(2 * words, ",sp"));
        if (widthOfType(to) == 1) {
            if (to.isUnsigned) {
                emit("bic", "0xFF00,r0");
            } else {
                emit("movb", "r0,r0");
            }
        }
        return;
    }
    if (!fromWide && toWide) {
        // The least significant word must end up on top of the stack, so the real value
        // (saved in r1 before SXT overwrites r0) is pushed LAST, after the extension words.
        int toWords = wordCountOf(to);
        genExpr(*e.lhs);  // r0 = the native-width value, already correctly sign/zero-extended to 16 bits
        emit("mov", "r0,r1");
        if (from.isUnsigned) {
            emit("mov", "0,r0");
        } else {
            emit("sxt", "r0");
        }
        for (int i = 1; i < toWords; i++) emit("push", "r0");
        emit("push", "r1");
        return;
    }
    // fromWide && toWide: pad/truncate word count, preserving sign.
    int fromWords = wordCountOf(from);
    int toWords = wordCountOf(to);
    genExpr(*e.lhs);
    if (toWords > fromWords) {
        // Extension words must land underneath the existing fromWords words, so those are
        // popped into registers first and pushed back on top of the new extension words,
        // restoring word 0 to the top.
        emit("mov", cat("[sp+", 2 * (fromWords - 1), "],r0"));
        emit("sxt", "r0");
        static const char *const tempRegs[] = {"r1", "r3", "r4"};
        for (int i = 0; i < fromWords; i++) emit("pop", tempRegs[i]);
        for (int i = 0; i < toWords - fromWords; i++) emit("push", "r0");
        for (int i = fromWords - 1; i >= 0; i--) emit("push", tempRegs[i]);
    } else if (toWords < fromWords) {
        emit("add", cat(2 * (fromWords - toWords), ",sp"));
    }
}

// No-op unless `to` is a `char`.
void Pdp11Codegen::narrowR0To(Type to) {
    if (widthOfType(to) != 1) return;
    if (to.isUnsigned) {
        emit("bic", "0xFF00,r0");
    } else {
        emit("movb", "r0,r0");  // narrow to the low byte, then (register dest) sign-extend back up
    }
}

// `R2` ends up holding the computed address via register-deferred `(Rn)` mode. An array's
// base is a compile-time-known address (a Global label, or a Local's frame-relative
// address); a plain pointer variable's base instead comes from reading its stored value.
// A Local array's elements ascend from its lowest address, `r5 - localArrayBase(slot)`, the
// same direction as a Global array's, so pointer arithmetic over either works the same way.
int Pdp11Codegen::localArrayBase(const ir::Slot *slot) { return ir::localLowOffset(*slot, typeWidths()); }

void Pdp11Codegen::genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
    genExpr(idxExpr);  // r0 = index
    int shift = elemShift(slot->type);
    if (shift >= 0) {
        // r0 *= widthOf(slot->type) (a power of two): one "asl" per shift
        // level doubles r0 each time, e.g. shift=2 (a 4-byte element)
        // needs two, not one.
        for (int i = 0; i < shift; i++) emit("asl", "r0");
    } else {
        // A non-power-of-two element width (e.g. a packed struct's) has
        // no shift-based scaling, so multiply by the real width via EIS
        // MUL instead; only the low word of the 32-bit product is kept,
        // same convention as genMul().
        emit("mov", cat(widthOfType(slot->type), ",r1"));
        emit("mul", "r1,r0");
        emit("mov", "r1,r0");
    }
    if (slot->arraySize > 0) {
        if (slot->kind == ir::SlotKind::Global) {
            emit("lea", cat(label(slot), ",r2"));
            emit("add", "r0,r2");
        } else {
            emit("mov", "r5,r2");
            emit("sub", cat(localArrayBase(slot), ",r2"));
            emit("add", "r0,r2");
        }
        return;
    }
    emit("mov", cat(operandOf(slot), ",r2"));
    emit("add", "r0,r2");
}

void Pdp11Codegen::genIndex(const ir::Expr &e) {
    genIndexAddr(e.slot, *e.lhs);
    if (widthOfType(e.slot->type) == 1) {
        // MOVB into a register destination always sign-extends to 16 bits; an unsigned char
        // needs its high byte masked back off afterward.
        emit("ldindb", "r2,r0");
        if (e.slot->type.isUnsigned) emit("bic", "0xFF00,r0");
    } else {
        emit("ldind", "r2,r0");
    }
}

// A Local array is reserved frame storage, freshly allocated on every call, so its
// initializer must be stored with real instructions each time this runs. A symbol element
// (a Slot's or Function's address) needs `lea`, since a bare-label operand otherwise means
// "read the value stored there", not "take its address".
void Pdp11Codegen::genLocalArrayInit(const ir::Slot *slot) {
    for (size_t i = 0; i < slot->arrayInit.size(); i++) {
        const ir::ArrayInitElem &elem = slot->arrayInit[i];
        if (elem.isSymbol) {
            emit("lea", cat((elem.slot ? label(elem.slot) : label(elem.func)), ",r0"));
        } else {
            emit("mov", cat(static_cast<int>(static_cast<uint16_t>(elem.value)), ",r0"));
        }
        int elemW = widthOfType(slot->type);
        emit(elemW == 1 ? "movb" : "mov",
             cat("r0,[r5-", localArrayBase(slot) - elemW * static_cast<int>(i), "]"));
    }
}

// A Local/Param's address is a runtime value (`r5` plus/minus a displacement); `lea` only
// works from a compile-time-absolute label (PC-immediate addressing), so this computes the
// address with real arithmetic instead.
void Pdp11Codegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        if (operand.slot->kind == ir::SlotKind::Global) {
            emit("lea", cat(label(operand.slot), ",r0"));
            return;
        }
        emit("mov", "r5,r0");
        if (operand.slot->kind == ir::SlotKind::Param) {
            emit("add", cat(operand.slot->frameOffset, ",r0"));
        } else {
            int below = ir::localOffsetDescendingWide(*operand.slot, typeWidths());
            emit("sub", cat(below, ",r0"));
        }
        return;
    }
    if (operand.kind == ir::ExprKind::Index) {
        genIndexAddr(operand.slot, *operand.lhs);
        emit("mov", "r2,r0");
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        emit("lea", cat(label(operand.func), ",r0"));
        return;
    }
    err(operand.line, "internal: unhandled addressable-operand kind");
}

// Entry convention: r0 = old value, r1 = rhs value, both at `type`'s width/signedness. `-`
// and `&` aren't commutative, so each is reordered to compute old-op-rhs with `old` already
// in r0 (`sub r1,r0` for `-`; `com r1`/`bic r1,r0` directly for `&`, since the destination is
// already r0). `/`/`%` reshuffles into genDivMod()'s r1=dividend/r2=divisor layout.
void Pdp11Codegen::genCompoundOp(const std::string &op, Type type) {
    bool isUnsigned = type.isUnsigned;
    if (op == "+") { emit("add", "r1,r0"); return; }
    if (op == "-") { emit("sub", "r1,r0"); return; }
    if (op == "*") { emit("mul", "r1,r0"); emit("mov", "r1,r0"); return; }
    if (op == "&") { emit("com", "r1"); emit("bic", "r1,r0"); return; }
    if (op == "|") { emit("bis", "r1,r0"); return; }
    if (op == "^") { emit("xor", "r1,r0"); return; }
    if (op == "<<" || op == ">>") {
        std::string Lloop = newLabel(), Lbody = newLabel(), Ldone = newLabel();
        emitLabel(Lloop);
        emit("tst", "r1");
        safeBranch("beq", Ldone, Lbody);
        emitLabel(Lbody);
        if (op == "<<") {
            emit("asl", "r0");
        } else if (isUnsigned) {
            emit("clc");
            emit("ror", "r0");
        } else {
            emit("asr", "r0");
        }
        emit("sub", "1,r1");
        emit("jmp", Lloop);
        emitLabel(Ldone);
        return;
    }
    // "/" or "%"
    emit("mov", "r1,r2");  // r2 = divisor
    emit("mov", "r0,r1");  // r1 = dividend
    if (isUnsigned) {
        emitUnsignedDiv();
    } else {
        emit("tst", "r1");
        emit("sxt", "r0");  // r0 = sign extension of the dividend (high word)
        emit("div", "r2,r0");           // r0 = quotient, r1 = remainder
    }
    if (op == "%") emit("mov", "r1,r0");
}

// r2 must already hold the resolved address on entry. The address rides the stack (not a
// register) across evaluating rhs, since that evaluation may use any register.
void Pdp11Codegen::genCompoundThroughR2(const ir::Expr &e) {
    emit("push", "r2");  // stash address
    genExpr(*e.rhs);            // r0 = rhs
    emit("push", "r0");    // stash rhs
    emit("pop", "r1");      // r1 = rhs
    emit("pop", "r2");       // r2 = address
    loadOperand("[r2+0]", e.type);  // r0 = old
    emit("push", "r2");        // re-stash address for the store below
    if (e.yieldOld) emit("push", "r0");  // stash old too
    genCompoundOp(e.op, e.type);                // r0 = new (raw)
    narrowR0To(e.type);
    if (e.yieldOld) {
        emit("pop", "r1");  // r1 = old (pre-op), stashed above, storeOperand() never touches r1
        emit("pop", "r2");   // r2 = address
        storeOperand("[r2+0]", e.type);  // stores the new value (still in r0)
        emit("mov", "r1,r0");       // r0 = old (final result)
    } else {
        emit("pop", "r2");  // r2 = address
        storeOperand("[r2+0]", e.type);  // r0 already = new
    }
}

void Pdp11Codegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    // Every operand here shares one common type via `e.lhs->type` (IRBuilder's usual-
    // arithmetic-conversion pass), except shift (only LHS matters) and &&/|| (width-independent).
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
            emit("add", "r1,r0");  // r0 = r0 + r1 = rhs + lhs
        } else {
            emit("sub", "r0,r1");  // r1 = r1 - r0 = lhs - rhs
            emit("mov", "r1,r0");
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
            // No real AND on the PDP-11 - only BIC (dst &= ~src). De Morgan: COM rhs first
            // (giving ~rhs), then BIC ~rhs,lhs leaves lhs & rhs.
            emit("com", "r0");
            emit("bic", "r0,r1");
            emit("mov", "r1,r0");
        } else if (op == "|") {
            emit("bis", "r1,r0");  // r0 |= r1
        } else {
            emit("xor", "r1,r0");  // r0 ^= r1
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

// No variable-count logical shift (EIS `ASH` shifts arithmetically), so loop one bit at a
// time, counting down in `r1`. `isUnsigned` picks `ROR` (zero-filling) vs `ASR`
// (sign-preserving) for `>>`; `<<` never cares about signedness.
void Pdp11Codegen::genShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned) {
    genExpr(lhs);
    emit("push", "r0");
    genExpr(rhs);
    emit("mov", "r0,r1");  // r1 = shift count
    emit("pop", "r0");     // r0 = value to shift
    std::string Lloop = newLabel(), Lbody = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("tst", "r1");
    safeBranch("beq", Ldone, Lbody);
    emitLabel(Lbody);
    if (isLeft) {
        emit("asl", "r0");
    } else if (isUnsigned) {
        emit("clc");
        emit("ror", "r0");
    } else {
        emit("asr", "r0");  // arithmetic: sign bit stays, same value rotated back into bit 15
    }
    emit("sub", "1,r1");
    emit("jmp", Lloop);
    emitLabel(Ldone);
}

// EIS `MUL`: the low word of its 32-bit product (`R1`) is the 16-bit truncated answer.
void Pdp11Codegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    emit("push", "r0");
    genExpr(rhs);
    emit("pop", "r1");    // r1 = lhs, r0 = rhs
    emit("mul", "r1,r0");  // r0:r1 = r0 * r1 = rhs * lhs (high word:low word)
    emit("mov", "r1,r0");
}

// EIS `DIV` needs a 32-bit dividend in `R0:R1`, so the 16-bit dividend is first
// sign-extended into that pair with `SXT` (`TST` first, to set the N flag `SXT` reads).
void Pdp11Codegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    emit("push", "r0");
    genExpr(rhs);
    emit("mov", "r0,r2");  // r2 = divisor
    emit("pop", "r1");     // r1 = dividend (low word)
    if (isUnsigned) {
        emitUnsignedDiv();
    } else {
        emit("tst", "r1");
        emit("sxt", "r0");  // r0 = sign extension of the dividend (high word)
        emit("div", "r2,r0");           // r0 = quotient, r1 = remainder
    }
    if (wantRemainder) emit("mov", "r1,r0");
}

// EIS `DIV` is signed: the divisor is a signed 16-bit value and the
// quotient must fit in 15 bits. With the dividend zero-extended into
// R0:R1 it is exact for divisors 2..0x7FFF. A divisor of 1 would give
// a quotient up to 0xFFFF, so it is handled directly; a divisor with
// its top bit set divides an unsigned 16-bit dividend at most once.
void Pdp11Codegen::emitUnsignedDiv() {
    std::string Lbig = newLabel(), Lsmall = newLabel(), Lone = newLabel(), Lnorm = newLabel(),
                Lge = newLabel(), Llt = newLabel(), Ldone = newLabel();
    emit("tst", "r2");  // clears V, so blt branches exactly when N is set
    safeBranch("blt", Lbig, Lsmall);
    emitLabel(Lsmall);
    emit("mov", "r2,r0");
    emit("sub", "1,r0");
    safeBranch("beq", Lone, Lnorm);
    emitLabel(Lnorm);
    emit("mov", "0,r0");
    emit("div", "r2,r0");
    emit("jmp", Ldone);
    emitLabel(Lone);
    emit("mov", "r1,r0");
    emit("mov", "0,r1");
    emit("jmp", Ldone);
    emitLabel(Lbig);
    emit("cmp", "r1,r2");  // flags = dividend - divisor
    safeBranch("bhis", Lge, Llt);
    emitLabel(Llt);
    emit("mov", "0,r0");
    emit("jmp", Ldone);
    emitLabel(Lge);
    emit("sub", "r2,r1");
    emit("mov", "1,r0");
    emitLabel(Ldone);
}

void Pdp11Codegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned) {
    if (op == "==") return emitCompare(lhs, rhs, "beq");
    if (op == "!=") return emitCompare(lhs, rhs, "bne");
    if (op == "<") return emitCompare(lhs, rhs, isUnsigned ? "blo" : "blt");
    if (op == "<=") return emitCompare(lhs, rhs, isUnsigned ? "blos" : "ble");
    if (op == ">") return emitCompare(lhs, rhs, isUnsigned ? "bhi" : "bgt");
    if (op == ">=") return emitCompare(lhs, rhs, isUnsigned ? "bhis" : "bge");
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

// `CMP src,dst` sets condition codes for `src - dst`, so `cmp r1,r0` (lhs, rhs) is directly
// `lhs - rhs`, with `branchIfTrue` applying with no further sign/overflow fixup.
void Pdp11Codegen::emitCompare(const ir::Expr &lhs, const ir::Expr &rhs, const std::string &branchIfTrue) {
    genExpr(lhs);
    emit("push", "r0");
    genExpr(rhs);
    emit("pop", "r1");    // r1 = lhs, r0 = rhs
    emit("cmp", "r1,r0");  // flags = r1 - r0 = lhs - rhs
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    safeBranch(branchIfTrue, Ltrue, Lfalse);
    emitLabel(Lfalse);
    emit("mov", "0,r0");
    emit("jmp", Ldone);
    emitLabel(Ltrue);
    emit("mov", "1,r0");
    emitLabel(Ldone);
}

// Leaves r0 holding a plain 0/nonzero truth value, with any wide stack space released.
void Pdp11Codegen::testTruthy(const ir::Expr &e) {
    if (!isWide(e.type)) {
        emit("tst", "r0");
        return;
    }
    int words = wordCountOf(e.type);
    emit("mov", "[sp],r0");
    for (int i = 1; i < words; i++) emit("bis", cat("[sp+", 2 * i, "],r0"));
    emit("add", cat(2 * words, ",sp"));
    emit("tst", "r0");
}

void Pdp11Codegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    testTruthy(lhs);
    std::string Lfalse = newLabel(), Lcheck2 = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    safeBranch("beq", Lfalse, Lcheck2);
    emitLabel(Lcheck2);
    genExpr(rhs);
    testTruthy(rhs);
    safeBranch("beq", Lfalse, Ltrue);
    emitLabel(Ltrue);
    emit("mov", "1,r0");
    emit("jmp", Ldone);
    emitLabel(Lfalse);
    emit("mov", "0,r0");
    emitLabel(Ldone);
}

void Pdp11Codegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    testTruthy(lhs);
    std::string Ltrue = newLabel(), Lcheck2 = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    safeBranch("bne", Ltrue, Lcheck2);
    emitLabel(Lcheck2);
    genExpr(rhs);
    testTruthy(rhs);
    safeBranch("bne", Ltrue, Lfalse);
    emitLabel(Lfalse);
    emit("mov", "0,r0");
    emit("jmp", Ldone);
    emitLabel(Ltrue);
    emit("mov", "1,r0");
    emitLabel(Ldone);
}

void Pdp11Codegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    testTruthy(rhs);
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    safeBranch("beq", Ltrue, Lfalse);
    emitLabel(Lfalse);
    emit("mov", "0,r0");
    emit("jmp", Ldone);
    emitLabel(Ltrue);
    emit("mov", "1,r0");
    emitLabel(Ldone);
}

std::optional<std::string> Pdp11Codegen::assembleInProcess(const std::string &asmText,
                                                            const std::string &format) const {
    if (format == "raw") return assemblePdp11(asmText);
    // `coff`/`elf`/`macho`/`aout`/`objraw`: build one format-neutral ObjectFile, then pick
    // that format's FileFormatWriter to serialize it.
    if (auto kind = fileFormatKindFromName(format)) {
        ObjectFile obj = assemblePdp11Object(asmText);
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

std::optional<std::string> Pdp11Codegen::debugMap(const std::string &asmText, const std::string &format,
        const std::string &sourcePath) const {
    (void)format;
    return assemblePdp11DebugMap(asmText, sourcePath, debugSymbols_);
}

std::optional<std::string> Pdp11Codegen::debugMapPreLink(const std::string &asmText,
                                                    const std::string &sourcePath) const {
    return assemblePdp11DebugMap(asmText, sourcePath, debugSymbols_, /*allowExternal=*/true);
}

// Only emits a marker when `debugMapEnabled_` (set via setDebugMapEnabled(),
// before generate() runs) and `line` actually differs from the last one
// emitted - see Pdp7Codegen.cpp's identical emitSourceLine(), which this
// mirrors exactly.
void Pdp11Codegen::emitSourceLine(int line) {
    if (!debugMapEnabled_ || line == 0 || line == lastDebugLine_) return;
    emitRaw(cat("__line ", line));
    lastDebugLine_ = line;
}

}  // namespace rcc
