#include "CP1610Codegen.h"

#include <cstdint>
#include <stdexcept>

#include "CP1610Assembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

// Where the whole program loads and starts executing - a WORD address (see CP1610Codegen.h's
// own header comment on why this target has no byte addressing at all).
constexpr uint32_t kBaseAddress = 0x1000;

// Where `r6` (the real hardware stack pointer) is set before calling main(), comfortably above
// the loaded program and its data (also a word address).
constexpr uint32_t kInitialSp = 0xE000;
}  // namespace

std::string CP1610Codegen::arrayElemText(const ir::ArrayInitElem &e) {
    if (!e.isSymbol) return std::to_string(static_cast<int>(static_cast<uint16_t>(e.value)));
    return e.slot ? label(e.slot) : label(e.func);
}

void CP1610Codegen::declareArray(const std::string &lbl, const std::vector<ir::ArrayInitElem> &values,
                                  Type elemType) {
    if (values.empty()) return;
    int w = widthOfType(elemType);  // already in native words - every element is >=1 word
    std::ostringstream line;
    line << lbl << ": dw ";
    bool first = true;
    for (const auto &e : values) {
        if (w > 1 && !e.isSymbol) {
            auto uv = static_cast<unsigned long>(e.value);
            for (int i = 0; i < w; i++) {
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

std::string CP1610Codegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    emitRaw(cat("org ", objectMode ? 0 : kBaseAddress));
    emitRaw("");
    if (!objectMode) {
        emitLabel("_start");
        emit("mvii", cat(kInitialSp, ",r6"));
        emit("jsr", cat("r5,", label(mainFn)));
        // The result sits in r0 once execution reaches `halt`.
        emitLabel("halt");
        emit("hlt");
        emitRaw("");
    }

    for (const auto &fb : prog.functionBodies) genFunction(fb);

    // Globals are laid out back-to-back, one native word per storage unit - no alignment
    // padding is ever needed, unlike a byte-addressed backend's globals (every unit here is
    // already exactly one addressable location).
    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        if (slot->isExternal) continue;
        const std::string lbl = label(slot.get());
        if (slot->arraySize > 0) {
            declareArray(lbl, slot->arrayInit, slot->type);
        } else if (slot->type.base == BaseType::Struct) {
            declareArray(lbl, std::vector<ir::ArrayInitElem>(slot->type.structSize), Type{BaseType::Char, false});
        } else {
            long v = slot->hasInit ? slot->initValue : 0;
            int w = widthOfType(slot->type);
            auto uv = static_cast<unsigned long>(v);
            std::ostringstream line;
            line << lbl << ": dw ";
            for (int i = 0; i < w; i++) {
                if (i > 0) line << ", ";
                line << static_cast<uint16_t>((uv >> (16 * i)) & 0xFFFF);
            }
            line << "  ; " << slot->sourceName;
            trailer_.push_back(line.str());
        }
    }

    // Scratch cells for `long`/`long long` binary operations (genWideAdd()/genWideBitwise()/
    // genWideCompare()/genWideShift()) - this CPU's stack (`r6`) only ever pushes/pops, with no
    // way to peek at a given depth the way a displacement-addressing backend's wide-value stack
    // technique needs, so both operands are instead popped into these named globals and operated
    // on there directly. Safe the same way Pdp7Codegen.h's own `__sp`/`__fp`/... cells are: each
    // one's live range is a single short, call-free instruction sequence that's already finished
    // using them well before any nested wide expression inside `lhs`/`rhs` could have been
    // evaluated (every use pops fresh values in immediately before reading them back out).
    for (int i = 0; i < 4; i++) {
        trailer_.push_back("__w" + std::to_string(i) + ": dw 0");
        trailer_.push_back("__v" + std::to_string(i) + ": dw 0");
    }
    // Sign-flag scratch for genDivMod()'s signed case - same safety argument as `__w`/`__v`.
    trailer_.push_back("__dsign: dw 0");
    trailer_.push_back("__ssign: dw 0");

    std::ostringstream out;
    out << code_.str();
    for (const auto &line : trailer_) out << line << "\n";
    return out.str();
}

std::map<std::string, std::string> CP1610Codegen::perFileAsm() const { return perFileAsm_; }

// Prologue: `jsr r5,label` (the caller's own instruction, not emitted here) leaves the return
// address in r5; `pshr r5` saves it first, `pshr r4` saves the caller's frame pointer, `movr
// r6,r4` makes r4 this call's frame pointer, then `sub frameSize,r6` reserves Locals below it.
// Epilogue (here and every `Return`) undoes it: `movr r4,r6` drops the Locals, `pulr r4`
// restores the caller's frame pointer, `pulr r7` pops the saved return address directly into
// the real hardware PC - this CPU's own documented "return" idiom (there's no dedicated
// instruction for it).
void CP1610Codegen::genFunction(const ir::FunctionBody &fn) {
    emitLabel(label(fn.func));
    emit("pshr", "r5");
    emit("pshr", "r4");
    emit("movr", "r6,r4");
    if (fn.func->frameSize > 0) emit("sub", cat(fn.func->frameSize, ",r6"));
    genStmt(*fn.body);
    emit("movr", "r4,r6");
    emit("pulr", "r4");
    emit("pulr", "r7");
    emitRaw("");
}

void CP1610Codegen::genStmt(const ir::Stmt &s) {
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
            emit("bze", s.elseStmt ? Lelse : Ldone);
            genStmt(*s.thenStmt);
            if (s.elseStmt) {
                emit("b", Ldone);
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
            emit("bze", Lend);
            loopLabels_.push_back({Lcont, Lend});
            genStmt(*s.loopBody);
            loopLabels_.pop_back();
            if (s.postStmt) {
                emitLabel(Lcont);
                genStmt(*s.postStmt);
            }
            emit("b", Lstart);
            emitLabel(Lend);
            return;
        }
        case ir::StmtKind::Break: {
            emit("b", loopLabels_.back().second);
            return;
        }
        case ir::StmtKind::Continue: {
            emit("b", loopLabels_.back().first);
            return;
        }
        case ir::StmtKind::Return: {
            if (s.initOrReturnOrExpr) {
                genExpr(*s.initOrReturnOrExpr);
                if (isWide(s.initOrReturnOrExpr->type)) {
                    int words = wordCountOf(s.initOrReturnOrExpr->type);
                    for (int i = 0; i < words; i++) emit("pulr", cat("r", i));
                }
            }
            emit("movr", "r4,r6");
            emit("pulr", "r4");
            emit("pulr", "r7");
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
                    genSlotAddr(s.declSlot);
                    storeOperand(operandOf(s.declSlot), s.declSlot->type);
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
            emit("b", irLabel(s.labelId));
            return;
        }
    }
}

// Computes `slot`'s effective address into r2. A Global's "address" is a compile-time-constant
// label value, loaded as a plain immediate (`mvii label,r2` - a label used as an immediate
// operand is just its own numeric word address, so this needs no separate "address-of"
// instruction the way a byte-addressed backend's `lea` does); a Local/Param's address is r4
// plus/minus a compile-time-constant word offset, computed with real arithmetic since this CPU
// has no indexed/displacement addressing mode at all.
void CP1610Codegen::genSlotAddr(const ir::Slot *slot) {
    if (slot->kind == ir::SlotKind::Global) {
        emit("mvii", cat(label(slot), ",r2"));
        return;
    }
    emit("movr", "r4,r2");
    if (slot->kind == ir::SlotKind::Param) {
        if (slot->frameOffset > 0) emit("add", cat(slot->frameOffset, ",r2"));
    } else {
        int below = ir::localOffsetDescendingWide(*slot, typeWidths());
        if (below > 0) emit("sub", cat(below, ",r2"));
    }
}

// Like genSlotAddr(), but for word `wordIndex` (0 = least significant) of a `long`/`long long`
// scalar - a Global's own label plus a literal word offset (direct mode, folded straight into
// the address operand - a label used this way is a plain compile-time constant, same as an
// equate); a Local/Param's address with that same offset folded into the ADDI/SUBI already
// being done.
void CP1610Codegen::genWideSlotWordAddr(const ir::Slot *slot, int wordIndex) {
    if (slot->kind == ir::SlotKind::Global) {
        std::string lbl = wordIndex == 0 ? label(slot) : (label(slot) + "+" + std::to_string(wordIndex));
        emit("mvii", cat(lbl, ",r2"));
        return;
    }
    emit("movr", "r4,r2");
    if (slot->kind == ir::SlotKind::Param) {
        int off = slot->frameOffset + wordIndex;
        if (off > 0) emit("add", cat(off, ",r2"));
    } else {
        int below = slot->frameOffset + wordIndex;
        if (below > 0) emit("sub", cat(below, ",r2"));
    }
}

void CP1610Codegen::genExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit: {
            if (isWide(e.type)) {
                genWideLoadLiteral(e.intValue, wordCountOf(e.type));
                return;
            }
            emit("mvii", cat(static_cast<int>(static_cast<uint16_t>(e.intValue)), ",r0"));
            return;
        }
        case ir::ExprKind::Var: {
            if (isWide(e.slot->type)) {
                genWideLoad(e.slot, wordCountOf(e.slot->type));
                return;
            }
            if (e.slot->kind != ir::SlotKind::Global) genSlotAddr(e.slot);
            loadOperand(operandOf(e.slot), e.slot->type);
            return;
        }
        case ir::ExprKind::Index: {
            genIndex(e);
            return;
        }
        case ir::ExprKind::Assign: {
            if (e.lhs) {  // slot[idx] = rhs - may be wide (e.g. a `long` array's element).
                genIndexAddr(e.slot, *e.lhs);  // r2 = target address
                emit("pshr", "r2");
                genExpr(*e.rhs);
                storeIndirect(e.slot->type);  // handles a wide element too
                return;
            }
            if (isWide(e.slot->type)) {
                genExpr(*e.rhs);  // pushes `words` words (LSW on top)
                genWideStore(e.slot, wordCountOf(e.slot->type));
                return;
            }
            genExpr(*e.rhs);
            if (e.slot->kind != ir::SlotKind::Global) genSlotAddr(e.slot);
            storeOperand(operandOf(e.slot), e.slot->type);
            return;
        }
        case ir::ExprKind::DerefAssign: {
            genExpr(*e.lhs);  // r0 = pointer value
            emit("movr", "r0,r2");
            emit("pshr", "r2");
            genExpr(*e.rhs);  // r0, or pushes `words` words if wide
            storeIndirect(e.type);
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            if (e.lhs) {  // slot[idx] op= rhs (including slot[idx]++/--) - never wide.
                genIndexAddr(e.slot, *e.lhs);  // r2 = address
                genCompoundThroughR2(e);
                return;
            }
            if (e.slot->kind != ir::SlotKind::Global) genSlotAddr(e.slot);
            std::string mem = operandOf(e.slot);
            emit("pshr", "r2");  // stash the computed address (Global's own `mem` doesn't need it, but pushing is harmless)
            genExpr(*e.rhs);     // r0 = rhs
            emit("movr", "r0,r1");
            emit("pulr", "r2");
            loadOperand(mem, e.type);  // r0 = old
            // genCompoundOp()'s own `*`/`/`/`%` paths use r2/r3 as scratch, so the address has
            // to be re-stashed across that call too (same as genCompoundThroughR2() already
            // does for the indexed/pointer case) - not just across evaluating rhs above.
            emit("pshr", "r2");
            if (e.yieldOld) emit("pshr", "r0");
            genCompoundOp(e.op, e.type);
            narrowR0To(e.type);
            if (e.yieldOld) {
                emit("pshr", "r0");  // new
                emit("pulr", "r1");  // r1 = new
                emit("pulr", "r3");  // r3 = old
                emit("pulr", "r2");  // r2 = address
                emit("movr", "r1,r0");  // r0 = new, ready for storeOperand()
                storeOperand(mem, e.type);
                emit("movr", "r3,r0");  // r0 = old (final result)
            } else {
                emit("pulr", "r2");  // r2 = address
                storeOperand(mem, e.type);  // r0 already = new
            }
            return;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            genExpr(*e.lhs);  // r0 = pointer value
            emit("movr", "r0,r2");
            genCompoundThroughR2(e);
            return;
        }
        case ir::ExprKind::Ternary: {
            genExpr(*e.lhs);
            testTruthy(*e.lhs);
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            emit("bze", Lelse);
            genExpr(*e.rhs);
            emit("b", Ldone);
            emitLabel(Lelse);
            genExpr(*e.elseExpr);
            emitLabel(Ldone);
            return;
        }
        case ir::ExprKind::Unary: {
            if (e.op == "-") {
                genExpr(*e.rhs);
                if (isWide(e.rhs->type)) {
                    // genWideNegate() operates on the `__v*` scratch cells (it's normally
                    // genWideAdd()'s own helper), not the real stack - genExpr() above left the
                    // value on the stack instead, so it has to move there and back.
                    int words = wordCountOf(e.rhs->type);
                    static const char *const vcells[] = {"__v0", "__v1", "__v2", "__v3"};
                    for (int i = 0; i < words; i++) {
                        emit("pulr", "r0");
                        emit("mvo", cat("r0,", vcells[i]));
                    }
                    genWideNegate(words);
                    for (int i = words - 1; i >= 0; i--) {
                        emit("mvi", cat(vcells[i], ",r0"));
                        emit("pshr", "r0");
                    }
                } else {
                    emit("negr", "r0");
                }
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                genExpr(*e.rhs);  // r0 = pointer value
                emit("movr", "r0,r2");
                loadIndirect("r2", e.type);
            } else if (e.op == "~") {
                genExpr(*e.rhs);
                if (isWide(e.rhs->type)) {
                    // genExpr() already pushed `words` words (LSW on top). Pop each into a
                    // register, complement it, then push every one back in the opposite order
                    // (MSW first) so the LSW ends up back on top - the net effect of
                    // complementing every word of a stack-resident wide value in place, with no
                    // displacement addressing mode to index into the stack directly.
                    static const char *const tempRegs[] = {"r0", "r1", "r2", "r3"};
                    int words = wordCountOf(e.rhs->type);
                    for (int i = 0; i < words; i++) {
                        emit("pulr", tempRegs[i]);
                        emit("comr", tempRegs[i]);
                    }
                    for (int i = words - 1; i >= 0; i--) emit("pshr", tempRegs[i]);
                } else {
                    emit("comr", "r0");
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
                    emit("pshr", "r0");
                    totalWords += 1;
                }
            }
            emit("jsr", cat("r5,", label(e.func)));
            if (totalWords > 0) emit("add", cat(totalWords, ",r6"));
            if (isWide(e.type)) {
                int words = wordCountOf(e.type);
                for (int i = words - 1; i >= 0; i--) emit("pshr", cat("r", i));
            }
            return;
        }
        case ir::ExprKind::CallPtr: {
            // CP1610 has no register-indirect call instruction at all - `JSR` only ever takes a
            // literal address word. A computed call is instead built from two things real
            // hardware *does* have: writing any register directly into r7 (the PC) is an
            // unconditional jump to that address, and the return address this needs is a
            // compile-time-known label (right after this call sequence) - put into r5 exactly
            // the way a real `JSR` leaves it there, rather than pushed by hand, so the callee's
            // own prologue (which always starts with a plain `pshr r5`, expecting a real `JSR`)
            // still works completely unmodified.
            genExpr(*e.lhs);  // r0 = the callee's own entry address
            emit("movr", "r0,r2");
            std::string Lret = newLabel();
            emit("mvii", cat(Lret, ",r5"));
            emit("movr", "r2,r7");
            emitLabel(Lret);
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

// Loads from `mem` (a Global label or "[r2]") into r0, then re-derives a `char`'s real 8-bit
// value - this target's storage is always a full word (no byte addressing at all - see
// CP1610Codegen.h's own header comment), so a `char` cell can accumulate stale high-byte bits
// from some earlier, wider write; every load re-applies C's own 8-bit truncation instead of
// trusting whatever happens to already be in the high byte.
void CP1610Codegen::loadOperand(const std::string &mem, Type type) {
    emit("mvi", cat(mem, ",r0"));
    if (isCharType(type)) narrowR0To(type);
}

void CP1610Codegen::storeOperand(const std::string &mem, Type type) {
    if (isCharType(type)) narrowR0To(type);
    emit("mvo", cat("r0,", mem));
}

// A wide pointee's words live at consecutive addresses starting at `addrReg` (always r2 - see
// Unary's "*" case) - no instruction can add a displacement, so this walks a *copy* of the base
// address (r5) forward by hand, reading each word into one of r0/r1/r3/r2 (r2 itself is free to
// reuse as the 4th scratch register once its own value has been copied into r5), then pushes
// them back in reverse (MSW first) so the LSW ends up on top, matching every narrow load's own
// convention.
void CP1610Codegen::loadIndirect(const std::string &addrReg, Type type) {
    if (!isWide(type)) {
        emit("mvi", cat("[", addrReg, "],r0"));
        if (isCharType(type)) narrowR0To(type);
        return;
    }
    int words = wordCountOf(type);
    static const char *const tempRegs[] = {"r0", "r1", "r3", "r2"};
    emit("movr", cat(addrReg, ",r5"));
    // `[r5]` is real hardware auto-increment addressing (mode 5 - see
    // CP1610Assembler.h's own header comment): each access already advances r5 by 1 on its
    // own, so consecutive words need no explicit `add 1,r5` between them (that would double-
    // advance it).
    for (int i = 0; i < words; i++) emit("mvi", cat("[r5],", tempRegs[i]));
    for (int i = words - 1; i >= 0; i--) emit("pshr", tempRegs[i]);
}

// `*lhs = rhs`'s store: r2 already holds `lhs`'s own pointer value, pushed on the stack before
// `rhs` was evaluated (which may have clobbered r2 as scratch); `rhs`'s own `words` pushed
// words (LSW on top) sit above it. Pops every word into a register first (freeing the stack
// down to the stashed address), then recovers the address into r5 and walks it forward by hand
// to write each word - the same technique as loadIndirect(), in reverse.
void CP1610Codegen::storeIndirect(Type type) {
    if (!isWide(type)) {
        emit("pulr", "r2");
        if (isCharType(type)) narrowR0To(type);
        emit("mvo", "r0,[r2]");
        return;
    }
    int words = wordCountOf(type);
    static const char *const tempRegs[] = {"r0", "r1", "r3", "r2"};
    for (int i = 0; i < words; i++) emit("pulr", tempRegs[i]);
    emit("pulr", "r5");  // the stashed address, now free since every word is popped
    // `[r5]` auto-increments on its own (see loadIndirect()'s own comment) - no explicit
    // `add 1,r5` between words.
    for (int i = 0; i < words; i++) emit("mvo", cat(tempRegs[i], ",[r5]"));
}

// Pushes `slot`'s `words`-word value, least-significant word on top (pushed last).
void CP1610Codegen::genWideLoad(const ir::Slot *slot, int words) {
    for (int i = words - 1; i >= 0; i--) {
        genWideSlotWordAddr(slot, i);
        emit("mvi", "[r2],r0");
        emit("pshr", "r0");
    }
}

void CP1610Codegen::genWideLoadLiteral(long value, int words) {
    auto uv = static_cast<unsigned long>(value);
    for (int i = words - 1; i >= 0; i--) {
        int shiftedWord = static_cast<int>((uv >> (16 * i)) & 0xFFFF);
        emit("mvii", cat(shiftedWord, ",r0"));
        emit("pshr", "r0");
    }
}

// Pops `words` words off the stack (least-significant on top) into `slot`'s storage.
void CP1610Codegen::genWideStore(const ir::Slot *slot, int words) {
    for (int i = 0; i < words; i++) {
        emit("pulr", "r0");
        genWideSlotWordAddr(slot, i);
        emit("mvo", "r0,[r2]");
    }
}

namespace {
const char *const kWScratch[4] = {"__w0", "__w1", "__w2", "__w3"};
const char *const kVScratch[4] = {"__v0", "__v1", "__v2", "__v3"};
}  // namespace

// Negates the `words`-word value in the `__v*` scratch cells, in place: `COM` every word, then
// ripples a `+1` through via `ADCR` (which only adds the carry bit) - no re-raise logic is
// needed here (unlike genWideRippleAdd()'s own real two-addend chain), since every word past
// the first adds nothing but that one carry bit.
void CP1610Codegen::genWideNegate(int words) {
    for (int i = 0; i < words; i++) {
        emit("mvi", cat(kVScratch[i], ",r0"));
        emit("comr", "r0");
        emit("mvo", cat("r0,", kVScratch[i]));
    }
    emit("mvi", cat(kVScratch[0], ",r0"));
    emit("add", "1,r0");
    emit("mvo", cat("r0,", kVScratch[0]));
    for (int i = 1; i < words; i++) {
        emit("mvi", cat(kVScratch[i], ",r0"));
        emit("adcr", "r0");
        emit("mvo", cat("r0,", kVScratch[i]));
    }
}

// `__w[] += __v[]` (or `__w[] -= __v[]`, via genWideNegate() on `__v[]` first - see
// genWideAdd()), word 0 upward. Word 0 is a plain `ADDR` (which also sets the carry the next
// word's `ADCR` reads); each later word folds that carry in with `ADCR` first (which only adds
// the carry bit into one register - there's no real two-operand add-with-carry here), then adds
// the other operand with a plain `ADDR` - which can itself carry again, so `BC`+`SETC` re-raises
// the carry flag by hand whenever the `ADCR` step already produced one (mirrors
// Pdp11Codegen.cpp's own genWideAdd(), using this CPU's real `SETC` instead of PDP-11's compare-
// based workaround).
void CP1610Codegen::genWideRippleAdd(int words) {
    emit("mvi", cat(kWScratch[0], ",r0"));
    emit("mvi", cat(kVScratch[0], ",r1"));
    emit("addr", "r1,r0");
    emit("mvo", cat("r0,", kWScratch[0]));
    for (int i = 1; i < words; i++) {
        std::string Lcarried = newLabel(), Ldone = newLabel();
        emit("mvi", cat(kWScratch[i], ",r0"));
        emit("adcr", "r0");
        emit("bc", Lcarried);
        emit("mvi", cat(kVScratch[i], ",r1"));
        emit("addr", "r1,r0");
        emit("b", Ldone);
        emitLabel(Lcarried);
        emit("mvi", cat(kVScratch[i], ",r1"));
        emit("addr", "r1,r0");
        emit("setc");
        emitLabel(Ldone);
        emit("mvo", cat("r0,", kWScratch[i]));
    }
}

void CP1610Codegen::genWideAdd(const ir::Expr &lhs, const ir::Expr &rhs, bool isAdd, int words) {
    genExpr(lhs);
    genExpr(rhs);
    for (int i = 0; i < words; i++) {
        emit("pulr", "r0");
        emit("mvo", cat("r0,", kVScratch[i]));
    }
    for (int i = 0; i < words; i++) {
        emit("pulr", "r0");
        emit("mvo", cat("r0,", kWScratch[i]));
    }
    if (!isAdd) genWideNegate(words);
    genWideRippleAdd(words);
    for (int i = words - 1; i >= 0; i--) {
        emit("mvi", cat(kWScratch[i], ",r0"));
        emit("pshr", "r0");
    }
}

void CP1610Codegen::genWideBitwise(const ir::Expr &lhs, const ir::Expr &rhs, const std::string &op, int words) {
    genExpr(lhs);
    genExpr(rhs);
    for (int i = 0; i < words; i++) {
        emit("pulr", "r0");
        emit("mvo", cat("r0,", kVScratch[i]));
    }
    for (int i = 0; i < words; i++) {
        emit("pulr", "r0");
        emit("mvo", cat("r0,", kWScratch[i]));
    }
    for (int i = 0; i < words; i++) {
        emit("mvi", cat(kWScratch[i], ",r0"));
        emit("mvi", cat(kVScratch[i], ",r1"));
        if (op == "&") {
            emit("andr", "r1,r0");
        } else if (op == "^") {
            emit("xorr", "r1,r0");
        } else {  // "|" - no real OR exists on this CPU; De Morgan: a|b = ~(~a & ~b)
            emit("comr", "r0");
            emit("comr", "r1");
            emit("andr", "r1,r0");
            emit("comr", "r0");
        }
        emit("mvo", cat("r0,", kWScratch[i]));
    }
    for (int i = words - 1; i >= 0; i--) {
        emit("mvi", cat(kWScratch[i], ",r0"));
        emit("pshr", "r0");
    }
}

void CP1610Codegen::genWideCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned,
                                    int words) {
    genExpr(lhs);
    genExpr(rhs);
    for (int i = 0; i < words; i++) {
        emit("pulr", "r0");
        emit("mvo", cat("r0,", kVScratch[i]));
    }
    for (int i = 0; i < words; i++) {
        emit("pulr", "r0");
        emit("mvo", cat("r0,", kWScratch[i]));
    }
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    if (op == "==" || op == "!=") {
        for (int i = words - 1; i >= 0; i--) {
            emit("mvi", cat(kWScratch[i], ",r0"));
            emit("mvi", cat(kVScratch[i], ",r1"));
            emit("cmpr", "r1,r0");
            std::string Lcontinue = newLabel();
            emit("bze", Lcontinue);
            emit("b", op == "==" ? Lfalse : Ltrue);
            emitLabel(Lcontinue);
        }
        emit("b", op == "==" ? Ltrue : Lfalse);
    } else {
        std::string signedBranch = op == "<" ? "blt" : op == "<=" ? "ble" : op == ">" ? "bgt" : "bge";
        // Once a word differs (the `bze` below didn't fire), a strict `<`/`>` on that one word
        // alone also decides `<=`/`>=` - `CMPR dst,src` sets carry = 1 iff dst >= src (this
        // CPU's own real "no borrow" convention), so the unsigned case only ever needs `bnc`
        // (for `<`/`<=`) or `bc` (for `>`/`>=`).
        bool wantLess = (op == "<" || op == "<=");
        for (int i = words - 1; i >= 0; i--) {
            emit("mvi", cat(kWScratch[i], ",r0"));
            emit("mvi", cat(kVScratch[i], ",r1"));
            emit("cmpr", "r1,r0");
            std::string Lcontinue = newLabel();
            emit("bze", Lcontinue);
            bool useSigned = (i == words - 1) && !isUnsigned;
            if (useSigned) {
                emit(signedBranch, Ltrue);
                emit("b", Lfalse);
            } else if (wantLess) {
                emit("bnc", Ltrue);
                emit("b", Lfalse);
            } else {
                emit("bc", Ltrue);
                emit("b", Lfalse);
            }
            emitLabel(Lcontinue);
        }
        emit("b", Lfalse);
    }
    emitLabel(Lfalse);
    emit("mvii", "0,r0");
    emit("b", Ldone);
    emitLabel(Ltrue);
    emit("mvii", "1,r0");
    emitLabel(Ldone);
}

// A `long`/`long long` shifted by a runtime count loops one bit at a time via `RLC`/`RRC`
// (rotate-through-carry across the scratch cells), the same bit-serial technique every other
// backend's own wide shift uses, adapted to this CPU's register-pressure limits the same way
// genWideAdd() is.
void CP1610Codegen::genWideShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned, int words) {
    genExpr(lhs);
    for (int i = 0; i < words; i++) {
        emit("pulr", "r0");
        emit("mvo", cat("r0,", kWScratch[i]));
    }
    genExpr(rhs);  // r0 = shift count
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("tstr", "r0");
    emit("bze", Ldone);
    emit("movr", "r0,r1");  // stash the remaining count (r0 is clobbered below)
    emit("clrc");
    if (isLeft) {
        for (int i = 0; i < words; i++) {
            emit("mvi", cat(kWScratch[i], ",r0"));
            emit("rlc", "r0,1");
            emit("mvo", cat("r0,", kWScratch[i]));
        }
    } else {
        for (int i = words - 1; i >= 0; i--) {
            emit("mvi", cat(kWScratch[i], ",r0"));
            if (i == words - 1 && !isUnsigned) {
                // `SARC` (not `RRC`) for the top word only: a real arithmetic shift (sign-fills
                // bit 15, instead of pulling in whatever's in the carry flag) that *also*
                // captures the bit it shifts out into carry, ready for the next word down's
                // own `RRC` - `RRC` alone (as every other word here uses) only ever rotates the
                // carry flag in, which is the wrong fill for the very first (most significant)
                // word of a signed shift.
                emit("sarc", "r0,1");
            } else {
                emit("rrc", "r0,1");
            }
            emit("mvo", cat("r0,", kWScratch[i]));
        }
    }
    emit("movr", "r1,r0");
    emit("decr", "r0");
    emit("b", Lloop);
    emitLabel(Ldone);
    for (int i = words - 1; i >= 0; i--) {
        emit("mvi", cat(kWScratch[i], ",r0"));
        emit("pshr", "r0");
    }
}

