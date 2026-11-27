#include "Pdp10Codegen.h"

#include <cstdint>
#include <tuple>
#include <stdexcept>

#include "Pdp10Assembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

// Where the whole program loads and starts executing; the test harness sets the simulated
// CPU's PC here directly (via the RIM10 loader's own final JRST word - see
// tests/run_tests.sh's check_pdp10()). Octal 1000: past the low-memory region real PDP-10
// software reserves.
constexpr uint64_t kBaseAddress = 0010000;

// Where P (AC17, the real stack pointer) starts, comfortably above the loaded program and its
// data; confirmed directly against the simulator that PUSH/PUSHJ increment P's address field
// *before* storing, so the real stack grows upward from here.
constexpr uint64_t kInitialSp = 0100000;

constexpr uint64_t kMask36 = 0777777777777ULL;

uint64_t trunc36(long v) { return static_cast<uint64_t>(v) & kMask36; }
}  // namespace

std::string Pdp10Codegen::arrayElemText(const ir::ArrayInitElem &e) {
    if (!e.isSymbol) return std::to_string(trunc36(e.value));
    return e.slot ? label(e.slot) : label(e.func);
}

void Pdp10Codegen::declareArray(const std::string &lbl, const std::vector<ir::ArrayInitElem> &values,
                                 Type elemType) {
    if (values.empty()) return;
    (void)elemType;  // every element is one native word here - see elemWordsOf()
    std::ostringstream line;
    line << lbl << ": dw ";
    for (size_t i = 0; i < values.size(); i++) {
        if (i) line << ", ";
        line << arrayElemText(values[i]);
    }
    trailer_.push_back(line.str());
}

