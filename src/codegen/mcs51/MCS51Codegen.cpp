#include "MCS51Codegen.h"

#include <cstdint>
#include <stdexcept>

#include "MCS51Assembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

// The software call-frame stack every activation's locals/params are
// carved out of at runtime: one fixed-size XDATA buffer. 2048 bytes is
// comfortably past any realistic recursion depth; overflow past it is
// unchecked.
constexpr const char *kFrameStackLabel = "__frame_stack";
constexpr int kFrameStackBytes = 2048;
}  // namespace

void MCS51Codegen::allocXdata(const std::string &lbl, int count) {
    // `lbl` and the `xdata` statement must share one physical line
    // (the grammar only attaches a label to a statement on that line).
    emitRaw(cat(lbl, ": xdata ", count));
}

void MCS51Codegen::declareGlobalTemp(const std::string &lbl, long initValue) {
    allocXdata(lbl, 2);
    uint16_t v16 = static_cast<uint16_t>(initValue);
    initCode_ << "    movdptr " << lbl << "\n";
    initCode_ << "    movai " << (v16 & 0xFF) << "\n    movxw\n";
    initCode_ << "    incdptr\n";
    initCode_ << "    movai " << ((v16 >> 8) & 0xFF) << "\n    movxw\n";
}

void MCS51Codegen::declareGlobalStruct(const std::string &lbl, int size) {
    allocXdata(lbl, size);
    for (int i = 0; i < size; i++) {
        std::string byteExpr = (i == 0) ? lbl : lbl + "+" + std::to_string(i);
        initCode_ << "    movdptr " << byteExpr << "\n";
        initCode_ << "    movai 0\n    movxw\n";
    }
}

// Writes array element `e` into the XDATA cell at `elemExpr`, exactly
// `elemWidth` bytes. A symbol element (address-of function/global) has
// no compile-time byte value, so its address is loaded into `bc` via
// loadCodeAddress()/loadXdataAddress() first, then stored - always a
// full 2-byte address regardless of `elemWidth`.
void MCS51Codegen::genArrayElementInit(const std::string &elemExpr, int elemWidth, const ir::ArrayInitElem &e) {
    if (e.isSymbol) {
        if (e.func) {
            initCode_ << "    movdptrc " << label(e.func) << "\n";
        } else {
            initCode_ << "    movdptr " << label(e.slot) << "\n";
        }
        initCode_ << "    mova dpl\n    movd bc\n";
        initCode_ << "    mova dph\n    movd bc+1\n";
        initCode_ << "    movdptr " << elemExpr << "\n";
        initCode_ << "    mova bc\n    movxw\n";
        initCode_ << "    incdptr\n";
        initCode_ << "    mova bc+1\n    movxw\n";
        return;
    }
    uint16_t v16 = static_cast<uint16_t>(e.value);
    initCode_ << "    movdptr " << elemExpr << "\n";
    initCode_ << "    movai " << (v16 & 0xFF) << "\n    movxw\n";
    if (elemWidth == 1) return;
    initCode_ << "    incdptr\n";
    initCode_ << "    movai " << ((v16 >> 8) & 0xFF) << "\n    movxw\n";
}

void MCS51Codegen::declareGlobalArray(const std::string &lbl, int arraySize, int elemWidth,
                                       const std::vector<ir::ArrayInitElem> &values) {
    allocXdata(lbl, arraySize * elemWidth);
    for (int i = 0; i < arraySize; i++) {
        // Element i's `movdptr` operand: `lbl` for the first element,
        // `lbl+<byte offset>` otherwise - the assembler folds the
        // offset into the symbol so the whole address stays one
        // relocatable expression.
        std::string elemExpr = (i == 0) ? lbl : lbl + "+" + std::to_string(i * elemWidth);
        static const ir::ArrayInitElem kZero{};
        const ir::ArrayInitElem &e = (static_cast<size_t>(i) < values.size()) ? values[i] : kZero;
        genArrayElementInit(elemExpr, elemWidth, e);
    }
}

void MCS51Codegen::copy16(const std::string &dst, const std::string &src) {
    emit("movdd", cat(dst, ", ", src));
    emit("movdd", cat(dst, "+1, ", src, "+1"));
}

void MCS51Codegen::loadXdata(const std::string &dstReg, const std::string &xdataExpr) {
    emit("movdptr", xdataExpr);
    emit("movxr");
    emit("movd", dstReg);
    emit("incdptr");
    emit("movxr");
    emit("movd", cat(dstReg, "+1"));
}

void MCS51Codegen::storeXdata(const std::string &xdataExpr, const std::string &srcReg) {
    emit("movdptr", xdataExpr);
    emit("mova", srcReg);
    emit("movxw");
    emit("incdptr");
    emit("mova", cat(srcReg, "+1"));
    emit("movxw");
}

void MCS51Codegen::loadXdataAddress(const std::string &dstReg, const std::string &xdataExpr) {
    emit("movdptr", xdataExpr);
    emit("mova", "dpl");
    emit("movd", dstReg);
    emit("mova", "dph");
    emit("movd", cat(dstReg, "+1"));
}

void MCS51Codegen::loadCodeAddress(const std::string &dstReg, const std::string &codeExpr) {
    emit("movdptrc", codeExpr);
    emit("mova", "dpl");
    emit("movd", dstReg);
    emit("mova", "dph");
    emit("movd", cat(dstReg, "+1"));
}

// Loads DPTR from `ptr` and writes `srcReg`'s low `width` bytes there
// one MOVX at a time, without disturbing `ptr`.
void MCS51Codegen::storeIndirect(const std::string &srcReg, int width) {
    emit("movdpl", "ptr");
    emit("movdph", "ptr+1");
    emit("mova", srcReg);
    emit("movxw");
    if (width == 1) return;
    emit("incdptr");
    emit("mova", cat(srcReg, "+1"));
    emit("movxw");
}

void MCS51Codegen::loadIndirect(const std::string &dstReg, int width, bool isUnsigned) {
    emit("movdpl", "ptr");
    emit("movdph", "ptr+1");
    emit("movxr");
    emit("movd", dstReg);
    if (width == 1) {
        if (isUnsigned) {
            emit("movai", 0);
            emit("movd", cat(dstReg, "+1"));
        } else {
            // The `anli`/Z-flag branch must come immediately after
            // `mova`, since any instruction between them could clobber PSW.Z.
            emit("mova", dstReg);
            emit("anli", 128);
            std::string Lneg = newLabel(), Lnonneg = newLabel(), Ldone = newLabel();
            safeBranch("jz", Lnonneg, Lneg);
            emitLabel(Lneg);
            emit("movai", 255);
            emit("movd", cat(dstReg, "+1"));
            emit("ljmp", Ldone);
            emitLabel(Lnonneg);
            emit("movai", 0);
            emit("movd", cat(dstReg, "+1"));
            emitLabel(Ldone);
        }
        return;
    }
    emit("incdptr");
    emit("movxr");
    emit("movd", cat(dstReg, "+1"));
}

