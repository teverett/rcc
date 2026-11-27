#include "PIC18Codegen.h"

#include <cstdint>
#include <stdexcept>
#include <tuple>

#include "PIC18Assembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

void emitInitByte(std::ostream &out, const std::string &lbl, uint8_t v) {
    if (v == 0) {
        out << "    clrf " << lbl << "\n";
    } else {
        out << "    movlw " << static_cast<int>(v) << "\n    movwf " << lbl << "\n";
    }
}
}  // namespace

void PIC18Codegen::declareRam(const std::string &lbl, int count, bool zeroAtStart) {
    ramDecls_.push_back(lbl + ": res " + std::to_string(count));
    if (zeroAtStart) zeroAtStart_.push_back({lbl, count});
}

void PIC18Codegen::copy2(const std::string &dst, const std::string &src) {
    if (dst == src) return;
    emit("movff", cat(src, ",", dst));
    emit("movff", cat(src, "+1,", dst, "+1"));
}

void PIC18Codegen::clear2(const std::string &lbl) {
    emit("clrf", lbl);
    emit("clrf", cat(lbl, "+1"));
}

void PIC18Codegen::shiftOneBit(const std::string &lbl, bool isLeft) {
    emit("bcf", "STATUS,0");
    if (isLeft) {
        emit("rlcf", cat(lbl, ",f"));
        emit("rlcf", cat(lbl, "+1,f"));
    } else {
        emit("rrcf", cat(lbl, "+1,f"));
        emit("rrcf", cat(lbl, ",f"));
    }
}

// dst = a + b via ADDWFC's carry chain; no manual fixup needed.
void PIC18Codegen::add16(const std::string &dst, const std::string &a, const std::string &b) {
    emit("movf", cat(b, ",w"));
    emit("addwf", cat(a, ",w"));
    emit("movwf", dst);
    emit("movf", cat(b, "+1,w"));
    emit("addwfc", cat(a, "+1,w"));
    emit("movwf", cat(dst, "+1"));
}

// dst = a - b via SUBWFB's borrow chain.
void PIC18Codegen::sub16(const std::string &dst, const std::string &a, const std::string &b) {
    emit("movf", cat(b, ",w"));
    emit("subwf", cat(a, ",w"));
    emit("movwf", dst);
    emit("movf", cat(b, "+1,w"));
    emit("subwfb", cat(a, "+1,w"));
    emit("movwf", cat(dst, "+1"));
}

// Size, in bytes, of `FSTACK`: more headroom for simultaneous call
// frames against fewer bytes left over for globals in the 96-byte
// Access Bank window.
constexpr int kFrameStackBytes = 48;

// Saves the caller's `FP` at `NFP-1`, then adopts `NFP` (the address the
// caller computed for this call's frame, immediately before `call`) as
// this call's `FP`.
void PIC18Codegen::emitPrologue() {
    emit("movf", "NFP,w");
    emit("addlw", "255");  // ptr = NFP-1 (255 == -1 mod 256)
    emit("movwf", "ptr");
    emit("movf", "ptr,w");
    emit("movwf", "FSR0L");
    emit("movf", "FP,w");
    emit("movwf", "INDF0");  // [NFP-1] = caller's FP
    emit("movf", "NFP,w");
    emit("movwf", "FP");
}

// Reverse: restores the caller's `FP` from `FP-1`, before `return`.
void PIC18Codegen::emitEpilogue() {
    emit("movf", "FP,w");
    emit("addlw", "255");  // ptr = FP-1
    emit("movwf", "ptr");
    emit("movf", "ptr,w");
    emit("movwf", "FSR0L");
    emit("movf", "INDF0,w");
    emit("movwf", "FP");
}

void PIC18Codegen::slotAddrToPtr(const ir::Slot *slot) {
    if (slot->kind == ir::SlotKind::Global) {
        emit("movlw", label(slot));
        emit("movwf", "ptr");
        return;
    }
    int off = slotFrameOffsetBytes(slot);
    emit("movf", "FP,w");
    if (off != 0) emit("addlw", off);
    emit("movwf", "ptr");
}

void PIC18Codegen::loadSlotToAc(const ir::Slot *slot) {
    int width = widthOfType(slot->type);
    if (slot->kind == ir::SlotKind::Global) {
        loadAcFromLabel(label(slot), width, slot->type.isUnsigned);
        return;
    }
    slotAddrToPtr(slot);
    loadAcIndirect(width, slot->type.isUnsigned);
}

void PIC18Codegen::storeAcToSlot(const ir::Slot *slot) {
    int width = widthOfType(slot->type);
    if (slot->kind == ir::SlotKind::Global) {
        storeAcToLabel(label(slot), width);
        return;
    }
    slotAddrToPtr(slot);
    storeAcIndirect(width);
}

void PIC18Codegen::inc16(const std::string &lbl) {
    std::string Lskip = newLabel();
    emit("incfsz", cat(lbl, ",f"));
    emit("goto", Lskip);
    emit("incf", cat(lbl, "+1,f"));
    emitLabel(Lskip);
}

void PIC18Codegen::testAcZero() {
    emit("movf", "ac,w");
    emit("iorwf", "ac+1,w");
}

void PIC18Codegen::gotoIfZero(const std::string &target) {
    emit("btfsc", "STATUS,2");
    emit("goto", target);
}

void PIC18Codegen::gotoIfNonZero(const std::string &target) {
    emit("btfss", "STATUS,2");
    emit("goto", target);
}

