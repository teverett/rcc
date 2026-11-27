#include "M6502Codegen.h"

#include <algorithm>
#include <tuple>
#include <cstdint>
#include <stdexcept>

#include "M6502Assembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

}  // namespace

// A literal element's decimal text, or a symbol element's label (a
// dispatch-table address-of-function/global). A symbol has no resolved
// numeric value at codegen time, so it's left as a label for the
// assembler's `dblo`/`dbhi` directives to split into bytes later.
std::string M6502Codegen::arrayElemText(const ir::ArrayInitElem &e) {
    if (!e.isSymbol) return std::to_string(e.value);
    return e.slot ? label(e.slot) : label(e.func);
}

// A symbol element is always native-int width (2 bytes); only a plain
// literal element can be `char`-width (1 byte) or wide (4/8 bytes). A wide
// literal has no dblo/dbhi counterpart, so it's split into plain bytes
// directly, least significant first.
void M6502Codegen::declareArray(const std::string &lbl, const std::vector<ir::ArrayInitElem> &values,
                                 Type elemType) {
    if (values.empty()) return;
    int w = widthOfType(elemType);
    bool first = true;
    auto appendTrailer = [&](const std::string &text) {
        trailer_.push_back((first ? lbl + ": " : "") + text);
        first = false;
    };
    for (const auto &e : values) {
        if (w == 1) {
            appendTrailer("db " + std::to_string(static_cast<int>(static_cast<uint8_t>(e.value))));
        } else if (w == 2) {
            std::string text = arrayElemText(e);
            appendTrailer("dblo " + text);
            trailer_.push_back("dbhi " + text);
        } else {
            auto uv = static_cast<unsigned long>(e.value);
            for (int b = 0; b < w; b++) {
                appendTrailer("db " + std::to_string(static_cast<int>((uv >> (8 * b)) & 0xFF)));
            }
        }
    }
}

void M6502Codegen::copy16(const std::string &dst, const std::string &src) {
    emit("lda", src);
    emit("sta", dst);
    emit("lda", src + "+1");
    emit("sta", dst + "+1");
}

namespace {
// `base+i` operand text (just `base` for byte 0).
std::string byteAt(const char *base, int i) { return i == 0 ? base : (std::string(base) + "+" + std::to_string(i)); }
}  // namespace

// Pushes `base`'s `nbytes` bytes ascending (byte 0 first), so the most
// significant byte ends up on top.
void M6502Codegen::pushWide(const char *base, int nbytes) {
    for (int i = 0; i < nbytes; i++) {
        emit("lda", byteAt(base, i));
        emit("pha");
    }
}

// Mirror of pushWide(): pops descending (most significant, on top, first).
void M6502Codegen::popWide(const char *base, int nbytes) {
    for (int i = nbytes - 1; i >= 0; i--) {
        emit("pla");
        emit("sta", byteAt(base, i));
    }
}

void M6502Codegen::copyWide(const char *dst, const char *src, int nbytes) {
    for (int i = 0; i < nbytes; i++) {
        emit("lda", byteAt(src, i));
        emit("sta", byteAt(dst, i));
    }
}

void M6502Codegen::negateWideBuffer(const char *buf, int nbytes) {
    for (int i = 0; i < nbytes; i++) {
        emit("lda", byteAt(buf, i));
        emit("eori", 255);
        emit("sta", byteAt(buf, i));
    }
    emit("clc");
    emit("lda", byteAt(buf, 0));
    emit("adci", 1);
    emit("sta", byteAt(buf, 0));
    for (int i = 1; i < nbytes; i++) {
        emit("lda", byteAt(buf, i));
        emit("adci", 0);
        emit("sta", byteAt(buf, i));
    }
}

void M6502Codegen::safeBranch(const std::string &branchMnemonic, const std::string &targetIfTaken,
                               const std::string &targetIfNot) {
    std::string Lnear = newLabel();
    emit(branchMnemonic, Lnear);
    emit("jmp", targetIfNot);
    emitLabel(Lnear);
    emit("jmp", targetIfTaken);
}

// A Global's wide-storage word label (`label+N`, N omitted for 0); a
// Local/Param's wide words go through `ptr`/Y instead.
std::string M6502Codegen::wideWordLabel(const ir::Slot *slot, int byteOffset) const {
    std::string lbl = label(slot);
    return byteOffset == 0 ? lbl : (lbl + "+" + std::to_string(byteOffset));
}

void M6502Codegen::genWideLoad(const ir::Slot *slot, int words) {
    int n = 2 * words;
    if (slot->kind == ir::SlotKind::Global) {
        for (int i = 0; i < n; i++) {
            emit("lda", wideWordLabel(slot, i));
            emit("sta", byteAt("wac", i));
        }
        return;
    }
    setPtrToFrameSlot(slot, 0);
    for (int i = 0; i < n; i++) {
        emit("ldyi", i);
        emit("ldaind", "ptr");
        emit("sta", byteAt("wac", i));
    }
}

void M6502Codegen::genWideLoadLiteral(long value, int words) {
    auto uv = static_cast<unsigned long>(value);
    int n = 2 * words;
    for (int i = 0; i < n; i++) {
        emit("ldai", (uv >> (8 * i)) & 0xFF);
        emit("sta", byteAt("wac", i));
    }
}

void M6502Codegen::genWideStore(const ir::Slot *slot, int words) {
    int n = 2 * words;
    if (slot->kind == ir::SlotKind::Global) {
        for (int i = 0; i < n; i++) {
            emit("lda", byteAt("wac", i));
            emit("sta", wideWordLabel(slot, i));
        }
        return;
    }
    setPtrToFrameSlot(slot, 0);
    for (int i = 0; i < n; i++) {
        emit("ldyi", i);
        emit("lda", byteAt("wac", i));
        emit("staind", "ptr");
    }
}

void M6502Codegen::genWideLoadAtPtr(int words) {
    int n = 2 * words;
    for (int i = 0; i < n; i++) {
        emit("ldyi", i);
        emit("ldaind", "ptr");
        emit("sta", byteAt("wac", i));
    }
}

void M6502Codegen::genWideStoreAtPtr(int words) {
    int n = 2 * words;
    for (int i = 0; i < n; i++) {
        emit("ldyi", i);
        emit("lda", byteAt("wac", i));
        emit("staind", "ptr");
    }
}

void M6502Codegen::loadOperandAtPtr(Type type) {
    int w = widthOfType(type);
    if (w > 2) {
        genWideLoadAtPtr(wordCountOf(type));
        return;
    }
    if (w == 1) {
        emit("ldyi", 0);
        emit("ldaind", "ptr");
        emit("sta", "ac");
        if (type.isUnsigned) {
            emit("ldai", 0);
            emit("sta", "ac+1");
            return;
        }
        emit("lda", "ac");
        emit("sta", "cx");
        emit("asl", "cx");
        std::string Lneg = newLabel(), Ldone = newLabel();
        emit("ldai", 0);
        emit("sta", "ac+1");
        safeBranch("bcs", Lneg, Ldone);
        emitLabel(Lneg);
        emit("ldai", 255);
        emit("sta", "ac+1");
        emitLabel(Ldone);
        return;
    }
    emit("ldyi", 0);
    emit("ldaind", "ptr");
    emit("sta", "ac");
    emit("ldyi", 1);
    emit("ldaind", "ptr");
    emit("sta", "ac+1");
}

void M6502Codegen::storeOperandAtPtr(Type type) {
    int w = widthOfType(type);
    if (w > 2) {
        genWideStoreAtPtr(wordCountOf(type));
        return;
    }
    emit("ldyi", 0);
    emit("lda", "ac");
    emit("staind", "ptr");
    if (w == 2) {
        emit("ldyi", 1);
        emit("lda", "ac+1");
        emit("staind", "ptr");
    }
}

void M6502Codegen::narrowAcTo(Type to) {
    if (widthOfType(to) != 1) return;  // a native word: ac already holds the full value
    if (to.isUnsigned) {
        emit("ldai", 0);
        emit("sta", "ac+1");
        return;
    }
    emit("lda", "ac");
    emit("sta", "cx");
    emit("asl", "cx");
    std::string Lneg = newLabel(), Ldone = newLabel();
    emit("ldai", 0);
    emit("sta", "ac+1");
    safeBranch("bcs", Lneg, Ldone);
    emitLabel(Lneg);
    emit("ldai", 255);
    emit("sta", "ac+1");
    emitLabel(Ldone);
}