void MCS51Codegen::addImm16(const std::string &reg, int value) {
    uint16_t v16 = static_cast<uint16_t>(value);
    emit("mova", reg);
    emit("addi", v16 & 0xFF);
    emit("movd", reg);
    emit("mova", cat(reg, "+1"));
    emit("addci", (v16 >> 8) & 0xFF);
    emit("movd", cat(reg, "+1"));
}

void MCS51Codegen::subImm16(const std::string &reg, int value) {
    uint16_t v16 = static_cast<uint16_t>(value);
    emit("mova", reg);
    emit("clrc");
    emit("subbi", v16 & 0xFF);
    emit("movd", reg);
    emit("mova", cat(reg, "+1"));
    emit("subbi", (v16 >> 8) & 0xFF);
    emit("movd", cat(reg, "+1"));
}

void MCS51Codegen::loadFrameAddress(const std::string &dstReg, const ir::Slot *slot) {
    copy16(dstReg, "fp");
    // A Param's position is a plain word index; a Local's
    // `frameOffset` is a byte offset (1-based from 2), placed right
    // after the params region.
    int byteOffset = (slot->kind == ir::SlotKind::Param) ? slot->paramIndex * 2
                                                           : (curParamCount_ * 2 + slot->frameOffset - 2);
    if (byteOffset != 0) addImm16(dstReg, byteOffset);
}

void MCS51Codegen::pushFrameWord(const std::string &srcReg) {
    emit("movdpl", "ssp");
    emit("movdph", "ssp+1");
    emit("mova", srcReg);
    emit("movxw");
    emit("incdptr");
    emit("mova", cat(srcReg, "+1"));
    emit("movxw");
    addImm16("ssp", 2);
}

void MCS51Codegen::genPrologue(const ir::Function *fn) {
    emit("push", "fp");
    emit("push", "fp+1");
    copy16("fp", "ssp");
    int nparams = static_cast<int>(fn->params.size());
    if (nparams > 0) subImm16("fp", nparams * 2);
    if (fn->frameSize > 0) addImm16("ssp", fn->frameSize);
    curParamCount_ = nparams;
}

void MCS51Codegen::genEpilogue() {
    copy16("ssp", "fp");
    emit("pop", "fp+1");
    emit("pop", "fp");
}

void MCS51Codegen::loadArrayElemValue(const ir::ArrayInitElem &e) {
    if (e.isSymbol) {
        if (e.func) {
            loadCodeAddress("ac", label(e.func));
        } else {
            loadXdataAddress("ac", label(e.slot));
        }
        return;
    }
    uint16_t v16 = static_cast<uint16_t>(e.value);
    emit("movai", v16 & 0xFF);
    emit("movd", "ac");
    emit("movai", (v16 >> 8) & 0xFF);
    emit("movd", "ac+1");
}

void MCS51Codegen::genLocalArrayInit(const ir::Slot *slot) {
    int elemWidth = widthOfType(slot->type);
    loadFrameAddress("bc", slot);
    for (size_t i = 0; i < slot->arrayInit.size(); i++) {
        copy16("ptr", "bc");
        if (i != 0) addImm16("ptr", static_cast<int>(i) * elemWidth);
        loadArrayElemValue(slot->arrayInit[i]);
        storeIndirect("ac", elemWidth);
    }
}

void MCS51Codegen::safeBranch(const std::string &branchMnemonic, const std::string &targetIfTaken,
                               const std::string &targetIfNot) {
    std::string Lnear = newLabel();
    emit(branchMnemonic, Lnear);
    emit("ljmp", targetIfNot);
    emitLabel(Lnear);
    emit("ljmp", targetIfTaken);
}

void MCS51Codegen::safeDjnz(const std::string &counterReg, const std::string &targetIfNonzero,
                             const std::string &targetIfZero) {
    std::string Lnear = newLabel();
    emit("djnz", cat(counterReg, ", ", Lnear));
    emit("ljmp", targetIfZero);
    emitLabel(Lnear);
    emit("ljmp", targetIfNonzero);
}

std::string MCS51Codegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    // mainFn is only null in object mode, for a file that doesn't
    // define `main` itself.

    // Pre-pass: every global's XDATA address is assigned up front,
    // before any code is generated, so a Var read has it regardless
    // of textual order. Locals/Params need no pre-pass: their storage
    // comes from the shared `__frame_stack` buffer at a
    // runtime-computed offset, never a per-slot XDATA label.
    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        // An external global isn't defined in this file; skipping it
        // keeps this file's XDATA layout from wasting space on it.
        if (slot->isExternal) continue;
        const std::string lbl = label(slot.get());
        if (slot->arraySize > 0) {
            declareGlobalArray(lbl, slot->arraySize, widthOfType(slot->type), slot->arrayInit);
        } else if (slot->type.base == BaseType::Struct) {
            declareGlobalStruct(lbl, slot->type.structSize);
        } else {
            declareGlobalTemp(lbl, slot->hasInit ? slot->initValue : 0);
        }
    }
    allocXdata(kFrameStackLabel, kFrameStackBytes);

    emitRaw(cat("org ", kMCS51BaseAddress));
    // Pseudo-register addresses: 0x30 sits above the 4 register banks
    // (0x00-0x1F) and the bit-addressable area (0x20-0x2F), below the
    // 128-byte floor every 8031/8032/8051/8052 guarantees. `dpl`/`dph`
    // are the real DPL/DPH SFRs (0x82/0x83), not invented cells.
    // Hex text preserved verbatim (emitEquate() would format these as
    // decimal, changing the generated text).
    emitRaw("ac = 0x30");
    emitRaw("bc = 0x32");
    emitRaw("cx = 0x34");
    emitRaw("dx = 0x36");
    emitRaw("ptr = 0x38");
    emitRaw("fp = 0x3A");
    emitRaw("ssp = 0x3C");
    emitRaw("dpl = 0x82");
    emitRaw("dph = 0x83");
    emitRaw("b = 0xF0");
    emitRaw("");

    // No object file emits the entry sequence itself in object mode;
    // MCS51LinkerFactory.cpp synthesizes one once every object is
    // merged and `main`'s linked address is resolvable.
    if (!objectMode) {
        emitLabel("_start");
        // `ssp` must be valid before `main`'s genPrologue() reads it.
        loadXdataAddress("ssp", kFrameStackLabel);
        code_ << initCode_.str();
        emit("lcall", label(mainFn));
        // The program's result sits in `ac`; this `mova` also leaves
        // the low byte in the real accumulator for a quick register-dump look.
        emit("mova", "ac");
        emitLabel("halt");
        emit("ljmp", "halt");
        emitRaw("");
    }

    for (const auto &fb : prog.functionBodies) {
        if (objectMode && fb.func == mainFn) {
            // This file's globals' init code has nowhere else to run
            // in object mode: the synthesized `_start` is built before
            // seeing any user object, so `fn_main`'s prologue - the
            // one point this file's code is guaranteed to run first -
            // takes over that role, `ssp` init included.
            emitLabel(label(fb.func));
            loadXdataAddress("ssp", kFrameStackLabel);
            genPrologue(fb.func);
            code_ << initCode_.str();
            genStmt(*fb.body);
            genEpilogue();
            emit("ret");
            emitRaw("");
        } else {
            genFunction(fb);
        }
    }

    return code_.str();
}