// POSTINC0 reads/writes through FSR0 and auto-increments it in the same
// instruction, one fewer than PIC16's FSR/INDF-plus-explicit-INCF pair.
void PIC18Codegen::storeAcIndirect(int width) {
    emit("movf", "ptr,w");
    emit("movwf", "FSR0L");
    if (width == 1) {
        emit("movf", "ac,w");
        emit("movwf", "INDF0");
        return;
    }
    emit("movf", "ac,w");
    emit("movwf", "POSTINC0");
    emit("movf", "ac+1,w");
    emit("movwf", "INDF0");
}

void PIC18Codegen::loadAcIndirect(int width, bool isUnsigned) {
    emit("movf", "ptr,w");
    emit("movwf", "FSR0L");
    if (width == 1) {
        emit("movf", "INDF0,w");
        emit("movwf", "ac");
        signExtendAcHighByte(isUnsigned);
        return;
    }
    emit("movf", "POSTINC0,w");
    emit("movwf", "ac");
    emit("movf", "INDF0,w");
    emit("movwf", "ac+1");
}

void PIC18Codegen::signExtendAcHighByte(bool isUnsigned) {
    if (isUnsigned) {
        emit("clrf", "ac+1");
        return;
    }
    emit("movf", "ac,w");
    emit("andlw", "0x80");
    std::string Lneg = newLabel(), Ldone = newLabel();
    gotoIfNonZero(Lneg);
    emit("clrf", "ac+1");
    emit("goto", Ldone);
    emitLabel(Lneg);
    emit("movlw", "0xFF");
    emit("movwf", "ac+1");
    emitLabel(Ldone);
}

void PIC18Codegen::loadAcFromLabel(const std::string &lbl, int width, bool isUnsigned) {
    emit("movff", cat(lbl, ",ac"));
    if (width == 1) {
        signExtendAcHighByte(isUnsigned);
        return;
    }
    emit("movff", cat(lbl, "+1,ac+1"));
}

void PIC18Codegen::storeAcToLabel(const std::string &lbl, int width) {
    emit("movff", cat("ac,", lbl));
    if (width == 1) return;
    emit("movff", cat("ac+1,", lbl, "+1"));
}

// Runtime push/pop on the shared `SSTACK` region via `SP`/FSR0.
// POSTINC0 (access, then increment) is a natural push. There's no
// hardware pre-decrement mode on this core, so pop decrements FSR0L
// explicitly before each plain INDF0 access. `SP` is kept in sync with
// FSR0L by copying it back out afterward.
void PIC18Codegen::pushCell2(const std::string &src) {
    emit("movf", "SP,w");
    emit("movwf", "FSR0L");
    emit("movf", cat(src, ",w"));
    emit("movwf", "POSTINC0");
    emit("movf", cat(src, "+1,w"));
    emit("movwf", "POSTINC0");
    emit("movf", "FSR0L,w");
    emit("movwf", "SP");
}

void PIC18Codegen::popCell2(const std::string &dst) {
    emit("movf", "SP,w");
    emit("movwf", "FSR0L");
    emit("decf", "FSR0L,f");
    emit("movf", "INDF0,w");
    emit("movwf", cat(dst, "+1"));
    emit("decf", "FSR0L,f");
    emit("movf", "INDF0,w");
    emit("movwf", dst);
    emit("movf", "FSR0L,w");
    emit("movwf", "SP");
}

void PIC18Codegen::pushPtrCell() {
    emit("movf", "SP,w");
    emit("movwf", "FSR0L");
    emit("movf", "ptr,w");
    emit("movwf", "POSTINC0");
    emit("movf", "FSR0L,w");
    emit("movwf", "SP");
}

void PIC18Codegen::popPtrCell() {
    emit("movf", "SP,w");
    emit("movwf", "FSR0L");
    emit("decf", "FSR0L,f");
    emit("movf", "INDF0,w");
    emit("movwf", "ptr");
    emit("movf", "FSR0L,w");
    emit("movwf", "SP");
}