// `op` applied to ac=old, bc=rhs, leaving the raw new value in ac.
void M6502Codegen::genCompoundOp(const std::string &op, Type type) {
    bool isUnsigned = type.isUnsigned;
    if (op == "+") {
        emit("clc");
        emit("lda", "ac");
        emit("adc", "bc");
        emit("sta", "ac");
        emit("lda", "ac+1");
        emit("adc", "bc+1");
        emit("sta", "ac+1");
        return;
    }
    if (op == "-") {
        emit("sec");
        emit("lda", "ac");
        emit("sbc", "bc");
        emit("sta", "ac");
        emit("lda", "ac+1");
        emit("sbc", "bc+1");
        emit("sta", "ac+1");
        return;
    }
    if (op == "*") {
        copy16("dx", "ac");  // dx = multiplier = old (bc already = rhs = multiplicand)
        genMulCore();
        return;
    }
    if (op == "/" || op == "%") {
        copy16("dx", "ac");  // dx = dividend = old (bc already = rhs = divisor)
        genDivModCore(op == "%", isUnsigned);
        return;
    }
    if (op == "&" || op == "|" || op == "^") {
        const char *insn = (op == "&") ? "and" : (op == "|") ? "ora" : "eor";
        emit("lda", "ac");
        emit(insn, "bc");
        emit("sta", "ac");
        emit("lda", "ac+1");
        emit(insn, "bc+1");
        emit("sta", "ac+1");
        return;
    }
    // "<<" or ">>": bc's low byte is the shift count; bc is dead afterward.
    emit("lda", "bc");
    emit("sta", "cx");
    genShiftCore(op == "<<", isUnsigned);
}

// `ptr` must already hold the resolved address on entry. e.rhs's own
// evaluation may clobber ptr/ac/bc/cx/dx (nested calls, etc.), so `ptr` is
// stashed on the hardware stack across it.
void M6502Codegen::genCompoundThroughPtr(const ir::Expr &e) {
    // Stash address.
    emit("lda", "ptr");
    emit("pha");
    emit("lda", "ptr+1");
    emit("pha");
    genExpr(*e.rhs);     // ac = rhs
    copy16("bc", "ac");  // bc = rhs - untouched by the address restore/old-load below
    // ptr restored.
    emit("pla");
    emit("sta", "ptr+1");
    emit("pla");
    emit("sta", "ptr");
    loadOperandAtPtr(e.type);  // ac = old
    if (e.yieldOld) {          // stash old - this expr's own result
        emit("lda", "ac");
        emit("pha");
        emit("lda", "ac+1");
        emit("pha");
    }
    genCompoundOp(e.op, e.type);  // ac = new (raw), using ac=old, bc=rhs
    narrowAcTo(e.type);
    storeOperandAtPtr(e.type);  // ptr is still valid - genCompoundOp()/narrowAcTo() never touch it
    if (e.yieldOld) {           // ac = old (final result)
        emit("pla");
        emit("sta", "ac+1");
        emit("pla");
        emit("sta", "ac");
    }
}

void M6502Codegen::inc16(const std::string &lbl) {
    std::string Lskip = newLabel();
    emit("inc", lbl);
    emit("lda", lbl);
    emit("bne", Lskip);
    emit("inc", lbl + "+1");
    emitLabel(Lskip);
}