std::map<std::string, std::string> MCS51Codegen::perFileAsm() const { return perFileAsm_; }

void MCS51Codegen::genFunction(const ir::FunctionBody &fn) {
    emitLabel(label(fn.func));
    genPrologue(fn.func);
    genStmt(*fn.body);
    genEpilogue();
    emit("ret");
    emitRaw("");
}

void MCS51Codegen::genStmt(const ir::Stmt &s) {
    switch (s.kind) {
        case ir::StmtKind::Block: {
            for (const auto &child : s.body) genStmt(*child);
            return;
        }
        case ir::StmtKind::If: {
            genExpr(*s.cond);
            emit("mova", "ac");
            emit("orl", "ac+1");
            std::string Lthen = newLabel();
            std::string Lelse = s.elseStmt ? newLabel() : "";
            std::string Ldone = newLabel();
            safeBranch("jz", s.elseStmt ? Lelse : Ldone, Lthen);
            emitLabel(Lthen);
            genStmt(*s.thenStmt);
            emit("ljmp", Ldone);
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
            emit("mova", "ac");
            emit("orl", "ac+1");
            safeBranch("jz", Lend, Lbody);
            emitLabel(Lbody);
            loopLabels_.push_back({Lcont, Lend});
            genStmt(*s.loopBody);
            loopLabels_.pop_back();
            if (s.postStmt) {
                emitLabel(Lcont);
                genStmt(*s.postStmt);
            }
            emit("ljmp", Lstart);
            emitLabel(Lend);
            return;
        }
        case ir::StmtKind::Break: {
            emit("ljmp", loopLabels_.back().second);
            return;
        }
        case ir::StmtKind::Continue: {
            emit("ljmp", loopLabels_.back().first);
            return;
        }
        case ir::StmtKind::Return: {
            if (s.initOrReturnOrExpr) genExpr(*s.initOrReturnOrExpr);
            // Early exit: genFunction()'s fall-through epilogue never
            // runs for this path, so the teardown is inlined here.
            genEpilogue();
            emit("ret");
            return;
        }
        case ir::StmtKind::LocalDecl: {
            if (s.declSlot->arraySize > 0) {
                genLocalArrayInit(s.declSlot);
            } else if (s.initOrReturnOrExpr) {
                genExpr(*s.initOrReturnOrExpr);
                loadFrameAddress("ptr", s.declSlot);
                storeIndirect("ac", widthOfType(s.declSlot->type));
            }
            // No "else" case for a bare, uninitialized scalar: its
            // frame word already exists (reserved by genPrologue()'s
            // `ssp` bump), holding whatever garbage was there until
            // something assigns it.
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
            emit("ljmp", irLabel(s.labelId));
            return;
        }
    }
}

void MCS51Codegen::requireNarrow(const ir::Expr &e) const {
    if (widthOfType(e.type) > 2) {
        err(e.line, "'long'/'long long' arithmetic isn't supported on this target");
    }
}