std::string PIC18Codegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    // mainFn is only ever null in object mode, for a file that doesn't
    // define `main` itself.

    if (objectMode) {
        // Plain compile-time equates, identical in every object and in
        // PIC18LinkerFactory.cpp's entry object, so none need relocation.
        emitEquate("ac", 0);
        emitEquate("bc", 2);
        emitEquate("cx", 4);
        emitEquate("dx", 6);
        emitEquate("ptr", 8);
        emitEquate("SP", 9);
        emitEquate("SSTACK", 10);
        emitEquate("FP", 34);
        emitEquate("NFP", 35);
        emitEquate("FSTACK", 36);
    } else {
        declareRam("ac", 2, false);
        declareRam("bc", 2, false);
        declareRam("cx", 2, false);
        declareRam("dx", 2, false);
        declareRam("ptr", 1, false);
        declareRam("SP", 1, false);
        declareRam("SSTACK", 24, false);
        declareRam("FP", 1, false);
        declareRam("NFP", 1, false);
        declareRam("FSTACK", kFrameStackBytes, false);
    }

    // --dump-asm wants one file per input, not one combined dump - see
    // AlphaCodegen.cpp's own generate() for the pattern this mirrors
    // (tellp()-position ranges, sliced into perFileAsm_ at the end).
    // `globalInit`'s own content is tracked against itself (not
    // wherever it's later spliced to - `header` or `code_`, depending
    // on `objectMode`), so the mainFn branch below is inlined rather
    // than going through genFunction()'s own `extraPrologue` splice,
    // to skip over that span instead of re-counting it.
    std::vector<std::tuple<std::string, size_t, size_t>> initRanges;
    std::vector<std::tuple<std::string, size_t, size_t>> ramRanges;
    std::vector<std::tuple<std::string, size_t, size_t>> codeRanges;

    std::ostringstream globalInit;
    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        if (slot->isExternal) continue;
        if (debugMapEnabled_) debugSymbols_.push_back({label(slot.get()), /*isFunc=*/false});
        size_t ramStart = ramDecls_.size();
        size_t initStart = static_cast<size_t>(globalInit.tellp());
        const std::string lbl = label(slot.get());
        int elemWidth = widthOfType(slot->type);
        if (slot->arraySize > 0) {
            declareRam(lbl, slot->arraySize * elemWidth, false);
            for (size_t i = 0; i < slot->arrayInit.size(); i++) {
                std::string base = lbl + "+" + std::to_string(i * elemWidth);
                emitInitElem(globalInit, base, elemWidth, slot->arrayInit[i]);
            }
        } else {
            declareRam(lbl, elemWidth, false);
            long v = slot->hasInit ? slot->initValue : 0;
            for (int i = 0; i < elemWidth; i++) {
                uint8_t b = static_cast<uint8_t>((static_cast<unsigned long>(v) >> (8 * i)) & 0xFF);
                emitInitByte(globalInit, i == 0 ? lbl : lbl + "+" + std::to_string(i), b);
            }
        }
        ramRanges.push_back({slot->sourceFile, ramStart, ramDecls_.size()});
        initRanges.push_back({slot->sourceFile, initStart, static_cast<size_t>(globalInit.tellp())});
    }

    for (const auto &fb : prog.functionBodies) {
        if (objectMode && fb.func == mainFn) {
            // globalInit has nowhere else to run in object mode, so it
            // runs in fn_main's own prologue instead.
            lastDebugLine_ = 0;
            if (debugMapEnabled_) debugSymbols_.push_back({label(fb.func), /*isFunc=*/true});
            paramCount_ = static_cast<int>(fb.func->params.size());
            frameSizeBytes_ = paramCount_ * 2 + fb.func->frameSize;
            size_t codeStart = static_cast<size_t>(code_.tellp());
            emitLabel(label(fb.func));
            emitPrologue();
            codeRanges.push_back({fb.func->sourceFile, codeStart, static_cast<size_t>(code_.tellp())});
            code_ << globalInit.str();
            codeStart = static_cast<size_t>(code_.tellp());
            genStmt(*fb.body);
            emitEpilogue();
            emit("return");
            emitRaw("");
            codeRanges.push_back({fb.func->sourceFile, codeStart, static_cast<size_t>(code_.tellp())});
        } else {
            size_t codeStart = static_cast<size_t>(code_.tellp());
            genFunction(fb);
            codeRanges.push_back({fb.func->sourceFile, codeStart, static_cast<size_t>(code_.tellp())});
        }
    }

    std::string codeStr = code_.str();
    std::string initStr = globalInit.str();
    perFileAsm_.clear();
    for (const auto &[file, s, e] : codeRanges) perFileAsm_[file] += codeStr.substr(s, e - s);
    for (const auto &[file, s, e] : initRanges) perFileAsm_[file] += initStr.substr(s, e - s);
    for (const auto &[file, s, e] : ramRanges) {
        for (size_t i = s; i < e; i++) perFileAsm_[file] += ramDecls_[i] + "\n";
    }

    std::ostringstream header;
    // Access Bank SFR short addresses, resolved via the Access Bank's
    // 0x60-0xFF->0xF60-0xFFF hardware aliasing.
    header << "STATUS = 0xD8\nFSR0L = 0xE9\nFSR0H = 0xEA\nPOSTINC0 = 0xEE\nINDF0 = 0xEF\nINTCON = 0xF2\n";
    header << "PRODL = 0xF3\nPRODH = 0xF4\n";
    // Program-counter-latch and top-of-return-stack registers, used by
    // genExpr's CallPtr case.
    header << "PCL = 0xF9\nPCLATH = 0xFA\nPCLATU = 0xFB\nSTKPTR = 0xFC\nTOSL = 0xFD\nTOSH = 0xFE\nTOSU = 0xFF\n\n";
    if (objectMode) {
        // PIC18LinkerFactory.cpp's synthesized entry object is solely
        // responsible for the reset vector, FSR0H/SP setup, and
        // call fn_main/halt, once every object is merged.
        header << "org 0\n\n";
    } else {
        header << "org 0\n    goto START\n\n";
        header << "START:\n";
        header << "    clrf FSR0H\n";  // data is restricted to the Access Bank's low byte, so FSR0H never changes
        header << "    movlw SSTACK\n    movwf SP\n";
        for (const auto &entry : zeroAtStart_) {
            for (int i = 0; i < entry.second; i++) {
                header << "    clrf " << entry.first;
                if (i > 0) header << "+" << i;
                header << "\n";
            }
        }
        header << globalInit.str();
        // fn_main's prologue expects `NFP` to hold its new frame's
        // address; FSTACK+1 leaves one byte at FSTACK+0 for the
        // prologue's "caller's FP" save, never meaningfully read back
        // since `main` has no real caller in whole-program mode.
        header << "    movlw FSTACK+1\n    movwf NFP\n";
        header << "    call " << label(mainFn) << "\n";
        header << "HALT:\n    goto HALT\n\n";
    }

    std::ostringstream out;
    out << header.str();
    out << code_.str();
    out << "\norgram 0x00\n";
    for (const auto &l : ramDecls_) out << l << "\n";
    return out.str();
}