std::string M6502Codegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    // Real separate compilation means `prog` may not define `main` at all.
    // `objectMode` means no object file emits this entry sequence; the
    // linker synthesizes exactly one once every object is merged. An
    // object file's base is `org 0` - placement is the linker's job.
    emitRaw("org " + std::to_string(objectMode ? 0 : baseAddress()));
    emitRaw("");
    // Fixed zero-page pseudo-registers, bound via compile-time equate (not
    // the usual data-trailer layout) so every object declares them
    // identically regardless of where it's linked, with no relocation
    // needed for the equate itself. `fp`/`sp`: the software frame pointer
    // and the frame-stack bump allocator. `wac`/`wbc`/`wtmp`/`wtmp2`: the
    // 8-byte wide-value buffers. See zeroPageBase()'s own comment for why
    // this isn't always $0000.
    {
        uint32_t zp = zeroPageBase();
        emitEquate("ptr", zp);
        emitEquate("ac", zp + 2);
        emitEquate("bc", zp + 4);
        emitEquate("cx", zp + 6);
        emitEquate("dx", zp + 8);
        emitEquate("fp", zp + 10);
        emitEquate("sp", zp + 12);
        emitEquate("aBase", zp + 14);
        emitEquate("wac", zp + 16);
        emitEquate("wbc", zp + 24);
        emitEquate("wtmp", zp + 32);
        emitEquate("wtmp2", zp + 40);
        emitRaw("");
    }
    if (!objectMode) {
        emitLabel("_start");
        // One-time init of the frame-stack bump allocator before the first
        // call; genFunction()'s prologue only reads/advances `sp` after this.
        uint32_t fsb = frameStackBase();
        emit("ldai", fsb & 0xFF);
        emit("sta", "sp");
        emit("ldai", (fsb >> 8) & 0xFF);
        emit("sta", "sp+1");
        emit("jsr", label(mainFn));
        // The program's final result sits in ac/ac+1 at `halt` below. This
        // extra `lda` also leaves the low byte in the real A register.
        emit("lda", "ac");
        emitLabel("halt");
        emit("jmp", "halt");
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
        // An external global isn't defined in this file; it's only
        // referenced by name, letting the assembler defer it to a relocation.
        if (slot->isExternal) continue;
        size_t gTrailerStart = trailer_.size();
        if (slot->hasAddress) trailer_.push_back(placementLine(slot->address));
        const std::string lbl = label(slot.get());
        if (debugMapEnabled_) debugSymbols_.push_back({lbl, /*isFunc=*/false});
        if (slot->arraySize > 0) {
            declareArray(lbl, slot->arrayInit, slot->type);
        } else {
            long v = slot->hasInit ? slot->initValue : 0;
            auto uv = static_cast<unsigned long>(v);
            int w = widthOfType(slot->type);
            trailer_.push_back(lbl + ": db " + std::to_string(uv & 0xFF) + "  ; " + slot->sourceName);
            for (int i = 1; i < w; i++) trailer_.push_back("db " + std::to_string((uv >> (8 * i)) & 0xFF));
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

// Single-file-only (no `--format=coff` for this target), so the whole
// generated text is attributed to whichever file defines `main`.
std::map<std::string, std::string> M6502Codegen::perFileAsm() const { return perFileAsm_; }

// `paramBytes` is the caller's already-reserved argument-area size;
// subtracting it off `sp` makes `fp` land on argument 0's cell. There's no
// immediate SBC on this target, so the subtraction is addition of the
// two's-complement, exact under mod-65536 wraparound.
void M6502Codegen::genFunction(const ir::FunctionBody &fn) {
    curFunction_ = fn.func;
    lastDebugLine_ = 0;
    if (debugMapEnabled_) debugSymbols_.push_back({label(fn.func), /*isFunc=*/true});
    if (fn.func->hasAddress) emitRaw(placementLine(fn.func->address));
    emitLabel(label(fn.func));
    // Every param reserves at least a native int's width (2 bytes); a wider
    // type reserves its own width - the sum is the caller's argument-area size.
    int paramBytes = 0;
    for (const ir::Slot *p : fn.func->params) paramBytes += std::max(widthOfType(p->type), 2);
    int localBytes = fn.func->frameSize;
    // Save the caller's `fp` on the hardware stack; restored by genEpilogue().
    emit("lda", "fp");
    emit("pha");
    emit("lda", "fp+1");
    emit("pha");
    if (paramBytes > 0) {
        int negParamBytes = (0x10000 - paramBytes) & 0xFFFF;
        emit("clc");
        emit("lda", "sp");
        emit("adci", negParamBytes & 0xFF);
        emit("sta", "fp");
        emit("lda", "sp+1");
        emit("adci", (negParamBytes >> 8) & 0xFF);
        emit("sta", "fp+1");
    } else {
        emit("lda", "sp");
        emit("sta", "fp");
        emit("lda", "sp+1");
        emit("sta", "fp+1");
    }
    if (localBytes > 0) {
        emit("clc");
        emit("lda", "sp");
        emit("adci", localBytes & 0xFF);
        emit("sta", "sp");
        emit("lda", "sp+1");
        emit("adci", (localBytes >> 8) & 0xFF);
        emit("sta", "sp+1");
    }
    genStmt(*fn.body);
    genEpilogue();
    emitRaw("");
}

void M6502Codegen::genEpilogue() {
    // sp = fp: Params and Locals collapse back to this call's frame base.
    emit("lda", "fp");
    emit("sta", "sp");
    emit("lda", "fp+1");
    emit("sta", "sp+1");
    emit("pla");  // restore caller's fp
    emit("sta", "fp+1");
    emit("pla");
    emit("sta", "fp");
    emit("rts");
}

int M6502Codegen::frameByteOffset(const ir::Slot *slot) const {
    if (slot->kind == ir::SlotKind::Param) {
        // IRBuilder's frameOffset for a Param is counted past a fixed
        // two-native-int reservation; `fp` here lands on the first
        // argument instead, so that reservation is subtracted back off.
        return slot->frameOffset - 2 * widthOfType(Type{BaseType::Int, false});
    }
    // Locals start right after params, so the first local's frameOffset
    // (one native int's width) must land there, not one int further on.
    int paramBytes = 0;
    for (const ir::Slot *p : curFunction_->params) paramBytes += std::max(widthOfType(p->type), 2);
    return paramBytes + slot->frameOffset - widthOfType(Type{BaseType::Int, false});
}

void M6502Codegen::setPtrToFrameSlot(const ir::Slot *slot, int extraByteOffset) {
    int off = frameByteOffset(slot) + extraByteOffset;
    emit("clc");
    emit("lda", "fp");
    emit("adci", off & 0xFF);
    emit("sta", "ptr");
    emit("lda", "fp+1");
    emit("adci", (off >> 8) & 0xFF);
    emit("sta", "ptr+1");
}

void M6502Codegen::loadSlotToAc(const ir::Slot *slot) {
    int w = widthOfType(slot->type);
    if (w == 1) {
        if (slot->kind == ir::SlotKind::Global) {
            emit("lda", label(slot));
            emit("sta", "ac");
        } else {
            setPtrToFrameSlot(slot, 0);
            emit("ldyi", 0);
            emit("ldaind", "ptr");
            emit("sta", "ac");
        }
        if (slot->type.isUnsigned) {
            emit("ldai", 0);
            emit("sta", "ac+1");
            return;
        }
        // No "test bit 7" instruction in this dialect, so the sign bit is
        // extracted into the carry via ASL on a throwaway copy (cx).
        emit("lda", "ac");
        emit("sta", "cx");
        emit("asl", "cx");
        std::string Lneg = newLabel(), Ldone = newLabel();
        emit("ldai", 0);
        emit("sta", "ac+1");
        safeBranch("bcs", Lneg, Ldone);
        emitLabel(Lneg);
        emit("ldai", 255);
        emit("sta", "ac+1");
        emitLabel(Ldone);
        return;
    }
    if (slot->kind == ir::SlotKind::Global) {
        copy16("ac", label(slot));
        return;
    }
    setPtrToFrameSlot(slot, 0);
    emit("ldyi", 0);
    emit("ldaind", "ptr");
    emit("sta", "ac");
    emit("ldyi", 1);
    emit("ldaind", "ptr");
    emit("sta", "ac+1");
}

void M6502Codegen::storeAcToSlot(const ir::Slot *slot) {
    if (widthOfType(slot->type) == 1) {
        if (slot->kind == ir::SlotKind::Global) {
            emit("lda", "ac");
            emit("sta", label(slot));
        } else {
            setPtrToFrameSlot(slot, 0);
            emit("ldyi", 0);
            emit("lda", "ac");
            emit("staind", "ptr");
        }
        return;
    }
    if (slot->kind == ir::SlotKind::Global) {
        copy16(label(slot), "ac");
        return;
    }
    setPtrToFrameSlot(slot, 0);
    emit("ldyi", 0);
    emit("lda", "ac");
    emit("staind", "ptr");
    emit("ldyi", 1);
    emit("lda", "ac+1");
    emit("staind", "ptr");
}

// Every call re-initializes the array, since there's no static trailer for
// Local storage; element i's cell is the array's frame cell plus i*2 bytes.
void M6502Codegen::genLocalArrayInit(const ir::Slot *slot) {
    int w = widthOfType(slot->type);
    for (size_t i = 0; i < slot->arrayInit.size(); i++) {
        const ir::ArrayInitElem &elem = slot->arrayInit[i];
        setPtrToFrameSlot(slot, w * static_cast<int>(i));
        if (w <= 2) {
            std::string text = arrayElemText(elem);
            emit("ldai", text);
            emit("sta", "ac");
            emit("ldyi", 0);
            emit("lda", "ac");
            emit("staind", "ptr");
            if (w == 2) {
                emit("ldai_hi", text);
                emit("sta", "ac+1");
                emit("ldyi", 1);
                emit("lda", "ac+1");
                emit("staind", "ptr");
            }
        } else {
            // A wide element's literal is never a symbol - split directly
            // into `w` plain bytes, least significant first.
            auto uv = static_cast<unsigned long>(elem.value);
            for (int b = 0; b < w; b++) {
                emit("ldai", (uv >> (8 * b)) & 0xFF);
                emit("sta", "ac");
                emit("ldyi", b);
                emit("lda", "ac");
                emit("staind", "ptr");
            }
        }
    }
}

void M6502Codegen::genStmt(const ir::Stmt &s) {
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
            if (s.initOrReturnOrExpr) genExpr(*s.initOrReturnOrExpr);
            genEpilogue();  // early exit: genFunction()'s fall-through epilogue doesn't run here
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
                    storeAcToSlot(s.declSlot);
                }
            }
            // No "else" case: an uninitialized scalar's frame cell already
            // exists (reserved by genFunction()'s `sp` bump).
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

void M6502Codegen::genExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target");
        case ir::ExprKind::IntLit: {
            if (isWide(e.type)) {
                genWideLoadLiteral(e.intValue, wordCountOf(e.type));
                return;
            }
            uint16_t v16 = static_cast<uint16_t>(e.intValue);
            emit("ldai", v16 & 0xFF);
            emit("sta", "ac");
            emit("ldai", (v16 >> 8) & 0xFF);
            emit("sta", "ac+1");
            return;
        }
        case ir::ExprKind::Var: {
            if (isWide(e.slot->type)) {
                genWideLoad(e.slot, wordCountOf(e.slot->type));
                return;
            }
            loadSlotToAc(e.slot);
            return;
        }
        case ir::ExprKind::Index: {
            genIndex(e);
            return;
        }
        case ir::ExprKind::Assign: {
            if (e.lhs) {  // slot[idx] = rhs
                int w = widthOfType(e.slot->type);
                genIndexAddr(e.slot, *e.lhs);  // ptr = target address
                // Save `ptr` across evaluating rhs, which might index
                // through `ptr` again.
                emit("lda", "ptr");
                emit("pha");
                emit("lda", "ptr+1");
                emit("pha");
                genExpr(*e.rhs);
                emit("pla");
                emit("sta", "ptr+1");
                emit("pla");
                emit("sta", "ptr");
                if (w > 2) {
                    genWideStoreAtPtr(wordCountOf(e.slot->type));
                } else {
                    emit("ldyi", 0);
                    emit("lda", "ac");
                    emit("staind", "ptr");
                    if (w == 2) {
                        emit("ldyi", 1);
                        emit("lda", "ac+1");
                        emit("staind", "ptr");
                    }
                }
                return;
            }
            genExpr(*e.rhs);
            if (isWide(e.slot->type)) {
                genWideStore(e.slot, wordCountOf(e.slot->type));
            } else {
                storeAcToSlot(e.slot);
            }
            return;
        }
        case ir::ExprKind::DerefAssign: {
            // Stores at e.type's width/signedness: the pointee type of a
            // typed pointer, or a plain native word for untyped-int-as-address.
            genExpr(*e.lhs);  // ac = pointer value
            copy16("ptr", "ac");
            emit("lda", "ptr");
            emit("pha");
            emit("lda", "ptr+1");
            emit("pha");
            genExpr(*e.rhs);
            emit("pla");
            emit("sta", "ptr+1");
            emit("pla");
            emit("sta", "ptr");
            storeOperandAtPtr(e.type);
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            if (e.lhs) {  // slot[idx] op= rhs (including slot[idx]++/--)
                genIndexAddr(e.slot, *e.lhs);  // ptr = target address
                genCompoundThroughPtr(e);
                return;
            }
            // Plain scalar target: its storage (fixed label or
            // fp+frameByteOffset()) is reconstructible after rhs evaluates,
            // since `fp` is restored by any nested call's epilogue.
            genExpr(*e.rhs);     // ac = rhs
            copy16("bc", "ac");  // bc = rhs
            loadSlotToAc(e.slot);  // ac = old
            if (e.yieldOld) {
                emit("lda", "ac");
                emit("pha");
                emit("lda", "ac+1");
                emit("pha");
            }
            genCompoundOp(e.op, e.type);
            narrowAcTo(e.type);
            storeAcToSlot(e.slot);
            if (e.yieldOld) {
                emit("pla");
                emit("sta", "ac+1");
                emit("pla");
                emit("sta", "ac");
            }
            return;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            genExpr(*e.lhs);     // ac = pointer value
            copy16("ptr", "ac");  // ptr = address
            genCompoundThroughPtr(e);
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
            bool wide = isWide(e.rhs->type);
            if (e.op == "-") {
                genExpr(*e.rhs);
                if (wide) {
                    negateWideBuffer("wac", 2 * wordCountOf(e.rhs->type));
                } else {
                    emit("ldai", 0);
                    emit("sec");
                    emit("sbc", "ac");
                    emit("sta", "ac");
                    emit("ldai", 0);
                    emit("sbc", "ac+1");
                    emit("sta", "ac+1");
                }
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                // Loads at e.type's width/signedness (see DerefAssign above).
                genExpr(*e.rhs);  // ac = pointer value
                copy16("ptr", "ac");
                loadOperandAtPtr(e.type);
            } else if (e.op == "~") {
                genExpr(*e.rhs);
                if (wide) {
                    int n = 2 * wordCountOf(e.rhs->type);
                    for (int i = 0; i < n; i++) {
                        std::string b = i == 0 ? "wac" : ("wac+" + std::to_string(i));
                        emit("lda", b);
                        emit("eori", 255);
                        emit("sta", b);
                    }
                } else {
                    emit("lda", "ac");
                    emit("eori", 255);
                    emit("sta", "ac");
                    emit("lda", "ac+1");
                    emit("eori", 255);
                    emit("sta", "ac+1");
                }
            } else {  // "&"
                genAddrOf(*e.rhs);
            }
            return;
        }
        case ir::ExprKind::Call: {
            // Every argument is evaluated and parked on the hardware stack
            // first, before any `sp`/`aBase` use, so a nested call in a
            // later argument can't invalidate an earlier one's computed
            // address. Each reserves at least a native int's width (2
            // bytes); a wider type reserves its own width - mirroring
            // genFunction()'s `paramBytes` on the callee side.
            std::vector<int> argWidths(e.args.size());
            int totalBytes = 0;
            for (size_t i = 0; i < e.args.size(); i++) {
                genExpr(*e.args[i]);
                int w = std::max(widthOfType(e.args[i]->type), 2);
                argWidths[i] = w;
                totalBytes += w;
                if (w > 2) {
                    pushWide("wac", w);
                } else {
                    emit("lda", "ac");
                    emit("pha");
                    emit("lda", "ac+1");
                    emit("pha");
                }
            }
            if (!e.args.empty()) {
                // aBase = sp (this call's argument area, becoming the
                // callee's `fp` once its prologue subtracts this size off).
                emit("lda", "sp");
                emit("sta", "aBase");
                emit("lda", "sp+1");
                emit("sta", "aBase+1");
                emit("clc");
                emit("lda", "sp");
                emit("adci", totalBytes & 0xFF);
                emit("sta", "sp");
                emit("lda", "sp+1");
                emit("adci", (totalBytes >> 8) & 0xFF);
                emit("sta", "sp+1");
                std::vector<int> offsets(e.args.size());
                int running = 0;
                for (size_t i = 0; i < e.args.size(); i++) {
                    offsets[i] = running;
                    running += argWidths[i];
                }
                // Unpark LIFO (last-pushed argument comes off first) into
                // its own cell, aBase+offsets[i].
                for (size_t i = e.args.size(); i-- > 0;) {
                    int w = argWidths[i];
                    if (w > 2) {
                        popWide("wac", w);
                    } else {
                        emit("pla");
                        emit("sta", "ac+1");
                        emit("pla");
                        emit("sta", "ac");
                    }
                    int off = offsets[i];
                    emit("clc");
                    emit("lda", "aBase");
                    emit("adci", off & 0xFF);
                    emit("sta", "ptr");
                    emit("lda", "aBase+1");
                    emit("adci", (off >> 8) & 0xFF);
                    emit("sta", "ptr+1");
                    if (w > 2) {
                        genWideStoreAtPtr(w / 2);
                    } else {
                        emit("ldyi", 0);
                        emit("lda", "ac");
                        emit("staind", "ptr");
                        emit("ldyi", 1);
                        emit("lda", "ac+1");
                        emit("staind", "ptr");
                    }
                }
            }
            emit("jsr", label(e.func));
            // A wide return value is wherever the callee's `return`
            // expression left it (`wac`, untouched by genEpilogue()).
            return;
        }
        case ir::ExprKind::CallPtr: {
            // e.args is always empty: there's no per-callee p_<fn>_<i> cell
            // to write into until the callee is known.
            //
            // The 6502 has no indirect-call instruction, only indirect JMP
            // ("jmpi", opcode $6C). A call through a computed address is
            // built by hand from JSR's return-address convention: JSR
            // pushes (return address - 1), RTS adds 1 back after popping.
            // Pushing (Lret-1) the same way, then jumping indirectly
            // through `ptr`, leaves the stack exactly as a real `jsr`
            // would, so `rts` returns to right after "jmpi ptr".
            genExpr(*e.lhs);  // ac = the callee's own entry address
            copy16("ptr", "ac");
            std::string Lret = newLabel();
            emit("ldai_hi", Lret + "-1");
            emit("pha");
            emit("ldai", Lret + "-1");
            emit("pha");
            emit("jmpi", "ptr");
            emitLabel(Lret);
            return;
        }
        case ir::ExprKind::Binary: {
            genBinary(e);
            return;
        }
        case ir::ExprKind::Intrinsic: {
            // SEI sets the interrupt-disable flag, CLI clears it.
            emit(e.op == "ion" ? "cli" : "sei");
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

// `slot->arraySize > 0` means the base is a compile-time-known label
// (an address, split into low/high bytes via `ldai`/`ldai_hi`);
// otherwise `slot` is a pointer variable, so the base comes from
// reading its stored value (an absolute-memory operand, not an immediate).
void M6502Codegen::genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
    genExpr(idxExpr);  // ac = index
    int shift = elemShift(slot->type);  // ac *= widthOf(slot->type), a power of two
    if (shift >= 0) {
        for (int i = 0; i < shift; i++) {
            emit("asl", "ac");
            emit("rol", "ac+1");
        }
    } else {
        // Non-power-of-two element width (a struct's): scale via the shared software
        // multiply routine instead - see genMulCore()'s own comment (bc * dx -> ac).
        emit("lda", "ac");
        emit("sta", "bc");
        emit("lda", "ac+1");
        emit("sta", "bc+1");
        emit("ldai", widthOfType(slot->type));
        emit("sta", "dx");
        emit("ldai", 0);
        emit("sta", "dx+1");
        genMulCore();
    }
    if (slot->kind == ir::SlotKind::Global) {
        std::string lbl = label(slot);
        emit("clc");
        if (slot->arraySize > 0) {
            emit("lda", "ac");
            emit("adci", lbl);
            emit("sta", "ptr");
            emit("lda", "ac+1");
            emit("adci_hi", lbl);
            emit("sta", "ptr+1");
        } else {
            emit("lda", "ac");
            emit("adc", lbl);
            emit("sta", "ptr");
            emit("lda", "ac+1");
            emit("adc", lbl + "+1");
            emit("sta", "ptr+1");
        }
        return;
    }
    // Local/Param: set `ptr` to fp+frameByteOffset() first; `ac` already
    // holds the scaled index.
    setPtrToFrameSlot(slot, 0);
    if (slot->arraySize > 0) {
        // `ptr` IS the array's first word - base-plus-scaled-index.
        emit("clc");
        emit("lda", "ac");
        emit("adc", "ptr");
        emit("sta", "ptr");
        emit("lda", "ac+1");
        emit("adc", "ptr+1");
        emit("sta", "ptr+1");
    } else {
        // slot is a pointer variable stored at that cell - read its value
        // (through `ptr`, still the cell's address) and add the index to that.
        emit("ldyi", 0);
        emit("ldaind", "ptr");
        emit("sta", "bc");
        emit("ldyi", 1);
        emit("ldaind", "ptr");
        emit("sta", "bc+1");
        emit("clc");
        emit("lda", "ac");
        emit("adc", "bc");
        emit("sta", "ptr");
        emit("lda", "ac+1");
        emit("adc", "bc+1");
        emit("sta", "ptr+1");
    }
}

void M6502Codegen::genIndex(const ir::Expr &e) {
    genIndexAddr(e.slot, *e.lhs);
    int w = widthOfType(e.slot->type);
    if (w > 2) {
        genWideLoadAtPtr(wordCountOf(e.slot->type));
        return;
    }
    if (w == 1) {
        emit("ldyi", 0);
        emit("ldaind", "ptr");
        emit("sta", "ac");
        if (e.slot->type.isUnsigned) {
            emit("ldai", 0);
            emit("sta", "ac+1");
            return;
        }
        emit("lda", "ac");
        emit("sta", "cx");
        emit("asl", "cx");
        std::string Lneg = newLabel(), Ldone = newLabel();
        emit("ldai", 0);
        emit("sta", "ac+1");
        safeBranch("bcs", Lneg, Ldone);
        emitLabel(Lneg);
        emit("ldai", 255);
        emit("sta", "ac+1");
        emitLabel(Ldone);
        return;
    }
    emit("ldyi", 0);
    emit("ldaind", "ptr");
    emit("sta", "ac");
    emit("ldyi", 1);
    emit("ldaind", "ptr");
    emit("sta", "ac+1");
}

void M6502Codegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        if (operand.slot->kind == ir::SlotKind::Global) {
            std::string lbl = label(operand.slot);
            emit("ldai", lbl);
            emit("sta", "ac");
            emit("ldai_hi", lbl);
            emit("sta", "ac+1");
        } else {
            setPtrToFrameSlot(operand.slot, 0);
            copy16("ac", "ptr");
        }
        return;
    }
    if (operand.kind == ir::ExprKind::Index) {
        genIndexAddr(operand.slot, *operand.lhs);
        copy16("ac", "ptr");
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        std::string lbl = label(operand.func);
        emit("ldai", lbl);
        emit("sta", "ac");
        emit("ldai_hi", lbl);
        emit("sta", "ac+1");
        return;
    }
    err(operand.line, "internal: unhandled addressable-operand kind");
}