// Deliberately not implemented - the CP1610 has no multiply/divide instruction even for a
// native 16-bit value (see genMul()/genDivMod()'s own software loops), so a from-scratch
// wide-multiply/divide algorithm would be a second, much larger software routine on top of
// that; matches the same scope cut Pdp11Codegen.cpp/Z8000Codegen.cpp both make for their own
// (hardware-multiply-capable) targets, for the same reason - see docs/known-bugs.md.
void CP1610Codegen::genWideMul(const ir::Expr &lhs, const ir::Expr &rhs, int) {
    err(lhs.line, "multiplying two 'long'/'long long' values isn't supported on this target yet - "
                   "see CP1610Codegen.cpp's own genWideMul() comment");
    (void)rhs;
}

void CP1610Codegen::genWideDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool, bool, int) {
    err(lhs.line, "dividing two 'long'/'long long' values isn't supported on this target yet - "
                   "see CP1610Codegen.cpp's own genWideDivMod() comment");
    (void)rhs;
}

// The real work here is crossing the native-word/`long`-or-`long long` boundary and re-deriving
// a `char`'s own truncated range.
void CP1610Codegen::genConvert(const ir::Expr &e) {
    Type from = e.lhs->type;
    Type to = e.type;
    bool fromWide = isWide(from);
    bool toWide = isWide(to);
    if (!fromWide && !toWide) {
        genExpr(*e.lhs);
        if (isCharType(to)) narrowR0To(to);
        return;
    }
    if (fromWide && !toWide) {
        int words = wordCountOf(from);
        genExpr(*e.lhs);
        emit("pulr", "r0");                                    // the low word is this type's own truncated value
        for (int i = 1; i < words; i++) emit("pulr", "r1");  // discard the rest
        if (isCharType(to)) narrowR0To(to);
        return;
    }
    if (!fromWide && toWide) {
        int toWords = wordCountOf(to);
        genExpr(*e.lhs);  // r0 = the native-width value, already correctly sign/zero-extended
        emit("movr", "r0,r1");
        if (from.isUnsigned) {
            emit("mvii", "0,r0");
        } else {
            // Sign-extend r1's sign bit into r0: 0 or -1 depending on r1's own top bit.
            emit("movr", "r1,r0");
            for (int i = 0; i < 4; i++) emit("sar", "r0,2");  // r0 >>= 8 (arithmetic)
            for (int i = 0; i < 4; i++) emit("sar", "r0,2");  // r0 >>= 8 more (= 16, all-sign)
        }
        for (int i = 1; i < toWords; i++) emit("pshr", "r0");
        emit("pshr", "r1");
        return;
    }
    int fromWords = wordCountOf(from);
    int toWords = wordCountOf(to);
    genExpr(*e.lhs);
    if (toWords > fromWords) {
        static const char *const tempRegs[] = {"r1", "r3"};
        // Pop the existing fromWords words into scratch, computing the sign-extension word
        // from the most significant one first.
        for (int i = 0; i < fromWords; i++) emit("pulr", i == 0 ? "r0" : tempRegs[i - 1]);
        emit("movr", cat(fromWords == 1 ? "r0" : tempRegs[fromWords - 2], ",r2"));
        if (to.isUnsigned && from.isUnsigned) {
            emit("mvii", "0,r2");
        } else {
            for (int i = 0; i < 4; i++) emit("sar", "r2,2");
            for (int i = 0; i < 4; i++) emit("sar", "r2,2");
        }
        for (int i = 0; i < toWords - fromWords; i++) emit("pshr", "r2");
        for (int i = fromWords - 1; i >= 0; i--) emit("pshr", i == 0 ? "r0" : tempRegs[i - 1]);
    } else if (toWords < fromWords) {
        for (int i = 0; i < fromWords - toWords; i++) emit("pulr", "r0");
    }
}