std::map<std::string, std::string> PIC18Codegen::perFileAsm() const { return perFileAsm_; }

std::optional<std::string> PIC18Codegen::debugMap(const std::string &asmText, const std::string &format,
                                                    const std::string &sourcePath) const {
    if (format != "hex") return std::nullopt;
    return assemblePIC18DebugMap(asmText, sourcePath, debugSymbols_);
}

std::optional<std::string> PIC18Codegen::debugMapPreLink(const std::string &asmText,
                                                    const std::string &sourcePath) const {
    return assemblePIC18DebugMap(asmText, sourcePath, debugSymbols_, /*allowExternal=*/true);
}

// Only emits a marker when `debugMapEnabled_` (set via setDebugMapEnabled(),
// before generate() runs) and `line` actually differs from the last one
// emitted - see Pdp7Codegen.cpp's identical emitSourceLine(), which this
// mirrors exactly.
void PIC18Codegen::emitSourceLine(int line) {
    if (!debugMapEnabled_ || line == 0 || line == lastDebugLine_) return;
    emitRaw(cat("__line ", line));
    lastDebugLine_ = line;
}

void PIC18Codegen::genFunction(const ir::FunctionBody &fn, const std::string &extraPrologue) {
    lastDebugLine_ = 0;
    if (debugMapEnabled_) debugSymbols_.push_back({label(fn.func), /*isFunc=*/true});
    paramCount_ = static_cast<int>(fn.func->params.size());
    frameSizeBytes_ = paramCount_ * 2 + fn.func->frameSize;
    emitLabel(label(fn.func));
    emitPrologue();
    code_ << extraPrologue;  // a pre-built blob (globalInit's own text), not a hardcoded instruction
    genStmt(*fn.body);
    emitEpilogue();
    emit("return");
    emitRaw("");
}