void MCS51Codegen::genExpr(const ir::Expr &e) {
    requireNarrow(e);
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit: {
            uint16_t v16 = static_cast<uint16_t>(e.intValue);
            emit("movai", v16 & 0xFF);
            emit("movd", "ac");
            emit("movai", (v16 >> 8) & 0xFF);
            emit("movd", "ac+1");
            return;
        }
        case ir::ExprKind::Var: {
            // A Global's value sits at a compile-time label; a
            // Local/Param's sits at an `fp`-relative frame address.
            if (e.slot->kind == ir::SlotKind::Global) {
                loadXdata("ac", label(e.slot));
            } else {
                loadFrameAddress("ptr", e.slot);
                loadIndirect("ac", widthOfType(e.slot->type), e.slot->type.isUnsigned);
            }
            return;
        }
        case ir::ExprKind::Index: {
            genIndex(e);
            return;
        }
        case ir::ExprKind::Assign: {
            if (e.lhs) {  // slot[idx] = rhs
                genIndexAddr(e.slot, *e.lhs);  // ptr = target XDATA address
                // Save `ptr` across evaluating rhs (which might itself
                // reuse `ptr`) via real PUSH/POP direct.
                emit("push", "ptr");
                emit("push", "ptr+1");
                genExpr(*e.rhs);
                emit("pop", "ptr+1");
                emit("pop", "ptr");
                storeIndirect("ac", widthOfType(e.slot->type));
                return;
            }
            if (e.slot->kind == ir::SlotKind::Global) {
                genExpr(*e.rhs);
                storeXdata(label(e.slot), "ac");
                return;
            }
            loadFrameAddress("ptr", e.slot);
            emit("push", "ptr");
            emit("push", "ptr+1");
            genExpr(*e.rhs);
            emit("pop", "ptr+1");
            emit("pop", "ptr");
            storeIndirect("ac", widthOfType(e.slot->type));
            return;
        }
        case ir::ExprKind::DerefAssign: {
            genExpr(*e.lhs);  // ac = pointer value
            copy16("ptr", "ac");
            emit("push", "ptr");
            emit("push", "ptr+1");
            genExpr(*e.rhs);
            emit("pop", "ptr+1");
            emit("pop", "ptr");
            storeIndirect("ac", widthOfType(e.type));
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            if (e.lhs) {  // slot[idx] op= rhs (including slot[idx]++/--)
                genIndexAddr(e.slot, *e.lhs);  // ptr = target XDATA address
                genCompoundThroughPtr(e);
                return;
            }
            if (e.slot->kind == ir::SlotKind::Global) {
                genExpr(*e.rhs);     // ac = rhs
                copy16("dx", "ac");  // dx = rhs, held (nothing else evaluates a genExpr() before it's consumed)
                loadXdata("ac", label(e.slot));  // ac = old
                copy16("bc", "ac");               // bc = old - genCompoundOp()'s own precondition
                if (e.yieldOld) {
                    emit("push", "ac");
                    emit("push", "ac+1");
                }
                copy16("ac", "dx");  // ac = rhs - genCompoundOp()'s own other precondition
                genCompoundOp(e.op, e.type);
                narrowAcTo(e.type);
                storeXdata(label(e.slot), "ac");
                if (e.yieldOld) {
                    emit("pop", "ac+1");
                    emit("pop", "ac");
                }
                return;
            }
            loadFrameAddress("ptr", e.slot);
            genCompoundThroughPtr(e);
            return;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            genExpr(*e.lhs);  // ac = pointer value
            copy16("ptr", "ac");
            genCompoundThroughPtr(e);
            return;
        }
        case ir::ExprKind::Ternary: {
            genExpr(*e.lhs);
            emit("mova", "ac");
            emit("orl", "ac+1");
            std::string Lthen = newLabel();
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            safeBranch("jz", Lelse, Lthen);
            emitLabel(Lthen);
            genExpr(*e.rhs);
            emit("ljmp", Ldone);
            emitLabel(Lelse);
            genExpr(*e.elseExpr);
            emitLabel(Ldone);
            return;
        }
        case ir::ExprKind::Unary: {
            if (e.op == "-") {
                genExpr(*e.rhs);
                emit("movai", 0);
                emit("clrc");
                emit("subb", "ac");
                emit("movd", "ac");
                emit("movai", 0);
                emit("subb", "ac+1");
                emit("movd", "ac+1");
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                genExpr(*e.rhs);  // ac = pointer value
                copy16("ptr", "ac");
                loadIndirect("ac", widthOfType(e.type), e.type.isUnsigned);
            } else if (e.op == "~") {
                genExpr(*e.rhs);
                emit("mova", "ac");
                emit("xrli", 255);
                emit("movd", "ac");
                emit("mova", "ac+1");
                emit("xrli", 255);
                emit("movd", "ac+1");
            } else {  // "&"
                genAddrOf(*e.rhs);
            }
            return;
        }
        case ir::ExprKind::Call: {
            // Each argument is computed and pushed onto the shared
            // frame stack positionally; the callee's genPrologue()
            // picks these words back up as its `fp`-relative Params.
            for (size_t i = 0; i < e.args.size(); i++) {
                genExpr(*e.args[i]);
                pushFrameWord("ac");
            }
            emit("lcall", label(e.func));
            return;
        }
        case ir::ExprKind::CallPtr: {
            // The real 8051 has no indirect CALL, only `JMP @A+DPTR`
            // (an indirect jump). A call through a computed address is
            // faked by manually pushing a return address (low byte
            // first, matching real LCALL's push order) before jumping,
            // so the callee's `ret` returns here as if LCALL had been used.
            genExpr(*e.lhs);  // ac = the callee's own entry address
            std::string ret = newLabel();
            loadCodeAddress("bc", ret);  // bc = this call's own return address
            emit("push", "bc");
            emit("push", "bc+1");
            emit("movdpl", "ac");
            emit("movdph", "ac+1");  // dptr = callee address
            emit("clra");            // a = 0, so @A+DPTR == DPTR exactly
            emit("jmpadptr");
            emitLabel(ret);
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
            // `SETB EA` (IE.7) enables all interrupts, `CLR EA` disables them.
            emit(e.op == "ion" ? "setea" : "clrea");
            return;
        }
        case ir::ExprKind::Func:
            err(e.line, "internal: Func reached genExpr - should only ever appear as '&'s operand");
    }
}

// Leaves slot[idxExpr]'s effective XDATA address in `ptr`. If
// `slot->arraySize > 0`, the base is the array's own address
// (loadXdataAddress() for a Global, loadFrameAddress() for a Local);
// otherwise `slot` is a pointer variable, so the base is read from its
// stored value instead. Either way the base ends up in `bc`, and a
// 16-bit add folds the scaled index into it.
void MCS51Codegen::genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
    int elemWidth = widthOfType(slot->type);
    genExpr(idxExpr);  // ac = index
    // elemWidth is 1/2/4/8 for every scalar this type system produces, so doubling
    // log2(elemWidth) times (real HW has no 16-bit multiply - see genMul()) covers those
    // exactly. A struct's width isn't always a power of two, though (e.g. three `char`
    // members), so any other width falls back to the same software multiply loop genMul()
    // uses (bc * dx -> ac), rather than being silently doubled the wrong number of times.
    int shift = elemWidth == 1 ? 0 : elemWidth == 2 ? 1 : elemWidth == 4 ? 2 : elemWidth == 8 ? 3 : -1;
    if (shift > 0) {
        for (int i = 0; i < shift; i++) {
            emit("clrc");
            emit("mova", "ac");
            emit("rlca");
            emit("movd", "ac");
            emit("mova", "ac+1");
            emit("rlca");
            emit("movd", "ac+1");
        }
    } else if (shift < 0) {
        copy16("bc", "ac");  // bc = multiplicand (the index)
        uint16_t w16 = static_cast<uint16_t>(elemWidth);
        emit("movai", w16 & 0xFF);
        emit("movd", "dx");
        emit("movai", (w16 >> 8) & 0xFF);
        emit("movd", "dx+1");  // dx = multiplier (the width)
        emit("movai", 0);
        emit("movd", "ac");
        emit("movai", 0);
        emit("movd", "ac+1");  // ac = product = 0
        emit("movai", 16);
        emit("movd", "cx");
        std::string Lloop = newLabel(), Ladd = newLabel(), Lskipadd = newLabel(), Ldone = newLabel();
        emitLabel(Lloop);
        emit("mova", "dx");
        emit("anli", 1);
        safeBranch("jnz", Ladd, Lskipadd);
        emitLabel(Ladd);
        emit("mova", "bc");
        emit("add", "ac");
        emit("movd", "ac");
        emit("mova", "bc+1");
        emit("addc", "ac+1");
        emit("movd", "ac+1");
        emitLabel(Lskipadd);
        emit("clrc");
        emit("mova", "bc");
        emit("rlca");
        emit("movd", "bc");
        emit("mova", "bc+1");
        emit("rlca");
        emit("movd", "bc+1");
        emit("clrc");
        emit("mova", "dx+1");
        emit("rrca");
        emit("movd", "dx+1");
        emit("mova", "dx");
        emit("rrca");
        emit("movd", "dx");
        safeDjnz("cx", Lloop, Ldone);
        emitLabel(Ldone);
    }
    bool isGlobal = slot->kind == ir::SlotKind::Global;
    if (slot->arraySize > 0) {
        if (isGlobal) {
            loadXdataAddress("bc", label(slot));
        } else {
            loadFrameAddress("bc", slot);
        }
    } else if (isGlobal) {
        loadXdata("bc", label(slot));
    } else {
        loadFrameAddress("ptr", slot);
        loadIndirect("bc", 2, /*isUnsigned=*/true);
    }
    emit("clrc");
    emit("mova", "bc");
    emit("add", "ac");
    emit("movd", "ptr");
    emit("mova", "bc+1");
    emit("addc", "ac+1");
    emit("movd", "ptr+1");
}