std::string Pdp10Codegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    emitRaw("org " + std::to_string(objectMode ? 0 : kBaseAddress));
    emitRaw("");
    if (!objectMode) {
        emitLabel("_start");
        emit("movei", cat("15,", kInitialSp));
        emit("pushj", cat("15,", label(mainFn)));
        emitLabel("halt");
        emit("halt");
        emit("jrst", "0,halt");
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

    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        if (slot->isExternal) continue;
        if (debugMapEnabled_) debugSymbols_.push_back({label(slot.get()), /*isFunc=*/false});
        size_t gTrailerStart = trailer_.size();
        const std::string lbl = label(slot.get());
        if (slot->arraySize > 0) {
            declareArray(lbl, slot->arrayInit, slot->type);
        } else if (slot->type.base == BaseType::Struct) {
            // No struct initializer exists in this subset, so this is
            // always zero-filled; reuses declareArray's own element-list
            // emission (one zero word per unit) instead of a new
            // directive.
            declareArray(lbl, std::vector<ir::ArrayInitElem>(slot->type.structSize), Type{BaseType::Char, false});
        } else {
            long v = slot->hasInit ? slot->initValue : 0;
            trailer_.push_back(lbl + ": dw " + std::to_string(trunc36(v)) + "  ; " + slot->sourceName);
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

std::map<std::string, std::string> Pdp10Codegen::perFileAsm() const { return perFileAsm_; }

// Prologue: `PUSH P,AC16` saves the caller's frame pointer, `MOVE AC16,P` makes AC16 this
// call's frame pointer (pointing at that saved word), `ADDI P,N` reserves N words for Locals
// above it. Epilogue (here and every `Return`) undoes it: `MOVE P,AC16` drops the Locals,
// `POP P,AC16` restores the caller's frame pointer, `POPJ P,` returns - see this class's own
// header comment for why every sign here is the mirror of Pdp11Codegen.cpp's.
void Pdp10Codegen::genFunction(const ir::FunctionBody &fn) {
    lastDebugLine_ = 0;
    if (debugMapEnabled_) debugSymbols_.push_back({label(fn.func), /*isFunc=*/true});
    emitLabel(label(fn.func));
    emit("push", "15,14");
    emit("move", "14,15");
    if (fn.func->frameSize > 0) emit("addi", cat("15,", fn.func->frameSize));
    genStmt(*fn.body);
    emit("move", "15,14");
    emit("pop", "15,14");
    emit("popj", "15,0");
}

void Pdp10Codegen::genStmt(const ir::Stmt &s) {
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
            emit("jumpe", cat("1,", (s.elseStmt ? Lelse : Ldone)));
            genStmt(*s.thenStmt);
            if (s.elseStmt) {
                emit("jrst", cat("0,", Ldone));
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
            emit("jumpe", cat("1,", Lend));
            loopLabels_.push_back({Lcont, Lend});
            genStmt(*s.loopBody);
            loopLabels_.pop_back();
            if (s.postStmt) {
                emitLabel(Lcont);
                genStmt(*s.postStmt);
            }
            emit("jrst", cat("0,", Lstart));
            emitLabel(Lend);
            return;
        }
        case ir::StmtKind::Break: {
            emit("jrst", cat("0,", loopLabels_.back().second));
            return;
        }
        case ir::StmtKind::Continue: {
            emit("jrst", cat("0,", loopLabels_.back().first));
            return;
        }
        case ir::StmtKind::Return: {
            if (s.initOrReturnOrExpr) {
                genExpr(*s.initOrReturnOrExpr);
                if (isWide(s.initOrReturnOrExpr->type)) {
                    int words = wordCountOf(s.initOrReturnOrExpr->type);
                    for (int i = 0; i < words; i++) popReg(i + 1);
                }
            }
            emit("move", "15,14");
            emit("pop", "15,14");
            emit("popj", "15,0");
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
            emit("jrst", cat("0,", irLabel(s.labelId)));
            return;
        }
    }
}

// Loads a literal into AC1: `MOVEI` zero-extends its 18-bit field, so it's only correct when
// the value's 36-bit two's-complement pattern fits in 18 bits unsigned (0..0777777) - any
// other value (negative, or >= 2^18) goes through a literal data cell instead, confirmed by
// Pdp10Assembler.h's own encoding (there's no sign-extending immediate form on this target).
namespace {
}  // namespace

void Pdp10Codegen::genExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit: {
            if (isWide(e.type)) {
                genWideLoadLiteral(e.intValue, wordCountOf(e.type));
                return;
            }
            uint64_t v = trunc36(e.intValue);
            if (v <= 0777777ULL) {
                emit("movei", cat("1,", v));
            } else {
                std::string lbl = allocTempCell();
                trailer_.back() = lbl + ": dw " + std::to_string(v);
                emit("move", cat("1,", lbl));
            }
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
                genIndexAddr(e.slot, *e.lhs);  // AC3 = target address
                pushReg(3);
                genExpr(*e.rhs);
                popReg(3);
                emit("movem", "1,[3+0]");
                return;
            }
            if (isWide(e.slot->type)) {
                // Mirrors Pdp11Codegen.cpp's own wide Assign exactly: peek each pushed rhs
                // word in place (via P itself as the index register, `[15-i]`) rather than
                // popping, then discard the whole pushed block in one `SUBI`. Popping into
                // storage and then re-pushing would leak those words onto the real hardware
                // stack forever whenever this Assign is used as a bare statement (its result
                // never consumed), permanently shifting every later `[15-N]` in this function
                // by `words`. An Assign used as a statement is by far the common case, and
                // IRBuilder never needs a wide Assign's own value chained into another
                // expression, so this never leaves anything reusable on the stack either.
                int words = wordCountOf(e.slot->type);
                genExpr(*e.rhs);
                for (int i = 0; i < words; i++) {
                    emit("move", cat("1,[15-", i, "]"));
                    emit("movem", cat("1,", wideWordOperand(e.slot, i)));
                }
                emit("subi", cat("15,", words));
                return;
            }
            genExpr(*e.rhs);
            storeOperand(operandOf(e.slot), e.slot->type);
            return;
        }
        case ir::ExprKind::DerefAssign: {
            genExpr(*e.lhs);  // AC1 = pointer value
            emit("move", "3,1");
            pushReg(3);
            genExpr(*e.rhs);
            storeIndirect(e.type);
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            if (e.lhs) {
                genIndexAddr(e.slot, *e.lhs);  // AC3 = address
                genCompoundThroughAC3(e);
                return;
            }
            std::string mem = operandOf(e.slot);
            genExpr(*e.rhs);  // AC1 = rhs
            emit("move", "2,1");
            loadOperand(mem, e.type);  // AC1 = old
            if (e.yieldOld) pushReg(1);
            genCompoundOp(e.op, e.type);
            narrowAC1To(e.type);
            storeOperand(mem, e.type);
            if (e.yieldOld) popReg(1);
            return;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            genExpr(*e.lhs);  // AC1 = pointer value
            emit("move", "3,1");
            genCompoundThroughAC3(e);
            return;
        }
        case ir::ExprKind::Ternary: {
            genExpr(*e.lhs);
            testTruthy(*e.lhs);
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            emit("jumpe", cat("1,", Lelse));
            genExpr(*e.rhs);
            emit("jrst", cat("0,", Ldone));
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
                    emit("movn", "1,1");
                }
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                genExpr(*e.rhs);  // AC1 = pointer value
                emit("move", "3,1");
                loadIndirect("3", e.type);
            } else if (e.op == "~") {
                genExpr(*e.rhs);
                if (isWide(e.rhs->type)) {
                    int words = wordCountOf(e.rhs->type);
                    for (int i = 0; i < words; i++) {
                        emit("setcm", cat("1,[15-", i, "]"));
                        emit("movem", cat("1,[15-", i, "]"));
                    }
                } else {
                    emit("setcm", "1,1");
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
                    pushReg(1);
                    totalWords += 1;
                }
            }
            emit("pushj", cat("15,", label(e.func)));
            if (totalWords > 0) emit("subi", cat("15,", totalWords));
            if (isWide(e.type)) {
                int words = wordCountOf(e.type);
                for (int i = words - 1; i >= 0; i--) pushReg(i + 1);
            }
            return;
        }
        case ir::ExprKind::CallPtr: {
            // "pushj 15,[3+0]" jumps to the address computed as C(AC3)+0 - the callee's own
            // entry address, computed at runtime, via index-mode addressing (confirmed
            // directly: PUSHJ's E operand is the jump target itself, and index-mode
            // resolution already folds in the index register's value before the jump, so no
            // separate indirect-addressing bit is needed here).
            genExpr(*e.lhs);  // AC1 = the callee's own entry address
            emit("move", "3,1");
            emit("pushj", "15,[3+0]");
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
        case ir::ExprKind::Convert: {
            genConvert(e);
            return;
        }
        case ir::ExprKind::Func:
            err(e.line, "internal: Func reached genExpr - should only ever appear as '&'s operand");
    }
}

std::string Pdp10Codegen::wideWordOperand(const ir::Slot *slot, int wordIndex) const {
    if (slot->kind == ir::SlotKind::Global) {
        return wordIndex == 0 ? label(slot) : (label(slot) + "+" + std::to_string(wordIndex));
    }
    if (slot->kind == ir::SlotKind::Param) {
        // A wide Param's word 0 sits at AC14-frameOffset (matching addr()'s own scalar
        // formula), with later words further *below* that - the same "ascend away from the
        // frame pointer" direction a Param's whole region already extends in (confirmed
        // directly: `(2+i)` offsets fell out of the real caller-push layout by hand).
        return "[14-" + std::to_string(slot->frameOffset + wordIndex) + "]";
    }
    return "[14+" + std::to_string(slot->frameOffset + wordIndex) + "]";
}