void PIC18Codegen::genStmt(const ir::Stmt &s) {
    emitSourceLine(s.line);
    switch (s.kind) {
        case ir::StmtKind::Block: {
            for (const auto &child : s.body) genStmt(*child);
            return;
        }
        case ir::StmtKind::If: {
            genExpr(*s.cond);
            testAcZero();
            std::string Lelse = s.elseStmt ? newLabel() : "";
            std::string Ldone = newLabel();
            gotoIfZero(s.elseStmt ? Lelse : Ldone);
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
            std::string Lstart = newLabel(), Lend = newLabel();
            std::string Lcont = s.postStmt ? newLabel() : Lstart;
            emitLabel(Lstart);
            genExpr(*s.cond);
            testAcZero();
            gotoIfZero(Lend);
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
            if (s.initOrReturnOrExpr) genExpr(*s.initOrReturnOrExpr);
            emitEpilogue();
            emit("return");
            return;
        }
        case ir::StmtKind::LocalDecl: {
            // Space for every Local is already accounted for; this only
            // ever has a scalar initializer left to emit.
            if (s.declSlot->arraySize == 0 && s.initOrReturnOrExpr) {
                genExpr(*s.initOrReturnOrExpr);
                storeAcToSlot(s.declSlot);
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

void PIC18Codegen::requireNarrow(const ir::Expr &e) const {
    if (widthOfType(e.type) > 2) {
        err(e.line, "'long'/'long long' arithmetic isn't supported on this target - "
                    "see PIC18Codegen.h's own header comment on requireNarrow()");
    }
}

void PIC18Codegen::genConvert(const ir::Expr &e) {
    genExpr(*e.lhs);
    if (widthOfType(e.type) != 1) return;
    signExtendAcHighByte(e.type.isUnsigned);
}

void PIC18Codegen::genExpr(const ir::Expr &e) {
    requireNarrow(e);
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit: {
            uint16_t v16 = static_cast<uint16_t>(e.intValue);
            emit("movlw", v16 & 0xFF);
            emit("movwf", "ac");
            emit("movlw", (v16 >> 8) & 0xFF);
            emit("movwf", "ac+1");
            return;
        }
        case ir::ExprKind::Var: {
            loadSlotToAc(e.slot);
            return;
        }
        case ir::ExprKind::Index: {
            genIndex(e);
            return;
        }
        case ir::ExprKind::Assign: {
            if (e.lhs) {  // slot[idx] = rhs
                genIndexAddr(e.slot, *e.lhs);  // ptr = target address
                pushPtrCell();
                genExpr(*e.rhs);
                popPtrCell();
                storeAcIndirect(widthOfType(e.slot->type));
                return;
            }
            genExpr(*e.rhs);
            storeAcToSlot(e.slot);
            return;
        }
        case ir::ExprKind::DerefAssign: {
            genExpr(*e.lhs);  // ac = pointer value
            emit("movf", "ac,w");
            emit("movwf", "ptr");
            pushPtrCell();
            genExpr(*e.rhs);
            popPtrCell();
            storeAcIndirect(widthOfType(e.type));
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            if (e.lhs) {  // slot[idx] op= rhs (including slot[idx]++/--)
                genIndexAddr(e.slot, *e.lhs);  // ptr = target address
                genCompoundThroughPtr(e);
                return;
            }
            // Scalar target - the address computation never touches
            // `ac`, so it's simply recomputed afterward.
            genExpr(*e.rhs);  // ac = rhs
            copy2("dx", "ac");  // dx = rhs, held until consumed
            loadSlotToAc(e.slot);  // ac = old
            copy2("bc", "ac");  // bc = old - genCompoundOp()'s precondition
            if (e.yieldOld) pushCell2("bc");  // old, saved across genCompoundOp()
            copy2("ac", "dx");  // ac = rhs - genCompoundOp()'s other precondition
            genCompoundOp(e.op, e.type);
            narrowAcTo(e.type);
            storeAcToSlot(e.slot);
            if (e.yieldOld) popCell2("ac");  // ac = old (final result)
            return;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            genExpr(*e.lhs);  // ac = pointer value
            emit("movf", "ac,w");
            emit("movwf", "ptr");
            genCompoundThroughPtr(e);
            return;
        }
        case ir::ExprKind::Ternary: {
            genExpr(*e.lhs);
            testAcZero();
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            gotoIfZero(Lelse);
            genExpr(*e.rhs);
            emit("goto", Ldone);
            emitLabel(Lelse);
            genExpr(*e.elseExpr);
            emitLabel(Ldone);
            return;
        }
        case ir::ExprKind::Unary: {
            if (e.op == "-") {
                genExpr(*e.rhs);
                emit("comf", "ac,f");
                emit("comf", "ac+1,f");
                inc16("ac");
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                genExpr(*e.rhs);  // ac = pointer value
                emit("movf", "ac,w");
                emit("movwf", "ptr");
                loadAcIndirect(widthOfType(e.type), e.type.isUnsigned);
            } else if (e.op == "~") {
                genExpr(*e.rhs);
                emit("comf", "ac,f");
                emit("comf", "ac+1,f");
            } else {  // "&"
                genAddrOf(*e.rhs);
            }
            return;
        }
        case ir::ExprKind::Call: {
            // The callee's new frame starts at FP + frameSizeBytes_, plus
            // one byte for the callee's saved-FP slot. frameSizeBytes_ is
            // a compile-time constant, so this offset from the current
            // `FP` is correct at any call depth, recursive or not.
            int newFrameOff = frameSizeBytes_ + 1;
            for (size_t i = 0; i < e.args.size(); i++) {
                genExpr(*e.args[i]);  // ac = this argument's value
                // ptr = FP + newFrameOff + i*2, recomputed fresh for every
                // argument since evaluating one may itself call through a
                // complete frame.
                emit("movf", "FP,w");
                emit("addlw", newFrameOff + static_cast<int>(i) * 2);
                emit("movwf", "ptr");
                storeAcIndirect(2);  // every argument reserves a native-int-width slot
            }
            // NFP = FP + newFrameOff, recomputed fresh immediately before
            // `call` so no argument evaluation above can have clobbered it.
            emit("movf", "FP,w");
            emit("addlw", newFrameOff);
            emit("movwf", "NFP");
            emit("call", label(e.func));
            return;
        }
        case ir::ExprKind::CallPtr: {
            // This core has no indirect-CALL instruction and no CALLW,
            // so this is the standard mechanism: PUSH reserves a fresh
            // slot on the hardware return stack, then its three bytes
            // (TOSU/TOSH/TOSL) are overwritten with the real desired
            // return point (a symbolic label, never a hand-counted
            // offset). PCLATU/PCLATH/PCL are then loaded from the
            // callee's address, PCL last - writing it latches
            // PCLATH/PCLATU into the PC and jumps.
            genExpr(*e.lhs);  // ac/ac+1 = the callee's own address
            // Same NFP handoff as the direct Call case; IRBuilder.cpp
            // enforces 0 arguments for this kind. Recomputed fresh after
            // evaluating the callee, which may itself call through a
            // normal frame.
            int newFrameOff = frameSizeBytes_ + 1;
            emit("movf", "FP,w");
            emit("addlw", newFrameOff);
            emit("movwf", "NFP");
            std::string Lret = newLabel();
            emit("push");
            emit("movlwcl", Lret);
            emit("movwf", "TOSL");
            emit("movlwch", Lret);
            emit("movwf", "TOSH");
            emit("clrf", "TOSU");
            emit("clrf", "PCLATU");
            emit("movf", "ac+1,w");
            emit("movwf", "PCLATH");
            emit("movf", "ac,w");
            emit("movwf", "PCL");
            emitLabel(Lret);
            return;
        }
        case ir::ExprKind::Binary: {
            genBinary(e);
            return;
        }
        case ir::ExprKind::Convert: {
            genConvert(e);
            return;
        }
        case ir::ExprKind::Intrinsic: {
            // Global-interrupt enable/disable: INTCON<7> (GIE).
            emit(e.op == "ion" ? "bsf" : "bcf", "INTCON,7");
            return;
        }
        case ir::ExprKind::Func:
            err(e.line, "internal: Func reached genExpr - should only ever appear as '&'s operand");
    }
}

void PIC18Codegen::genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
    genExpr(idxExpr);  // ac = index
    // ac *= element width (elemShift()); a char element needs no shift.
    for (int i = 0, n = elemShift(slot->type); i < n; i++) shiftOneBit("ac", /*isLeft=*/true);
    if (slot->arraySize > 0) {
        // `slot` is the array itself; its base address is computed into
        // `bc`, since `ac` still holds `index*2`.
        if (slot->kind == ir::SlotKind::Global) {
            emit("movlw", label(slot));
            emit("movwf", "bc");
        } else {
            emit("movf", "FP,w");
            int off = slotFrameOffsetBytes(slot);
            if (off != 0) emit("addlw", off);
            emit("movwf", "bc");
        }
    } else {
        // `slot` holds a pointer value - save index*2 in `bc` before
        // loadSlotToAc() overwrites `ac` with that value.
        emit("movf", "ac,w");
        emit("movwf", "bc");
        loadSlotToAc(slot);  // ac = the pointer's own current value
    }
    emit("movf", "bc,w");
    emit("addwf", "ac,w");
    emit("movwf", "ptr");
}

void PIC18Codegen::genIndex(const ir::Expr &e) {
    genIndexAddr(e.slot, *e.lhs);
    loadAcIndirect(widthOfType(e.slot->type), e.slot->type.isUnsigned);
}

// A literal element's value is split into two compile-time-known bytes
// and loaded via emitInitByte(). A symbol element (dispatch-table
// address-of-function/global) has no compile-time byte value: a
// Function's address splits low/high byte like genAddrOf()'s Func case
// (movlwcl/movlwch); a Slot's one-byte RAM address loads with a zero
// high byte, like genAddrOf()'s Var case.
void PIC18Codegen::emitInitElem(std::ostream &out, const std::string &base, int width, const ir::ArrayInitElem &e) {
    if (e.isSymbol) {
        const std::string hi = base + "+1";
        if (e.func) {
            out << "    movlwcl " << label(e.func) << "\n    movwf " << base << "\n";
            out << "    movlwch " << label(e.func) << "\n    movwf " << hi << "\n";
        } else {
            out << "    movlw " << label(e.slot) << "\n    movwf " << base << "\n";
            out << "    clrf " << hi << "\n";
        }
        for (int i = 2; i < width; i++) out << "    clrf " << base << "+" << i << "\n";
        return;
    }
    for (int i = 0; i < width; i++) {
        uint8_t b = static_cast<uint8_t>((static_cast<unsigned long>(e.value) >> (8 * i)) & 0xFF);
        emitInitByte(out, i == 0 ? base : base + "+" + std::to_string(i), b);
    }
}

void PIC18Codegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        if (operand.slot->kind == ir::SlotKind::Global) {
            emit("movlw", label(operand.slot));
            emit("movwf", "ac");
        } else {
            // A Local/Param's address is FP plus a compile-time offset -
            // a runtime value, unlike a Global's fixed label above.
            emit("movf", "FP,w");
            int off = slotFrameOffsetBytes(operand.slot);
            if (off != 0) emit("addlw", off);
            emit("movwf", "ac");
        }
        emit("clrf", "ac+1");
        return;
    }
    if (operand.kind == ir::ExprKind::Index) {
        genIndexAddr(operand.slot, *operand.lhs);
        emit("movf", "ptr,w");
        emit("movwf", "ac");
        emit("clrf", "ac+1");
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        // `ac` holds fn's program-memory address, split low/high byte.
        // GOTO/CALL always encode their target directly in the
        // instruction word, so there's no single instruction for taking
        // a function's address as plain data; see genExpr's CallPtr case
        // for calling through a value in this form.
        emit("movlwcl", label(operand.func));
        emit("movwf", "ac");
        emit("movlwch", label(operand.func));
        emit("movwf", "ac+1");
        return;
    }
    err(operand.line, "internal: unhandled addressable-operand kind");
}

