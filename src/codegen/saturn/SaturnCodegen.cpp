#include "SaturnCodegen.h"

#include <cstdint>
#include <stdexcept>
#include <utility>

#include "SaturnAssembler.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}
}  // namespace

std::optional<std::string> SaturnCodegen::assembleInProcess(const std::string &asmText,
                                                              const std::string &format) const {
    if (format == "raw") return assembleSaturn(asmText, /*allowExternal=*/false);
    if (format == "coff") return assembleSaturn(asmText, /*allowExternal=*/true);
    return std::nullopt;
}

// `char`/`short` are genuinely narrower than this backend's 32-bit registers (`ldb`/`ldh`
// already zero-extend on load, so this is belt-and-suspenders there, but a value arriving via
// arithmetic rather than a fresh load - e.g. after `add`/`sub`/a runtime multiply/divide call -
// has no such guarantee), so the VALUE (never the storage - that's already the right width) is
// masked, and re-sign-extended if signed, at every point it's read or converted.
void SaturnCodegen::narrowA(Type to) {
    // `to.base` names a pointer's pointee type, not its own, so narrowing `a` as a `char`/
    // `short` here would corrupt a real address.
    if (isPointer(to)) return;
    int width = widthOfType(to);
    if (width >= 4) return;  // int/long/long long are already full register width (or wide)
    uint32_t mask = width == 1 ? 0xFFu : 0xFFFFu;
    uint32_t signBit = width == 1 ? 0x80u : 0x8000u;
    uint32_t signExtend = ~mask;
    emit("and", cat("a, ", mask));
    if (to.isUnsigned) return;
    std::string Ldone = newLabel();
    emit("cmp", cat("a, ", signBit));
    emit("jl", Ldone);
    emit("or", cat("a, ", signExtend));
    emitLabel(Ldone);
}

// --- generate() -----------------------------------------------------------

std::string SaturnCodegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }

    std::vector<std::tuple<std::string, size_t, size_t>> codeRanges;
    std::vector<std::tuple<std::string, size_t, size_t>> trailerRanges;

    std::string homeFile = mainFn ? mainFn->sourceFile
                            : !prog.functionBodies.empty() ? prog.functionBodies.front().func->sourceFile
                            : !prog.slots.empty() ? prog.slots.front()->sourceFile
                                                   : std::string();

    emitRaw(cat("org ", objectMode ? 0 : kSaturnBaseAddress));
    emitRaw("");

    if (mainFn && !objectMode) {
        size_t rangeStart = static_cast<size_t>(code_.tellp());
        emit("mov", cat("d0, ", kSaturnStackTop));
        emit("gosbvl", label(mainFn));
        emit("halt");
        codeRanges.push_back({mainFn->sourceFile, rangeStart, static_cast<size_t>(code_.tellp())});
    }

    for (const auto &fb : prog.functionBodies) {
        if (fb.func->hasAddress) {
            err(fb.line, "'@' fixed placement isn't supported on this backend (see "
                         "docs/platforms.md's saturn entry)");
        }
        size_t fCodeStart = static_cast<size_t>(code_.tellp());
        size_t fTrailerStart = trailer_.size();
        genFunction(fb);
        codeRanges.push_back({fb.func->sourceFile, fCodeStart, static_cast<size_t>(code_.tellp())});
        trailerRanges.push_back({fb.func->sourceFile, fTrailerStart, trailer_.size()});
    }

    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        if (slot->isExternal) continue;
        if (slot->hasAddress) {
            err(0, "'@' fixed placement isn't supported on this backend (see docs/platforms.md's "
                   "saturn entry)");
        }
        size_t gTrailerStart = trailer_.size();
        // A `char`/`short` element (byteWidth < 4) is one `db`/`dh` value; an `int`/pointer/
        // `long`/`long long` element (always a multiple of 4 bytes) is `byteWidth / 4`
        // consecutive `dw` words, word 0/low at the lowest address.
        int byteWidth = widthOfType(slot->type);
        int units = byteWidth < 4 ? 1 : byteWidth / 4;
        std::string directive = byteWidth == 1 ? "db" : byteWidth == 2 ? "dh" : "dw";
        auto elemUnitText = [&](const ir::ArrayInitElem &e, int unitIdx) -> std::string {
            if (!e.isSymbol) {
                long v = units == 1 ? e.value : (e.value >> (32 * unitIdx)) & 0xFFFFFFFFL;
                return std::to_string(v);
            }
            if (unitIdx > 0) return "0";
            return e.slot ? label(e.slot) : label(e.func);
        };
        if (slot->arraySize > 0) {
            bool first = true;
            for (size_t i = 0; i < slot->arrayInit.size(); i++) {
                for (int u = 0; u < units; u++) {
                    std::string text = elemUnitText(slot->arrayInit[i], u);
                    if (first) {
                        trailer_.push_back(label(slot.get()) + ": " + directive + " " + text + "  ; " +
                                            slot->sourceName);
                        first = false;
                    } else {
                        trailer_.push_back(directive + " " + text);
                    }
                }
            }
        } else {
            long v = slot->hasInit ? slot->initValue : 0;
            for (int u = 0; u < units; u++) {
                long unitValue = units == 1 ? v : (v >> (32 * u)) & 0xFFFFFFFFL;
                std::string text = std::to_string(unitValue);
                trailer_.push_back((u == 0 ? label(slot.get()) + ": " + directive + " " : directive + " ") + text +
                                    (u == 0 ? "  ; " + slot->sourceName : ""));
            }
        }
        trailerRanges.push_back({slot->sourceFile, gTrailerStart, trailer_.size()});
    }

    // Scratch cells the multiply/divide runtime routines (saturnRuntimeAsm()) use. Only emitted
    // in whole-program mode - under objectMode every file leaves these external, and
    // SaturnLinkerFactory.cpp's entry object supplies the one shared definition (same reasoning
    // as pdp7's own __sp/__fp/etc. - see Pdp7Codegen.cpp's generate()).
    if (!objectMode) {
        size_t sharedStart = static_cast<size_t>(code_.tellp());
        code_ << saturnRuntimeAsm();
        codeRanges.push_back({homeFile, sharedStart, static_cast<size_t>(code_.tellp())});
    }

    std::string codeStr = code_.str();
    perFileAsm_.clear();
    for (const auto &[file, s, e] : codeRanges) perFileAsm_[file] += codeStr.substr(s, e - s);
    for (const auto &[file, s, e] : trailerRanges) {
        for (size_t i = s; i < e; i++) perFileAsm_[file] += trailer_[i] + "\n";
    }

    std::ostringstream out;
    out << codeStr;
    for (const auto &line : trailer_) out << line << "\n";
    return out.str();
}

std::map<std::string, std::string> SaturnCodegen::perFileAsm() const { return perFileAsm_; }

void SaturnCodegen::genFunction(const ir::FunctionBody &fn) {
    curFunctionLabel_ = label(fn.func);
    emitLabel(curFunctionLabel_);
    emitPrologue(fn.func);
    genStmt(*fn.body);
    emitEpilogueAndReturn(/*hasValue=*/false, Type{});
}

// --- Frame mechanics -------------------------------------------------------

void SaturnCodegen::emitPrologue(const ir::Function *fn) {
    // `gosbvl` already pushed the return address onto the `d0` stack before jumping here.
    emit("push", "d1");     // save caller's d1 (oldFP)
    emit("mov", "d1, d0");  // this call's own frame pointer
    if (fn->frameSize > 0) {
        emit("sub", cat("d0, ", fn->frameSize));  // reserve this call's Locals
    }
}