void MCS51Codegen::genIndex(const ir::Expr &e) {
    genIndexAddr(e.slot, *e.lhs);
    loadIndirect("ac", widthOfType(e.slot->type), e.slot->type.isUnsigned);
}

void MCS51Codegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        if (operand.slot->kind == ir::SlotKind::Global) {
            loadXdataAddress("ac", label(operand.slot));
        } else {
            loadFrameAddress("ac", operand.slot);
        }
        return;
    }
    if (operand.kind == ir::ExprKind::Index) {
        genIndexAddr(operand.slot, *operand.lhs);
        copy16("ac", "ptr");
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        loadCodeAddress("ac", label(operand.func));
        return;
    }
    err(operand.line, "internal: unhandled addressable-operand kind");
}

void MCS51Codegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    if (op == "+" || op == "-") {
        genExpr(*e.lhs);
        emit("push", "ac");
        emit("push", "ac+1");
        genExpr(*e.rhs);
        emit("pop", "bc+1");
        emit("pop", "bc");  // bc = lhs
        if (op == "+") {
            emit("mova", "bc");
            emit("add", "ac");
            emit("movd", "ac");
            emit("mova", "bc+1");
            emit("addc", "ac+1");
            emit("movd", "ac+1");
        } else {
            emit("mova", "bc");
            emit("clrc");
            emit("subb", "ac");
            emit("movd", "ac");
            emit("mova", "bc+1");
            emit("subb", "ac+1");
            emit("movd", "ac+1");
        }
        return;
    }
    if (op == "*") {
        genMul(*e.lhs, *e.rhs);
        return;
    }
    if (op == "/") {
        genDivMod(*e.lhs, *e.rhs, /*wantRemainder=*/false, e.lhs->type.isUnsigned);
        return;
    }
    if (op == "%") {
        genDivMod(*e.lhs, *e.rhs, /*wantRemainder=*/true, e.lhs->type.isUnsigned);
        return;
    }
    if (op == "&" || op == "|" || op == "^") {
        genExpr(*e.lhs);
        emit("push", "ac");
        emit("push", "ac+1");
        genExpr(*e.rhs);
        emit("pop", "bc+1");
        emit("pop", "bc");
        const char *insn = (op == "&") ? "anl" : (op == "|") ? "orl" : "xrl";
        emit("mova", "bc");
        emit(insn, "ac");
        emit("movd", "ac");
        emit("mova", "bc+1");
        emit(insn, "ac+1");
        emit("movd", "ac+1");
        return;
    }
    if (op == "<<" || op == ">>") {
        genShift(*e.lhs, *e.rhs, op == "<<", e.lhs->type.isUnsigned);
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
    genCompare(op, *e.lhs, *e.rhs, e.lhs->type.isUnsigned);
}

// Loops `rhs` times via DJNZ, guarded by an upfront zero check: DJNZ
// decrements before testing, so shift-by-zero must be caught before
// entering the loop body, not after.
void MCS51Codegen::genShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned) {
    genExpr(lhs);
    emit("push", "ac");
    emit("push", "ac+1");
    genExpr(rhs);
    emit("mova", "ac");
    emit("movd", "cx");  // cx = shift count (always small; low byte is enough)
    emit("pop", "bc+1");
    emit("pop", "bc");
    copy16("ac", "bc");  // ac = value to shift
    std::string Lloop = newLabel(), Lbody = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("mova", "cx");
    safeBranch("jz", Ldone, Lbody);
    emitLabel(Lbody);
    if (isLeft) {
        emit("clrc");
        emit("mova", "ac");
        emit("rlca");
        emit("movd", "ac");
        emit("mova", "ac+1");
        emit("rlca");
        emit("movd", "ac+1");
    } else if (isUnsigned) {
        // Carry is cleared first, so RRC's top-bit fill is always 0.
        emit("clrc");
        emit("mova", "ac+1");
        emit("rrca");
        emit("movd", "ac+1");
        emit("mova", "ac");
        emit("rrca");
        emit("movd", "ac");
    } else {
        // The sign bit is carried into the carry flag first (via a
        // left rotate), so the RRC that follows fills the vacated top
        // bit with the operand's sign instead of 0.
        emit("mova", "ac+1");
        emit("rlca");
        emit("mova", "ac+1");
        emit("rrca");
        emit("movd", "ac+1");
        emit("mova", "ac");
        emit("rrca");
        emit("movd", "ac");
    }
    safeDjnz("cx", Lloop, Ldone);
    emitLabel(Ldone);
}

// `ac` is always a plain 16-bit word, so a same-width conversion is a
// pure reinterpretation - nothing to do. Narrowing to `char` masks off
// the high byte and, for a signed target, sign-extends bit 7 across
// it, so a `char` value always arrives already correctly extended
// (making a later widen-from-`char` a pure pass-through).
void MCS51Codegen::genConvert(const ir::Expr &e) {
    genExpr(*e.lhs);
    if (widthOfType(e.type) != 1) return;
    if (e.type.isUnsigned) {
        emit("movai", 0);
        emit("movd", "ac+1");
    } else {
        // The branch on `anli`'s Z flag must come immediately after it.
        emit("mova", "ac");
        emit("anli", 128);
        std::string Lneg = newLabel(), Lnonneg = newLabel(), Ldone = newLabel();
        safeBranch("jz", Lnonneg, Lneg);
        emitLabel(Lneg);
        emit("movai", 255);
        emit("movd", "ac+1");
        emit("ljmp", Ldone);
        emitLabel(Lnonneg);
        emit("movai", 0);
        emit("movd", "ac+1");
        emitLabel(Ldone);
    }
}