void PIC18Codegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    if (op == "+" || op == "-") {
        genExpr(*e.lhs);
        pushCell2("ac");
        genExpr(*e.rhs);
        popCell2("bc");
        if (op == "+") {
            add16("ac", "bc", "ac");
        } else {
            sub16("ac", "bc", "ac");
        }
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
    if (op == "&" || op == "|" || op == "^") {
        genExpr(*e.lhs);
        pushCell2("ac");
        genExpr(*e.rhs);
        popCell2("bc");
        const char *insn = (op == "&") ? "andwf" : (op == "|") ? "iorwf" : "xorwf";
        emit("movf", "bc,w");
        emit(insn, "ac,w");
        emit("movwf", "ac");
        emit("movf", "bc+1,w");
        emit(insn, "ac+1,w");
        emit("movwf", "ac+1");
        return;
    }
    if (op == "<<" || op == ">>") {
        genShift(*e.lhs, *e.rhs, op == "<<");
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
    genCompare(op, *e.lhs, *e.rhs);
}

void PIC18Codegen::genShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft) {
    genExpr(lhs);
    pushCell2("ac");
    genExpr(rhs);
    emit("movf", "ac,w");
    emit("movwf", "cx");
    popCell2("ac");
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("movf", "cx,w");
    emit("btfsc", "STATUS,2");
    emit("goto", Ldone);
    shiftOneBit("ac", isLeft);
    emit("decf", "cx,f");
    emit("goto", Lloop);
    emitLabel(Ldone);
}

// True when `e` is an integer-promoted `unsigned char` (or narrower unsigned) operand: the
// pre-promotion type IRBuilder's promote() wrapped is exactly what MULWF's 8x8->16 unsigned
// multiply computes, so the hardware result needs no further adjustment.
bool PIC18Codegen::isUnsignedByteOperand(const ir::Expr &e) const {
    return e.kind == ir::ExprKind::Convert && widthOfType(e.lhs->type) == 1 && e.lhs->type.isUnsigned;
}