void SaturnCodegen::emitEpilogueAndReturn(bool hasValue, Type retType) {
    bool wide = hasValue && isWide(retType);
    int chunks = wide ? chunkCount(retType) : 0;
    std::vector<std::string> retCells;
    if (hasValue && wide) {
        // genWideExpr() already pushed `chunks` 32-bit chunks (chunk 0/low on top); the frame
        // teardown below discards everything below the *caller's* own stack level, so pop them
        // into named cells first and push them back once the real frame is gone.
        for (int i = 0; i < chunks; i++) {
            std::string cell = "__retvalW" + std::to_string(i);
            trailer_.push_back(cell + ": dw 0");
            emit("pop", "a");
            emit("st", cat("[", cell, "], a"));
            retCells.push_back(cell);
        }
    }
    emit("mov", "d0, d1");  // discard this call's own Locals
    emit("pop", "d1");      // restore caller's d1 (oldFP); `d0` now points at the return
                             // address `gosbvl` pushed (top of stack).
    if (!wide) {
        // A narrow return value is already sitting in `a` from the caller's own genExpr()/
        // genStmt() Return case; `rtn` pops the return address and jumps to it directly.
        emit("rtn");
        return;
    }
    // A wide return value's chunks must land ABOVE the return address once we're back in the
    // caller (see genWideExpr()'s own Call case), so the return address has to come off the
    // stack before they go on - `rtn` alone can't be reordered around that, hence `jmpr`.
    emit("pop", "b");  // b = return address
    for (int i = chunks - 1; i >= 0; i--) {
        emit("ld", cat("a, [", retCells[i], "]"));
        emit("push", "a");
    }
    emit("jmpr", "b");
}

namespace {
// `widthOfType()` is always 1 (char), 2 (short) or 4 (int/pointer) at every call site below -
// long/long long always go through the wide (genWideExpr) path instead, which only ever deals
// in 4-byte chunks via plain `ld`/`st`.
const char *ldMnemonic(int byteWidth) { return byteWidth == 1 ? "ldb" : byteWidth == 2 ? "ldh" : "ld"; }
const char *stMnemonic(int byteWidth) { return byteWidth == 1 ? "stb" : byteWidth == 2 ? "sth" : "st"; }
}  // namespace

void SaturnCodegen::emitLoadSlotAddress(const ir::Slot *slot) {
    if (slot->kind == ir::SlotKind::Global) {
        emit("mov", cat("a, ", label(slot)));
        return;
    }
    long offset = (slot->kind == ir::SlotKind::Param) ? static_cast<long>(slot->frameOffset)
                                                        : -localBaseOffset(slot);
    emit("mov", "a, d1");
    if (offset > 0) {
        emit("add", cat("a, ", offset));
    } else if (offset < 0) {
        emit("sub", cat("a, ", -offset));
    }
}

void SaturnCodegen::emitLoadSlot(const ir::Slot *slot) {
    const char *ld = ldMnemonic(widthOfType(slot->type));
    if (slot->kind == ir::SlotKind::Global) {
        emit(ld, cat("a, [", label(slot), "]"));
        narrowA(slot->type);
        return;
    }
    emitLoadSlotAddress(slot);
    emit(ld, "a, [a]");
    narrowA(slot->type);
}

void SaturnCodegen::emitStoreSlot(const ir::Slot *slot) {
    const char *st = stMnemonic(widthOfType(slot->type));
    if (slot->kind == ir::SlotKind::Global) {
        emit(st, cat("[", label(slot), "], a"));
        return;
    }
    emit("push", "a");
    emitLoadSlotAddress(slot);
    emit("mov", "c, a");
    emit("pop", "a");
    emit(st, "[c], a");
}

void SaturnCodegen::emitSlotBaseAddress(const ir::Slot *slot) {
    if (slot->arraySize > 0) {
        emitLoadSlotAddress(slot);
    } else {
        emitLoadSlot(slot);
    }
}

long SaturnCodegen::localBaseOffset(const ir::Slot *slot) { return ir::localLowOffset(*slot, typeWidths()); }

void SaturnCodegen::emitLocalArrayInit(const ir::Slot *slot) {
    // A `char`/`short` element (byteWidth < 4) is a single narrower store; an `int`/pointer/
    // `long`/`long long` element (always a multiple of 4 bytes) is `byteWidth / 4` consecutive
    // 4-byte words, word 0/low at the lowest address - the same layout a Global array's
    // initializer uses (see generate()'s own per-element loop).
    int byteWidth = widthOfType(slot->type);
    int units = byteWidth < 4 ? 1 : byteWidth / 4;
    const char *st = stMnemonic(byteWidth < 4 ? byteWidth : 4);
    for (size_t i = 0; i < slot->arrayInit.size(); i++) {
        const ir::ArrayInitElem &elem = slot->arrayInit[i];
        for (int w = 0; w < units; w++) {
            if (!elem.isSymbol) {
                long unitValue = units == 1 ? elem.value : (elem.value >> (32 * w)) & 0xFFFFFFFFL;
                emit("mov", cat("a, ", unitValue));
            } else if (w > 0) {
                // A symbol element is a plain word-wide address; only word 0 holds it, every
                // higher word is zero.
                emit("mov", "a, 0");
            } else if (elem.slot) {
                emitLoadSlotAddress(elem.slot);
            } else {
                emit("mov", cat("a, ", label(elem.func)));
            }
            emit("mov", "c, d1");
            long offset = -localBaseOffset(slot) + static_cast<long>(i) * byteWidth + w * 4;
            if (offset > 0) {
                emit("add", cat("c, ", offset));
            } else if (offset < 0) {
                emit("sub", cat("c, ", -offset));
            }
            emit(st, "[c], a");
        }
    }
}

// --- Element addressing -----------------------------------------------------

// Every scalar element width this type system ever produces on its own (1/2/4 words -
// char/short/int collapse to 1, long to 2, long long to 4) is a power of two, so scaling an
// index by the element's real byte stride is usually just a `shl`. A struct element's width is
// whatever its members add up to, though, and isn't always a power of two (e.g. three `char`
// members), so this falls back to the software `__mul32` routine - see genMul() - for any width
// `log2ElemWidth()` can't represent exactly.
namespace {
// Returns -1 (not a valid shift) for a width that isn't exactly a power of two, instead of
// silently rounding it down via the shift-right loop losing its low bits.
int log2ElemWidth(int bytesPerElem) {
    if (bytesPerElem <= 0 || (bytesPerElem & (bytesPerElem - 1)) != 0) return -1;
    int shiftAmt = 0;
    for (int w = bytesPerElem; w > 1; w >>= 1) shiftAmt++;
    return shiftAmt;
}
}  // namespace

void SaturnCodegen::computeElementAddressA(const ir::Slot *slot, const ir::Expr &idxExpr) {
    int bytesPerElem = widthOfType(slot->type);
    int shiftAmt = log2ElemWidth(bytesPerElem);
    if (exprIsCallFree(idxExpr)) {
        genExpr(idxExpr);  // index in `a`
        emit("mov", "c, a");
        if (shiftAmt < 0) {
            // Non-power-of-two width: scale via __mul32 first (it clobbers `a`), then compute
            // the base address afterward, once `a` is free again.
            emit("st", "[__mul_a], c");
            emit("mov", cat("a, ", bytesPerElem));
            emit("st", "[__mul_b], a");
            emit("gosbvl", "__mul32");
            emit("mov", "c, a");
            emitSlotBaseAddress(slot);  // base in `a`
            emit("add", "a, c");
            return;
        }
        emitSlotBaseAddress(slot);  // base in `a`
        if (shiftAmt != 0) emit("shl", cat("c, ", shiftAmt));
        emit("add", "a, c");
        return;
    }
    // idxExpr may itself call: compute the base first and protect it on the stack while the
    // index evaluates.
    emitSlotBaseAddress(slot);
    emit("push", "a");
    genExpr(idxExpr);
    emit("mov", "c, a");
    if (shiftAmt < 0) {
        emit("st", "[__mul_a], c");
        emit("mov", cat("a, ", bytesPerElem));
        emit("st", "[__mul_b], a");
        emit("gosbvl", "__mul32");
        emit("mov", "c, a");
        emit("pop", "a");
        emit("add", "a, c");
        return;
    }
    if (shiftAmt != 0) emit("shl", cat("c, ", shiftAmt));
    emit("pop", "a");
    emit("add", "a, c");
}