// No-op unless `to` is a `char`. Every storage cell is a full word (see this file's own header
// comment), so a `char`'s own 8-bit range is re-derived from r0's low byte every time: `AND
// 0x00FF,r0` for unsigned, or an arithmetic-shift round trip (left 8, then right 8 - there's no
// dedicated sign-extend instruction) for signed.
void CP1610Codegen::narrowR0To(Type to) {
    if (!isCharType(to)) return;
    if (to.isUnsigned) {
        emit("and", "0x00FF,r0");
        return;
    }
    emit("and", "0x00FF,r0");
    for (int i = 0; i < 4; i++) emit("sll", "r0,2");  // r0 <<= 8
    for (int i = 0; i < 4; i++) emit("sar", "r0,2");  // r0 >>= 8 (arithmetic)
}

// Software 16x16->16 (truncated) multiply: standard shift-and-add, 16 iterations. r1 = the
// multiplicand (doubled via `SLL` each iteration), r2 = the multiplier (consumed one bit at a
// time: `CLRC` then `RRC r2,1` shifts it right logically while also capturing the bit it shifts
// out into the carry flag, since there's no separate single-bit-test instruction), r3 = loop
// counter, r0 = the running result - the CP1610 has no multiply instruction at all.
void CP1610Codegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    emit("pshr", "r0");
    genExpr(rhs);
    emit("movr", "r0,r2");  // r2 = rhs (the multiplier, consumed)
    emit("pulr", "r1");     // r1 = lhs (the multiplicand)
    emit("mvii", "0,r0");   // r0 = running result
    emit("mvii", "16,r3");
    std::string Lloop = newLabel(), Ladd = newLabel(), Lskip = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("tstr", "r3");
    emit("bze", Ldone);
    emit("clrc");
    emit("rrc", "r2,1");
    emit("bc", Ladd);
    emit("b", Lskip);
    emitLabel(Ladd);
    emit("addr", "r1,r0");
    emitLabel(Lskip);
    emit("sll", "r1,1");
    emit("decr", "r3");
    emit("b", Lloop);
    emitLabel(Ldone);
}