// An implicit or explicit type conversion (see IR.h's own ExprKind::
// Every arithmetic/comparison operand narrower than `int` is already
// promoted by IRBuilder, so the real work here is narrowing/widening a
// `char` and crossing the native-word/wide boundary (padding `wac` with
// sign/zero-extension bytes, or leaving the low bytes alone when shrinking).
void M6502Codegen::genConvert(const ir::Expr &e) {
    Type from = e.lhs->type;
    Type to = e.type;
    bool fromWide = isWide(from);
    bool toWide = isWide(to);
    if (!fromWide && !toWide) {
        genExpr(*e.lhs);  // ac
        if (widthOfType(to) == 1) {
            if (to.isUnsigned) {
                emit("ldai", 0);
                emit("sta", "ac+1");
            } else {
                emit("lda", "ac");
                emit("sta", "cx");
                emit("asl", "cx");
                std::string Lneg = newLabel(), Ldone = newLabel();
                emit("ldai", 0);
                emit("sta", "ac+1");
                safeBranch("bcs", Lneg, Ldone);
                emitLabel(Lneg);
                emit("ldai", 255);
                emit("sta", "ac+1");
                emitLabel(Ldone);
            }
        }
        return;
    }
    if (fromWide && !toWide) {
        genExpr(*e.lhs);  // wac holds `wordCountOf(from)` words
        emit("lda", byteAt("wac", 0));
        emit("sta", "ac");
        emit("lda", byteAt("wac", 1));
        emit("sta", "ac+1");
        if (widthOfType(to) == 1) {
            if (to.isUnsigned) {
                emit("ldai", 0);
                emit("sta", "ac+1");
            } else {
                emit("lda", "ac");
                emit("sta", "cx");
                emit("asl", "cx");
                std::string Lneg = newLabel(), Ldone = newLabel();
                emit("ldai", 0);
                emit("sta", "ac+1");
                safeBranch("bcs", Lneg, Ldone);
                emitLabel(Lneg);
                emit("ldai", 255);
                emit("sta", "ac+1");
                emitLabel(Ldone);
            }
        }
        return;
    }
    if (!fromWide && toWide) {
        int toWords = wordCountOf(to);
        genExpr(*e.lhs);  // ac, already correctly sign/zero-extended to 16 bits
        emit("lda", "ac");
        emit("sta", byteAt("wac", 0));
        emit("lda", "ac+1");
        emit("sta", byteAt("wac", 1));
        if (toWords > 1) {
            // Sign/zero-extend per the source's signedness. A pointer value
            // is always zero-extended: `from.isUnsigned` would otherwise
            // describe the pointee's signedness, not the pointer's own.
            if (isPointer(from) || from.isUnsigned) {
                emit("ldai", 0);
                emit("sta", "dx");
            } else {
                emit("lda", "ac+1");
                emit("sta", "cx");
                emit("asl", "cx");
                std::string Lneg = newLabel(), Ldone = newLabel();
                emit("ldai", 0);
                emit("sta", "dx");
                safeBranch("bcc", Ldone, Lneg);
                emitLabel(Lneg);
                emit("ldai", 255);
                emit("sta", "dx");
                emitLabel(Ldone);
            }
            for (int i = 2; i < 2 * toWords; i++) {
                emit("lda", "dx");
                emit("sta", byteAt("wac", i));
            }
        }
        return;
    }
    // fromWide && toWide: pad/truncate word count, preserving sign.
    int fromN = 2 * wordCountOf(from);
    int toN = 2 * wordCountOf(to);
    genExpr(*e.lhs);  // wac holds `fromN` bytes
    if (toN > fromN) {
        if (from.isUnsigned) {
            emit("ldai", 0);
            emit("sta", "dx");
        } else {
            emit("lda", byteAt("wac", fromN - 1));
            emit("sta", "cx");
            emit("asl", "cx");
            std::string Lneg = newLabel(), Ldone = newLabel();
            emit("ldai", 0);
            emit("sta", "dx");
            safeBranch("bcc", Ldone, Lneg);
            emitLabel(Lneg);
            emit("ldai", 255);
            emit("sta", "dx");
            emitLabel(Ldone);
        }
        for (int i = fromN; i < toN; i++) {
            emit("lda", "dx");
            emit("sta", byteAt("wac", i));
        }
    }
    // toN < fromN: the low `toN` bytes already in `wac` are the truncated value.
}