// `to.base != Char` is by far the common case (`short`/`int` share `char`'s own one-word
// storage on this target but never need masking - see typeWidths()).
void Pdp10Codegen::narrowAC1To(Type to) {
    if (isPointer(to)) return;
    if (to.base != BaseType::Char) return;
    // 255/128/256 decimal (0377/0200/0400 octal) - this dialect's assembler always parses a
    // bare number as decimal, never octal, regardless of a leading zero (see Pdp10Assembler.h).
    emit("andi", "1,255");
    if (to.isUnsigned) return;
    std::string Ldone = newLabel();
    emit("move", "2,1");
    emit("andi", "2,128");
    emit("jumpe", cat("2,", Ldone));
    emit("subi", "1,256");
    emitLabel(Ldone);
}

void Pdp10Codegen::loadOperand(const std::string &mem, Type type) {
    emit("move", cat("1,", mem));
    narrowAC1To(type);
}

void Pdp10Codegen::storeOperand(const std::string &mem, Type) { emit("movem", "1," + mem); }

void Pdp10Codegen::loadIndirect(const std::string &addrReg, Type type) {
    if (!isWide(type)) {
        emit("move", cat("1,[", addrReg, "+0]"));
        narrowAC1To(type);
        return;
    }
    int words = wordCountOf(type);
    for (int i = words - 1; i >= 0; i--) {
        emit("move", cat("1,[", addrReg, "+", i, "]"));
        pushReg(1);
    }
}

void Pdp10Codegen::storeIndirect(Type type) {
    if (!isWide(type)) {
        popReg(3);
        emit("movem", "1,[3+0]");
        return;
    }
    int words = wordCountOf(type);
    static const int tempRegs[] = {4, 5, 6, 7};
    for (int i = 0; i < words; i++) popReg(tempRegs[i]);
    popReg(3);
    for (int i = 0; i < words; i++) emit("movem", cat(tempRegs[i], ",[3+", i, "]"));
}

void Pdp10Codegen::genWideLoad(const ir::Slot *slot, int words) {
    for (int i = words - 1; i >= 0; i--) {
        emit("move", cat("1,", wideWordOperand(slot, i)));
        pushReg(1);
    }
}

void Pdp10Codegen::genWideLoadLiteral(long value, int words) {
    // `value` is a host `long` (64 bits) - `long long` here is 4 words of 36 bits (144 bits),
    // wider than that host type, so word 1 and up need this value's own sign extended *past*
    // its host width, not just the bits a plain *unsigned* shift would zero-fill in from the
    // top. A plain signed arithmetic right shift (relied on elsewhere in this codebase too -
    // see Pdp7Codegen.cpp's own pushWideLiteral()) sign-extends correctly even past the host
    // type's own 64 bits, since shifting an all-ones (-1) pattern right by any amount stays
    // all-ones; trunc36() is never applied to the whole value up front either, since that
    // would also wrongly clip any word above the first 36 bits of a genuinely wide literal.
    for (int i = words - 1; i >= 0; i--) {
        unsigned shiftBits = static_cast<unsigned>(typeWidths().unitBits) * static_cast<unsigned>(i);
        long shifted = (shiftBits >= 64) ? (value < 0 ? -1 : 0) : (value >> shiftBits);
        uint64_t word = static_cast<uint64_t>(shifted) & kMask36;
        if (word <= 0777777ULL) {
            emit("movei", cat("1,", word));
        } else {
            std::string lbl = allocTempCell();
            trailer_.back() = lbl + ": dw " + std::to_string(word);
            emit("move", cat("1,", lbl));
        }
        pushReg(1);
    }
}

void Pdp10Codegen::genWideStore(const ir::Slot *slot, int words) {
    for (int i = 0; i < words; i++) {
        popReg(1);
        emit("movem", cat("1,", wideWordOperand(slot, i)));
    }
}

void Pdp10Codegen::emitUnsignedLess(const std::string &a, const std::string &b) {
    std::string LaNeg = newLabel(), LaPos = newLabel(), Lsame = newLabel(), Ltrue = newLabel(), Lfalse = newLabel(),
                Ldone = newLabel();
    emit("move", cat("1,", a));
    emit("jumpl", cat("1,", LaNeg));
    emit("jrst", cat("0,", LaPos));
    emitLabel(LaPos);  // sign(a) clear
    emit("move", cat("2,", b));
    emit("jumpl", cat("2,", Ltrue));  // sign(b) set -> a(small) < b(large)
    emit("jrst", cat("0,", Lsame));
    emitLabel(LaNeg);  // sign(a) set
    emit("move", cat("2,", b));
    emit("jumpl", cat("2,", Lsame));  // sign(b) also set -> same sign
    emit("jrst", cat("0,", Lfalse));  // sign(b) clear -> a(large) >= b(small)
    emitLabel(Lsame);
    emit("move", cat("1,", a));
    emit("move", cat("2,", b));
    emit("sub", "1,2");  // safe: same sign, no overflow
    emit("jumpl", cat("1,", Ltrue));
    emit("jrst", cat("0,", Lfalse));
    emitLabel(Ltrue);
    emit("movei", "1,1");
    emit("jrst", cat("0,", Ldone));
    emitLabel(Lfalse);
    emit("movei", "1,0");
    emitLabel(Ldone);
}

// dest = a + b + carryCell; updates carryCell to this word's own carry-out, captured as two
// half-adds (a+b, then +carryIn) ORed together - the same two-stage shape as
// Pdp7Codegen.cpp's own emitWideAddWithCarry(), just detecting each half-add's overflow via
// an unsigned compare instead of the link bit.
void Pdp10Codegen::emitWideAddWithCarry(const std::string &dest, const std::string &a, const std::string &b,
                                         const std::string &carryCell) {
    std::string t = allocTempCell();
    std::string carry1 = allocTempCell(), carry2 = allocTempCell();

    emit("move", cat("1,", a));
    emit("add", cat("1,", b));
    emit("movem", cat("1,", t));
    emitUnsignedLess(t, a);
    emit("movem", cat("1,", carry1));

    emit("move", cat("1,", t));
    emit("add", cat("1,", carryCell));
    emit("movem", cat("1,", dest));
    emitUnsignedLess(dest, t);
    emit("movem", cat("1,", carry2));

    emit("move", cat("1,", carry1));
    emit("ior", cat("1,", carry2));
    emit("movem", cat("1,", carryCell));
}