void SaturnCodegen::computeElementAddress(const ir::Slot *slot, const ir::Expr &idxExpr) {
    computeElementAddressA(slot, idxExpr);
    emit("push", "a");
}

void SaturnCodegen::genIndex(const ir::Expr &e) {
    computeElementAddressA(e.slot, *e.lhs);
    emit(ldMnemonic(widthOfType(e.slot->type)), "a, [a]");
    narrowA(e.type);
}

void SaturnCodegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Index) {
        computeElementAddressA(operand.slot, *operand.lhs);
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        emit("mov", cat("a, ", label(operand.func)));
        return;
    }
    // A bare Var naming either a declared array (its own address) or a plain scalar (its
    // address).
    emitLoadSlotAddress(operand.slot);
}

bool SaturnCodegen::exprIsCallFree(const ir::Expr &e) const {
    if (e.kind == ir::ExprKind::Call || e.kind == ir::ExprKind::CallPtr) return false;
    for (const ir::Expr *child : {e.lhs.get(), e.rhs.get(), e.elseExpr.get()}) {
        if (child && !exprIsCallFree(*child)) return false;
    }
    for (const auto &arg : e.args) {
        if (!exprIsCallFree(*arg)) return false;
    }
    return true;
}

// --- Statements -------------------------------------------------------------

void SaturnCodegen::genStmt(const ir::Stmt &s) {
    switch (s.kind) {
        case ir::StmtKind::Block: {
            for (const auto &child : s.body) genStmt(*child);
            return;
        }
        case ir::StmtKind::If: {
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            genCondBranch(*s.cond, s.elseStmt ? Lelse : Ldone);
            genStmt(*s.thenStmt);
            if (s.elseStmt) {
                emit("goto", Ldone);
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
            genCondBranch(*s.cond, Lend);
            loopLabels_.push_back({Lcont, Lend});
            genStmt(*s.loopBody);
            loopLabels_.pop_back();
            if (s.postStmt) {
                emitLabel(Lcont);
                genStmt(*s.postStmt);
            }
            emit("goto", Lstart);
            emitLabel(Lend);
            return;
        }
        case ir::StmtKind::Break: {
            emit("goto", loopLabels_.back().second);
            return;
        }
        case ir::StmtKind::Continue: {
            emit("goto", loopLabels_.back().first);
            return;
        }
        case ir::StmtKind::Return: {
            bool hasValue = s.initOrReturnOrExpr != nullptr;
            if (hasValue) {
                if (isWide(s.initOrReturnOrExpr->type)) {
                    genWideExpr(*s.initOrReturnOrExpr);
                } else {
                    genExpr(*s.initOrReturnOrExpr);
                }
            }
            emitEpilogueAndReturn(hasValue, hasValue ? s.initOrReturnOrExpr->type : Type{});
            return;
        }
        case ir::StmtKind::LocalDecl: {
            if (s.declSlot->arraySize > 0) {
                emitLocalArrayInit(s.declSlot);
            } else if (isWide(s.declSlot->type)) {
                if (s.initOrReturnOrExpr) {
                    genWideExpr(*s.initOrReturnOrExpr);
                } else {
                    pushWideLiteral(s.declSlot->type, 0);
                }
                emitLoadSlotAddress(s.declSlot);
                popWideToAddress(s.declSlot->type);
            } else {
                if (s.initOrReturnOrExpr) {
                    genExpr(*s.initOrReturnOrExpr);
                } else {
                    emit("mov", "a, 0");
                }
                emitStoreSlot(s.declSlot);
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
            emit("goto", irLabel(s.labelId));
            return;
        }
    }
}

// --- Conditions ---------------------------------------------------------

std::string SaturnCodegen::jccForFalse(const std::string &op, bool isUnsigned) {
    // After `cmp c, a` (c = lhs, a = rhs): jump away when `c op a` is FALSE.
    if (op == "==") return "jne";
    if (op == "!=") return "je";
    if (op == "<") return isUnsigned ? "jae" : "jge";
    if (op == "<=") return isUnsigned ? "ja" : "jg";
    if (op == ">") return isUnsigned ? "jbe" : "jle";
    return isUnsigned ? "jb" : "jl";  // ">="
}

std::string SaturnCodegen::jccForTrue(const std::string &op, bool isUnsigned) {
    if (op == "==") return "je";
    if (op == "!=") return "jne";
    if (op == "<") return isUnsigned ? "jb" : "jl";
    if (op == "<=") return isUnsigned ? "jbe" : "jle";
    if (op == ">") return isUnsigned ? "ja" : "jg";
    return isUnsigned ? "jae" : "jge";  // ">="
}

namespace {
bool isRelational(const std::string &op) {
    return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
}
}  // namespace

void SaturnCodegen::genCondBranch(const ir::Expr &cond, const std::string &label) {
    if (cond.kind == ir::ExprKind::Binary && cond.op == "&&") {
        genCondBranch(*cond.lhs, label);
        genCondBranch(*cond.rhs, label);
        return;
    }
    if (cond.kind == ir::ExprKind::Binary && cond.op == "||") {
        std::string Lcontinue = newLabel();
        genCondBranchTrue(*cond.lhs, Lcontinue);
        genCondBranch(*cond.rhs, label);
        emitLabel(Lcontinue);
        return;
    }
    if (cond.kind == ir::ExprKind::Unary && cond.op == "!") {
        genCondBranchTrue(*cond.rhs, label);
        return;
    }
    if (cond.kind == ir::ExprKind::Binary && isRelational(cond.op) && !isWide(cond.lhs->type) &&
        !isWide(cond.rhs->type)) {
        bool unsignedCmp = cond.lhs->type.isUnsigned || cond.rhs->type.isUnsigned || isPointer(cond.lhs->type) ||
                            isPointer(cond.rhs->type);
        genOperandsIntoCA(*cond.lhs, *cond.rhs);
        emit("cmp", "c, a");
        emit(jccForFalse(cond.op, unsignedCmp), label);
        return;
    }
    genExpr(cond);
    emit("cmp", "a, 0");
    emit("je", label);
}

void SaturnCodegen::genCondBranchTrue(const ir::Expr &cond, const std::string &label) {
    if (cond.kind == ir::ExprKind::Binary && cond.op == "||") {
        genCondBranchTrue(*cond.lhs, label);
        genCondBranchTrue(*cond.rhs, label);
        return;
    }
    if (cond.kind == ir::ExprKind::Binary && cond.op == "&&") {
        std::string Lcontinue = newLabel();
        genCondBranch(*cond.lhs, Lcontinue);
        genCondBranchTrue(*cond.rhs, label);
        emitLabel(Lcontinue);
        return;
    }
    if (cond.kind == ir::ExprKind::Unary && cond.op == "!") {
        genCondBranch(*cond.rhs, label);
        return;
    }
    if (cond.kind == ir::ExprKind::Binary && isRelational(cond.op) && !isWide(cond.lhs->type) &&
        !isWide(cond.rhs->type)) {
        bool unsignedCmp = cond.lhs->type.isUnsigned || cond.rhs->type.isUnsigned || isPointer(cond.lhs->type) ||
                            isPointer(cond.rhs->type);
        genOperandsIntoCA(*cond.lhs, *cond.rhs);
        emit("cmp", "c, a");
        emit(jccForTrue(cond.op, unsignedCmp), label);
        return;
    }
    genExpr(cond);
    emit("cmp", "a, 0");
    emit("jne", label);
}

void SaturnCodegen::genCompareValue(const std::string &op, bool isUnsigned) {
    // On entry: `cmp c, a` has already run (c = lhs, a = rhs). Leaves a 0/1 result in `a`.
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit(jccForTrue(op, isUnsigned), Ltrue);
    emit("mov", "a, 0");
    emit("goto", Ldone);
    emitLabel(Ltrue);
    emit("mov", "a, 1");
    emitLabel(Ldone);
}

std::string SaturnCodegen::allocTemp() { return "T" + std::to_string(tempCounter_++); }
void SaturnCodegen::declareTemp(const std::string &label) { trailer_.push_back(label + ": dw 0"); }

// --- Binary operators -------------------------------------------------------

// Always protects `lhs` via the stack across `rhs`'s own evaluation, rather than trying to keep
// it in a register: `rhs` need not itself call a function to clobber `c`/`b` - almost every
// operator this backend emits (div/mod/multiply via the runtime routines, a nested binary op's
// own `genOperandsIntoCA`) uses them as scratch regardless, so "rhs is call-free" is not the
// same as "rhs leaves c alone".
void SaturnCodegen::genOperandsIntoCA(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    emit("push", "a");
    genExpr(rhs);
    emit("pop", "c");
}

void SaturnCodegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    if (op == "+" || op == "-") {
        genOperandsIntoCA(*e.lhs, *e.rhs);
        emit(op == "+" ? "add" : "sub", "c, a");
        emit("mov", "a, c");
        narrowA(e.type);
        return;
    }
    if (op == "&" || op == "^" || op == "|") {
        genBitwise(op, *e.lhs, *e.rhs);
        return;
    }
    if (op == "<<" || op == ">>") {
        genShift(op, *e.lhs, *e.rhs);
        return;
    }
    if (op == "*") {
        genMul(*e.lhs, *e.rhs);
        return;
    }
    if (op == "/") {
        genDivMod(*e.lhs, *e.rhs, /*wantRemainder=*/false);
        return;
    }
    if (op == "%") {
        genDivMod(*e.lhs, *e.rhs, /*wantRemainder=*/true);
        return;
    }
    if (isRelational(op)) {
        bool unsignedCmp = e.lhs->type.isUnsigned || e.rhs->type.isUnsigned || isPointer(e.lhs->type) ||
                            isPointer(e.rhs->type);
        if (isWide(e.lhs->type) || isWide(e.rhs->type)) {
            genWideExpr(*e.lhs);
            genWideExpr(*e.rhs);
            genWideCompare(op, chunkCount(isWide(e.lhs->type) ? e.lhs->type : e.rhs->type), unsignedCmp);
            return;
        }
        genOperandsIntoCA(*e.lhs, *e.rhs);
        emit("cmp", "c, a");
        genCompareValue(op, unsignedCmp);
        return;
    }
    if (op == "&&") {
        std::string Lfalse = newLabel(), Ldone = newLabel();
        genCondBranch(*e.lhs, Lfalse);
        genCondBranch(*e.rhs, Lfalse);
        emit("mov", "a, 1");
        emit("goto", Ldone);
        emitLabel(Lfalse);
        emit("mov", "a, 0");
        emitLabel(Ldone);
        return;
    }
    if (op == "||") {
        std::string Ltrue = newLabel(), Ldone = newLabel();
        genCondBranchTrue(*e.lhs, Ltrue);
        genCondBranchTrue(*e.rhs, Ltrue);
        emit("mov", "a, 0");
        emit("goto", Ldone);
        emitLabel(Ltrue);
        emit("mov", "a, 1");
        emitLabel(Ldone);
        return;
    }
    err(e.line, "internal: unhandled binary operator '" + op + "'");
}

void SaturnCodegen::genBitwise(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs) {
    genOperandsIntoCA(lhs, rhs);
    emit(op == "&" ? "and" : op == "|" ? "or" : "xor", "c, a");
    emit("mov", "a, c");
}

void SaturnCodegen::genShift(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs) {
    genOperandsIntoCA(lhs, rhs);  // c = value, a = count
    if (op == "<<") {
        emit("shl", "c, a");
    } else if (lhs.type.isUnsigned) {
        emit("shr", "c, a");
    } else {
        emit("sar", "c, a");
    }
    emit("mov", "a, c");
    narrowA(lhs.type);
}

// Unsigned 32-bit multiply via the shared `__mul32` runtime routine (shift-add loop - real
// Saturn hardware has no multiply instruction). The low 32 bits of a truncated product are the
// same bit pattern whether the operands are signed or unsigned, so one routine serves both.
void SaturnCodegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genOperandsIntoCA(lhs, rhs);
    emit("st", "[__mul_a], c");
    emit("st", "[__mul_b], a");
    emit("gosbvl", "__mul32");
}