// Pushes lhs across evaluating rhs, leaving wbc=lhs, wac=rhs, then combines
// byte by byte with a single carry chain (CLC/SEC once; ADC/SBC fold in the
// previous byte's carry automatically) into `wac`.
void M6502Codegen::genWideAdd(const ir::Expr &lhs, const ir::Expr &rhs, bool isAdd, int words) {
    int n = 2 * words;
    genExpr(lhs);
    pushWide("wac", n);
    genExpr(rhs);
    popWide("wbc", n);  // wbc = lhs, wac = rhs
    emit(isAdd ? "clc" : "sec");
    for (int i = 0; i < n; i++) {
        emit("lda", byteAt("wbc", i));
        emit(isAdd ? "adc" : "sbc", byteAt("wac", i));
        emit("sta", byteAt("wac", i));
    }
}

void M6502Codegen::genWideBitwise(const ir::Expr &lhs, const ir::Expr &rhs, const std::string &op, int words) {
    int n = 2 * words;
    genExpr(lhs);
    pushWide("wac", n);
    genExpr(rhs);
    popWide("wbc", n);  // wbc = lhs, wac = rhs
    const char *insn = (op == "&") ? "and" : (op == "|") ? "ora" : "eor";
    for (int i = 0; i < n; i++) {
        emit("lda", byteAt("wbc", i));
        emit(insn, byteAt("wac", i));
        emit("sta", byteAt("wac", i));
    }
}

// XORs every byte pair together (into `dx`, ORed across bytes), zero iff equal.
void M6502Codegen::emitWideEqualityCompare(const ir::Expr &lhs, const ir::Expr &rhs, int words, bool wantEqual) {
    int n = 2 * words;
    genExpr(lhs);
    pushWide("wac", n);
    genExpr(rhs);
    popWide("wbc", n);  // wbc = lhs, wac = rhs
    emit("lda", byteAt("wbc", 0));
    emit("eor", byteAt("wac", 0));
    emit("sta", "dx");
    for (int i = 1; i < n; i++) {
        emit("lda", byteAt("wbc", i));
        emit("eor", byteAt("wac", i));
        emit("ora", "dx");
        emit("sta", "dx");
    }
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    emit("lda", "dx");
    safeBranch(wantEqual ? "beq" : "bne", Ltrue, Lfalse);
    emitLabel(Ltrue);
    emit("ldai", 1);
    emit("sta", "ac");
    emit("ldai", 0);
    emit("sta", "ac+1");
    emit("jmp", Ldone);
    emitLabel(Lfalse);
    emit("ldai", 0);
    emit("sta", "ac");
    emit("ldai", 0);
    emit("sta", "ac+1");
    emitLabel(Ldone);
}

// One chained SBC from least to most significant byte computes the full
// subtraction in one pass; the final byte's flags alone decide the
// comparison, with the same N-xor-V overflow fixup as emitLessCompare().
void M6502Codegen::emitWideLessCompare(const ir::Expr &a, const ir::Expr &b, bool isUnsigned, int words,
                                        bool invert) {
    int n = 2 * words;
    genExpr(a);
    pushWide("wac", n);
    genExpr(b);
    popWide("wbc", n);  // wbc = a, wac = b
    emit("sec");
    for (int i = 0; i < n; i++) {
        emit("lda", byteAt("wbc", i));
        emit("sbc", byteAt("wac", i));
    }
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    if (isUnsigned) {
        safeBranch(invert ? "bcs" : "bcc", Ltrue, Lfalse);  // carry alone already means unsigned "<"
    } else {
        std::string Lfixskip = newLabel();
        emit("bvc", Lfixskip);
        emit("eori", 128);
        emitLabel(Lfixskip);
        safeBranch(invert ? "bpl" : "bmi", Ltrue, Lfalse);
    }
    emitLabel(Ltrue);
    emit("ldai", 1);
    emit("sta", "ac");
    emit("ldai", 0);
    emit("sta", "ac+1");
    emit("jmp", Ldone);
    emitLabel(Lfalse);
    emit("ldai", 0);
    emit("sta", "ac");
    emit("ldai", 0);
    emit("sta", "ac+1");
    emitLabel(Ldone);
}