// Software restoring division, unrolled 16 iterations (no hardware multiply/divide exists on
// this CPU - see this file's own header comment): `SLLC r0,1` shifts the dividend left while
// capturing its old top bit into carry; `RLC r2,1` then folds that bit into the remainder's own
// bottom bit while shifting it left - the standard bit-serial "long division" step, one pair of
// instructions instead of the usual separate shift-then-OR. `r1` holds the (unsigned) divisor
// throughout; `r3` accumulates the quotient the same way, fed a 0 or 1 each iteration via
// `SETC`/`CLRC` before its own `RLC`. The signed case takes the absolute value of both operands
// first (stashing each original sign in a scratch global, since all four real registers are
// already busy - same technique genWideAdd() uses), then fixes the quotient's sign (negated
// when the operand signs differed) and the remainder's sign (always the dividend's own original
// sign, C's own truncating-division convention) afterward.
void CP1610Codegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    emit("pshr", "r0");
    genExpr(rhs);
    emit("movr", "r0,r1");  // r1 = divisor
    emit("pulr", "r0");     // r0 = dividend
    if (!isUnsigned) {
        std::string Lskip1 = newLabel(), Lskip2 = newLabel();
        emit("mvii", "0,r2");
        emit("tstr", "r0");
        emit("bpl", Lskip1);
        emit("mvii", "1,r2");
        emit("negr", "r0");
        emitLabel(Lskip1);
        emit("mvo", "r2,__dsign");
        emit("mvii", "0,r2");
        emit("tstr", "r1");
        emit("bpl", Lskip2);
        emit("mvii", "1,r2");
        emit("negr", "r1");
        emitLabel(Lskip2);
        emit("mvo", "r2,__ssign");
    }
    emit("mvii", "0,r2");  // remainder
    emit("mvii", "0,r3");  // quotient
    for (int i = 0; i < 16; i++) {
        std::string Lge = newLabel(), Lcont = newLabel();
        emit("clrc");
        emit("sllc", "r0,1");
        emit("rlc", "r2,1");
        emit("cmpr", "r1,r2");
        emit("bc", Lge);
        emit("clrc");
        emit("b", Lcont);
        emitLabel(Lge);
        emit("subr", "r1,r2");
        emit("setc");
        emitLabel(Lcont);
        emit("rlc", "r3,1");
    }
    if (!isUnsigned) {
        std::string LsameSign = newLabel(), LnoNegRem = newLabel();
        emit("mvi", "__dsign,r0");
        emit("mvi", "__ssign,r1");
        emit("cmpr", "r1,r0");
        emit("bze", LsameSign);
        emit("negr", "r3");
        emitLabel(LsameSign);
        emit("mvi", "__dsign,r0");
        emit("tstr", "r0");
        emit("bze", LnoNegRem);
        emit("negr", "r2");
        emitLabel(LnoNegRem);
    }
    emit("movr", cat(wantRemainder ? "r2" : "r3", ",r0"));
}