void Pdp10Codegen::genWideAdd(const ir::Expr &lhs, const ir::Expr &rhs, bool isAdd, int words) {
    genExpr(lhs);
    genExpr(rhs);
    std::vector<std::string> rhsT(words), lhsT(words), resT(words);
    for (auto &t : rhsT) t = allocTempCell();
    for (auto &t : lhsT) t = allocTempCell();
    for (auto &t : resT) t = allocTempCell();
    for (int i = 0; i < words; i++) {
        popReg(1);
        emit("movem", cat("1,", rhsT[i]));
    }
    for (int i = 0; i < words; i++) {
        popReg(1);
        emit("movem", cat("1,", lhsT[i]));
    }

    std::string carry = allocTempCell();
    std::string zero = allocTempCell();  // stays 0 - used as the "+0" addend when negating rhs

    if (!isAdd) {
        emit("movei", "1,1");
        emit("movem", cat("1,", carry));
        for (int i = 0; i < words; i++) {
            emit("setcm", cat("1,", rhsT[i]));
            emit("movem", cat("1,", rhsT[i]));
            emitWideAddWithCarry(rhsT[i], rhsT[i], zero, carry);
        }
    }

    emit("movei", "1,0");
    emit("movem", cat("1,", carry));
    for (int i = 0; i < words; i++) emitWideAddWithCarry(resT[i], lhsT[i], rhsT[i], carry);
    for (int i = words - 1; i >= 0; i--) {
        emit("move", cat("1,", resT[i]));
        pushReg(1);
    }
}

void Pdp10Codegen::genWideNegate(int words) {
    std::vector<std::string> t(words);
    for (auto &x : t) x = allocTempCell();
    for (int i = 0; i < words; i++) {
        popReg(1);
        emit("setcm", "1,1");
        emit("movem", cat("1,", t[i]));
    }
    std::string carry = allocTempCell();
    std::string zero = allocTempCell();
    emit("movei", "1,1");
    emit("movem", cat("1,", carry));
    for (int i = 0; i < words; i++) emitWideAddWithCarry(t[i], t[i], zero, carry);
    for (int i = words - 1; i >= 0; i--) {
        emit("move", cat("1,", t[i]));
        pushReg(1);
    }
}

void Pdp10Codegen::genWideBitwise(const ir::Expr &lhs, const ir::Expr &rhs, const std::string &op, int words) {
    genExpr(lhs);
    genExpr(rhs);
    std::vector<std::string> rhsT(words), lhsT(words);
    for (auto &t : rhsT) t = allocTempCell();
    for (auto &t : lhsT) t = allocTempCell();
    for (int i = 0; i < words; i++) {
        popReg(1);
        emit("movem", cat("1,", rhsT[i]));
    }
    for (int i = 0; i < words; i++) {
        popReg(1);
        emit("movem", cat("1,", lhsT[i]));
    }
    const char *mnem = op == "&" ? "and" : op == "|" ? "ior" : "xor";
    for (int i = words - 1; i >= 0; i--) {
        emit("move", cat("1,", lhsT[i]));
        emit(mnem, cat("1,", rhsT[i]));
        pushReg(1);
    }
}

void Pdp10Codegen::genWideCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned,
                                   int words) {
    genExpr(lhs);
    genExpr(rhs);
    std::vector<std::string> rhsT(words), lhsT(words);
    for (auto &t : rhsT) t = allocTempCell();
    for (auto &t : lhsT) t = allocTempCell();
    for (int i = 0; i < words; i++) {
        popReg(1);
        emit("movem", cat("1,", rhsT[i]));
    }
    for (int i = 0; i < words; i++) {
        popReg(1);
        emit("movem", cat("1,", lhsT[i]));
    }
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    if (op == "==" || op == "!=") {
        for (int i = words - 1; i >= 0; i--) {
            std::string Lcontinue = newLabel();
            emit("move", cat("1,", lhsT[i]));
            emit("sub", cat("1,", rhsT[i]));
            emit("jumpe", cat("1,", Lcontinue));
            emit("jrst", cat("0,", (op == "==" ? Lfalse : Ltrue)));
            emitLabel(Lcontinue);
        }
        emit("jrst", cat("0,", (op == "==" ? Ltrue : Lfalse)));
    } else {
        for (int i = words - 1; i >= 0; i--) {
            std::string Lcontinue = newLabel(), Ldiffer = newLabel();
            emit("move", cat("1,", lhsT[i]));
            emit("sub", cat("1,", rhsT[i]));
            emit("jumpe", cat("1,", Lcontinue));
            emit("jrst", cat("0,", Ldiffer));
            emitLabel(Ldiffer);
            bool topWordUnsigned = (i == words - 1) && isUnsigned;
            bool lowerWordAlwaysUnsigned = (i != words - 1);
            if (topWordUnsigned || lowerWordAlwaysUnsigned) {
                emitUnsignedLess(lhsT[i], rhsT[i]);
                std::string Llt = newLabel();
                emit("jumpn", cat("1,", Llt));
                // lhs word >= rhs word (and they differ, so strictly greater)
                emit("jrst", cat("0,", ((op == "<" || op == "<=") ? Lfalse : Ltrue)));
                emitLabel(Llt);
                emit("jrst", cat("0,", ((op == "<" || op == "<=") ? Ltrue : Lfalse)));
            } else {
                std::string signedBranch = (op == "<" || op == "<=") ? "jumpl" : "jumpg";
                emit(signedBranch, cat("1,", Ltrue));
                emit("jrst", cat("0,", Lfalse));
            }
            emitLabel(Lcontinue);
        }
        emit("jrst", cat("0,", Lfalse));
    }
    emitLabel(Lfalse);
    emit("movei", "1,0");
    emit("jrst", cat("0,", Ldone));
    emitLabel(Ltrue);
    emit("movei", "1,1");
    emitLabel(Ldone);
}