void M6502Codegen::genWideCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned,
                                   int words) {
    if (op == "==") return emitWideEqualityCompare(lhs, rhs, words, /*wantEqual=*/true);
    if (op == "!=") return emitWideEqualityCompare(lhs, rhs, words, /*wantEqual=*/false);
    if (op == "<") return emitWideLessCompare(lhs, rhs, isUnsigned, words, /*invert=*/false);
    if (op == ">") return emitWideLessCompare(rhs, lhs, isUnsigned, words, /*invert=*/false);
    if (op == "<=") return emitWideLessCompare(rhs, lhs, isUnsigned, words, /*invert=*/true);
    if (op == ">=") return emitWideLessCompare(lhs, rhs, isUnsigned, words, /*invert=*/true);
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

// Loops one bit at a time, chaining carry across all `n` bytes via ASL/ROL
// (left) or LSR/ROR (right, highest byte first so carry flows downward). A
// signed right shift re-derives the sign bit into carry before the top
// byte's ROR instead of LSR's fixed 0-fill (using `dx` as scratch, since
// `cx` holds the loop count and every byte of `wac` is live).
void M6502Codegen::genWideShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned, int words) {
    int n = 2 * words;
    genExpr(lhs);
    pushWide("wac", n);
    genExpr(rhs);  // ac = shift count (already promoted to a plain int)
    emit("lda", "ac");
    emit("sta", "cx");
    popWide("wac", n);
    std::string Lloop = newLabel(), Lbody = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("lda", "cx");
    safeBranch("beq", Ldone, Lbody);
    emitLabel(Lbody);
    if (isLeft) {
        emit("asl", byteAt("wac", 0));
        for (int i = 1; i < n; i++) emit("rol", byteAt("wac", i));
    } else if (isUnsigned) {
        emit("lsr", byteAt("wac", n - 1));
        for (int i = n - 2; i >= 0; i--) emit("ror", byteAt("wac", i));
    } else {
        emit("lda", byteAt("wac", n - 1));
        emit("sta", "dx");
        emit("asl", "dx");
        emit("ror", byteAt("wac", n - 1));
        for (int i = n - 2; i >= 0; i--) emit("ror", byteAt("wac", i));
    }
    emit("dec", "cx");
    emit("jmp", Lloop);
    emitLabel(Ldone);
}

// Shift-and-add multiply, `n*8` bits serially: `wbc` is the multiplicand
// (shifted left each iteration, truncated mod 2^(8n)), `wac` the multiplier
// (shifted right, consumed one bit at a time), `wtmp` the product. Needs no
// signedness: a truncated product's bit pattern is the same either way.
void M6502Codegen::genWideMul(const ir::Expr &lhs, const ir::Expr &rhs, int words) {
    int n = 2 * words;
    genExpr(lhs);
    pushWide("wac", n);
    genExpr(rhs);
    popWide("wbc", n);  // wbc = multiplicand, wac = multiplier
    for (int i = 0; i < n; i++) {
        emit("ldai", 0);
        emit("sta", byteAt("wtmp", i));
    }
    emit("ldai", 8 * n);
    emit("sta", "cx");
    std::string Lloop = newLabel(), Lbody = newLabel(), Ldone = newLabel();
    std::string Ladd = newLabel(), Lskipadd = newLabel();
    emitLabel(Lloop);
    emit("lda", "cx");
    safeBranch("beq", Ldone, Lbody);
    emitLabel(Lbody);
    emit("lda", byteAt("wac", 0));
    emit("andi", 1);
    safeBranch("bne", Ladd, Lskipadd);
    emitLabel(Ladd);
    emit("clc");
    for (int i = 0; i < n; i++) {
        emit("lda", byteAt("wtmp", i));
        emit("adc", byteAt("wbc", i));
        emit("sta", byteAt("wtmp", i));
    }
    emitLabel(Lskipadd);
    emit("asl", byteAt("wbc", 0));
    for (int i = 1; i < n; i++) emit("rol", byteAt("wbc", i));
    emit("lsr", byteAt("wac", n - 1));
    for (int i = n - 2; i >= 0; i--) emit("ror", byteAt("wac", i));
    emit("dec", "cx");
    emit("jmp", Lloop);
    emitLabel(Ldone);
    copyWide("wac", "wtmp", n);
}

// Binary long division, `n*8` bits serially: `wbc` and `wtmp` (running
// remainder) act as one combined `2n`-byte shift register - each iteration
// shifts the pair left by one bit, then tries `wtmp -= wac` (the divisor):
// no borrow means quotient bit 1 and the subtraction stands; otherwise bit
// 0 and `wac` is added back. Repeated subtraction (the narrow genDivMod()
// algorithm) would need up to 2^(8n) iterations, infeasible at 4/8 bytes.
//
// Signed division wraps this unsigned core: negate either operand's
// magnitude if it started negative (flags in `dx`/`dx+1`), then fix the
// quotient's sign (negative iff signs differed) and the remainder's
// (always the dividend's original sign) afterward.
void M6502Codegen::genWideDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned,
                                  int words) {
    int n = 2 * words;
    auto isNegByte = [&](const char *lblByte, const char *dst) {
        emit("lda", lblByte);
        emit("sta", "cx");
        emit("asl", "cx");
        std::string Lneg = newLabel(), Ldone = newLabel();
        emit("ldai", 0);
        emit("sta", dst);
        safeBranch("bcc", Ldone, Lneg);
        emitLabel(Lneg);
        emit("ldai", 1);
        emit("sta", dst);
        emitLabel(Ldone);
    };
    auto negateIfFlag = [&](const char *flag, const char *buf) {
        std::string Lskip = newLabel(), Ldo = newLabel();
        emit("lda", flag);
        safeBranch("beq", Lskip, Ldo);
        emitLabel(Ldo);
        negateWideBuffer(buf, n);
        emitLabel(Lskip);
    };
    genExpr(lhs);
    pushWide("wac", n);
    genExpr(rhs);
    popWide("wbc", n);  // wbc = dividend, wac = divisor
    if (!isUnsigned) {
        isNegByte(byteAt("wbc", n - 1).c_str(), "dx");
        isNegByte(byteAt("wac", n - 1).c_str(), "dx+1");
        negateIfFlag("dx", "wbc");
        negateIfFlag("dx+1", "wac");
    }
    for (int i = 0; i < n; i++) {
        emit("ldai", 0);
        emit("sta", byteAt("wtmp", i));
        emit("sta", byteAt("wtmp2", i));
    }
    emit("ldai", 8 * n);
    emit("sta", "cx");
    std::string Lloop = newLabel(), Lbody = newLabel(), Ldone = newLabel();
    std::string Lrestore = newLabel(), Lsetbit = newLabel(), Lshiftq = newLabel();
    emitLabel(Lloop);
    emit("lda", "cx");
    safeBranch("beq", Ldone, Lbody);
    emitLabel(Lbody);
    // Combined 1-bit left shift of (wtmp:wbc): wbc's outgoing top bit
    // becomes wtmp's new bottom bit via the carry chain.
    emit("asl", byteAt("wbc", 0));
    for (int i = 1; i < n; i++) emit("rol", byteAt("wbc", i));
    for (int i = 0; i < n; i++) emit("rol", byteAt("wtmp", i));
    // Trial subtraction: wtmp -= wac, always committed.
    emit("sec");
    for (int i = 0; i < n; i++) {
        emit("lda", byteAt("wtmp", i));
        emit("sbc", byteAt("wac", i));
        emit("sta", byteAt("wtmp", i));
    }
    safeBranch("bcs", Lsetbit, Lrestore);
    emitLabel(Lrestore);
    emit("clc");
    for (int i = 0; i < n; i++) {
        emit("lda", byteAt("wtmp", i));
        emit("adc", byteAt("wac", i));
        emit("sta", byteAt("wtmp", i));
    }
    emit("clc");
    emit("jmp", Lshiftq);
    emitLabel(Lsetbit);
    emit("sec");
    emitLabel(Lshiftq);
    for (int i = 0; i < n; i++) emit("rol", byteAt("wtmp2", i));
    emit("dec", "cx");
    emit("jmp", Lloop);
    emitLabel(Ldone);
    if (!isUnsigned) {
        emit("lda", "dx");
        emit("eor", "dx+1");
        std::string Lskip = newLabel(), Ldo = newLabel();
        safeBranch("beq", Lskip, Ldo);
        emitLabel(Ldo);
        negateWideBuffer("wtmp2", n);
        emitLabel(Lskip);
        negateIfFlag("dx", "wtmp");
    }
    copyWide("wac", wantRemainder ? "wtmp" : "wtmp2", n);
}