void PIC18Codegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    if (isUnsignedByteOperand(lhs) && isUnsignedByteOperand(rhs)) {
        genExpr(lhs);
        pushCell2("ac");
        genExpr(rhs);
        // popCell2() uses W as scratch, so bc=lhs must be popped before ac=rhs is read into W.
        popCell2("bc");
        emit("movf", "ac,w");
        emit("mulwf", "bc");
        emit("movff", "PRODL,ac");
        emit("movff", "PRODH,ac+1");
        return;
    }
    genExpr(lhs);
    pushCell2("ac");
    genExpr(rhs);
    copy2("dx", "ac");
    popCell2("bc");
    clear2("ac");
    emit("movlw", 16);
    emit("movwf", "cx");
    std::string Lloop = newLabel(), Ldone = newLabel(), Lskipadd = newLabel();
    emitLabel(Lloop);
    emit("movf", "cx,w");
    emit("btfsc", "STATUS,2");
    emit("goto", Ldone);
    emit("movf", "dx,w");
    emit("andlw", 1);
    emit("btfsc", "STATUS,2");
    emit("goto", Lskipadd);
    add16("ac", "ac", "bc");
    emitLabel(Lskipadd);
    shiftOneBit("bc", /*isLeft=*/true);
    shiftOneBit("dx", /*isLeft=*/false);
    emit("decf", "cx,f");
    emit("goto", Lloop);
    emitLabel(Ldone);
}

void PIC18Codegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder) {
    genExpr(lhs);
    pushCell2("ac");
    genExpr(rhs);
    copy2("bc", "ac");
    popCell2("dx");
    clear2("ac");
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    sub16("cx", "dx", "bc");
    emit("btfss", "STATUS,0");
    emit("goto", Ldone);
    copy2("dx", "cx");
    inc16("ac");
    emit("goto", Lloop);
    emitLabel(Ldone);
    if (wantRemainder) copy2("ac", "dx");
}

// Precondition: `bc` holds `old`, `ac` holds `rhs` - the same register
// roles genBinary()'s operator bodies settle into after evaluating their
// own lhs/rhs. Leaves the raw new value in `ac`.
void PIC18Codegen::genCompoundOp(const std::string &op, Type) {
    if (op == "+") {
        add16("ac", "bc", "ac");
        return;
    }
    if (op == "-") {
        sub16("ac", "bc", "ac");
        return;
    }
    if (op == "*") {
        copy2("dx", "ac");  // dx = multiplier (rhs) - bc already holds the multiplicand (old)
        clear2("ac");
        emit("movlw", 16);
        emit("movwf", "cx");
        std::string Lloop = newLabel(), Ldone = newLabel(), Lskipadd = newLabel();
        emitLabel(Lloop);
        emit("movf", "cx,w");
        emit("btfsc", "STATUS,2");
        emit("goto", Ldone);
        emit("movf", "dx,w");
        emit("andlw", 1);
        emit("btfsc", "STATUS,2");
        emit("goto", Lskipadd);
        add16("ac", "ac", "bc");
        emitLabel(Lskipadd);
        shiftOneBit("bc", /*isLeft=*/true);
        shiftOneBit("dx", /*isLeft=*/false);
        emit("decf", "cx,f");
        emit("goto", Lloop);
        emitLabel(Ldone);
        return;
    }
    if (op == "/" || op == "%") {
        copy2("dx", "bc");  // dx = dividend (old) - copied before bc is overwritten below
        copy2("bc", "ac");   // bc = divisor (rhs)
        clear2("ac");
        std::string Lloop = newLabel(), Ldone = newLabel();
        emitLabel(Lloop);
        sub16("cx", "dx", "bc");
        emit("btfss", "STATUS,0");
        emit("goto", Ldone);
        copy2("dx", "cx");
        inc16("ac");
        emit("goto", Lloop);
        emitLabel(Ldone);
        if (op == "%") copy2("ac", "dx");
        return;
    }
    if (op == "&" || op == "|" || op == "^") {
        const char *insn = (op == "&") ? "andwf" : (op == "|") ? "iorwf" : "xorwf";
        emit("movf", "bc,w");
        emit(insn, "ac,w");
        emit("movwf", "ac");
        emit("movf", "bc+1,w");
        emit(insn, "ac+1,w");
        emit("movwf", "ac+1");
        return;
    }
    // "<<" or ">>"
    emit("movf", "ac,w");
    emit("movwf", "cx");  // cx = shift count (rhs's own low byte)
    copy2("ac", "bc");    // ac = value to shift (old)
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("movf", "cx,w");
    emit("btfsc", "STATUS,2");
    emit("goto", Ldone);
    shiftOneBit("ac", op == "<<");
    emit("decf", "cx,f");
    emit("goto", Lloop);
    emitLabel(Ldone);
}