int CP1610Codegen::localArrayBase(const ir::Slot *slot) { return ir::localLowOffset(*slot, typeWidths()); }

// Leaves slot[idxExpr]'s effective address in r2. An array's base is a compile-time-known
// address (a Global label, or a Local's frame-relative address, computed the same way
// genSlotAddr() does); a plain pointer variable's base instead comes from reading its own
// stored value first.
void CP1610Codegen::genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
    genExpr(idxExpr);  // r0 = index
    int shift = elemShift(slot->type);
    if (shift >= 0) {
        for (int i = 0; i < shift; i++) emit("addr", "r0,r0");
    } else {
        // A non-power-of-two element width (e.g. a packed struct's) has no shift-based scaling;
        // multiply by the real width in place, inline, with the same shift-and-add technique
        // genMul() uses (r1=multiplicand/index, r2=multiplier/width consumed, r3=counter, r0=
        // the running result) - both operands are already available without a real genMul()
        // call's own push/pop dance.
        emit("movr", "r0,r1");
        emit("mvii", cat(widthOfType(slot->type), ",r2"));
        emit("mvii", "0,r0");
        emit("mvii", "16,r3");
        std::string Lloop = newLabel(), Ladd = newLabel(), Lskip = newLabel(), Ldone = newLabel();
        emitLabel(Lloop);
        emit("tstr", "r3");
        emit("bze", Ldone);
        emit("clrc");
        emit("rrc", "r2,1");
        emit("bc", Ladd);
        emit("b", Lskip);
        emitLabel(Ladd);
        emit("addr", "r1,r0");
        emitLabel(Lskip);
        emit("sll", "r1,1");
        emit("decr", "r3");
        emit("b", Lloop);
        emitLabel(Ldone);
    }
    if (slot->arraySize > 0) {
        if (slot->kind == ir::SlotKind::Global) {
            emit("mvii", cat(label(slot), ",r2"));
        } else {
            emit("movr", "r4,r2");
            int below = localArrayBase(slot);
            if (below > 0) emit("sub", cat(below, ",r2"));
        }
        emit("addr", "r0,r2");
        return;
    }
    // A plain pointer variable: load its own stored value (the base address) first.
    if (slot->kind == ir::SlotKind::Global) {
        emit("mvi", cat(label(slot), ",r2"));
    } else {
        genSlotAddr(slot);  // r2 = the pointer variable's own address
        emit("mvi", "[r2],r3");
        emit("movr", "r3,r2");  // r2 = the pointer's stored value
    }
    emit("addr", "r0,r2");
}