const ir::Expr &MCS51Codegen::stripWideningConvert(const ir::Expr &e) const {
    if (e.kind == ir::ExprKind::Convert && widthOfType(e.type) != 1 && widthOfType(e.lhs->type) == 1) {
        return *e.lhs;
    }
    return e;
}

// `MUL AB` computes an exact unsigned 8x8->16 product directly in hardware
// (low byte left in A, high byte in B), matching this case exactly: both
// operands narrow enough to fit A and B, and the full 16-bit product kept
// rather than truncated. A signed operand's full product depends on sign,
// which `MUL AB` doesn't account for, so this only applies when both
// operands are unsigned.
void MCS51Codegen::genNarrowMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    emit("push", "ac");
    genExpr(rhs);
    emit("mova", "ac");
    emit("movd", "b");  // b = rhs
    emit("pop", "ac");
    emit("mova", "ac");
    emit("mul");  // a = lhs; after mul: a = low byte, b = high byte of product
    emit("movd", "ac");
    emit("mova", "b");
    emit("movd", "ac+1");
}

// Shift-and-add multiply (real `MUL AB` is only 8x8->16, too narrow
// for 16-bit `int`): `bc` is the multiplicand (shifted left), `dx` the
// multiplier (shifted right, consumed one bit at a time), `ac` the
// product, `cx` a fixed 16-iteration counter. Non-negative operands only.
void MCS51Codegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    const ir::Expr &nl = stripWideningConvert(lhs);
    const ir::Expr &nr = stripWideningConvert(rhs);
    if (widthOfType(nl.type) == 1 && nl.type.isUnsigned && widthOfType(nr.type) == 1 && nr.type.isUnsigned) {
        genNarrowMul(nl, nr);
        return;
    }
    genExpr(lhs);
    emit("push", "ac");
    emit("push", "ac+1");
    genExpr(rhs);
    copy16("dx", "ac");  // dx = multiplier
    emit("pop", "bc+1");
    emit("pop", "bc");  // bc = multiplicand
    emit("movai", 0);
    emit("movd", "ac");
    emit("movai", 0);
    emit("movd", "ac+1");  // ac = product = 0
    emit("movai", 16);
    emit("movd", "cx");
    std::string Lloop = newLabel(), Ladd = newLabel(), Lskipadd = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("mova", "dx");
    emit("anli", 1);
    safeBranch("jnz", Ladd, Lskipadd);
    emitLabel(Ladd);
    emit("mova", "bc");
    emit("add", "ac");
    emit("movd", "ac");
    emit("mova", "bc+1");
    emit("addc", "ac+1");
    emit("movd", "ac+1");
    emitLabel(Lskipadd);
    emit("clrc");
    emit("mova", "bc");
    emit("rlca");
    emit("movd", "bc");
    emit("mova", "bc+1");
    emit("rlca");
    emit("movd", "bc+1");
    emit("clrc");
    emit("mova", "dx+1");
    emit("rrca");
    emit("movd", "dx+1");
    emit("mova", "dx");
    emit("rrca");
    emit("movd", "dx");
    safeDjnz("cx", Lloop, Ldone);
    emitLabel(Ldone);
}

// Repeated subtraction (real `DIV AB` is only 8/8->8r8, same width
// problem as `MUL AB`): `dx` is the running remainder (starts as the
// dividend), `bc` the divisor, `ac` the quotient count. A signed
// operand is divided by absolute value, with `ptr`'s low/high byte
// holding the dividend's/divisor's original sign so the quotient's
// sign (1 iff the signs differed) and remainder's sign (the
// dividend's) can be restored afterward.
void MCS51Codegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    emit("push", "ac");
    emit("push", "ac+1");
    genExpr(rhs);
    copy16("bc", "ac");  // bc = divisor
    emit("pop", "dx+1");
    emit("pop", "dx");  // dx = dividend (running remainder)
    auto isNeg = [&](const char *cell, const char *dst) {
        emit("mova", cat(cell, "+1"));
        emit("anli", 128);
        std::string Lneg = newLabel(), Lnonneg = newLabel(), Ldone = newLabel();
        safeBranch("jz", Lnonneg, Lneg);
        emitLabel(Lneg);
        emit("movai", 1);
        emit("movd", dst);
        emit("ljmp", Ldone);
        emitLabel(Lnonneg);
        emit("movai", 0);
        emit("movd", dst);
        emitLabel(Ldone);
    };
    auto negate16 = [&](const char *cell) {
        emit("movai", 0);
        emit("clrc");
        emit("subb", cell);
        emit("movd", cell);
        emit("movai", 0);
        emit("subb", cat(cell, "+1"));
        emit("movd", cat(cell, "+1"));
    };
    auto negateIf = [&](const char *flagCell, const char *cell) {
        emit("mova", flagCell);
        std::string Lskip = newLabel(), Ldo = newLabel();
        safeBranch("jz", Lskip, Ldo);
        emitLabel(Ldo);
        negate16(cell);
        emitLabel(Lskip);
    };
    if (!isUnsigned) {
        isNeg("dx", "ptr");
        isNeg("bc", "ptr+1");
        negateIf("ptr", "dx");
        negateIf("ptr+1", "bc");
    }
    emit("movai", 0);
    emit("movd", "ac");
    emit("movai", 0);
    emit("movd", "ac+1");  // ac = quotient = 0
    std::string Lloop = newLabel(), Lcommit = newLabel(), Llt = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("clrc");
    emit("mova", "dx");
    emit("subb", "bc");
    emit("movd", "cx");
    emit("mova", "dx+1");
    emit("subb", "bc+1");
    safeBranch("jc", Llt, Lcommit);
    emitLabel(Lcommit);
    emit("movd", "dx+1");
    emit("movdd", "dx, cx");
    emit("mova", "ac");
    emit("addi", 1);
    emit("movd", "ac");
    emit("mova", "ac+1");
    emit("addci", 0);
    emit("movd", "ac+1");
    emit("ljmp", Lloop);
    emitLabel(Llt);
    emit("ljmp", Ldone);
    emitLabel(Ldone);
    if (!isUnsigned) {
        // Quotient is negative iff the two original signs differed.
        emit("mova", "ptr");
        emit("xrl", "ptr+1");
        std::string Lskip = newLabel(), Ldo = newLabel();
        safeBranch("jz", Lskip, Ldo);
        emitLabel(Ldo);
        negate16("ac");
        emitLabel(Lskip);
        negateIf("ptr", "dx");
    }
    if (wantRemainder) copy16("ac", "dx");
}