void M6502Codegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    // Every arithmetic/bitwise/comparison operand shares one common type
    // (IRBuilder's usual-arithmetic-conversion pass), except shift (only
    // LHS's type matters) and &&/|| (width-independent).
    if (op == "&&") {
        genLogicalAnd(*e.lhs, *e.rhs);
        return;
    }
    if (op == "||") {
        genLogicalOr(*e.lhs, *e.rhs);
        return;
    }
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
        emit("lda", "ac");
        emit("pha");
        emit("lda", "ac+1");
        emit("pha");
        genExpr(*e.rhs);
        emit("pla");
        emit("sta", "bc+1");
        emit("pla");
        emit("sta", "bc");  // bc = lhs
        if (op == "+") {
            emit("clc");
            emit("lda", "bc");
            emit("adc", "ac");
            emit("sta", "ac");
            emit("lda", "bc+1");
            emit("adc", "ac+1");
            emit("sta", "ac+1");
        } else {
            emit("sec");
            emit("lda", "bc");
            emit("sbc", "ac");
            emit("sta", "ac");
            emit("lda", "bc+1");
            emit("sbc", "ac+1");
            emit("sta", "ac+1");
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
        emit("lda", "ac");
        emit("pha");
        emit("lda", "ac+1");
        emit("pha");
        genExpr(*e.rhs);
        emit("pla");
        emit("sta", "bc+1");
        emit("pla");
        emit("sta", "bc");
        const char *insn = (op == "&") ? "and" : (op == "|") ? "ora" : "eor";
        emit("lda", "bc");
        emit(insn, "ac");
        emit("sta", "ac");
        emit("lda", "bc+1");
        emit(insn, "ac+1");
        emit("sta", "ac+1");
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
    if (wide) {
        genWideCompare(op, *e.lhs, *e.rhs, isUnsigned, words);
        return;
    }
    genCompare(op, *e.lhs, *e.rhs, isUnsigned);
}

// No variable-count shift instruction (ASL/LSR shift exactly one bit), so
// this loops `rhs` times instead.
void M6502Codegen::genShiftCore(bool isLeft, bool isUnsigned) {
    std::string Lloop = newLabel(), Lbody = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("lda", "cx");
    safeBranch("beq", Ldone, Lbody);
    emitLabel(Lbody);
    if (isLeft) {
        emit("asl", "ac");
        emit("rol", "ac+1");
    } else if (isUnsigned) {
        // Logical (unsigned) shift: fills with 0 either way.
        emit("lsr", "ac+1");
        emit("ror", "ac");
    } else {
        // Arithmetic (signed) shift: re-derive the sign bit (ac+1 bit 7)
        // into carry via a throwaway copy (bc+1, since ASL needs a memory
        // operand and would destroy ac+1), then rotate it back in at the top.
        emit("lda", "ac+1");
        emit("sta", "bc+1");
        emit("asl", "bc+1");
        emit("ror", "ac+1");
        emit("ror", "ac");
    }
    emit("dec", "cx");
    emit("jmp", Lloop);
    emitLabel(Ldone);
}

void M6502Codegen::genShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned) {
    genExpr(lhs);
    emit("lda", "ac");
    emit("pha");
    emit("lda", "ac+1");
    emit("pha");
    genExpr(rhs);
    emit("lda", "ac");
    emit("sta", "cx");  // cx = shift count (always small; low byte is enough)
    emit("pla");
    emit("sta", "bc+1");
    emit("pla");
    emit("sta", "bc");
    copy16("ac", "bc");  // ac = value to shift; bc is dead from here on (free as scratch)
    genShiftCore(isLeft, isUnsigned);
}

// Shift-and-add multiply: `bc` the multiplicand (shifted left each
// iteration), `dx` the multiplier (shifted right, consumed one bit at a
// time), `ac` the product. Only verified against non-negative operands.
void M6502Codegen::genMulCore() {
    emit("ldai", 0);
    emit("sta", "ac");
    emit("ldai", 0);
    emit("sta", "ac+1");  // ac = product = 0
    emit("ldai", 16);
    emit("sta", "cx");
    std::string Lloop = newLabel(), Lbody = newLabel(), Ldone = newLabel();
    std::string Ladd = newLabel(), Lskipadd = newLabel();
    emitLabel(Lloop);
    emit("lda", "cx");
    safeBranch("beq", Ldone, Lbody);
    emitLabel(Lbody);
    emit("lda", "dx");
    emit("andi", 1);
    safeBranch("bne", Ladd, Lskipadd);
    emitLabel(Ladd);
    emit("clc");
    emit("lda", "ac");
    emit("adc", "bc");
    emit("sta", "ac");
    emit("lda", "ac+1");
    emit("adc", "bc+1");
    emit("sta", "ac+1");
    emitLabel(Lskipadd);
    emit("asl", "bc");
    emit("rol", "bc+1");
    emit("lsr", "dx+1");
    emit("ror", "dx");
    emit("dec", "cx");
    emit("jmp", Lloop);
    emitLabel(Ldone);
}

void M6502Codegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    emit("lda", "ac");
    emit("pha");
    emit("lda", "ac+1");
    emit("pha");
    genExpr(rhs);
    copy16("dx", "ac");  // dx = multiplier
    emit("pla");
    emit("sta", "bc+1");
    emit("pla");
    emit("sta", "bc");  // bc = multiplicand
    genMulCore();
}

// Repeated subtraction: `dx` is the running remainder (starts as the
// dividend), `bc` the divisor, `ac` the quotient, counting how many times
// `bc` was subtracted before going negative. Only verified against
// non-negative operands.
void M6502Codegen::genDivModCore(bool wantRemainder, bool isUnsigned) {
    auto negate16 = [&](const char *lbl) {
        emit("ldai", 0);
        emit("sec");
        emit("sbc", lbl);
        emit("sta", lbl);
        emit("ldai", 0);
        emit("sbc", std::string(lbl) + "+1");
        emit("sta", std::string(lbl) + "+1");
    };
    // Leaves 0/1 in `dst` per `lbl`'s top (sign) bit.
    auto isNeg = [&](const char *lbl, const char *dst) {
        emit("lda", std::string(lbl) + "+1");
        emit("sta", "cx");
        emit("asl", "cx");
        std::string Lneg = newLabel(), Ldone = newLabel();
        emit("ldai", 0);
        emit("sta", dst);
        safeBranch("bcc", Ldone, Lneg);
        emitLabel(Lneg);
        emit("ldai", 1);
        emit("sta", dst);
        emitLabel(Ldone);
    };
    auto negateIf = [&](const char *flag, const char *lbl) {
        std::string Lskip = newLabel(), Ldo = newLabel();
        emit("lda", flag);
        safeBranch("beq", Lskip, Ldo);
        emitLabel(Ldo);
        negate16(lbl);
        emitLabel(Lskip);
    };
    // `cx`'s low/high byte double as the dividend/divisor sign flags, but
    // isNeg() also uses `cx` as scratch, so the dividend's flag is stashed
    // in `aBase` and restored before either flag is read below.
    if (!isUnsigned) {
        isNeg("dx", "cx");
        emit("lda", "cx");
        emit("sta", "aBase");  // stash the dividend's own sign flag (aBase: free here)
        isNeg("bc", "cx+1");
        emit("lda", "aBase");
        emit("sta", "cx");  // restore the dividend's own sign flag over cx's own low byte
        negateIf("cx", "dx");
        negateIf("cx+1", "bc");
    }
    emit("ldai", 0);
    emit("sta", "ac");
    emit("ldai", 0);
    emit("sta", "ac+1");  // ac = quotient = 0
    std::string Lloop = newLabel(), Lcommit = newLabel(), Llt = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    // Tentatively compute dx - bc; carry set means no borrow (dx >= bc).
    // The low byte's result is stashed on the stack until it's known
    // whether to commit.
    emit("sec");
    emit("lda", "dx");
    emit("sbc", "bc");
    emit("pha");
    emit("lda", "dx+1");
    emit("sbc", "bc+1");
    safeBranch("bcs", Lcommit, Llt);
    emitLabel(Lcommit);
    emit("sta", "dx+1");
    emit("pla");
    emit("sta", "dx");
    inc16("ac");
    emit("jmp", Lloop);
    emitLabel(Llt);
    emit("pla");  // discard the stashed low byte - subtraction rejected
    emit("jmp", Ldone);
    emitLabel(Ldone);
    if (!isUnsigned) {
        // Quotient is negative iff the two original signs differed; the
        // remainder always takes the dividend's original sign.
        emit("lda", "cx");
        emit("eor", "cx+1");
        std::string Lskip = newLabel(), Ldo = newLabel();
        safeBranch("beq", Lskip, Ldo);
        emitLabel(Ldo);
        negate16("ac");
        emitLabel(Lskip);
        negateIf("cx", "dx");
    }
    if (wantRemainder) copy16("ac", "dx");
}