void CP1610Codegen::genIndex(const ir::Expr &e) {
    genIndexAddr(e.slot, *e.lhs);  // r2 = element address
    loadIndirect("r2", e.slot->type);  // handles a wide (`long`/`long long`) element too
}

// A Local array is reserved frame storage, freshly allocated on every call, so its initializer
// must be stored with real instructions each time this runs. A symbol element (a Slot's or
// Function's address) is just that label used as a plain immediate (see genSlotAddr()'s own
// header comment on why that alone is enough - no separate "address-of" instruction exists).
void CP1610Codegen::genLocalArrayInit(const ir::Slot *slot) {
    for (size_t i = 0; i < slot->arrayInit.size(); i++) {
        const ir::ArrayInitElem &elem = slot->arrayInit[i];
        if (elem.isSymbol) {
            emit("mvii", cat(elem.slot ? label(elem.slot) : label(elem.func), ",r0"));
        } else {
            emit("mvii", cat(static_cast<int>(static_cast<uint16_t>(elem.value)), ",r0"));
        }
        emit("movr", "r4,r2");
        int elemW = widthOfType(slot->type);
        int below = localArrayBase(slot) - elemW * static_cast<int>(i);
        if (below > 0) emit("sub", cat(below, ",r2"));
        emit("mvo", "r0,[r2]");
    }
}