// Precondition: `bc` holds `old`, `ac` holds `rhs`. Leaves the raw new
// value in `ac`.
void MCS51Codegen::genCompoundOp(const std::string &op, Type type) {
    if (op == "+") {
        emit("mova", "bc");
        emit("add", "ac");
        emit("movd", "ac");
        emit("mova", "bc+1");
        emit("addc", "ac+1");
        emit("movd", "ac+1");
        return;
    }
    if (op == "-") {
        emit("mova", "bc");
        emit("clrc");
        emit("subb", "ac");
        emit("movd", "ac");
        emit("mova", "bc+1");
        emit("subb", "ac+1");
        emit("movd", "ac+1");
        return;
    }
    if (op == "*") {
        copy16("dx", "ac");  // dx = multiplier (rhs) - bc already holds the multiplicand (old)
        emit("movai", 0);
        emit("movd", "ac");
        emit("movai", 0);
        emit("movd", "ac+1");  // ac = product = 0
        emit("movai", 16);
        emit("movd", "cx");
        std::string Lloop = newLabel(), Ladd = newLabel(), Lskipadd = newLabel(), Ldone = newLabel();
        emitLabel(Lloop);
        emit("mova", "dx");
        emit("anli", 1);
        safeBranch("jnz", Ladd, Lskipadd);
        emitLabel(Ladd);
        emit("mova", "bc");
        emit("add", "ac");
        emit("movd", "ac");
        emit("mova", "bc+1");
        emit("addc", "ac+1");
        emit("movd", "ac+1");
        emitLabel(Lskipadd);
        emit("clrc");
        emit("mova", "bc");
        emit("rlca");
        emit("movd", "bc");
        emit("mova", "bc+1");
        emit("rlca");
        emit("movd", "bc+1");
        emit("clrc");
        emit("mova", "dx+1");
        emit("rrca");
        emit("movd", "dx+1");
        emit("mova", "dx");
        emit("rrca");
        emit("movd", "dx");
        safeDjnz("cx", Lloop, Ldone);
        emitLabel(Ldone);
        return;
    }
    if (op == "/" || op == "%") {
        copy16("dx", "bc");  // dx = dividend (old) - copied before bc is overwritten below
        copy16("bc", "ac");  // bc = divisor (rhs)
        emit("movai", 0);
        emit("movd", "ac");
        emit("movai", 0);
        emit("movd", "ac+1");  // ac = quotient = 0
        std::string Lloop = newLabel(), Lcommit = newLabel(), Llt = newLabel(), Ldone = newLabel();
        emitLabel(Lloop);
        emit("clrc");
        emit("mova", "dx");
        emit("subb", "bc");
        emit("movd", "cx");
        emit("mova", "dx+1");
        emit("subb", "bc+1");
        safeBranch("jc", Llt, Lcommit);
        emitLabel(Lcommit);
        emit("movd", "dx+1");
        emit("movdd", "dx, cx");
        emit("mova", "ac");
        emit("addi", 1);
        emit("movd", "ac");
        emit("mova", "ac+1");
        emit("addci", 0);
        emit("movd", "ac+1");
        emit("ljmp", Lloop);
        emitLabel(Llt);
        emit("ljmp", Ldone);
        emitLabel(Ldone);
        if (op == "%") copy16("ac", "dx");
        return;
    }
    if (op == "&" || op == "|" || op == "^") {
        const char *insn = (op == "&") ? "anl" : (op == "|") ? "orl" : "xrl";
        emit("mova", "bc");
        emit(insn, "ac");
        emit("movd", "ac");
        emit("mova", "bc+1");
        emit(insn, "ac+1");
        emit("movd", "ac+1");
        return;
    }
    // "<<" or ">>"
    emit("mova", "ac");
    emit("movd", "cx");  // cx = shift count (rhs's own low byte) - ac about to be overwritten
    copy16("ac", "bc");  // ac = value to shift (old)
    bool isLeft = op == "<<";
    bool isUnsigned = type.isUnsigned;
    std::string Lloop = newLabel(), Lbody = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("mova", "cx");
    safeBranch("jz", Ldone, Lbody);
    emitLabel(Lbody);
    if (isLeft) {
        emit("clrc");
        emit("mova", "ac");
        emit("rlca");
        emit("movd", "ac");
        emit("mova", "ac+1");
        emit("rlca");
        emit("movd", "ac+1");
    } else if (isUnsigned) {
        emit("clrc");
        emit("mova", "ac+1");
        emit("rrca");
        emit("movd", "ac+1");
        emit("mova", "ac");
        emit("rrca");
        emit("movd", "ac");
    } else {
        emit("mova", "ac+1");
        emit("rlca");
        emit("mova", "ac+1");
        emit("rrca");
        emit("movd", "ac+1");
        emit("mova", "ac");
        emit("rrca");
        emit("movd", "ac");
    }
    safeDjnz("cx", Lloop, Ldone);
    emitLabel(Ldone);
}

// `ptr` holds the resolved address on entry; stashed on the real
// hardware stack while `rhs` is evaluated, since evaluating it might
// itself reuse `ptr`. `yieldOld`'s `old` value survives on that same
// stack across genCompoundOp()'s own register use.
void MCS51Codegen::genCompoundThroughPtr(const ir::Expr &e) {
    emit("push", "ptr");
    emit("push", "ptr+1");  // stash address
    genExpr(*e.rhs);        // ac = rhs
    copy16("dx", "ac");     // dx = rhs, held safely until genCompoundOp() consumes it
    emit("pop", "ptr+1");
    emit("pop", "ptr");  // ptr = address, restored only now
    loadIndirect("ac", widthOfType(e.type), e.type.isUnsigned);  // ac = old
    copy16("bc", "ac");  // bc = old - genCompoundOp()'s own precondition
    if (e.yieldOld) {
        emit("push", "ac");
        emit("push", "ac+1");  // old, surviving genCompoundOp()'s own register use
    }
    copy16("ac", "dx");           // ac = rhs - genCompoundOp()'s own other precondition
    genCompoundOp(e.op, e.type);  // ac = new (raw)
    narrowAcTo(e.type);
    storeIndirect("ac", widthOfType(e.type));  // stores ac (= new) - ptr untouched since the pop above
    if (e.yieldOld) {
        emit("pop", "ac+1");
        emit("pop", "ac");  // ac = old (final result)
    }
}

// Same truncate-then-sign/zero-extend step as genConvert()'s
// int-to-char case; the only width this ever narrows to.
void MCS51Codegen::narrowAcTo(Type to) {
    if (widthOfType(to) != 1) return;
    if (to.isUnsigned) {
        emit("movai", 0);
        emit("movd", "ac+1");
        return;
    }
    emit("mova", "ac");
    emit("anli", 128);
    std::string Lneg = newLabel(), Lnonneg = newLabel(), Ldone = newLabel();
    safeBranch("jz", Lnonneg, Lneg);
    emitLabel(Lneg);
    emit("movai", 255);
    emit("movd", "ac+1");
    emit("ljmp", Ldone);
    emitLabel(Lnonneg);
    emit("movai", 0);
    emit("movd", "ac+1");
    emitLabel(Ldone);
}

