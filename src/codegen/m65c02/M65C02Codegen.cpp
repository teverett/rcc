#include "M65C02Codegen.h"

#include <algorithm>
#include <tuple>
#include <cstdint>
#include <stdexcept>

#include "M65C02Assembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

// $0600: the conventional "put your test program here" address for a bare
// 6502 setup, above zero page and the hardware stack page $0100-$01FF.
constexpr uint32_t kBaseAddress = 0x0600;

// Initial value of the software call-frame stack pointer `sp`; high enough
// to stay clear of code+data growing up from kBaseAddress.
constexpr uint32_t kFrameStackTop = 0xF000;
}  // namespace

// A literal element's decimal text, or a symbol element's label - the
// address isn't known at codegen time, so it's left for the assembler to
// resolve via dblo/dbhi.
std::string M65C02Codegen::arrayElemText(const ir::ArrayInitElem &e) {
    if (!e.isSymbol) return std::to_string(e.value);
    return e.slot ? label(e.slot) : label(e.func);
}

void M65C02Codegen::declareArray(const std::string &lbl, const std::vector<ir::ArrayInitElem> &values,
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

void M65C02Codegen::copy16(const std::string &dst, const std::string &src) {
    emit("lda", src);
    emit("sta", dst);
    emit("lda", src + "+1");
    emit("sta", dst + "+1");
}

void M65C02Codegen::zeroZp(const std::string &lbl) {
    emit("stzp", lbl);
    emit("stzp", lbl + "+1");
}

namespace {
std::string byteAt(const char *base, int i) { return i == 0 ? base : (std::string(base) + "+" + std::to_string(i)); }
}  // namespace

void M65C02Codegen::pushWide(const char *base, int nbytes) {
    for (int i = 0; i < nbytes; i++) {
        emit("lda", byteAt(base, i));
        emit("pha");
    }
}

void M65C02Codegen::popWide(const char *base, int nbytes) {
    for (int i = nbytes - 1; i >= 0; i--) {
        emit("pla");
        emit("sta", byteAt(base, i));
    }
}

void M65C02Codegen::copyWide(const char *dst, const char *src, int nbytes) {
    for (int i = 0; i < nbytes; i++) {
        emit("lda", byteAt(src, i));
        emit("sta", byteAt(dst, i));
    }
}

void M65C02Codegen::negateWideBuffer(const char *buf, int nbytes) {
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

void M65C02Codegen::safeBranch(const std::string &branchMnemonic, const std::string &targetIfTaken,
                               const std::string &targetIfNot) {
    std::string Lnear = newLabel();
    emit(branchMnemonic, Lnear);
    emit("jmp", targetIfNot);
    emitLabel(Lnear);
    emit("jmp", targetIfTaken);
}

void M65C02Codegen::inc16(const std::string &lbl) {
    std::string Lskip = newLabel();
    emit("inc", lbl);
    emit("lda", lbl);
    emit("bne", Lskip);
    emit("inc", lbl + "+1");
    emitLabel(Lskip);
}

void M65C02Codegen::addConst16(const std::string &dstReg, const std::string &srcReg, int disp) {
    if (disp == 0) {
        copy16(dstReg, srcReg);
        return;
    }
    uint16_t k = static_cast<uint16_t>(disp);
    emit("lda", srcReg);
    emit("clc");
    emit("adci", k);
    emit("sta", dstReg);
    emit("lda", srcReg + "+1");
    emit("adci_hi", k);
    emit("sta", dstReg + "+1");
}

void M65C02Codegen::add16(const std::string &dst, const std::string &a, const std::string &b) {
    emit("clc");
    emit("lda", a);
    emit("adc", b);
    emit("sta", dst);
    emit("lda", a + "+1");
    emit("adc", b + "+1");
    emit("sta", dst + "+1");
}

void M65C02Codegen::sub16(const std::string &dst, const std::string &a, const std::string &b) {
    emit("sec");
    emit("lda", a);
    emit("sbc", b);
    emit("sta", dst);
    emit("lda", a + "+1");
    emit("sbc", b + "+1");
    emit("sta", dst + "+1");
}

void M65C02Codegen::loadIndirect(const std::string &zpPtr, const std::string &dstReg) {
    emit("ldyi", 0);
    emit("ldaind", zpPtr);
    emit("sta", dstReg);
    emit("ldyi", 1);
    emit("ldaind", zpPtr);
    emit("sta", dstReg + "+1");
}

void M65C02Codegen::storeIndirect(const std::string &zpPtr, const std::string &srcReg) {
    emit("ldyi", 0);
    emit("lda", srcReg);
    emit("staind", zpPtr);
    emit("ldyi", 1);
    emit("lda", srcReg + "+1");
    emit("staind", zpPtr);
}

void M65C02Codegen::slotAddrToPtr(const ir::Slot *slot) {
    if (slot->kind == ir::SlotKind::Param) {
        // Slot::frameOffset assumes a two-int-width reservation above fp,
        // but this frame reserves only one word there (the saved fp), so
        // the extra reservation is subtracted back off here.
        addConst16("ptr", "fp", slot->frameOffset - 2 * widthOfType(Type{BaseType::Int, false}) + 2);
    } else {
        addConst16("ptr", "fp", -ir::localLowOffset(*slot, typeWidths()));
    }
}

void M65C02Codegen::loadSlotToAc(const ir::Slot *slot) {
    int w = widthOfType(slot->type);
    if (w == 1) {
        if (slot->kind == ir::SlotKind::Global) {
            emit("lda", label(slot));
            emit("sta", "ac");
        } else {
            slotAddrToPtr(slot);
            emit("ldyi", 0);
            emit("ldaind", "ptr");
            emit("sta", "ac");
        }
        if (slot->type.isUnsigned) {
            emit("stzp", "ac+1");
            return;
        }
        emit("lda", "ac");
        emit("sta", "cx");
        emit("asl", "cx");
        std::string Lneg = newLabel(), Ldone = newLabel();
        emit("stzp", "ac+1");
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
    slotAddrToPtr(slot);
    loadIndirect("ptr", "ac");
}

void M65C02Codegen::storeAcToSlot(const ir::Slot *slot) {
    if (widthOfType(slot->type) == 1) {
        if (slot->kind == ir::SlotKind::Global) {
            emit("lda", "ac");
            emit("sta", label(slot));
        } else {
            slotAddrToPtr(slot);
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
    slotAddrToPtr(slot);
    storeIndirect("ptr", "ac");
}

void M65C02Codegen::genWideLoad(const ir::Slot *slot, int words) {
    int n = 2 * words;
    if (slot->kind == ir::SlotKind::Global) {
        std::string lbl = label(slot);
        for (int i = 0; i < n; i++) {
            emit("lda", lbl + (i ? "+" + std::to_string(i) : ""));
            emit("sta", byteAt("wac", i));
        }
        return;
    }
    slotAddrToPtr(slot);
    genWideLoadAtPtr(words);
}

void M65C02Codegen::genWideLoadLiteral(long value, int words) {
    auto uv = static_cast<unsigned long>(value);
    int n = 2 * words;
    for (int i = 0; i < n; i++) {
        emit("ldai", (uv >> (8 * i)) & 0xFF);
        emit("sta", byteAt("wac", i));
    }
}

void M65C02Codegen::genWideStore(const ir::Slot *slot, int words) {
    int n = 2 * words;
    if (slot->kind == ir::SlotKind::Global) {
        std::string lbl = label(slot);
        for (int i = 0; i < n; i++) {
            emit("lda", byteAt("wac", i));
            emit("sta", lbl + (i ? "+" + std::to_string(i) : ""));
        }
        return;
    }
    slotAddrToPtr(slot);
    genWideStoreAtPtr(words);
}

void M65C02Codegen::genWideLoadAtPtr(int words) {
    int n = 2 * words;
    for (int i = 0; i < n; i++) {
        emit("ldyi", i);
        emit("ldaind", "ptr");
        emit("sta", byteAt("wac", i));
    }
}

void M65C02Codegen::genWideStoreAtPtr(int words) {
    int n = 2 * words;
    for (int i = 0; i < n; i++) {
        emit("ldyi", i);
        emit("lda", byteAt("wac", i));
        emit("staind", "ptr");
    }
}

void M65C02Codegen::loadOperandAtPtr(Type type) {
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
            emit("stzp", "ac+1");
            return;
        }
        emit("lda", "ac");
        emit("sta", "cx");
        emit("asl", "cx");
        std::string Lneg = newLabel(), Ldone = newLabel();
        emit("stzp", "ac+1");
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

void M65C02Codegen::storeOperandAtPtr(Type type) {
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

void M65C02Codegen::narrowAcTo(Type to) {
    if (widthOfType(to) != 1) return;  // a native word: ac already holds the full value
    if (to.isUnsigned) {
        emit("stzp", "ac+1");
        return;
    }
    emit("lda", "ac");
    emit("sta", "cx");
    emit("asl", "cx");
    std::string Lneg = newLabel(), Ldone = newLabel();
    emit("stzp", "ac+1");
    safeBranch("bcs", Lneg, Ldone);
    emitLabel(Lneg);
    emit("ldai", 255);
    emit("sta", "ac+1");
    emitLabel(Ldone);
}

// `op` applied to ac=old, bc=rhs, leaving the raw new value in ac.
void M65C02Codegen::genCompoundOp(const std::string &op, Type type) {
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
        copy16("dx", "ac");  // dx = multiplier = old
        genMulCore();
        return;
    }
    if (op == "/" || op == "%") {
        copy16("dx", "ac");  // dx = dividend = old
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
    emit("lda", "bc");  // bc's low byte is the shift count; bc is dead afterward
    emit("sta", "cx");
    genShiftCore(op == "<<", isUnsigned);
}

// `ptr` must already hold the resolved address on entry. `e.rhs`'s
// evaluation can clobber ptr/ac/bc/cx/dx, so the address is stashed on the
// hardware stack across it, and again around genCompoundOp() since its
// "/"/"%" case also uses ptr as scratch.
void M65C02Codegen::genCompoundThroughPtr(const ir::Expr &e) {
    emit("lda", "ptr");
    emit("pha");
    emit("lda", "ptr+1");
    emit("pha");
    genExpr(*e.rhs);      // ac = rhs
    copy16("bc", "ac");   // bc = rhs
    emit("pla");
    emit("sta", "ptr+1");
    emit("pla");
    emit("sta", "ptr");
    loadOperandAtPtr(e.type);  // ac = old
    if (e.yieldOld) {
        emit("lda", "ac");
        emit("pha");
        emit("lda", "ac+1");
        emit("pha");
    }
    emit("lda", "ptr");
    emit("pha");
    emit("lda", "ptr+1");
    emit("pha");
    genCompoundOp(e.op, e.type);  // ac = new (raw)
    narrowAcTo(e.type);
    emit("pla");
    emit("sta", "ptr+1");
    emit("pla");
    emit("sta", "ptr");
    storeOperandAtPtr(e.type);
    if (e.yieldOld) {  // ac = old (final result)
        emit("pla");
        emit("sta", "ac+1");
        emit("pla");
        emit("sta", "ac");
    }
}

void M65C02Codegen::loadArrayElemToAc(const ir::ArrayInitElem &e) {
    if (!e.isSymbol) {
        uint16_t v16 = static_cast<uint16_t>(e.value);
        emit("ldai", v16 & 0xFF);
        emit("sta", "ac");
        emit("ldai", (v16 >> 8) & 0xFF);
        emit("sta", "ac+1");
        return;
    }
    std::string lbl = e.slot ? label(e.slot) : label(e.func);
    emit("ldai", lbl);
    emit("sta", "ac");
    emit("ldai_hi", lbl);
    emit("sta", "ac+1");
}

void M65C02Codegen::genLocalArrayInit(const ir::Slot *slot) {
    int w = widthOfType(slot->type);
    for (size_t i = 0; i < slot->arrayInit.size(); i++) {
        const ir::ArrayInitElem &elem = slot->arrayInit[i];
        addConst16("ptr", "fp", -ir::localLowOffset(*slot, typeWidths()) + w * static_cast<int>(i));
        if (w <= 2) {
            loadArrayElemToAc(elem);
            if (w == 1) {
                emit("ldyi", 0);
                emit("lda", "ac");
                emit("staind", "ptr");
            } else {
                storeIndirect("ptr", "ac");
            }
        } else {
            // A wide element's literal is never a symbol; split directly
            // into w plain bytes, least significant first.
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

void M65C02Codegen::emitEpilogue() {
    loadIndirect("fp", "ptr");  // ptr = caller's saved fp
    addConst16("sp", "fp", 2);  // sp = just above this frame's saved-fp word
    copy16("fp", "ptr");        // fp = caller's frame, restored
}

std::string M65C02Codegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    // Real separate compilation means `prog` may not define `main` at all;
    // objectMode means the entry sequence isn't emitted here at all -
    // M65C02Linker.cpp synthesizes it once every object is merged and
    // main's linked address is known. An object file's own base is org 0;
    // placing it is the linker's job.
    emitRaw("org " + std::to_string(objectMode ? 0 : kBaseAddress));
    emitRaw("");
    // The fixed zero-page pseudo-registers, bound as equates rather than a
    // sequential data-trailer layout. Safe to declare identically in every
    // separately-compiled object since an equate's value is a fixed
    // constant, not a linker-visible address.
    emitEquate("ptr", 0);
    emitEquate("ac", 2);
    emitEquate("bc", 4);
    emitEquate("cx", 6);
    emitEquate("dx", 8);
    emitEquate("sp", 10);
    emitEquate("fp", 12);
    emitEquate("wac", 14);
    emitEquate("wbc", 22);
    emitEquate("wtmp", 30);
    emitEquate("wtmp2", 38);
    emitRaw("");
    if (!objectMode) {
        emitLabel("_start");
        // Seeds the software call-frame stack before main's prologue runs.
        // fp only needs zeroing for tidiness: main never returns to a
        // caller that reads its saved value back.
        emit("ldai", kFrameStackTop);
        emit("sta", "sp");
        emit("ldai_hi", kFrameStackTop);
        emit("sta", "sp+1");
        zeroZp("fp");
        emit("jsr", label(mainFn));
        // The program's result sits in ac/ac+1; this lda also leaves the
        // low byte in the real A register for a quick register-dump look.
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
        // An external global isn't defined in this file: skip it here and
        // let the assembler defer the reference as a relocation.
        if (slot->isExternal) continue;
        if (debugMapEnabled_) debugSymbols_.push_back({label(slot.get()), /*isFunc=*/false});
        size_t gTrailerStart = trailer_.size();
        if (slot->hasAddress) trailer_.push_back(placementLine(slot->address));
        const std::string lbl = label(slot.get());
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

std::map<std::string, std::string> M65C02Codegen::perFileAsm() const { return perFileAsm_; }

// Prologue: pushes the caller's fp onto the software stack right where the
// caller's pushed arguments end, sets fp to that saved word, then reserves
// frameSize*2 more bytes below fp for locals.
void M65C02Codegen::genFunction(const ir::FunctionBody &fn) {
    lastDebugLine_ = 0;
    if (debugMapEnabled_) debugSymbols_.push_back({label(fn.func), /*isFunc=*/true});
    if (fn.func->hasAddress) emitRaw(placementLine(fn.func->address));
    emitLabel(label(fn.func));
    addConst16("ptr", "sp", -2);     // ptr = saved-fp word's address
    storeIndirect("ptr", "fp");      // [ptr] = caller's fp
    copy16("fp", "ptr");             // fp = this call's frame base
    addConst16("sp", "fp", -fn.func->frameSize);  // reserve the locals below fp
    genStmt(*fn.body);
    emitEpilogue();
    emit("rts");
    emitRaw("");
}

void M65C02Codegen::genStmt(const ir::Stmt &s) {
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
            // Early exit: genFunction()'s fall-through epilogue never runs
            // for this path, so it's repeated inline here.
            emitEpilogue();
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
                    storeAcToSlot(s.declSlot);
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

void M65C02Codegen::genExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit: {
            if (isWide(e.type)) {
                genWideLoadLiteral(e.intValue, wordCountOf(e.type));
                return;
            }
            // Each byte is handled independently since the two halves of a
            // 16-bit literal aren't usually zero together.
            uint16_t v16 = static_cast<uint16_t>(e.intValue);
            if ((v16 & 0xFF) == 0) {
                emit("stzp", "ac");
            } else {
                emit("ldai", v16 & 0xFF);
                emit("sta", "ac");
            }
            if (((v16 >> 8) & 0xFF) == 0) {
                emit("stzp", "ac+1");
            } else {
                emit("ldai", (v16 >> 8) & 0xFF);
                emit("sta", "ac+1");
            }
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
                // Stash ptr on the hardware stack since rhs might index
                // through ptr again.
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
            // A plain scalar target's storage is reconstructible after rhs
            // evaluates (fp is restored by any nested call's epilogue
            // before it returns), so nothing needs stashing across rhs here.
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
                genExpr(*e.rhs);  // ac = pointer value
                copy16("ptr", "ac");
                loadOperandAtPtr(e.type);
            } else if (e.op == "~") {
                genExpr(*e.rhs);
                if (wide) {
                    int n = 2 * wordCountOf(e.rhs->type);
                    for (int i = 0; i < n; i++) {
                        std::string b = byteAt("wac", i);
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
            // Pushes each argument in reverse declaration order so
            // argument 0 ends up closest to the callee's frame (fp+2), then
            // cleans the pushed words back off itself (cdecl-style).
            int totalBytes = 0;
            for (size_t i = e.args.size(); i-- > 0;) {
                genExpr(*e.args[i]);
                int w = std::max(widthOfType(e.args[i]->type), 2);
                totalBytes += w;
                addConst16("sp", "sp", -w);
                if (w > 2) {
                    for (int b = 0; b < w; b++) {
                        emit("ldyi", b);
                        emit("lda", byteAt("wac", b));
                        emit("staind", "sp");
                    }
                } else {
                    storeIndirect("sp", "ac");
                }
            }
            emit("jsr", label(e.func));
            if (totalBytes > 0) addConst16("sp", "sp", totalBytes);
            return;
        }
        case ir::ExprKind::CallPtr: {
            // The 6502 has no indirect-call instruction, only jmp (addr)
            // (jmpi). This emulates JSR's return-address convention by hand
            // (JSR pushes return-address-1, high then low byte; RTS pulls
            // low then high and adds 1 back), then jumps indirectly through
            // ptr, leaving the hardware stack exactly as a real jsr would
            // have, so the callee's rts returns right after "jmpi ptr".
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

// slot->arraySize > 0 means the array's storage is indexed directly: a
// Global's is base+idx*2 off a compile-time label, a Local's is
// fp-relative at base+idx*2 (a Param array always decays to a pointer, so
// can't reach this branch). Otherwise slot is a plain pointer variable:
// the base comes from reading its stored value, and idx*2 is always ADDED
// to it regardless of where the pointer variable lives.
void M65C02Codegen::genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
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
    if (slot->arraySize > 0) {
        if (slot->kind == ir::SlotKind::Global) {
            std::string lbl = label(slot);
            emit("clc");
            emit("lda", "ac");
            emit("adci", lbl);
            emit("sta", "ptr");
            emit("lda", "ac+1");
            emit("adci_hi", lbl);
            emit("sta", "ptr+1");
        } else {
            addConst16("ptr", "fp", -ir::localLowOffset(*slot, typeWidths()));  // ptr = element 0's own address
            add16("ptr", "ptr", "ac");                                          // ptr += idx*2
        }
    } else if (slot->kind == ir::SlotKind::Global) {
        std::string lbl = label(slot);
        emit("clc");
        emit("lda", "ac");
        emit("adc", lbl);
        emit("sta", "ptr");
        emit("lda", "ac+1");
        emit("adc", lbl + "+1");
        emit("sta", "ptr+1");
    } else {
        slotAddrToPtr(slot);        // ptr = slot's own frame address
        loadIndirect("ptr", "bc");  // bc = slot's own stored pointer value
        add16("ptr", "ac", "bc");   // ptr = idx*2 + bc
    }
}

void M65C02Codegen::genIndex(const ir::Expr &e) {
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
            emit("stzp", "ac+1");
            return;
        }
        emit("lda", "ac");
        emit("sta", "cx");
        emit("asl", "cx");
        std::string Lneg = newLabel(), Ldone = newLabel();
        emit("stzp", "ac+1");
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

void M65C02Codegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        if (operand.slot->kind == ir::SlotKind::Global) {
            std::string lbl = label(operand.slot);
            emit("ldai", lbl);
            emit("sta", "ac");
            emit("ldai_hi", lbl);
            emit("sta", "ac+1");
        } else {
            slotAddrToPtr(operand.slot);
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

void M65C02Codegen::genConvert(const ir::Expr &e) {
    Type from = e.lhs->type;
    Type to = e.type;
    bool fromWide = isWide(from);
    bool toWide = isWide(to);
    auto narrowCharFixup = [&]() {
        if (widthOfType(to) != 1) return;
        if (to.isUnsigned) {
            emit("stzp", "ac+1");
            return;
        }
        emit("lda", "ac");
        emit("sta", "cx");
        emit("asl", "cx");
        std::string Lneg = newLabel(), Ldone = newLabel();
        emit("stzp", "ac+1");
        safeBranch("bcs", Lneg, Ldone);
        emitLabel(Lneg);
        emit("ldai", 255);
        emit("sta", "ac+1");
        emitLabel(Ldone);
    };
    if (!fromWide && !toWide) {
        genExpr(*e.lhs);
        narrowCharFixup();
        return;
    }
    if (fromWide && !toWide) {
        genExpr(*e.lhs);
        emit("lda", byteAt("wac", 0));
        emit("sta", "ac");
        emit("lda", byteAt("wac", 1));
        emit("sta", "ac+1");
        narrowCharFixup();
        return;
    }
    if (!fromWide && toWide) {
        int toWords = wordCountOf(to);
        genExpr(*e.lhs);
        emit("lda", "ac");
        emit("sta", byteAt("wac", 0));
        emit("lda", "ac+1");
        emit("sta", byteAt("wac", 1));
        if (toWords > 1) {
            // from.isUnsigned describes the pointee's signedness for a
            // pointer type, not the pointer's own (which has no sign), so
            // isPointer(from) is checked separately to force zero-extension.
            if (isPointer(from) || from.isUnsigned) {
                emit("stzp", "dx");
            } else {
                emit("lda", "ac+1");
                emit("sta", "cx");
                emit("asl", "cx");
                std::string Lneg = newLabel(), Ldone = newLabel();
                emit("stzp", "dx");
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
    int fromN = 2 * wordCountOf(from);
    int toN = 2 * wordCountOf(to);
    genExpr(*e.lhs);
    if (toN > fromN) {
        if (from.isUnsigned) {
            emit("stzp", "dx");
        } else {
            emit("lda", byteAt("wac", fromN - 1));
            emit("sta", "cx");
            emit("asl", "cx");
            std::string Lneg = newLabel(), Ldone = newLabel();
            emit("stzp", "dx");
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
}

void M65C02Codegen::genWideAdd(const ir::Expr &lhs, const ir::Expr &rhs, bool isAdd, int words) {
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

void M65C02Codegen::genWideBitwise(const ir::Expr &lhs, const ir::Expr &rhs, const std::string &op, int words) {
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

void M65C02Codegen::emitWideEqualityCompare(const ir::Expr &lhs, const ir::Expr &rhs, int words, bool wantEqual) {
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
    emit("stzp", "ac+1");
    emit("jmp", Ldone);
    emitLabel(Lfalse);
    zeroZp("ac");
    emitLabel(Ldone);
}

void M65C02Codegen::emitWideLessCompare(const ir::Expr &a, const ir::Expr &b, bool isUnsigned, int words,
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
        safeBranch(invert ? "bcs" : "bcc", Ltrue, Lfalse);
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
    emit("stzp", "ac+1");
    emit("jmp", Ldone);
    emitLabel(Lfalse);
    zeroZp("ac");
    emitLabel(Ldone);
}

void M65C02Codegen::genWideCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned,
                                    int words) {
    if (op == "==") return emitWideEqualityCompare(lhs, rhs, words, /*wantEqual=*/true);
    if (op == "!=") return emitWideEqualityCompare(lhs, rhs, words, /*wantEqual=*/false);
    if (op == "<") return emitWideLessCompare(lhs, rhs, isUnsigned, words, /*invert=*/false);
    if (op == ">") return emitWideLessCompare(rhs, lhs, isUnsigned, words, /*invert=*/false);
    if (op == "<=") return emitWideLessCompare(rhs, lhs, isUnsigned, words, /*invert=*/true);
    if (op == ">=") return emitWideLessCompare(lhs, rhs, isUnsigned, words, /*invert=*/true);
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

void M65C02Codegen::genWideShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned, int words) {
    int n = 2 * words;
    genExpr(lhs);
    pushWide("wac", n);
    genExpr(rhs);
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

void M65C02Codegen::genWideMul(const ir::Expr &lhs, const ir::Expr &rhs, int words) {
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

void M65C02Codegen::genWideDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned,
                                   int words) {
    int n = 2 * words;
    auto isNegByte = [&](const std::string &lblByte, const char *dst) {
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
        isNegByte(byteAt("wbc", n - 1), "dx");
        isNegByte(byteAt("wac", n - 1), "dx+1");
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
    emit("asl", byteAt("wbc", 0));
    for (int i = 1; i < n; i++) emit("rol", byteAt("wbc", i));
    for (int i = 0; i < n; i++) emit("rol", byteAt("wtmp", i));
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

void M65C02Codegen::testTruthy(const ir::Expr &e) {
    if (!isWide(e.type)) {
        emit("lda", "ac");
        emit("ora", "ac+1");
        return;
    }
    int n = 2 * wordCountOf(e.type);
    emit("lda", byteAt("wac", 0));
    for (int i = 1; i < n; i++) emit("ora", byteAt("wac", i));
    emit("sta", "ac");
    emit("stzp", "ac+1");
    emit("lda", "ac");
}

void M65C02Codegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
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

// ASL/LSR shift exactly one bit, so this loops `rhs` times.
void M65C02Codegen::genShiftCore(bool isLeft, bool isUnsigned) {
    std::string Lloop = newLabel(), Lbody = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("lda", "cx");
    safeBranch("beq", Ldone, Lbody);
    emitLabel(Lbody);
    if (isLeft) {
        emit("asl", "ac");
        emit("rol", "ac+1");
    } else if (isUnsigned) {
        emit("lsr", "ac+1");
        emit("ror", "ac");
    } else {
        // Re-derives the current sign bit into carry each iteration (via a
        // throwaway copy in bc+1) and rotates it back in at the top,
        // instead of LSR's fixed 0-fill.
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

void M65C02Codegen::genShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned) {
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

// Shift-and-add multiply: bc is the multiplicand (shifted left each
// iteration), dx the multiplier (shifted right, consumed one bit at a
// time), ac the accumulating product.
void M65C02Codegen::genMulCore() {
    zeroZp("ac");  // ac = product = 0
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

void M65C02Codegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
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

// Repeated subtraction: dx is the running remainder (starts as the
// dividend), bc the fixed divisor, ac the quotient, counting how many
// times bc was subtracted out of dx before it would've gone negative.
void M65C02Codegen::genDivModCore(bool wantRemainder, bool isUnsigned) {
    auto negate16 = [&](const char *lbl) {
        emit("ldai", 0);
        emit("sec");
        emit("sbc", lbl);
        emit("sta", lbl);
        emit("ldai", 0);
        emit("sbc", std::string(lbl) + "+1");
        emit("sta", std::string(lbl) + "+1");
    };
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
    // ptr's low/high byte double as this operation's two sign flags.
    if (!isUnsigned) {
        isNeg("dx", "ptr");
        isNeg("bc", "ptr+1");
        negateIf("ptr", "dx");
        negateIf("ptr+1", "bc");
    }
    zeroZp("ac");  // ac = quotient = 0
    std::string Lloop = newLabel(), Lcommit = newLabel(), Llt = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    // Tentatively computes dx - bc; carry set means dx >= bc. The low
    // byte's result is stashed on the stack until it's known whether to
    // commit it.
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
    emit("pla");  // discard the rejected tentative low byte
    emit("jmp", Ldone);
    emitLabel(Ldone);
    if (!isUnsigned) {
        emit("lda", "ptr");
        emit("eor", "ptr+1");
        std::string Lskip = newLabel(), Ldo = newLabel();
        safeBranch("beq", Lskip, Ldo);
        emitLabel(Ldo);
        negate16("ac");
        emitLabel(Lskip);
        negateIf("ptr", "dx");
    }
    if (wantRemainder) copy16("ac", "dx");
}

void M65C02Codegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
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

void M65C02Codegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned) {
    if (op == "==") return emitEqualityCompare(lhs, rhs, /*wantEqual=*/true);
    if (op == "!=") return emitEqualityCompare(lhs, rhs, /*wantEqual=*/false);
    // Every ordering comparison reduces to "is a < b?": a>b is b<a, a<=b
    // is !(b<a), a>=b is !(a<b).
    if (op == "<") return emitLessCompare(lhs, rhs, isUnsigned, /*invert=*/false);
    if (op == ">") return emitLessCompare(rhs, lhs, isUnsigned, /*invert=*/false);
    if (op == "<=") return emitLessCompare(rhs, lhs, isUnsigned, /*invert=*/true);
    if (op == ">=") return emitLessCompare(lhs, rhs, isUnsigned, /*invert=*/true);
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

// a-b (full 16 bits, borrow chained from low into high byte) is zero iff
// a==b; needs no sign handling.
void M65C02Codegen::emitEqualityCompare(const ir::Expr &lhs, const ir::Expr &rhs, bool wantEqual) {
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
    emit("stzp", "ac+1");
    emit("jmp", Ldone);
    emitLabel(Lfalse);
    zeroZp("ac");
    emitLabel(Ldone);
}

// Signed 16-bit "a < b" via subtraction: after the final SBC, the sign bit
// (N) reflects the true signed result only once corrected for overflow
// (V) - if V is set the apparent sign is backwards, so EOR #$80 fixes it.
void M65C02Codegen::emitLessCompare(const ir::Expr &a, const ir::Expr &b, bool isUnsigned, bool invert) {
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
        safeBranch(invert ? "bcs" : "bcc", Ltrue, Lfalse);
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
    emit("stzp", "ac+1");
    emit("jmp", Ldone);
    emitLabel(Lfalse);
    zeroZp("ac");
    emitLabel(Ldone);
}

void M65C02Codegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
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
    emit("stzp", "ac+1");
    emit("jmp", Ldone);
    emitLabel(Lfalse);
    zeroZp("ac");
    emitLabel(Ldone);
}

void M65C02Codegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
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
    emit("stzp", "ac+1");
    emit("jmp", Ldone);
    emitLabel(Lfalse);
    zeroZp("ac");
    emitLabel(Ldone);
}

void M65C02Codegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    testTruthy(rhs);
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    safeBranch("beq", Ltrue, Lfalse);
    emitLabel(Ltrue);
    emit("ldai", 1);
    emit("sta", "ac");
    emit("stzp", "ac+1");
    emit("jmp", Ldone);
    emitLabel(Lfalse);
    zeroZp("ac");
    emitLabel(Ldone);
}

std::optional<std::string> M65C02Codegen::assembleInProcess(const std::string &asmText,
                                                             const std::string &format) const {
    if (format == "raw") return assembleM65C02(asmText);
    if (auto kind = fileFormatKindFromName(format)) {
        ObjectFile obj = assembleM65C02Object(asmText);
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

std::optional<std::string> M65C02Codegen::debugMap(const std::string &asmText, const std::string &format,
        const std::string &sourcePath) const {
    (void)format;
    return assembleM65C02DebugMap(asmText, sourcePath, debugSymbols_);
}

std::optional<std::string> M65C02Codegen::debugMapPreLink(const std::string &asmText,
                                                    const std::string &sourcePath) const {
    return assembleM65C02DebugMap(asmText, sourcePath, debugSymbols_, /*allowExternal=*/true);
}

// Only emits a marker when `debugMapEnabled_` (set via setDebugMapEnabled(),
// before generate() runs) and `line` actually differs from the last one
// emitted - see Pdp7Codegen.cpp's identical emitSourceLine(), which this
// mirrors exactly.
void M65C02Codegen::emitSourceLine(int line) {
    if (!debugMapEnabled_ || line == 0 || line == lastDebugLine_) return;
    emitRaw(cat("__line ", line));
    lastDebugLine_ = line;
}

}  // namespace rcc