// A Local/Param's address is a runtime value (r4 plus/minus a displacement); a Global's is a
// compile-time-constant label, loaded as a plain immediate (see genSlotAddr()'s own header
// comment - there's no separate "lea"-style instruction on this target either way).
void CP1610Codegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        if (operand.slot->kind == ir::SlotKind::Global) {
            emit("mvii", cat(label(operand.slot), ",r0"));
            return;
        }
        emit("movr", "r4,r0");
        if (operand.slot->kind == ir::SlotKind::Param) {
            if (operand.slot->frameOffset > 0) emit("add", cat(operand.slot->frameOffset, ",r0"));
        } else {
            int below = ir::localOffsetDescendingWide(*operand.slot, typeWidths());
            if (below > 0) emit("sub", cat(below, ",r0"));
        }
        return;
    }
    if (operand.kind == ir::ExprKind::Index) {
        genIndexAddr(operand.slot, *operand.lhs);
        emit("movr", "r2,r0");
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        emit("mvii", cat(label(operand.func), ",r0"));
        return;
    }
    err(operand.line, "internal: unhandled addressable-operand kind");
}

// Entry convention: r0 = old value, r1 = rhs value, both at `type`'s width/signedness.
void CP1610Codegen::genCompoundOp(const std::string &op, Type type) {
    bool isUnsigned = type.isUnsigned;
    if (op == "+") { emit("addr", "r1,r0"); return; }
    if (op == "-") { emit("subr", "r1,r0"); return; }
    if (op == "&") { emit("andr", "r1,r0"); return; }
    if (op == "^") { emit("xorr", "r1,r0"); return; }
    if (op == "|") {
        // De Morgan: a|b = ~(~a & ~b)
        emit("comr", "r0");
        emit("comr", "r1");
        emit("andr", "r1,r0");
        emit("comr", "r0");
        return;
    }
    if (op == "*") {
        // r0/r1 already hold the operands this needs; inline the same shift-and-add technique
        // genMul() uses rather than round-tripping through genExpr().
        emit("mvii", "0,r2");
        emit("mvii", "16,r3");
        std::string Lloop = newLabel(), Ladd = newLabel(), Lskip = newLabel(), Ldone = newLabel();
        emitLabel(Lloop);
        emit("tstr", "r3");
        emit("bze", Ldone);
        emit("clrc");
        emit("rrc", "r1,1");
        emit("bc", Ladd);
        emit("b", Lskip);
        emitLabel(Ladd);
        emit("addr", "r0,r2");
        emitLabel(Lskip);
        emit("sll", "r0,1");
        emit("decr", "r3");
        emit("b", Lloop);
        emitLabel(Ldone);
        emit("movr", "r2,r0");
        return;
    }
    if (op == "<<" || op == ">>") {
        std::string Lloop = newLabel(), Ldone = newLabel();
        emitLabel(Lloop);
        emit("tstr", "r1");
        emit("bze", Ldone);
        if (op == "<<") {
            emit("sll", "r0,1");
        } else if (isUnsigned) {
            emit("slr", "r0,1");
        } else {
            emit("sar", "r0,1");
        }
        emit("decr", "r1");
        emit("b", Lloop);
        emitLabel(Ldone);
        return;
    }
    // "/" or "%": r0 = old (dividend), r1 = rhs (divisor) - already exactly genDivMod()'s own
    // entry convention once r2/r3 are seeded, so this inlines the same restoring-division loop
    // rather than round-tripping through genExpr().
    bool wantRemainder = (op == "%");
    if (!isUnsigned) {
        std::string Lskip1 = newLabel(), Lskip2 = newLabel();
        emit("mvii", "0,r2");
        emit("tstr", "r0");
        emit("bpl", Lskip1);
        emit("mvii", "1,r2");
        emit("negr", "r0");
        emitLabel(Lskip1);
        emit("mvo", "r2,__dsign");
        emit("mvii", "0,r2");
        emit("tstr", "r1");
        emit("bpl", Lskip2);
        emit("mvii", "1,r2");
        emit("negr", "r1");
        emitLabel(Lskip2);
        emit("mvo", "r2,__ssign");
    }
    emit("mvii", "0,r2");
    emit("mvii", "0,r3");
    for (int i = 0; i < 16; i++) {
        std::string Lge = newLabel(), Lcont = newLabel();
        emit("clrc");
        emit("sllc", "r0,1");
        emit("rlc", "r2,1");
        emit("cmpr", "r1,r2");
        emit("bc", Lge);
        emit("clrc");
        emit("b", Lcont);
        emitLabel(Lge);
        emit("subr", "r1,r2");
        emit("setc");
        emitLabel(Lcont);
        emit("rlc", "r3,1");
    }
    if (!isUnsigned) {
        std::string LsameSign = newLabel(), LnoNegRem = newLabel();
        emit("mvi", "__dsign,r0");
        emit("mvi", "__ssign,r1");
        emit("cmpr", "r1,r0");
        emit("bze", LsameSign);
        emit("negr", "r3");
        emitLabel(LsameSign);
        emit("mvi", "__dsign,r0");
        emit("tstr", "r0");
        emit("bze", LnoNegRem);
        emit("negr", "r2");
        emitLabel(LnoNegRem);
    }
    emit("movr", cat(wantRemainder ? "r2" : "r3", ",r0"));
}