void M6502Codegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    emit("lda", "ac");
    emit("pha");
    emit("lda", "ac+1");
    emit("pha");
    genExpr(rhs);
    copy16("bc", "ac");  // bc = divisor
    emit("pla");
    emit("sta", "dx+1");
    emit("pla");
    emit("sta", "dx");  // dx = dividend (running remainder)
    genDivModCore(wantRemainder, isUnsigned);
}

void M6502Codegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned) {
    if (op == "==") return emitEqualityCompare(lhs, rhs, /*wantEqual=*/true);
    if (op == "!=") return emitEqualityCompare(lhs, rhs, /*wantEqual=*/false);
    // Every ordering comparison reduces to "is a < b?" by choosing operand
    // order and whether to invert: a>b is b<a; a<=b is !(b<a); a>=b is !(a<b).
    if (op == "<") return emitLessCompare(lhs, rhs, isUnsigned, /*invert=*/false);
    if (op == ">") return emitLessCompare(rhs, lhs, isUnsigned, /*invert=*/false);
    if (op == "<=") return emitLessCompare(rhs, lhs, isUnsigned, /*invert=*/true);
    if (op == ">=") return emitLessCompare(lhs, rhs, isUnsigned, /*invert=*/true);
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

// `==`/`!=` need no sign handling: a-b (16 bits, borrow chained low into
// high byte) is zero iff a==b; `dx` holds the low byte while A is needed
// for the high byte's subtraction.
void M6502Codegen::emitEqualityCompare(const ir::Expr &lhs, const ir::Expr &rhs, bool wantEqual) {
    genExpr(lhs);
    emit("lda", "ac");
    emit("pha");
    emit("lda", "ac+1");
    emit("pha");
    genExpr(rhs);
    emit("pla");
    emit("sta", "bc+1");
    emit("pla");
    emit("sta", "bc");  // bc = lhs
    emit("sec");
    emit("lda", "bc");
    emit("sbc", "ac");
    emit("sta", "dx");
    emit("lda", "bc+1");
    emit("sbc", "ac+1");
    emit("ora", "dx");
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    safeBranch(wantEqual ? "beq" : "bne", Ltrue, Lfalse);
    emitLabel(Ltrue);
    emit("ldai", 1);
    emit("sta", "ac");
    emit("ldai", 0);
    emit("sta", "ac+1");
    emit("jmp", Ldone);
    emitLabel(Lfalse);
    emit("ldai", 0);
    emit("sta", "ac");
    emit("ldai", 0);
    emit("sta", "ac+1");
    emitLabel(Ldone);
}

// Signed 16-bit "a < b" via subtraction: after the final (high) byte SBC,
// N reflects the true signed result only once corrected for overflow (V) -
// if V is set, `EOR #$80` flips the sign bit to fix it (the 6502's N-xor-V
// rule, with no single instruction to test it directly).
void M6502Codegen::emitLessCompare(const ir::Expr &a, const ir::Expr &b, bool isUnsigned, bool invert) {
    genExpr(a);
    emit("lda", "ac");
    emit("pha");
    emit("lda", "ac+1");
    emit("pha");
    genExpr(b);
    emit("pla");
    emit("sta", "bc+1");
    emit("pla");
    emit("sta", "bc");  // bc = a
    emit("sec");
    emit("lda", "bc");
    emit("sbc", "ac");
    emit("lda", "bc+1");
    emit("sbc", "ac+1");
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    if (isUnsigned) {
        // No fixup needed: carry after the subtraction directly means
        // "no borrow" (a>=b) or "borrow" (a<b).
        safeBranch(invert ? "bcs" : "bcc", Ltrue, Lfalse);
    } else {
        std::string Lfixskip = newLabel();
        emit("bvc", Lfixskip);
        emit("eori", 128);
        emitLabel(Lfixskip);
        safeBranch(invert ? "bpl" : "bmi", Ltrue, Lfalse);  // N now correctly reflects a<b (BMI) / a>=b (BPL)
    }
    emitLabel(Ltrue);
    emit("ldai", 1);
    emit("sta", "ac");
    emit("ldai", 0);
    emit("sta", "ac+1");
    emit("jmp", Ldone);
    emitLabel(Lfalse);
    emit("ldai", 0);
    emit("sta", "ac");
    emit("ldai", 0);
    emit("sta", "ac+1");
    emitLabel(Ldone);
}

// Leaves `ac` holding 0 (false) or nonzero (true), with Z set correctly for
// an immediately-following beq/bne. For a wide value, ORs all `wac` bytes
// into `ac` first (true iff any byte is nonzero).
void M6502Codegen::testTruthy(const ir::Expr &e) {
    if (!isWide(e.type)) {
        emit("lda", "ac");
        emit("ora", "ac+1");
        return;
    }
    int n = 2 * wordCountOf(e.type);
    emit("lda", byteAt("wac", 0));
    for (int i = 1; i < n; i++) emit("ora", byteAt("wac", i));
    emit("sta", "ac");
    emit("ldai", 0);
    emit("sta", "ac+1");
    emit("lda", "ac");
}

void M6502Codegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string Lcont = newLabel(), Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    genExpr(lhs);
    testTruthy(lhs);
    safeBranch("beq", Lfalse, Lcont);
    emitLabel(Lcont);
    genExpr(rhs);
    testTruthy(rhs);
    safeBranch("beq", Lfalse, Ltrue);
    emitLabel(Ltrue);
    emit("ldai", 1);
    emit("sta", "ac");
    emit("ldai", 0);
    emit("sta", "ac+1");
    emit("jmp", Ldone);
    emitLabel(Lfalse);
    emit("ldai", 0);
    emit("sta", "ac");
    emit("ldai", 0);
    emit("sta", "ac+1");
    emitLabel(Ldone);
}

void M6502Codegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string Lcont = newLabel(), Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    genExpr(lhs);
    testTruthy(lhs);
    safeBranch("bne", Ltrue, Lcont);
    emitLabel(Lcont);
    genExpr(rhs);
    testTruthy(rhs);
    safeBranch("bne", Ltrue, Lfalse);
    emitLabel(Ltrue);
    emit("ldai", 1);
    emit("sta", "ac");
    emit("ldai", 0);
    emit("sta", "ac+1");
    emit("jmp", Ldone);
    emitLabel(Lfalse);
    emit("ldai", 0);
    emit("sta", "ac");
    emit("ldai", 0);
    emit("sta", "ac+1");
    emitLabel(Ldone);
}

void M6502Codegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    testTruthy(rhs);
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    safeBranch("beq", Ltrue, Lfalse);
    emitLabel(Ltrue);
    emit("ldai", 1);
    emit("sta", "ac");
    emit("ldai", 0);
    emit("sta", "ac+1");
    emit("jmp", Ldone);
    emitLabel(Lfalse);
    emit("ldai", 0);
    emit("sta", "ac");
    emit("ldai", 0);
    emit("sta", "ac+1");
    emitLabel(Ldone);
}

std::optional<std::string> M6502Codegen::assembleInProcess(const std::string &asmText,
                                                             const std::string &format) const {
    if (format == "raw") return assembleM6502(asmText);
    // `coff`/`elf`/`macho`/`aout`/`objraw`: builds one format-neutral
    // ObjectFile, then picks that format's FileFormatWriter to serialize it.
    if (auto kind = fileFormatKindFromName(format)) {
        uint16_t machineType = kM6502CoffMachine;
        if (variant_ == Variant::Atari2600) machineType = kM6502Atari2600CoffMachine;
        else if (variant_ == Variant::Nes2A03) machineType = kM6502Nes2A03CoffMachine;
        else if (variant_ == Variant::Atari5200) machineType = kM6502Atari5200CoffMachine;
        ObjectFile obj = assembleM6502Object(asmText, machineType);
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

std::optional<std::string> M6502Codegen::debugMap(const std::string &asmText, const std::string &format,
                                                    const std::string &sourcePath) const {
    if (format != "raw") return std::nullopt;
    return assembleM6502DebugMap(asmText, sourcePath, debugSymbols_);
}

std::optional<std::string> M6502Codegen::debugMapPreLink(const std::string &asmText,
                                                    const std::string &sourcePath) const {
    return assembleM6502DebugMap(asmText, sourcePath, debugSymbols_, /*allowExternal=*/true);
}

// Only emits a marker when `debugMapEnabled_` (set via setDebugMapEnabled(),
// before generate() runs) and `line` actually differs from the last one
// emitted - see Pdp7Codegen.cpp's identical emitSourceLine(), which this
// mirrors exactly.
void M6502Codegen::emitSourceLine(int line) {
    if (!debugMapEnabled_ || line == 0 || line == lastDebugLine_) return;
    emitRaw(cat("__line ", line));
    lastDebugLine_ = line;
}

}  // namespace rcc