// Signed division/modulo reduces to the unsigned routine by negating any negative operand first
// and fixing the result's sign back up afterward - the same idea as pdp7's own callDivMod(),
// adapted since this backend has real registers to track the sign flags in rather than needing a
// second runtime routine.
void SaturnCodegen::callDivMod(bool isUnsigned, bool, bool) {
    if (isUnsigned) {
        emit("gosbvl", "__udivmod32");
        return;
    }
    // `c` = dividend, `a` = divisor on entry (see genDivMod()). Track each operand's sign, negate
    // if negative, divide unsigned, then restore the quotient's sign (XOR of the two original
    // signs) and the remainder's sign (always the dividend's own sign, per C's truncating
    // division).
    std::string Ldvdpos = newLabel(), Ldvddone = newLabel();
    std::string Ldvspos = newLabel(), Ldvsdone = newLabel();
    // `__udivmod32` reads its operands back from `__dm_a`/`__dm_b` (not from `c`/`a`
    // directly), so a negated register has to be written back to its own memory cell too -
    // leaving the stale original (possibly negative) value there would feed the unsigned
    // routine a negative dividend/divisor bit pattern instead of its corrected magnitude.
    emit("st", "[__dm_dvdneg], 0");
    emit("cmp", "c, 0");
    emit("jge", Ldvdpos);
    emit("neg", "c");
    emit("st", "[__dm_a], c");
    emit("st", "[__dm_dvdneg], 1");
    emitLabel(Ldvdpos);
    emit("st", "[__dm_dvsneg], 0");
    emit("cmp", "a, 0");
    emit("jge", Ldvspos);
    emit("neg", "a");
    emit("st", "[__dm_b], a");
    emit("st", "[__dm_dvsneg], 1");
    emitLabel(Ldvspos);
    emit("gosbvl", "__udivmod32");  // quotient in a, remainder in __dm_a
    // Quotient's sign: dividend-sign XOR divisor-sign.
    std::string Lqpos = newLabel(), Lqdone = newLabel();
    emit("mov", "c, a");  // c = unsigned quotient (a is about to be clobbered by loads)
    emit("ld", "a, [__dm_dvdneg]");
    emit("mov", "b, a");
    emit("ld", "a, [__dm_dvsneg]");
    emit("xor", "b, a");
    emit("cmp", "b, 0");
    emit("je", Lqpos);
    emit("neg", "c");
    emitLabel(Lqpos);
    emit("mov", "a, c");
    // Remainder's sign always matches the dividend's original sign.
    emit("ld", "b, [__dm_dvdneg]");
    emit("cmp", "b, 0");
    emit("je", Lqdone);
    emit("ld", "c, [__dm_a]");
    emit("neg", "c");
    emit("st", "[__dm_a], c");
    emitLabel(Lqdone);
}

void SaturnCodegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder) {
    genOperandsIntoCA(lhs, rhs);  // c = dividend, a = divisor
    bool isUnsigned = lhs.type.isUnsigned || rhs.type.isUnsigned;
    emit("st", "[__dm_a], c");
    emit("st", "[__dm_b], a");
    callDivMod(isUnsigned, false, false);
    if (wantRemainder) emit("ld", "a, [__dm_a]");
    narrowA(lhs.type);
}

// --- Main expression dispatch ------------------------------------------------

void SaturnCodegen::genExpr(const ir::Expr &e) {
    // genExpr()'s contract is "leaves the result in `a`" - a wide result can't fit there, so
    // this computes it via genWideExpr() and collapses to just the low chunk, discarding the
    // rest. A caller that needs the full wide value (LocalDecl's initializer, Return) calls
    // genWideExpr() directly instead.
    if (isWide(e.type)) {
        genWideExpr(e);
        int chunks = chunkCount(e.type);
        emit("pop", "a");
        for (int i = 1; i < chunks; i++) emit("pop", "c");
        return;
    }
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit: {
            emit("mov", cat("a, ", e.intValue));
            return;
        }
        case ir::ExprKind::Var: {
            emitLoadSlot(e.slot);
            return;
        }
        case ir::ExprKind::Index: {
            genIndex(e);
            return;
        }
        case ir::ExprKind::Assign: {
            if (e.lhs) {  // slot[idx] = rhs
                computeElementAddress(e.slot, *e.lhs);  // pushes the element's own address
                genExpr(*e.rhs);
                emit("pop", "c");
                emit(stMnemonic(widthOfType(e.slot->type)), "[c], a");
                return;
            }
            genExpr(*e.rhs);
            emitStoreSlot(e.slot);
            return;
        }
        case ir::ExprKind::Unary: {
            if (e.op == "-") {
                genExpr(*e.rhs);
                emit("neg", "a");
            } else if (e.op == "!") {
                genExpr(*e.rhs);
                std::string Ltrue = newLabel(), Ldone = newLabel();
                emit("cmp", "a, 0");
                emit("je", Ltrue);
                emit("mov", "a, 0");
                emit("goto", Ldone);
                emitLabel(Ltrue);
                emit("mov", "a, 1");
                emitLabel(Ldone);
            } else if (e.op == "*") {
                genExpr(*e.rhs);  // a = pointer value
                emit(ldMnemonic(widthOfType(e.type)), "a, [a]");
                narrowA(e.type);
            } else if (e.op == "~") {
                genExpr(*e.rhs);
                emit("not", "a");
            } else {  // "&"
                genAddrOf(*e.rhs);
            }
            return;
        }
        case ir::ExprKind::DerefAssign: {
            genExpr(*e.lhs);  // a = pointer value
            emit("push", "a");
            genExpr(*e.rhs);
            emit("pop", "c");
            emit(stMnemonic(widthOfType(e.type)), "[c], a");
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            genCompoundScalar(e);
            return;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            genExpr(*e.lhs);  // a = pointer value
            emit("push", "a");
            genCompoundThroughAddress(e);
            return;
        }
        case ir::ExprKind::Ternary: {
            std::string Lelse = newLabel(), Ldone = newLabel();
            genCondBranch(*e.lhs, Lelse);
            genExpr(*e.rhs);
            emit("goto", Ldone);
            emitLabel(Lelse);
            genExpr(*e.elseExpr);
            emitLabel(Ldone);
            return;
        }
        case ir::ExprKind::Call: {
            int totalBytes = 0;
            for (size_t i = e.args.size(); i-- > 0;) {
                if (isWide(e.args[i]->type)) {
                    genWideExpr(*e.args[i]);
                    totalBytes += widthOfType(e.args[i]->type);  // already real bytes, 4/chunk
                } else {
                    genExpr(*e.args[i]);
                    emit("push", "a");
                    totalBytes += 4;
                }
            }
            emit("gosbvl", label(e.func));
            if (totalBytes > 0) emit("add", cat("d0, ", totalBytes));
            narrowA(e.type);
            return;
        }
        case ir::ExprKind::CallPtr: {
            genExpr(*e.lhs);  // a = callee's entry address
            emit("gosbvlr", "a");
            return;
        }
        case ir::ExprKind::Binary: {
            genBinary(e);
            return;
        }
        case ir::ExprKind::Intrinsic: {
            // Real Saturn interrupt-enable/disable has no analogue in this freestanding,
            // simplified model; this is a harmless no-op, the same convention most other
            // backends (e.g. mips) use for this IR node.
            emit(";", cat(e.op, " (no-op on this backend)"));
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

void SaturnCodegen::genConvert(const ir::Expr &e) {
    if (isWide(e.lhs->type)) {
        genWideExpr(*e.lhs);
        int chunks = chunkCount(e.lhs->type);
        emit("pop", "a");  // the low chunk - this is the value being kept
        for (int i = 1; i < chunks; i++) emit("pop", "c");
        narrowA(e.type);
        return;
    }
    genExpr(*e.lhs);
    narrowA(e.type);
}

// --- Compound assignment ('+=' etc., prefix/postfix '++'/'--') -------------------------------

void SaturnCodegen::combineInPlace(const ir::Expr &e, const std::string &dst, const std::string &rhsReg) {
    const std::string &op = e.op;
    if (op == "+") {
        emit("add", cat(dst, ", ", rhsReg));
    } else if (op == "-") {
        emit("sub", cat(dst, ", ", rhsReg));
    } else if (op == "&") {
        emit("and", cat(dst, ", ", rhsReg));
    } else if (op == "|") {
        emit("or", cat(dst, ", ", rhsReg));
    } else if (op == "^") {
        emit("xor", cat(dst, ", ", rhsReg));
    } else if (op == "<<") {
        emit("shl", cat(dst, ", ", rhsReg));
    } else if (op == ">>") {
        emit(e.type.isUnsigned ? "shr" : "sar", cat(dst, ", ", rhsReg));
    } else if (op == "*") {
        emit("st", cat("[__mul_a], ", dst));
        emit("st", cat("[__mul_b], ", rhsReg));
        emit("gosbvl", "__mul32");
        if (dst != "a") emit("mov", cat(dst, ", a"));
    } else if (op == "/" || op == "%") {
        bool isUnsigned = e.type.isUnsigned;
        emit("st", cat("[__dm_a], ", dst));
        emit("st", cat("[__dm_b], ", rhsReg));
        if (dst != "c") emit("mov", cat("c, ", dst));
        if (rhsReg != "a") emit("ld", "a, [__dm_b]");
        callDivMod(isUnsigned, false, false);
        if (op == "%") emit("ld", "a, [__dm_a]");
        if (dst != "a") emit("mov", cat(dst, ", a"));
    } else {
        err(e.line, "internal: unhandled compound-assignment operator '" + op + "'");
    }
}

void SaturnCodegen::genCompoundScalar(const ir::Expr &e) {
    if (e.lhs) {  // slot[idx] (op)= rhs
        computeElementAddress(e.slot, *e.lhs);  // pushes the element's own address
        genCompoundThroughAddress(e);
        return;
    }
    // Plain scalar target - its own address (a Global's label, or a Local/Param's `d1`-relative
    // offset) is cheap enough to recompute from scratch after rhs's own evaluation via
    // emitLoadSlot()/emitStoreSlot(), neither of which ever touches `c`, so `c` safely holds
    // rhs across the `emitLoadSlot()` call below with nothing needing the stack.
    genExpr(*e.rhs);
    emit("mov", "c, a");  // c = rhs
    emitLoadSlot(e.slot);  // a = old value
    if (e.yieldOld) emit("push", "a");
    emit("mov", "b, a");  // b = old value (dst for combineInPlace)
    combineInPlace(e, "b", "c");
    emit("mov", "a, b");
    narrowA(e.type);
    emitStoreSlot(e.slot);  // leaves `a` (the new value) unchanged
    if (e.yieldOld) emit("pop", "a");
}

// The target address must already be pushed on the stack by the caller, before e.rhs is
// evaluated, so it survives whatever e.rhs itself does to `b`/`c` - not just a nested call, but
// also a nested div/mod/multiply. It's then stashed in a named cell, not a register: `combine
// InPlace()`'s own divide/multiply paths (via the `__udivmod32`/`__mul32` runtime routines)
// freely clobber `b`/`c` as scratch, same as a nested call would, so the address can't ride in
// either of them across that call.
void SaturnCodegen::genCompoundThroughAddress(const ir::Expr &e) {
    const char *ld = ldMnemonic(widthOfType(e.type));
    const char *st = stMnemonic(widthOfType(e.type));
    std::string addrCell = allocTemp();
    declareTemp(addrCell);
    genExpr(*e.rhs);  // a = rhs
    emit("pop", "c");  // c = address
    emit("st", cat("[", addrCell, "], c"));
    emit("mov", "b, a");  // b = rhs
    emit(ld, "a, [c]");   // a = old value
    if (e.yieldOld) emit("push", "a");
    combineInPlace(e, "a", "b");  // result in `a`; may clobber `b`/`c` freely
    narrowA(e.type);
    emit("ld", cat("c, [", addrCell, "]"));
    emit(st, "[c], a");
    if (e.yieldOld) emit("pop", "a");
}

// --- `long`/`long long` (wide) support --------------------------------------------------------

void SaturnCodegen::pushWideFromAddress(Type type) {
    emit("mov", "c, a");  // c = base address
    int chunks = chunkCount(type);
    for (int i = chunks - 1; i >= 0; i--) {
        emit("mov", "a, c");
        if (i > 0) emit("add", cat("a, ", i * 4));
        emit("ld", "a, [a]");
        emit("push", "a");
    }
}

void SaturnCodegen::popWideToAddress(Type type) {
    emit("mov", "c, a");  // c = base address
    int chunks = chunkCount(type);
    for (int i = 0; i < chunks; i++) {
        emit("pop", "a");
        emit("mov", "b, c");
        if (i > 0) emit("add", cat("b, ", i * 4));
        emit("st", "[b], a");
    }
}

void SaturnCodegen::pushWideLiteral(Type type, long value) {
    int chunks = chunkCount(type);
    // Each chunk is a 32-bit slice of `value`'s own two's-complement pattern - an arithmetic
    // right shift sign-extends correctly even past the host `long`'s own width.
    for (int i = chunks - 1; i >= 0; i--) {
        long chunkVal = (value >> (32 * i)) & 0xFFFFFFFFL;
        emit("mov", cat("a, ", chunkVal));
        emit("push", "a");
    }
}

// dest = dest + addend + carryCell (read then overwritten with this chunk's carry-out). Computed
// as two separate half-adds (`dest += addend`, then `dest += carryIn`), each checked for
// unsigned wraparound against its own pre-add value via `cmp`+`jb` (this backend has no
// hardware carry flag to read directly) and OR'd together - a single combined "result < addend"
// test is NOT equivalent: it misses the case where `dest` was already 0xFFFFFFFF and `carryIn`
// alone supplies the second carry (same two-step shape as pdp7's own emitWideAddWithCarry()).
void SaturnCodegen::emitAddWithCarry(const std::string &dest, const std::string &addend,
                                      const std::string &carryCell) {
    std::string prev = allocTemp(), carry1 = allocTemp(), carry2 = allocTemp();
    declareTemp(prev);
    declareTemp(carry1);
    declareTemp(carry2);

    auto halfAdd = [&](const std::string &rhsSrc, const std::string &carryOutCell) {
        emit("st", cat("[", prev, "], ", dest));
        emit("add", cat(dest, ", ", rhsSrc));
        std::string Lset = newLabel(), Ldone = newLabel();
        emit("ld", cat("b, [", prev, "]"));
        emit("cmp", cat(dest, ", b"));
        emit("jb", Lset);
        emit("st", cat("[", carryOutCell, "], 0"));
        emit("goto", Ldone);
        emitLabel(Lset);
        emit("st", cat("[", carryOutCell, "], 1"));
        emitLabel(Ldone);
    };
    halfAdd(addend, carry1);
    emit("ld", cat("b, [", carryCell, "]"));
    halfAdd("b", carry2);
    emit("ld", cat("a, [", carry1, "]"));
    emit("ld", cat("b, [", carry2, "]"));
    emit("or", "a, b");
    emit("st", cat("[", carryCell, "], a"));
}

void SaturnCodegen::genWideNegate(int chunks) {
    std::vector<std::string> t(chunks);
    for (auto &x : t) {
        x = allocTemp();
        declareTemp(x);
    }
    // Two's-complement negate: complement every chunk, then add 1 with carry rippling upward.
    for (int i = 0; i < chunks; i++) {
        emit("pop", "a");
        emit("not", "a");
        emit("st", cat("[", t[i], "], a"));
    }
    std::string carryCell = allocTemp();
    declareTemp(carryCell);
    emit("st", cat("[", carryCell, "], 1"));  // the "+1" of two's-complement negation
    for (int i = 0; i < chunks; i++) {
        emit("ld", cat("c, [", t[i], "]"));
        emit("mov", "a, 0");
        emitAddWithCarry("c", "a", carryCell);
        emit("st", cat("[", t[i], "], c"));
    }
    for (int i = chunks - 1; i >= 0; i--) {
        emit("ld", cat("a, [", t[i], "]"));
        emit("push", "a");
    }
}

void SaturnCodegen::genWideAddSub(bool isAdd, int chunks) {
    std::vector<std::string> rhsT(chunks), lhsT(chunks);
    for (auto &t : rhsT) {
        t = allocTemp();
        declareTemp(t);
    }
    for (auto &t : lhsT) {
        t = allocTemp();
        declareTemp(t);
    }
    // genWideExpr() pushes chunk 0 (low) last, so it's on top: the FIRST pop must land in
    // index 0, not chunks-1.
    for (int i = 0; i < chunks; i++) {
        emit("pop", "a");
        emit("st", cat("[", rhsT[i], "], a"));
    }
    for (int i = 0; i < chunks; i++) {
        emit("pop", "a");
        emit("st", cat("[", lhsT[i], "], a"));
    }

    std::string carryCell = allocTemp();
    declareTemp(carryCell);
    if (!isAdd) {
        // a - b = a + (~b + 1): negate rhsT in place first via the same ripple-carry machinery.
        emit("st", cat("[", carryCell, "], 1"));
        for (int i = 0; i < chunks; i++) {
            emit("ld", cat("c, [", rhsT[i], "]"));
            emit("not", "c");
            emit("mov", "a, 0");
            emitAddWithCarry("c", "a", carryCell);
            emit("st", cat("[", rhsT[i], "], c"));
        }
    }
    emit("st", cat("[", carryCell, "], 0"));
    for (int i = 0; i < chunks; i++) {
        emit("ld", cat("c, [", lhsT[i], "]"));
        emit("ld", cat("a, [", rhsT[i], "]"));
        emitAddWithCarry("c", "a", carryCell);
        emit("st", cat("[", lhsT[i], "], c"));
    }
    for (int i = chunks - 1; i >= 0; i--) {
        emit("ld", cat("a, [", lhsT[i], "]"));
        emit("push", "a");
    }
}

// Each chunk is independent - no carry chain needed.
void SaturnCodegen::genWideBitwise(const std::string &op, int chunks) {
    std::vector<std::string> rhsT(chunks), lhsT(chunks);
    for (auto &t : rhsT) {
        t = allocTemp();
        declareTemp(t);
    }
    for (auto &t : lhsT) {
        t = allocTemp();
        declareTemp(t);
    }
    for (int i = 0; i < chunks; i++) {
        emit("pop", "a");
        emit("st", cat("[", rhsT[i], "], a"));
    }
    for (int i = 0; i < chunks; i++) {
        emit("pop", "a");
        emit("st", cat("[", lhsT[i], "], a"));
    }
    for (int i = chunks - 1; i >= 0; i--) {
        emit("ld", cat("c, [", lhsT[i], "]"));
        emit("ld", cat("a, [", rhsT[i], "]"));
        emit(op == "&" ? "and" : op == "|" ? "or" : "xor", "c, a");
        emit("mov", "a, c");
        emit("push", "a");
    }
}

void SaturnCodegen::genWideCompare(const std::string &op, int chunks, bool isUnsigned) {
    std::vector<std::string> rhsT(chunks), lhsT(chunks);
    for (auto &t : rhsT) {
        t = allocTemp();
        declareTemp(t);
    }
    for (auto &t : lhsT) {
        t = allocTemp();
        declareTemp(t);
    }
    for (int i = 0; i < chunks; i++) {
        emit("pop", "a");
        emit("st", cat("[", rhsT[i], "], a"));
    }
    for (int i = 0; i < chunks; i++) {
        emit("pop", "a");
        emit("st", cat("[", lhsT[i], "], a"));
    }

    if (op == "==" || op == "!=") {
        std::string Ldiffer = newLabel(), Ldone = newLabel();
        for (int i = chunks - 1; i >= 0; i--) {
            emit("ld", cat("c, [", lhsT[i], "]"));
            emit("ld", cat("a, [", rhsT[i], "]"));
            emit("cmp", "c, a");
            emit("jne", Ldiffer);
        }
        emit("mov", cat("a, ", op == "==" ? 1 : 0));
        emit("goto", Ldone);
        emitLabel(Ldiffer);
        emit("mov", cat("a, ", op == "==" ? 0 : 1));
        emitLabel(Ldone);
        return;
    }
    // Compare from the most-significant chunk down, short-circuiting once a chunk differs; only
    // the top chunk's compare is signed (when `!isUnsigned`) - lower chunks are always unsigned
    // magnitude once the higher chunks are known equal.
    std::string Llt = newLabel(), Lge = newLabel(), Ldone = newLabel();
    for (int i = chunks - 1; i >= 0; i--) {
        bool chunkUnsigned = (i == chunks - 1) ? isUnsigned : true;
        emit("ld", cat("c, [", lhsT[i], "]"));
        emit("ld", cat("a, [", rhsT[i], "]"));
        emit("cmp", "c, a");
        emit(jccForTrue("<", chunkUnsigned), Llt);
        if (i > 0) {
            // Equal so far: fall through to the next (lower) chunk's comparison. Only a
            // genuine difference (greater, here) short-circuits early.
            emit(jccForTrue(">", chunkUnsigned), Lge);
        } else {
            emit("goto", Lge);
        }
    }
    emitLabel(Llt);
    emit("mov", cat("a, ", (op == "<" || op == "<=") ? 1 : 0));
    emit("goto", Ldone);
    emitLabel(Lge);
    emit("mov", cat("a, ", (op == "<" || op == "<=") ? 0 : 1));
    emitLabel(Ldone);
}

// Multiply and divide/modulo are not implemented for a wide operand: a genuine multi-chunk
// multiply/divide needs the ripple-carry machinery above nested inside its own shift-and-add/
// compare-shift-subtract loop, and this backend's software `__mul32`/`__udivmod32` routines are
// only verified for a single 32-bit chunk - the same scope decision pdp7 makes (see
// Pdp7Codegen.cpp's own genWideMulDivErr()).
void SaturnCodegen::genWideMulDivErr(int line, const std::string &opName) {
    err(line, "'" + opName + "' on a 'long'/'long long' operand isn't supported on this backend "
                              "(no verified multi-chunk multiply/divide routine - see "
                              "SaturnCodegen.cpp's own genWideMulDivErr() comment)");
}

void SaturnCodegen::genWideExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::IntLit: {
            pushWideLiteral(e.type, e.intValue);
            return;
        }
        case ir::ExprKind::Var: {
            emitLoadSlotAddress(e.slot);
            pushWideFromAddress(e.slot->type);
            return;
        }
        case ir::ExprKind::Index: {
            computeElementAddressA(e.slot, *e.lhs);
            pushWideFromAddress(e.type);
            return;
        }
        case ir::ExprKind::DerefAssign: {
            int chunks = chunkCount(e.type);
            genExpr(*e.lhs);  // a = pointer value
            emit("push", "a");
            genWideExpr(*e.rhs);
            std::vector<std::string> temps(chunks);
            for (auto &t : temps) {
                t = allocTemp();
                declareTemp(t);
            }
            for (int i = 0; i < chunks; i++) {
                emit("pop", "a");
                emit("st", cat("[", temps[i], "], a"));
            }
            emit("pop", "a");  // a = the pointer's own address
            emit("mov", "c, a");
            for (int i = 0; i < chunks; i++) {
                emit("ld", cat("a, [", temps[i], "]"));
                emit("mov", "b, c");
                if (i > 0) emit("add", cat("b, ", i * 4));
                emit("st", "[b], a");
            }
            for (int i = chunks - 1; i >= 0; i--) {
                emit("ld", cat("a, [", temps[i], "]"));
                emit("push", "a");
            }
            return;
        }
        case ir::ExprKind::Assign: {
            int chunks = chunkCount(e.slot->type);
            std::vector<std::string> temps(chunks);
            for (auto &t : temps) {
                t = allocTemp();
                declareTemp(t);
            }
            if (e.lhs) {  // slot[idx] = rhs, a wide array element
                computeElementAddress(e.slot, *e.lhs);  // pushes the element's own address
                genWideExpr(*e.rhs);
                for (int i = 0; i < chunks; i++) {
                    emit("pop", "a");
                    emit("st", cat("[", temps[i], "], a"));
                }
                emit("pop", "a");  // a = the element's own address
                emit("mov", "c, a");
            } else {
                genWideExpr(*e.rhs);
                for (int i = 0; i < chunks; i++) {
                    emit("pop", "a");
                    emit("st", cat("[", temps[i], "], a"));
                }
                emitLoadSlotAddress(e.slot);
                emit("mov", "c, a");
            }
            for (int i = 0; i < chunks; i++) {
                emit("ld", cat("a, [", temps[i], "]"));
                emit("mov", "b, c");
                if (i > 0) emit("add", cat("b, ", i * 4));
                emit("st", "[b], a");
            }
            for (int i = chunks - 1; i >= 0; i--) {
                emit("ld", cat("a, [", temps[i], "]"));
                emit("push", "a");
            }
            return;
        }
        case ir::ExprKind::Unary: {
            if (e.op == "-") {
                genWideExpr(*e.rhs);
                genWideNegate(chunkCount(e.type));
                return;
            }
            if (e.op == "*") {
                genExpr(*e.rhs);  // a = pointer value
                pushWideFromAddress(e.type);
                return;
            }
            if (e.op == "~") {
                int chunks = chunkCount(e.type);
                genWideExpr(*e.rhs);
                std::vector<std::string> t(chunks);
                for (auto &x : t) {
                    x = allocTemp();
                    declareTemp(x);
                }
                for (int i = 0; i < chunks; i++) {
                    emit("pop", "a");
                    emit("not", "a");
                    emit("st", cat("[", t[i], "], a"));
                }
                for (int i = chunks - 1; i >= 0; i--) {
                    emit("ld", cat("a, [", t[i], "]"));
                    emit("push", "a");
                }
                return;
            }
            err(e.line, "internal: unary '" + e.op + "' on a wide ('long'/'long long') value isn't "
                                                       "supported on this backend");
        }
        case ir::ExprKind::Convert: {
            Type from = e.lhs->type;
            int toChunks = chunkCount(e.type);
            if (!isWide(from)) {
                genExpr(*e.lhs);
                std::string Tlo = allocTemp();
                declareTemp(Tlo);
                emit("st", cat("[", Tlo, "], a"));
                std::string Text = allocTemp();
                declareTemp(Text);
                if (from.isUnsigned) {
                    emit("st", cat("[", Text, "], 0"));
                } else {
                    std::string Lneg = newLabel(), Ldone = newLabel();
                    emit("cmp", "a, 0");
                    emit("jl", Lneg);
                    emit("st", cat("[", Text, "], 0"));
                    emit("goto", Ldone);
                    emitLabel(Lneg);
                    emit("st", cat("[", Text, "], 0xFFFFFFFF"));
                    emitLabel(Ldone);
                }
                for (int i = 0; i < toChunks - 1; i++) {
                    emit("ld", cat("a, [", Text, "]"));
                    emit("push", "a");
                }
                emit("ld", cat("a, [", Tlo, "]"));
                emit("push", "a");
                return;
            }
            int fromChunks = chunkCount(from);
            genWideExpr(*e.lhs);
            std::vector<std::string> temps(fromChunks);
            for (auto &t : temps) {
                t = allocTemp();
                declareTemp(t);
            }
            for (int i = 0; i < fromChunks; i++) {
                emit("pop", "a");
                emit("st", cat("[", temps[i], "], a"));
            }
            if (toChunks <= fromChunks) {
                for (int i = toChunks - 1; i >= 0; i--) {
                    emit("ld", cat("a, [", temps[i], "]"));
                    emit("push", "a");
                }
                return;
            }
            std::string Text = allocTemp();
            declareTemp(Text);
            if (from.isUnsigned) {
                emit("st", cat("[", Text, "], 0"));
            } else {
                std::string Lneg = newLabel(), Ldone = newLabel();
                emit("ld", cat("a, [", temps[fromChunks - 1], "]"));
                emit("cmp", "a, 0");
                emit("jl", Lneg);
                emit("st", cat("[", Text, "], 0"));
                emit("goto", Ldone);
                emitLabel(Lneg);
                emit("st", cat("[", Text, "], 0xFFFFFFFF"));
                emitLabel(Ldone);
            }
            for (int i = 0; i < toChunks - fromChunks; i++) {
                emit("ld", cat("a, [", Text, "]"));
                emit("push", "a");
            }
            for (int i = fromChunks - 1; i >= 0; i--) {
                emit("ld", cat("a, [", temps[i], "]"));
                emit("push", "a");
            }
            return;
        }
        case ir::ExprKind::Call: {
            // Same argument-passing convention as genExpr()'s Call case - see
            // emitEpilogueAndReturn()'s own comment for why a wide result needs the args
            // popped first, then discarded, before the saved chunks go back on the stack.
            int totalBytes = 0;
            for (size_t i = e.args.size(); i-- > 0;) {
                if (isWide(e.args[i]->type)) {
                    genWideExpr(*e.args[i]);
                    totalBytes += widthOfType(e.args[i]->type);  // already real bytes, 4/chunk
                } else {
                    genExpr(*e.args[i]);
                    emit("push", "a");
                    totalBytes += 4;
                }
            }
            emit("gosbvl", label(e.func));
            int chunks = chunkCount(e.type);
            std::vector<std::string> retTemps(chunks);
            for (auto &t : retTemps) {
                t = allocTemp();
                declareTemp(t);
            }
            for (int i = 0; i < chunks; i++) {
                emit("pop", "a");
                emit("st", cat("[", retTemps[i], "], a"));
            }
            if (totalBytes > 0) emit("add", cat("d0, ", totalBytes));
            for (int i = chunks - 1; i >= 0; i--) {
                emit("ld", cat("a, [", retTemps[i], "]"));
                emit("push", "a");
            }
            return;
        }
        case ir::ExprKind::Binary: {
            const std::string &op = e.op;
            if (op == "+" || op == "-") {
                genWideExpr(*e.lhs);
                genWideExpr(*e.rhs);
                genWideAddSub(op == "+", chunkCount(e.type));
                return;
            }
            if (op == "&" || op == "|" || op == "^") {
                genWideExpr(*e.lhs);
                genWideExpr(*e.rhs);
                genWideBitwise(op, chunkCount(e.type));
                return;
            }
            if (op == "*") genWideMulDivErr(e.line, "*");
            if (op == "/") genWideMulDivErr(e.line, "/");
            if (op == "%") genWideMulDivErr(e.line, "%");
            err(e.line, "internal: binary '" + op + "' on a wide ('long'/'long long') value isn't "
                                                      "supported on this backend");
        }
        default:
            err(e.line, "internal: this expression kind can't produce a wide ('long'/'long long') "
                          "value on this backend");
    }
}