// `long`/`long long` multiply/divide need a software multi-word routine this target has no
// verified implementation of - deliberately not implemented, mirroring Pdp11Codegen.cpp's own
// genWideMul()/genWideDivMod().
void Pdp10Codegen::genWideMul(const ir::Expr &lhs, const ir::Expr &, int) {
    err(lhs.line, "multiplying two 'long'/'long long' values isn't supported on this target yet - "
                  "see Pdp10Codegen.cpp's own genWideMul() comment");
}

void Pdp10Codegen::genWideDivMod(const ir::Expr &lhs, const ir::Expr &, bool, bool, int) {
    err(lhs.line, "dividing two 'long'/'long long' values isn't supported on this target yet - "
                  "see Pdp10Codegen.cpp's own genWideDivMod() comment");
}

// A runtime-variable shift count is obtained by holding the count in a dedicated AC and using
// it as the shift instruction's own index register with a zero displacement (confirmed
// directly against the simulator - see this class's own header comment).
void Pdp10Codegen::genWideShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned, int words) {
    genExpr(lhs);
    pushReg(2);  // reserve a stack slot for the shift count, mirroring the words' own layout
    genExpr(rhs);
    emit("movem", "1,[15+0]");  // stash the count where the reserved slot is (top of stack)
    std::vector<std::string> carry(words);
    for (auto &c : carry) c = allocTempCell();
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("move", "1,[15+0]");
    emit("jumpe", cat("1,", Ldone));
    for (int step = 0; step < words; step++) {
        int i = isLeft ? step : (words - 1 - step);
        std::string w = "[15-" + std::to_string(1 + i) + "]";
        if (isLeft) {
            emit("move", cat("2,", w));
            emit("lsh", "2,-35");
            emit("movem", cat("2,", carry[step]));
            emit("move", cat("2,", w));
            emit("lsh", "2,1");
            if (step > 0) emit("ior", cat("2,", carry[step - 1]));
            emit("movem", cat("2,", w));
        } else {
            bool signedTop = (step == 0) && !isUnsigned;  // step 0 = the most significant word
            emit("move", cat("2,", w));
            emit("andi", "2,1");
            emit("movem", cat("2,", carry[step]));
            emit("move", cat("2,", w));
            emit(signedTop ? "ash" : "lsh", "2,-1");
            if (step > 0) {
                emit("move", cat("3,", carry[step - 1]));
                emit("lsh", "3,35");
                emit("ior", "2,3");
            }
            emit("movem", cat("2,", w));
        }
    }
    emit("move", "1,[15+0]");
    emit("subi", "1,1");
    emit("movem", "1,[15+0]");
    emit("jrst", cat("0,", Lloop));
    emitLabel(Ldone);
    popReg(2);  // discard the shift-count slot; the `words`-word result stays on top
}

void Pdp10Codegen::genConvert(const ir::Expr &e) {
    Type from = e.lhs->type;
    Type to = e.type;
    bool fromWide = isWide(from);
    bool toWide = isWide(to);
    if (!fromWide && !toWide) {
        genExpr(*e.lhs);
        narrowAC1To(to);
        return;
    }
    if (fromWide && !toWide) {
        int words = wordCountOf(from);
        genExpr(*e.lhs);
        emit("move", "1,[15+0]");
        for (int i = 0; i < words; i++) popReg(2);
        narrowAC1To(to);
        return;
    }
    if (!fromWide && toWide) {
        int toWords = wordCountOf(to);
        genExpr(*e.lhs);
        emit("move", "2,1");
        if (from.isUnsigned) {
            emit("movei", "1,0");
        } else {
            // Sign-extend AC1's own current (narrow) value into AC1: a negative value's top
            // bit is already set (every narrow type is a full word here), so replicating it
            // is just "all-ones if negative, else 0" - computed via an arithmetic right shift
            // of a copy by 35, which fills every bit with the sign bit.
            emit("ash", "1,-35");
        }
        for (int i = 1; i < toWords; i++) pushReg(1);
        pushReg(2);
        return;
    }
    int fromWords = wordCountOf(from);
    int toWords = wordCountOf(to);
    genExpr(*e.lhs);
    if (toWords > fromWords) {
        emit("move", cat("1,[15-", (fromWords - 1), "]"));
        emit("ash", "1,-35");
        static const int tempRegs[] = {2, 4, 5, 6};
        for (int i = 0; i < fromWords; i++) popReg(tempRegs[i]);
        for (int i = 0; i < toWords - fromWords; i++) pushReg(1);
        for (int i = fromWords - 1; i >= 0; i--) pushReg(tempRegs[i]);
    } else if (toWords < fromWords) {
        // The kept words (0..toWords-1, least significant) are already on top; the discarded,
        // more-significant words sit underneath them, so they can't simply be "popped" - the
        // kept words must be lifted off, the gap below them closed, then put back.
        static const int tempRegs[] = {2, 4, 5, 6};
        for (int i = 0; i < toWords; i++) popReg(tempRegs[i]);
        emit("subi", cat("15,", (fromWords - toWords)));
        for (int i = toWords - 1; i >= 0; i--) pushReg(tempRegs[i]);
    }
}

// On pdp11/pdp7's downward-growing frame, an array's elements ascend starting from
// `ir::localLowOffset()` (the position *farthest* from the frame pointer, since a bigger
// offset there means a lower address). This target's frame grows the opposite way (see this
// class's own header comment), so "ascending away from the frame pointer" instead starts from
// the position *nearest* it - `slot->frameOffset` itself, with no further adjustment needed.
int Pdp10Codegen::localArrayBase(const ir::Slot *slot) { return slot->frameOffset; }