void MCS51Codegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned) {
    if (op == "==") return emitEqualityCompare(lhs, rhs, /*wantEqual=*/true);
    if (op == "!=") return emitEqualityCompare(lhs, rhs, /*wantEqual=*/false);
    // Every ordering comparison reduces to "is a < b?" by choosing
    // operand order and whether to invert the sense.
    if (op == "<") return emitLessCompare(lhs, rhs, isUnsigned, /*invert=*/false);
    if (op == ">") return emitLessCompare(rhs, lhs, isUnsigned, /*invert=*/false);
    if (op == "<=") return emitLessCompare(rhs, lhs, isUnsigned, /*invert=*/true);
    if (op == ">=") return emitLessCompare(lhs, rhs, isUnsigned, /*invert=*/true);
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

// a-b (16-bit, borrow chained low into high) is zero iff a==b; the
// final ORL leaves "low result OR high result" in A, zero iff both
// halves were zero (JZ/JNZ test A directly, with no separate zero flag).
void MCS51Codegen::emitEqualityCompare(const ir::Expr &lhs, const ir::Expr &rhs, bool wantEqual) {
    genExpr(lhs);
    emit("push", "ac");
    emit("push", "ac+1");
    genExpr(rhs);
    emit("pop", "bc+1");
    emit("pop", "bc");  // bc = lhs
    emit("mova", "bc");
    emit("clrc");
    emit("subb", "ac");
    emit("movd", "dx");
    emit("mova", "bc+1");
    emit("subb", "ac+1");
    emit("orl", "dx");
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    safeBranch(wantEqual ? "jz" : "jnz", Ltrue, Lfalse);
    emitLabel(Ltrue);
    emit("movai", 1);
    emit("movd", "ac");
    emit("movai", 0);
    emit("movd", "ac+1");
    emit("ljmp", Ldone);
    emitLabel(Lfalse);
    emit("movai", 0);
    emit("movd", "ac");
    emit("movai", 0);
    emit("movd", "ac+1");
    emitLabel(Ldone);
}

// "a < b" via subtraction. Unsigned: the carry after the chained SUBB
// directly says borrow (a<b) or no borrow (a>=b). Signed uses the "N
// xor V" idiom: ACC.7 after the final SUBB only reflects the true
// signed result once corrected for the real PSW.OV overflow flag, via
// an XRL A,#0x80 applied iff OV is set.
void MCS51Codegen::emitLessCompare(const ir::Expr &a, const ir::Expr &b, bool isUnsigned, bool invert) {
    genExpr(a);
    emit("push", "ac");
    emit("push", "ac+1");
    genExpr(b);
    emit("pop", "bc+1");
    emit("pop", "bc");  // bc = a
    emit("mova", "bc");
    emit("clrc");
    emit("subb", "ac");
    emit("mova", "bc+1");
    emit("subb", "ac+1");
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    if (isUnsigned) {
        safeBranch(invert ? "jnc" : "jc", Ltrue, Lfalse);
    } else {
        std::string Lfixskip = newLabel();
        emit("jnbov", Lfixskip);
        emit("xrli", 128);
        emitLabel(Lfixskip);
        // ACC.7 now correctly reflects "a < b" (set) / "a >= b" (clear).
        safeBranch(invert ? "jnbacc7" : "jbacc7", Ltrue, Lfalse);
    }
    emitLabel(Ltrue);
    emit("movai", 1);
    emit("movd", "ac");
    emit("movai", 0);
    emit("movd", "ac+1");
    emit("ljmp", Ldone);
    emitLabel(Lfalse);
    emit("movai", 0);
    emit("movd", "ac");
    emit("movai", 0);
    emit("movd", "ac+1");
    emitLabel(Ldone);
}

void MCS51Codegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string Lcont = newLabel(), Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    genExpr(lhs);
    emit("mova", "ac");
    emit("orl", "ac+1");
    safeBranch("jz", Lfalse, Lcont);
    emitLabel(Lcont);
    genExpr(rhs);
    emit("mova", "ac");
    emit("orl", "ac+1");
    safeBranch("jz", Lfalse, Ltrue);
    emitLabel(Ltrue);
    emit("movai", 1);
    emit("movd", "ac");
    emit("movai", 0);
    emit("movd", "ac+1");
    emit("ljmp", Ldone);
    emitLabel(Lfalse);
    emit("movai", 0);
    emit("movd", "ac");
    emit("movai", 0);
    emit("movd", "ac+1");
    emitLabel(Ldone);
}

void MCS51Codegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string Lcont = newLabel(), Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    genExpr(lhs);
    emit("mova", "ac");
    emit("orl", "ac+1");
    safeBranch("jnz", Ltrue, Lcont);
    emitLabel(Lcont);
    genExpr(rhs);
    emit("mova", "ac");
    emit("orl", "ac+1");
    safeBranch("jnz", Ltrue, Lfalse);
    emitLabel(Ltrue);
    emit("movai", 1);
    emit("movd", "ac");
    emit("movai", 0);
    emit("movd", "ac+1");
    emit("ljmp", Ldone);
    emitLabel(Lfalse);
    emit("movai", 0);
    emit("movd", "ac");
    emit("movai", 0);
    emit("movd", "ac+1");
    emitLabel(Ldone);
}

void MCS51Codegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    emit("mova", "ac");
    emit("orl", "ac+1");
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    safeBranch("jz", Ltrue, Lfalse);
    emitLabel(Ltrue);
    emit("movai", 1);
    emit("movd", "ac");
    emit("movai", 0);
    emit("movd", "ac+1");
    emit("ljmp", Ldone);
    emitLabel(Lfalse);
    emit("movai", 0);
    emit("movd", "ac");
    emit("movai", 0);
    emit("movd", "ac+1");
    emitLabel(Ldone);
}

std::optional<std::string> MCS51Codegen::assembleInProcess(const std::string &asmText,
                                                             const std::string &format) const {
    if (format == "raw") return assembleMCS51(asmText);
    // Builds one format-neutral ObjectFile, then picks the requested
    // format's FileFormatWriter to serialize it.
    if (auto kind = fileFormatKindFromName(format)) {
        ObjectFile obj = assembleMCS51Object(asmText);
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

}  // namespace rcc