// --- Shared runtime: software multiply/divide ------------------------------------------------
//
// Real Saturn hardware has neither a multiply nor a divide instruction, so both are software
// routines here, called via `gosbvl`/returned from via `rtn` exactly like any C function - see
// SaturnCodegen.h's own header comment for the calling convention they share with everything
// else this backend generates.
std::string saturnRuntimeAsm() {
    return R"ASM(
__mul_a: dw 0
__mul_b: dw 0
__dm_a: dw 0
__dm_b: dw 0
__dm_dvdneg: dw 0
__dm_dvsneg: dw 0

; a = __mul_a * __mul_b (unsigned 32-bit multiply, truncated to the low 32 bits - the same bit
; pattern as a signed multiply's low word, so one routine serves both). Shift-and-add, LSB
; first: `c` is the shifting multiplier (consumed as it's tested), `a` the shifting
; multiplicand, `b` the accumulating result; `__mul_t` stashes `c` across the one-bit test
; below, since this ISA's `and`/`shr` only take a register or immediate operand, never a
; memory one.
__mul_t: dw 0
__mul32:
    ld  a, [__mul_a]
    ld  c, [__mul_b]
    mov b, 0
__mul32_loop:
    cmp c, 0
    je __mul32_done
    st  [__mul_t], c
    and c, 1
    cmp c, 0
    je __mul32_skip
    add b, a
__mul32_skip:
    ld  c, [__mul_t]
    shr c, 1
    shl a, 1
    goto __mul32_loop
__mul32_done:
    mov a, b
    rtn

; a = __dm_a / __dm_b (unsigned), remainder left in __dm_a. Restoring binary long division: `a`
; is the shifting dividend, `c` the shifting remainder, `b` the shifting quotient, `d1` (saved/
; restored - a leaf routine, so repurposing it is safe) the iteration counter. The divisor is
; reloaded from memory each iteration, held transiently in `a` itself (stashed in `__udm_shifted`
; while that happens) since there's no spare register for a fourth live value.
__udm_shifted: dw 0
__udivmod32:
    push d1
    ld  a, [__dm_a]
    mov b, 0
    mov c, 0
    mov d1, 32
__udivmod32_loop:
    cmp d1, 0
    je __udivmod32_done
    dec d1
    cmp a, 0
    jl __udivmod32_bitset
    shl a, 1
    shl c, 1
    goto __udivmod32_shifted_lbl
__udivmod32_bitset:
    shl a, 1
    shl c, 1
    or  c, 1
__udivmod32_shifted_lbl:
    shl b, 1
    st  [__udm_shifted], a
    ld  a, [__dm_b]
    cmp c, a
    jb __udivmod32_noSub
    sub c, a
    or  b, 1
__udivmod32_noSub:
    ld  a, [__udm_shifted]
    goto __udivmod32_loop
__udivmod32_done:
    pop d1
    st  [__dm_a], c
    mov a, b
    rtn
)ASM";
}

}  // namespace rcc