// AC3 ends up holding the computed address, read/written via index-mode `[3+0]`. An array's
// base is a compile-time-known address (a Global's own label, loaded directly via `MOVEI`
// since that instruction's immediate field is never dereferenced - see this class's own
// header comment; or a Local's frame-relative address, computed off AC16); a plain pointer
// variable's base instead comes from reading its stored value.
void Pdp10Codegen::genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
    genExpr(idxExpr);  // AC1 = index
    // Every scalar element type (char/short/int/pointer) is exactly one native word; a struct
    // element can be several words, so the index is scaled by the element's own width before use.
    int elemW = widthOfType(slot->type);
    if (elemW != 1) {
        emit("movei", cat("2,", elemW));
        emit("mul", "1,2");
        emit("move", "1,2");
    }
    if (slot->arraySize > 0) {
        if (slot->kind == ir::SlotKind::Global) {
            emit("movei", cat("3,", label(slot)));
        } else {
            // A Local array's lowest address is AC16 + localArrayBase(slot) (see addr()'s own
            // Local formula with localOffsetDescendingWide() - an array uses localLowOffset()
            // directly, the same value).
            emit("move", "3,14");
            int off = localArrayBase(slot);
            if (off >= 0) {
                emit("addi", cat("3,", off));
            } else {
                emit("subi", cat("3,", (-off)));
            }
        }
        emit("add", "3,1");
        return;
    }
    emit("move", cat("3,", operandOf(slot)));
    emit("add", "3,1");
}

void Pdp10Codegen::genIndex(const ir::Expr &e) {
    genIndexAddr(e.slot, *e.lhs);
    emit("move", "1,[3+0]");
    narrowAC1To(e.slot->type);
}

void Pdp10Codegen::genLocalArrayInit(const ir::Slot *slot) {
    int base = localArrayBase(slot);
    for (size_t i = 0; i < slot->arrayInit.size(); i++) {
        const ir::ArrayInitElem &elem = slot->arrayInit[i];
        if (elem.isSymbol) {
            emit("movei", cat("1,", (elem.slot ? label(elem.slot) : label(elem.func))));
        } else {
            uint64_t v = trunc36(elem.value);
            if (v <= 0777777ULL) {
                emit("movei", cat("1,", v));
            } else {
                std::string lbl = allocTempCell();
                trailer_.back() = lbl + ": dw " + std::to_string(v);
                emit("move", cat("1,", lbl));
            }
        }
        emit("movem", cat("1,[14+", (base + static_cast<int>(i)), "]"));
    }
}

void Pdp10Codegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        if (operand.slot->kind == ir::SlotKind::Global) {
            emit("movei", cat("1,", label(operand.slot)));
            return;
        }
        emit("move", "1,14");
        if (operand.slot->kind == ir::SlotKind::Param) {
            emit("subi", cat("1,", operand.slot->frameOffset));
        } else if (operand.slot->type.base == BaseType::Struct) {
            // See addr()'s own comment: a struct Local's base is
            // frameOffset directly, the same as localArrayBase() uses
            // for an array, not localOffsetDescendingWide()'s
            // downward-growing-frame formula.
            emit("addi", cat("1,", operand.slot->frameOffset));
        } else {
            emit("addi", cat("1,", ir::localOffsetDescendingWide(*operand.slot, typeWidths())));
        }
        return;
    }
    if (operand.kind == ir::ExprKind::Index) {
        genIndexAddr(operand.slot, *operand.lhs);
        emit("move", "1,3");
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        emit("movei", cat("1,", label(operand.func)));
        return;
    }
    err(operand.line, "internal: unhandled addressable-operand kind");
}

// Entry: AC1 = old value, AC2 = rhs value, both already at `type`'s width (every type is one
// native word here - no narrowing is ever needed, unlike pdp11's `char`). `-`/`&` aren't
// commutative, so each computes old-op-rhs with `old` already in AC1.
void Pdp10Codegen::genCompoundOp(const std::string &op, Type type) {
    bool isUnsigned = type.isUnsigned;
    if (op == "+") { emit("add", "1,2"); return; }
    if (op == "-") { emit("sub", "1,2"); return; }
    if (op == "*") { emit("mul", "1,2"); emit("move", "1,2"); return; }
    if (op == "&") { emit("and", "1,2"); return; }
    if (op == "|") { emit("ior", "1,2"); return; }
    if (op == "^") { emit("xor", "1,2"); return; }
    if (op == "<<" || op == ">>") {
        if (op == "<<") {
            emit("lsh", "1,[2+0]");
        } else if (isUnsigned) {
            emit("movn", "2,2");
            emit("lsh", "1,[2+0]");
        } else {
            emit("movn", "2,2");
            emit("ash", "1,[2+0]");
        }
        return;
    }
    // "/" or "%" - AC2 already holds the divisor (entry convention).
    if (isUnsigned) {
        emitUnsignedDiv();
    } else {
        emit("move", "3,2");
        emit("idiv", "1,3");
    }
    if (op == "%") emit("move", "1,2");
}

void Pdp10Codegen::genCompoundThroughAC3(const ir::Expr &e) {
    pushReg(3);
    genExpr(*e.rhs);
    pushReg(1);
    popReg(2);
    popReg(3);
    emit("move", "1,[3+0]");
    pushReg(3);
    if (e.yieldOld) pushReg(1);
    genCompoundOp(e.op, e.type);
    narrowAC1To(e.type);
    if (e.yieldOld) {
        popReg(4);  // AC4 = old (pre-op) value, stashed above
        popReg(3);
        emit("movem", "1,[3+0]");
        emit("move", "1,4");
    } else {
        popReg(3);
        emit("movem", "1,[3+0]");
    }
}