// `ptr` holds the resolved address on entry, stashed via pushPtrCell()
// while `rhs` (which may itself clobber every pseudo-register) is
// evaluated. `yieldOld`'s value survives on `SSTACK` across
// genCompoundOp()'s use of bc/cx/dx.
void PIC18Codegen::genCompoundThroughPtr(const ir::Expr &e) {
    pushPtrCell();        // stash address
    genExpr(*e.rhs);        // ac = rhs
    copy2("dx", "ac");         // dx = rhs, held until genCompoundOp() consumes it
    popPtrCell();                // ptr = address, restored only now
    loadAcIndirect(widthOfType(e.type), e.type.isUnsigned);  // ac = old
    copy2("bc", "ac");                                         // bc = old - genCompoundOp()'s precondition
    if (e.yieldOld) pushCell2("bc");  // old, saved across genCompoundOp()
    copy2("ac", "dx");                  // ac = rhs - genCompoundOp()'s other precondition
    genCompoundOp(e.op, e.type);          // ac = new (raw)
    narrowAcTo(e.type);
    storeAcIndirect(widthOfType(e.type));  // ptr still untouched since popPtrCell()
    if (e.yieldOld) popCell2("ac");          // ac = old (final result)
}

void PIC18Codegen::narrowAcTo(Type to) {
    if (widthOfType(to) != 1) return;
    signExtendAcHighByte(to.isUnsigned);
}

void PIC18Codegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs) {
    if (op == "==") return emitEqualityCompare(lhs, rhs, /*wantEqual=*/true);
    if (op == "!=") return emitEqualityCompare(lhs, rhs, /*wantEqual=*/false);
    if (op == "<") return emitLessCompare(lhs, rhs, /*invert=*/false);
    if (op == ">") return emitLessCompare(rhs, lhs, /*invert=*/false);
    if (op == "<=") return emitLessCompare(rhs, lhs, /*invert=*/true);
    if (op == ">=") return emitLessCompare(lhs, rhs, /*invert=*/true);
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

void PIC18Codegen::emitEqualityCompare(const ir::Expr &lhs, const ir::Expr &rhs, bool wantEqual) {
    genExpr(lhs);
    pushCell2("ac");
    genExpr(rhs);
    popCell2("bc");  // bc = lhs, ac = rhs
    sub16("cx", "bc", "ac");
    emit("movf", "cx,w");
    emit("iorwf", "cx+1,w");
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit(wantEqual ? "btfsc" : "btfss", "STATUS,2");
    emit("goto", Ltrue);
    clear2("ac");
    emit("goto", Ldone);
    emitLabel(Ltrue);
    emit("movlw", 1);
    emit("movwf", "ac");
    emit("clrf", "ac+1");
    emitLabel(Ldone);
}

// Signed 16-bit "a < b" via the N-xor-OV rule: after sub16()'s final
// SUBWFB, "a < b" holds iff STATUS<N> and STATUS<OV> disagree.
void PIC18Codegen::emitLessCompare(const ir::Expr &a, const ir::Expr &b, bool invert) {
    genExpr(a);
    pushCell2("ac");
    genExpr(b);
    popCell2("bc");  // bc = a, ac = b
    sub16("cx", "bc", "ac");  // cx = a - b; STATUS<N,OV> reflect the signed result
    std::string LovSet = newLabel(), Lless = newLabel(), Lnotless = newLabel(), Ldone = newLabel();
    emit("btfsc", "STATUS,3");
    emit("goto", LovSet);
    emit("btfsc", "STATUS,4");
    emit("goto", Lless);
    emit("goto", Lnotless);
    emitLabel(LovSet);
    emit("btfss", "STATUS,4");
    emit("goto", Lless);
    emit("goto", Lnotless);
    emitLabel(Lless);
    if (!invert) {
        emit("movlw", 1);
        emit("movwf", "ac");
        emit("clrf", "ac+1");
    } else {
        clear2("ac");
    }
    emit("goto", Ldone);
    emitLabel(Lnotless);
    if (!invert) {
        clear2("ac");
    } else {
        emit("movlw", 1);
        emit("movwf", "ac");
        emit("clrf", "ac+1");
    }
    emitLabel(Ldone);
}

void PIC18Codegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    genExpr(lhs);
    testAcZero();
    gotoIfZero(Lfalse);
    genExpr(rhs);
    testAcZero();
    gotoIfZero(Lfalse);
    emitLabel(Ltrue);
    emit("movlw", 1);
    emit("movwf", "ac");
    emit("clrf", "ac+1");
    emit("goto", Ldone);
    emitLabel(Lfalse);
    clear2("ac");
    emitLabel(Ldone);
}

void PIC18Codegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    genExpr(lhs);
    testAcZero();
    gotoIfNonZero(Ltrue);
    genExpr(rhs);
    testAcZero();
    gotoIfNonZero(Ltrue);
    emitLabel(Lfalse);
    clear2("ac");
    emit("goto", Ldone);
    emitLabel(Ltrue);
    emit("movlw", 1);
    emit("movwf", "ac");
    emit("clrf", "ac+1");
    emitLabel(Ldone);
}

void PIC18Codegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    testAcZero();
    std::string Ltrue = newLabel(), Ldone = newLabel();
    gotoIfZero(Ltrue);
    clear2("ac");
    emit("goto", Ldone);
    emitLabel(Ltrue);
    emit("movlw", 1);
    emit("movwf", "ac");
    emit("clrf", "ac+1");
    emitLabel(Ldone);
}

std::optional<std::string> PIC18Codegen::assembleInProcess(const std::string &asmText,
                                                             const std::string &format) const {
    if (format == "hex") return assemblePIC18(asmText);
    // coff/elf/macho/aout/objraw: build one format-neutral ObjectFile,
    // then pick that format's FileFormatWriter to serialize it.
    if (auto kind = fileFormatKindFromName(format)) {
        ObjectFile obj = assemblePIC18Object(asmText);
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

}  // namespace rcc