// r2 must already hold the resolved address on entry. The address rides the real stack (not a
// register) across evaluating rhs, since that evaluation may use any register.
void CP1610Codegen::genCompoundThroughR2(const ir::Expr &e) {
    emit("pshr", "r2");
    genExpr(*e.rhs);
    emit("movr", "r0,r1");
    emit("pulr", "r2");
    emit("mvi", "[r2],r0");
    if (isCharType(e.type)) narrowR0To(e.type);  // r0 = old
    emit("pshr", "r2");
    if (e.yieldOld) emit("pshr", "r0");
    genCompoundOp(e.op, e.type);  // r0 = new (raw)
    narrowR0To(e.type);
    if (e.yieldOld) {
        emit("pulr", "r1");      // r1 = old value (pushed last above, popped first)
        emit("pulr", "r2");      // r2 = address
        emit("mvo", "r0,[r2]");  // writes the new value (still in r0)
        emit("movr", "r1,r0");   // r0 = old (final result)
    } else {
        emit("pulr", "r2");      // r2 = address
        emit("mvo", "r0,[r2]");  // r0 already = new
    }
}

void CP1610Codegen::genBinary(const ir::Expr &e) {
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
        emit("pshr", "r0");
        genExpr(*e.rhs);
        emit("pulr", "r1");  // r1 = lhs, r0 = rhs
        if (op == "+") {
            emit("addr", "r1,r0");
        } else {
            emit("subr", "r0,r1");  // r1 = r1 - r0 = lhs - rhs
            emit("movr", "r1,r0");
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
        emit("pshr", "r0");
        genExpr(*e.rhs);
        emit("pulr", "r1");  // r1 = lhs, r0 = rhs
        if (op == "&") {
            emit("andr", "r1,r0");
        } else if (op == "^") {
            emit("xorr", "r1,r0");
        } else {  // "|" - De Morgan: a|b = ~(~a & ~b)
            emit("comr", "r0");
            emit("comr", "r1");
            emit("andr", "r1,r0");
            emit("comr", "r0");
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

// No variable-count shift instruction exists, so this loops one bit at a time via `RLC`/`RRC`
// (the same technique genWideShift() uses per-word).
void CP1610Codegen::genShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned) {
    genExpr(lhs);
    emit("pshr", "r0");
    genExpr(rhs);
    emit("movr", "r0,r1");  // r1 = shift count
    emit("pulr", "r0");     // r0 = value to shift
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("tstr", "r1");
    emit("bze", Ldone);
    if (isLeft) {
        emit("sll", "r0,1");
    } else if (isUnsigned) {
        emit("slr", "r0,1");
    } else {
        emit("sar", "r0,1");  // arithmetic: real hardware sign-fill, no carry trick needed
    }
    emit("decr", "r1");
    emit("b", Lloop);
    emitLabel(Ldone);
}

void CP1610Codegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned) {
    if (op == "==") return emitCompare(lhs, rhs, "bze", false);
    if (op == "!=") return emitCompare(lhs, rhs, "bnze", false);
    // `CMPR dst,src` sets carry = 1 iff dst >= src (unsigned) - this CPU's own real "no borrow"
    // convention (see `SUBR`'s own identical carry rule) - so unsigned `<` is carry *clear*,
    // not set.
    if (op == "<") return emitCompare(lhs, rhs, isUnsigned ? "bnc" : "blt", false);
    if (op == ">=") return emitCompare(lhs, rhs, isUnsigned ? "bc" : "bge", false);
    // Unsigned `>`/`<=` have no single matching condition code of their own - spelled here as
    // the equivalent unsigned `<`/`>=` test with the operands swapped instead.
    if (op == ">") return emitCompare(lhs, rhs, isUnsigned ? "bnc" : "bgt", isUnsigned);
    if (op == "<=") return emitCompare(lhs, rhs, isUnsigned ? "bc" : "ble", isUnsigned);
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

// `CMPR src,dst` sets condition codes for `dst - src`, so `cmpr r0,r1` (rhs, lhs) is directly
// `lhs - rhs` - or, when `swapOperands` is set (see genCompare()'s own comment), `rhs - lhs`.
void CP1610Codegen::emitCompare(const ir::Expr &lhs, const ir::Expr &rhs, const std::string &branchIfTrue,
                                 bool swapOperands) {
    genExpr(lhs);
    emit("pshr", "r0");
    genExpr(rhs);
    emit("pulr", "r1");  // r1 = lhs, r0 = rhs
    emit("cmpr", swapOperands ? "r1,r0" : "r0,r1");
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    emit(branchIfTrue, Ltrue);
    emit("b", Lfalse);
    emitLabel(Lfalse);
    emit("mvii", "0,r0");
    emit("b", Ldone);
    emitLabel(Ltrue);
    emit("mvii", "1,r0");
    emitLabel(Ldone);
}

void CP1610Codegen::testTruthy(const ir::Expr &e) {
    if (!isWide(e.type)) {
        emit("tstr", "r0");
        return;
    }
    int words = wordCountOf(e.type);
    for (int i = 0; i < words; i++) {
        emit("pulr", "r0");
        emit("mvo", cat("r0,", kWScratch[i]));
    }
    emit("mvi", cat(kWScratch[0], ",r0"));
    for (int i = 1; i < words; i++) {
        emit("mvi", cat(kWScratch[i], ",r1"));
        // r0 |= r1 (De Morgan)
        emit("comr", "r0");
        emit("comr", "r1");
        emit("andr", "r1,r0");
        emit("comr", "r0");
    }
    emit("tstr", "r0");
}

void CP1610Codegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    testTruthy(lhs);
    std::string Lfalse = newLabel(), Ldone = newLabel();
    emit("bze", Lfalse);
    genExpr(rhs);
    testTruthy(rhs);
    emit("bze", Lfalse);
    emit("mvii", "1,r0");
    emit("b", Ldone);
    emitLabel(Lfalse);
    emit("mvii", "0,r0");
    emitLabel(Ldone);
}

void CP1610Codegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    testTruthy(lhs);
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit("bnze", Ltrue);
    genExpr(rhs);
    testTruthy(rhs);
    emit("bnze", Ltrue);
    emit("mvii", "0,r0");
    emit("b", Ldone);
    emitLabel(Ltrue);
    emit("mvii", "1,r0");
    emitLabel(Ldone);
}

void CP1610Codegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    testTruthy(rhs);
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit("bze", Ltrue);
    emit("mvii", "0,r0");
    emit("b", Ldone);
    emitLabel(Ltrue);
    emit("mvii", "1,r0");
    emitLabel(Ldone);
}

std::optional<std::string> CP1610Codegen::assembleInProcess(const std::string &asmText,
                                                              const std::string &format) const {
    if (format == "raw") return assembleCP1610(asmText);
    if (auto kind = fileFormatKindFromName(format)) {
        ObjectFile obj = assembleCP1610Object(asmText);
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

}  // namespace rcc