void Pdp10Codegen::genBinary(const ir::Expr &e) {
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
        pushReg(1);
        genExpr(*e.rhs);
        popReg(2);
        if (op == "+") {
            emit("add", "1,2");
        } else {
            emit("move", "3,1");
            emit("move", "1,2");
            emit("sub", "1,3");
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
            genWideDivMod(*e.lhs, *e.rhs, op == "%", isUnsigned, words);
        } else {
            genDivMod(*e.lhs, *e.rhs, op == "%", isUnsigned);
        }
        return;
    }
    if (op == "&" || op == "|" || op == "^") {
        if (wide) {
            genWideBitwise(*e.lhs, *e.rhs, op, words);
            return;
        }
        genExpr(*e.lhs);
        pushReg(1);
        genExpr(*e.rhs);
        popReg(2);
        const char *mnem = op == "&" ? "and" : op == "|" ? "ior" : "xor";
        emit(mnem, "1,2");
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

// A shift by a runtime-computed count: the count ends up in AC2, used as the shift
// instruction's own index register with a zero displacement (`[2+0]`) - confirmed directly
// against the simulator that this really does vary the shift amount, not a fixed literal.
void Pdp10Codegen::genShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned) {
    genExpr(lhs);
    pushReg(1);
    genExpr(rhs);
    emit("move", "2,1");
    popReg(1);
    if (isLeft) {
        emit("lsh", "1,[2+0]");
    } else {
        emit("movn", "2,2");
        emit(isUnsigned ? "lsh" : "ash", "1,[2+0]");
    }
}

// `MUL AC,E`: confirmed directly against the simulator that the double-length product's
// *low* (truncated single-length) word lands in `AC+1`, not `AC` - so the real single-word
// product ends up in AC2 here, copied back to AC1 afterward.
void Pdp10Codegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    pushReg(1);
    genExpr(rhs);
    popReg(2);
    emit("mul", "1,2");  // AC1 = high word, AC2 = low (truncated) word
    emit("move", "1,2");
}

// `IDIV AC,E`: confirmed directly against the simulator to be single-length (unlike pdp11's
// double-length EIS `DIV`) - quotient in AC, remainder in AC+1, no sign-extension step needed.
void Pdp10Codegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    pushReg(1);
    genExpr(rhs);
    emit("move", "3,1");  // AC3 = divisor
    popReg(1);                  // AC1 = dividend
    if (isUnsigned) {
        emit("move", "2,3");
        emitUnsignedDiv();
    } else {
        emit("idiv", "1,3");
    }
    if (wantRemainder) emit("move", "1,2");
}

// A classic restoring shift-subtract binary long division, 36 bits at a time: confirmed
// directly against the simulator that the real double-length `DIV` instruction does *not*
// treat a zero-extended (AC=0) dividend/AC+1 pair as a plain 72-bit unsigned magnitude when
// AC+1's own sign bit is set (dividing 0,,777777777777 by 16 gave a quotient of only
// 2147483647, not the expected 4294967295) - so rather than depend on guessing its real
// semantics, this never uses `DIV`/`IDIV` at all: each iteration shifts the next dividend bit
// into the remainder, then subtracts the divisor out of it (via the overflow-safe
// emitUnsignedLess()) whenever it fits, accumulating the quotient the same way by hand -
// exactly the long-division algorithm taught by hand, just in binary.
void Pdp10Codegen::emitUnsignedDiv() {
    // Entry: AC1 = dividend, AC2 = divisor. Clobbers AC1-AC2 (via emitUnsignedLess()) plus
    // the cells below; result: AC1 = quotient, AC2 = remainder.
    std::string uCell = allocTempCell(), vCell = allocTempCell(), rCell = allocTempCell(),
                qCell = allocTempCell(), bitCell = allocTempCell(), counter = allocTempCell();
    emit("movem", cat("1,", uCell));
    emit("movem", cat("2,", vCell));
    emit("movei", "1,0");
    emit("movem", cat("1,", rCell));
    emit("movem", cat("1,", qCell));
    emit("movei", cat("1,", typeWidths().unitBits));
    emit("movem", cat("1,", counter));

    std::string Lloop = newLabel(), Lskip = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("move", cat("1,", counter));
    emit("jumpe", cat("1,", Ldone));
    // bit = U's own top bit; U <<= 1 (the bit shifted out is already captured, so it's fine
    // to lose it).
    emit("move", cat("1,", uCell));
    emit("lsh", "1,-35");
    emit("movem", cat("1,", bitCell));
    emit("move", cat("1,", uCell));
    emit("lsh", "1,1");
    emit("movem", cat("1,", uCell));
    // R = (R << 1) | bit; Q <<= 1 (a 0 bit shifted in by default).
    emit("move", cat("1,", rCell));
    emit("lsh", "1,1");
    emit("ior", cat("1,", bitCell));
    emit("movem", cat("1,", rCell));
    emit("move", cat("1,", qCell));
    emit("lsh", "1,1");
    emit("movem", cat("1,", qCell));
    // If R >= V: R -= V, and set Q's own low bit (just shifted in as 0 above).
    emitUnsignedLess(rCell, vCell);  // AC1 = (R < V) ? 1 : 0
    emit("jumpn", cat("1,", Lskip));
    emit("move", cat("1,", rCell));
    emit("sub", cat("1,", vCell));
    emit("movem", cat("1,", rCell));
    emit("move", cat("1,", qCell));
    emit("iori", "1,1");
    emit("movem", cat("1,", qCell));
    emitLabel(Lskip);
    emit("move", cat("1,", counter));
    emit("subi", "1,1");
    emit("movem", cat("1,", counter));
    emit("jrst", cat("0,", Lloop));
    emitLabel(Ldone);
    emit("move", cat("1,", qCell));
    emit("move", cat("2,", rCell));
}

void Pdp10Codegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned) {
    if (op == "==") return emitCompare(lhs, rhs, "jumpe", false);
    if (op == "!=") return emitCompare(lhs, rhs, "jumpn", false);
    if (op == "<") return emitCompare(lhs, rhs, "jumpl", isUnsigned);
    if (op == "<=") return emitCompare(lhs, rhs, "jumple", isUnsigned);
    if (op == ">") return emitCompare(lhs, rhs, "jumpg", isUnsigned);
    if (op == ">=") return emitCompare(lhs, rhs, "jumpge", isUnsigned);
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

// `==`/`!=` compute `lhs - rhs` into AC1 and jump on it directly (safe regardless of overflow -
// the result is exactly 0 iff the two operands are equal, overflow or not). `<`/`<=`/`>`/`>=`
// go through emitUnsignedLess() instead of a plain signed `SUB`+jump when `isUnsigned` (never
// true for `==`/`!=` - see genCompare()), expressing each operator as that one primitive with
// operands possibly swapped/negated (`a<=b` is `!(b<a)`, etc.) - a plain `sub`+sign-test can't
// safely decide an unsigned comparison on its own (see emitUnsignedLess()'s own header comment).
void Pdp10Codegen::emitCompare(const ir::Expr &lhs, const ir::Expr &rhs, const std::string &jumpIfTrue,
                                bool isUnsigned) {
    genExpr(lhs);
    pushReg(1);
    genExpr(rhs);
    popReg(2);
    emit("move", "3,1");
    emit("move", "1,2");  // AC3 = rhs, AC1 = lhs
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    if (isUnsigned) {
        // emitUnsignedLess() uses AC1/AC2 as its own scratch, so the operands handed to it
        // must live somewhere else first - AC4 (a stable copy of lhs; AC3 already holds rhs).
        emit("move", "4,1");
        bool wantLess = (jumpIfTrue == "jumpl" || jumpIfTrue == "jumpge");  // "<" or ">=" (negated)
        bool negate = (jumpIfTrue == "jumple" || jumpIfTrue == "jumpge");  // "<=" or ">=" negate the raw test
        const char *a = wantLess ? "4" : "3";
        const char *b = wantLess ? "3" : "4";
        emitUnsignedLess(a, b);
        emit(negate ? "jumpe" : "jumpn", cat("1,", Ltrue));
        emit("jrst", cat("0,", Lfalse));
    } else {
        emit("sub", "1,3");  // AC1 = lhs - rhs
        emit(jumpIfTrue, cat("1,", Ltrue));
        emit("jrst", cat("0,", Lfalse));
    }
    emitLabel(Lfalse);
    emit("movei", "1,0");
    emit("jrst", cat("0,", Ldone));
    emitLabel(Ltrue);
    emit("movei", "1,1");
    emitLabel(Ldone);
}

void Pdp10Codegen::testTruthy(const ir::Expr &e) {
    if (!isWide(e.type)) return;  // AC1 already holds the value, JUMPx-ready
    int words = wordCountOf(e.type);
    popReg(1);
    for (int i = 1; i < words; i++) {
        popReg(2);
        emit("ior", "1,2");
    }
}

void Pdp10Codegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    testTruthy(lhs);
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    emit("jumpe", cat("1,", Lfalse));
    genExpr(rhs);
    testTruthy(rhs);
    emit("jumpe", cat("1,", Lfalse));
    emit("jrst", cat("0,", Ltrue));
    emitLabel(Ltrue);
    emit("movei", "1,1");
    emit("jrst", cat("0,", Ldone));
    emitLabel(Lfalse);
    emit("movei", "1,0");
    emitLabel(Ldone);
}

void Pdp10Codegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    testTruthy(lhs);
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    emit("jumpn", cat("1,", Ltrue));
    genExpr(rhs);
    testTruthy(rhs);
    emit("jumpn", cat("1,", Ltrue));
    emit("jrst", cat("0,", Lfalse));
    emitLabel(Lfalse);
    emit("movei", "1,0");
    emit("jrst", cat("0,", Ldone));
    emitLabel(Ltrue);
    emit("movei", "1,1");
    emitLabel(Ldone);
}

void Pdp10Codegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    testTruthy(rhs);
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    emit("jumpe", cat("1,", Ltrue));
    emit("jrst", cat("0,", Lfalse));
    emitLabel(Lfalse);
    emit("movei", "1,0");
    emit("jrst", cat("0,", Ldone));
    emitLabel(Ltrue);
    emit("movei", "1,1");
    emitLabel(Ldone);
}

std::optional<std::string> Pdp10Codegen::assembleInProcess(const std::string &asmText,
                                                            const std::string &format) const {
    if (format == "raw") return assemblePdp10(asmText);
    if (auto kind = fileFormatKindFromName(format)) {
        ObjectFile obj = assemblePdp10Object(asmText);
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

std::optional<std::string> Pdp10Codegen::debugMap(const std::string &asmText, const std::string &format,
        const std::string &sourcePath) const {
    (void)format;
    return assemblePdp10DebugMap(asmText, sourcePath, debugSymbols_);
}

std::optional<std::string> Pdp10Codegen::debugMapPreLink(const std::string &asmText,
                                                    const std::string &sourcePath) const {
    return assemblePdp10DebugMap(asmText, sourcePath, debugSymbols_, /*allowExternal=*/true);
}

// Only emits a marker when `debugMapEnabled_` (set via setDebugMapEnabled(),
// before generate() runs) and `line` actually differs from the last one
// emitted - see Pdp7Codegen.cpp's identical emitSourceLine(), which this
// mirrors exactly.
void Pdp10Codegen::emitSourceLine(int line) {
    if (!debugMapEnabled_ || line == 0 || line == lastDebugLine_) return;
    emitRaw(cat("__line ", line));
    lastDebugLine_ = line;
}

}  // namespace rcc
