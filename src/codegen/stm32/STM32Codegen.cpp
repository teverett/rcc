#include "STM32Codegen.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <tuple>

#include "STM32Assembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

// Flash: holds the reset vector table and every instruction, nothing else - read-only on
// this board. `-kernel` loads the `--format=raw` output here.
constexpr uint32_t kFlashBase = 0x08000000;

// SRAM on this board (8KB, 0x20000000-0x20001FFF): every global/local/temp/array slot lives
// here, reserved via `ds`, never given image bytes.
constexpr uint32_t kRamBase = 0x20000000;

// Top of SRAM - where sp starts, loaded by the Cortex-M reset exception from vector 0.
constexpr uint32_t kInitialSP = 0x20002000;
}  // namespace

void STM32Codegen::declareTemp(const std::string &lbl, Type type) {
    ramTrailer_.push_back(lbl + ": dsb " + std::to_string(widthOfType(type)));
}

void STM32Codegen::declareArray(const std::string &lbl, const std::vector<ir::ArrayInitElem> &values, Type elemType) {
    if (values.empty()) return;
    ramTrailer_.push_back(lbl + ": dsb " + std::to_string(widthOfType(elemType) * values.size()));
}

// A literal element's value via `movl`; a symbol element's address via the same `lea`
// genAddrOf() uses, with the thumb-bit adjustment (+1) when the element names a Function. The
// loaded value is copied into the RAM cell `lbl+offset` resolves to, truncated to `elemType`'s
// width; a `long long` literal also needs its high word written separately since `str` here is
// always exactly 4 bytes.
void STM32Codegen::genArrayElemInit(const std::string &lbl, size_t offset, Type elemType, const ir::ArrayInitElem &e) {
    if (!e.isSymbol && e.value == 0) return;  // RAM already starts zeroed - nothing to copy
    int w = widthOfType(elemType);
    if (e.isSymbol) {
        emit("lea", cat((e.slot ? label(e.slot) : label(e.func)), ",r0"));
        if (!e.slot) {
            emit("movl", "1,r1");
            emit("add", "r0,r0,r1");
        }
    } else if (w == 8) {
        auto v = static_cast<uint64_t>(e.value);
        emit("movl", cat(static_cast<int32_t>(static_cast<uint32_t>(v)), ",r0"));
        emit("movl", cat(static_cast<int32_t>(static_cast<uint32_t>(v >> 32)), ",r1"));
    } else {
        emit("movl", cat(static_cast<int32_t>(static_cast<uint32_t>(e.value)), ",r0"));
    }
    emit("lea", cat(lbl, "+", offset, ",r2"));
    const char *store = w == 1 ? "strb" : w == 2 ? "strh" : "str";
    emit(store, "r0,[r2]");
    if (w == 8) {
        emit("movl", "4,r3");
        emit("add", "r2,r2,r3");
        emit("str", "r1,[r2]");
    }
}

// Flash-resident code that writes `value` into the RAM cell `lbl` resolves to, truncated to
// `type`'s width. RAM starts out undefined, so a nonzero initializer needs this explicit
// one-time copy, emitted into `_start` for a global or at the `LocalDecl` site for a local.
void STM32Codegen::genGlobalInit(const std::string &ramLabel, Type type, long value) {
    if (value == 0) return;  // RAM already starts zeroed - nothing to copy
    int w = widthOfType(type);
    emit("movl", cat(static_cast<int32_t>(static_cast<uint32_t>(value)), ",r0"));
    if (w == 8) emit("movl", cat(static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint64_t>(value) >> 32)), ",r1"));
    emit("lea", cat(ramLabel, ",r2"));
    const char *store = w == 1 ? "strb" : w == 2 ? "strh" : "str";
    emit(store, "r0,[r2]");
    if (w == 8) {
        emit("movl", "4,r3");
        emit("add", "r2,r2,r3");
        emit("str", "r1,[r2]");
    }
}

// `byteOffsetFromFp` is already a byte displacement (IRBuilder.cpp accounts for the slot's
// width/alignment and, for a Param, the reservation past the saved r7/return address), so this
// just adds or subtracts it from r7 directly.
void STM32Codegen::loadFrameWordAddr(const std::string &reg, int byteOffsetFromFp) {
    emit("mov", cat(reg, ",r7"));
    if (byteOffsetFromFp == 0) return;
    emit("movl", cat((byteOffsetFromFp > 0 ? byteOffsetFromFp : -byteOffsetFromFp), ",r3"));
    emit((byteOffsetFromFp > 0 ? "add" : "sub"), cat("", reg, ",", reg, ",r3"));
}

void STM32Codegen::loadFrameAddr(const std::string &reg, const ir::Slot *slot) {
    int byteOffsetFromFp =
        slot->kind == ir::SlotKind::Param ? slot->frameOffset : -ir::localLowOffset(*slot, typeWidths());
    loadFrameWordAddr(reg, byteOffsetFromFp);
}

void STM32Codegen::loadSlotAddr(const std::string &reg, const ir::Slot *slot) {
    if (slot->kind == ir::SlotKind::Global) {
        emit("lea", cat(label(slot), ",", reg));
    } else {
        loadFrameAddr(reg, slot);
    }
}

void STM32Codegen::loadArrayInitElem(const std::string &reg, const ir::ArrayInitElem &e) {
    if (!e.isSymbol) {
        emit("movl", cat(static_cast<int32_t>(static_cast<uint32_t>(e.value)), ",", reg));
        return;
    }
    emit("lea", cat((e.slot ? label(e.slot) : label(e.func)), ",", reg));
    if (!e.slot) {
        emit("movl", "1,r1");
        emit("add", cat(reg, ",", reg, ",r1"));
    }
}

std::string STM32Codegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    // In object mode, `prog` may not define `main`; no object file emits the vector
    // table/reset-handler/`_start` - STM32LinkerFactory.cpp synthesizes exactly one once every
    // object is merged. An object's flash section is `org 0`; placing it is the linker's job.
    //
    // RAM keeps its real fixed hardware base (kRamBase) even in object mode, since its
    // placement is independent of where this object's flash ends up linked - so a RAM address
    // resolves to its final value at assembly time, no base-shift relocation needed.
    //
    // Running byte offset within RAM, used to pad ahead of a global wider than 1 byte (an
    // unaligned ldr/str/ldrh/strh is UNPREDICTABLE on this core). kRamBase is word-aligned, so
    // tracking this from 0 is exact.
    // --dump-asm wants one file per input, not one combined dump - see
    // AlphaCodegen.cpp's own generate() for the pattern this mirrors
    // (tellp()-position ranges, sliced into perFileAsm_ at the end).
    // `initRanges` are captured against `code_` before the swap below
    // (`initCode = code_.str(); code_.str("")`), so they stay valid
    // offsets into `initCode` afterward - the same bytes, just renamed.
    std::vector<std::tuple<std::string, size_t, size_t>> initRanges;
    std::vector<std::tuple<std::string, size_t, size_t>> ramRanges;
    std::vector<std::tuple<std::string, size_t, size_t>> codeRanges;

    size_t ramOffset = 0;
    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        // An external global is defined and reserves its RAM storage in another file; this
        // file only references it by name, deferred to a relocation.
        if (slot->isExternal) continue;
        if (debugMapEnabled_) debugSymbols_.push_back({label(slot.get()), /*isFunc=*/false});
        size_t ramStart = ramTrailer_.size();
        size_t initStart = static_cast<size_t>(code_.tellp());
        int align = std::min(widthOfType(slot->type), 4);
        size_t padding = (align - (ramOffset % align)) % align;
        if (padding > 0) {
            ramTrailer_.push_back("dsb " + std::to_string(padding));
            ramOffset += padding;
        }
        const std::string lbl = label(slot.get());
        if (slot->arraySize > 0) {
            declareArray(lbl, slot->arrayInit, slot->type);
            ramOffset += static_cast<size_t>(widthOfType(slot->type)) * static_cast<size_t>(slot->arraySize);
            // The startup copy writes each nonzero element into RAM before `main` runs, spliced
            // into `_start`; skipped in object mode since no `_start` is emitted there.
            if (!objectMode) {
                int w = widthOfType(slot->type);
                for (size_t i = 0; i < slot->arrayInit.size(); i++) {
                    genArrayElemInit(lbl, i * static_cast<size_t>(w), slot->type, slot->arrayInit[i]);
                }
            }
        } else {
            declareTemp(lbl, slot->type);
            ramOffset += static_cast<size_t>(widthOfType(slot->type));
            if (!objectMode) genGlobalInit(lbl, slot->type, slot->hasInit ? slot->initValue : 0);
        }
        ramRanges.push_back({slot->sourceFile, ramStart, ramTrailer_.size()});
        initRanges.push_back({slot->sourceFile, initStart, static_cast<size_t>(code_.tellp())});
    }
    std::string initCode = code_.str();
    code_.str("");

    emitRaw(cat("org ", (objectMode ? 0 : kFlashBase)));
    if (!objectMode) {
        emitRaw(cat("dl ", kInitialSP));  // vector[0]: initial sp
        emitRaw("dlt _start");               // vector[1]: reset handler, thumb bit forced on
        emitLabel("_start");
        emitVerbatim(initCode);
        emit("bl", label(mainFn));
        // main's result sits in r0 when execution reaches `halt`.
        emitLabel("halt");
        emit("b", "halt");
        emitRaw("");
    }

    for (const auto &fb : prog.functionBodies) {
        size_t codeStart = static_cast<size_t>(code_.tellp());
        genFunction(fb);
        codeRanges.push_back({fb.func->sourceFile, codeStart, static_cast<size_t>(code_.tellp())});
    }

    emitRaw("");
    emitRaw(cat("ramorg ", kRamBase));
    for (const auto &line : ramTrailer_) emitVerbatim(line);

    std::string codeStr = code_.str();
    perFileAsm_.clear();
    for (const auto &[file, s, e] : codeRanges) perFileAsm_[file] += codeStr.substr(s, e - s);
    for (const auto &[file, s, e] : initRanges) perFileAsm_[file] += initCode.substr(s, e - s);
    for (const auto &[file, s, e] : ramRanges) {
        for (size_t i = s; i < e; i++) perFileAsm_[file] += ramTrailer_[i] + "\n";
    }

    return code_.str();
}

std::map<std::string, std::string> STM32Codegen::perFileAsm() const { return perFileAsm_; }

std::optional<std::string> STM32Codegen::debugMap(const std::string &asmText, const std::string &format,
                                                    const std::string &sourcePath) const {
    if (format != "raw") return std::nullopt;
    return assembleSTM32DebugMap(asmText, sourcePath, debugSymbols_);
}

std::optional<std::string> STM32Codegen::debugMapPreLink(const std::string &asmText,
                                                    const std::string &sourcePath) const {
    return assembleSTM32DebugMap(asmText, sourcePath, debugSymbols_, /*allowExternal=*/true);
}

// Only emits a marker when `debugMapEnabled_` (set via setDebugMapEnabled(),
// before generate() runs) and `line` actually differs from the last one
// emitted - see Pdp7Codegen.cpp's identical emitSourceLine(), which this
// mirrors exactly.
void STM32Codegen::emitSourceLine(int line) {
    if (!debugMapEnabled_ || line == 0 || line == lastDebugLine_) return;
    emitRaw(cat("__line ", line));
    lastDebugLine_ = line;
}

void STM32Codegen::genFunction(const ir::FunctionBody &fn) {
    lastDebugLine_ = 0;
    if (debugMapEnabled_) debugSymbols_.push_back({label(fn.func), /*isFunc=*/true});
    emitLabel(label(fn.func));
    emit("pushlr");
    emit("push", "r7");
    emit("mov", "r7,sp");
    // frameSize is a byte count; each push/pop reserves/releases a whole 4-byte word (there's
    // no `sub sp,#imm` here), so this rounds up to whole words.
    frameSize_ = fn.func->frameSize;
    int frameWords = (frameSize_ + 3) / 4;
    for (int i = 0; i < frameWords; i++) emit("push", "r0");
    genStmt(*fn.body);
    emitEpilogue();  // only reached if control falls off the end with no `return`
    emitRaw("");
}

void STM32Codegen::emitEpilogue() {
    int frameWords = (frameSize_ + 3) / 4;
    for (int i = 0; i < frameWords; i++) emit("pop", "r1");
    emit("pop", "r7");
    emit("poplr");
}

void STM32Codegen::genStmt(const ir::Stmt &s) {
    emitSourceLine(s.line);
    switch (s.kind) {
        case ir::StmtKind::Block: {
            for (const auto &child : s.body) genStmt(*child);
            return;
        }
        case ir::StmtKind::If: {
            genExpr(*s.cond);
            if (isWide(s.cond->type)) emit("orr", "r0,r1");
            emit("cmpz", "r0");
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            emit("beq", (s.elseStmt ? Lelse : Ldone));
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
            std::string Lstart = newLabel();
            std::string Lend = newLabel();
            std::string Lcont = s.postStmt ? newLabel() : Lstart;
            emitLabel(Lstart);
            genExpr(*s.cond);
            if (isWide(s.cond->type)) emit("orr", "r0,r1");
            emit("cmpz", "r0");
            emit("beq", Lend);
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
            if (s.initOrReturnOrExpr) genExpr(*s.initOrReturnOrExpr);
            // Early exit, so genFunction()'s fall-off-the-end epilogue never runs for this path.
            emitEpilogue();
            return;
        }
        case ir::StmtKind::LocalDecl: {
            if (s.declSlot->arraySize > 0) {
                genLocalArrayInit(s.declSlot);
            } else if (s.initOrReturnOrExpr) {
                genExpr(*s.initOrReturnOrExpr);
                loadFrameAddr("r2", s.declSlot);
                storeOperand("r2", s.declSlot->type);
            }
            // No "else" for a bare, uninitialized scalar: its frame word already exists
            // (genFunction()'s reserve-pushes made room for it), like an ordinary C automatic.
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

void STM32Codegen::genExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit: {
            if (isWide(e.type)) {
                auto v = static_cast<uint64_t>(e.intValue);
                emit("movl", cat(static_cast<int32_t>(static_cast<uint32_t>(v)), ",r0"));
                emit("movl", cat(static_cast<int32_t>(static_cast<uint32_t>(v >> 32)), ",r1"));
                return;
            }
            emit("movl", cat(static_cast<int32_t>(static_cast<uint32_t>(e.intValue)), ",r0"));
            return;
        }
        case ir::ExprKind::Var: {
            loadSlotAddr("r2", e.slot);
            loadOperand("r2", e.slot->type);
            return;
        }
        case ir::ExprKind::Index: {
            genIndex(e);
            return;
        }
        case ir::ExprKind::Assign: {
            if (e.lhs) {  // slot[idx] = rhs
                genIndexAddr(e.slot, *e.lhs);  // r2 = target address
                emit("push", "r2");
                genExpr(*e.rhs);
                emit("pop", "r2");
                storeOperand("r2", e.slot->type);
                return;
            }
            genExpr(*e.rhs);
            loadSlotAddr("r2", e.slot);
            storeOperand("r2", e.slot->type);
            return;
        }
        case ir::ExprKind::DerefAssign: {
            // Stores at e.type's width/signedness: the pointee type of a typed pointer, or a
            // plain native int word for the untyped-int-as-address style.
            genExpr(*e.lhs);  // r0 = pointer value
            emit("mov", "r2,r0");
            emit("push", "r2");
            genExpr(*e.rhs);  // r0, or r1:r0 if wide
            emit("pop", "r2");
            storeOperand("r2", e.type);
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            if (e.lhs) {  // slot[idx] op= rhs (including slot[idx]++/--)
                genIndexAddr(e.slot, *e.lhs);  // r2 = address
                genCompoundThroughR2(e);
                return;
            }
            // Plain scalar target - loadSlotAddr() is side-effect-free, so it's computed after
            // e.rhs, same order as the Assign case above.
            genExpr(*e.rhs);  // r0 = rhs
            emit("mov", "r1,r0");
            loadSlotAddr("r2", e.slot);
            loadOperand("r2", e.type);  // r0 = old
            if (e.yieldOld) emit("push", "r0");
            genCompoundOp(e.op, e.type);
            narrowR0To(e.type);
            storeOperand("r2", e.type);
            if (e.yieldOld) emit("pop", "r0");
            return;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            genExpr(*e.lhs);  // r0 = pointer value
            emit("mov", "r2,r0");  // r2 = address
            genCompoundThroughR2(e);
            return;
        }
        case ir::ExprKind::Ternary: {
            genExpr(*e.lhs);
            if (isWide(e.lhs->type)) emit("orr", "r0,r1");
            emit("cmpz", "r0");
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            emit("beq", Lelse);
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
                    genNegate64();
                } else {
                    emit("neg", "r0,r0");
                }
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                // Loads at e.type's width/signedness, as DerefAssign above.
                genExpr(*e.rhs);  // r0 = pointer value
                emit("mov", "r2,r0");
                loadOperand("r2", e.type);
            } else if (e.op == "~") {
                genExpr(*e.rhs);
                emit("mvn", "r0,r0");
                if (isWide(e.rhs->type)) emit("mvn", "r1,r1");
            } else {  // "&"
                genAddrOf(*e.rhs);
            }
            return;
        }
        case ir::ExprKind::Call: {
            // Arguments are pushed last-declared one first, matching Param addressing. The
            // caller cleans them back off with one dummy `pop r1` per argument (no
            // `add sp,#imm` here). A wide argument pushes its high word first so its low word
            // ends up closer to r7, matching a wide Param's frameOffset/frameOffset+4 layout.
            int pushedWords = 0;
            for (size_t i = e.args.size(); i-- > 0;) {
                genExpr(*e.args[i]);
                if (isWide(e.args[i]->type)) {
                    emit("push", "r1");
                    emit("push", "r0");
                    pushedWords += 2;
                } else {
                    emit("push", "r0");
                    pushedWords += 1;
                }
            }
            emit("bl", label(e.func));
            for (int i = 0; i < pushedWords; i++) emit("pop", "r1");
            return;
        }
        case ir::ExprKind::CallPtr: {
            // e.args is always empty: no per-callee p_<fn>_<i> cell exists until the callee is
            // known. `blx r0` is an indirect call through whatever address r0 holds.
            genExpr(*e.lhs);  // r0 = the callee's own entry address
            emit("blx", "r0");
            return;
        }
        case ir::ExprKind::Binary: {
            genBinary(e);
            return;
        }
        case ir::ExprKind::Intrinsic: {
            // Cortex-M has real PRIMASK/cpsie/cpsid, but nothing here sets up an
            // exception/interrupt table for it to matter against on a bare-metal no-OS compile.
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

// `r2` holds the computed address. `arraySize > 0` means the base is the array's first word
// (a label for a Global, or an r7-relative address for a Local/Param); otherwise `slot` is a
// pointer variable, so the base comes from reading that variable's stored value first. Either
// array's element addresses increase with the index.
void STM32Codegen::genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
    genExpr(idxExpr);  // r0 = index
    // r0 *= the element type's width via a variable-count `lsl`, or a real multiply for a
    // non-power-of-two width (a struct's).
    int shift = elemShift(slot->type);
    if (shift > 0) {
        emit("movl", cat(shift, ",r3"));
        emit("lsl", "r0,r0,r3");
    } else if (shift < 0) {
        scaleIndexByWidth(slot->type);
    }
    if (slot->arraySize > 0) {
        if (slot->kind == ir::SlotKind::Global) {
            emit("lea", cat(label(slot), ",r2"));
            emit("add", "r2,r2,r0");
        } else {
            loadFrameAddr("r2", slot);
            emit("add", "r2,r2,r0");
        }
        return;
    }
    loadSlotAddr("r2", slot);
    emit("ldr", "r2,[r2]");  // r2 = the pointer variable's own stored value
    emit("add", "r2,r2,r0");
}

void STM32Codegen::genIndex(const ir::Expr &e) {
    genIndexAddr(e.slot, *e.lhs);
    loadOperand("r2", e.slot->type);
}

void STM32Codegen::genCompoundOp(const std::string &op, Type type) {
    bool isUnsigned = type.isUnsigned;
    if (op == "+") { emit("add", "r0,r0,r1"); return; }
    if (op == "-") { emit("sub", "r0,r0,r1"); return; }
    if (op == "*") { emit("mul", "r1,r0"); return; }
    if (op == "&") { emit("and", "r0,r1"); return; }
    if (op == "|") { emit("orr", "r0,r1"); return; }
    if (op == "^") { emit("eor", "r0,r1"); return; }
    if (op == "<<" || op == ">>") {
        const char *insn = op == "<<" ? "lsl" : (isUnsigned ? "lsr" : "asr");
        emit(insn, "r0,r0,r1");
        return;
    }
    // "/" or "%": r4 = old / rhs (never r2, which must stay intact as the address register for
    // the store right after this call returns); "%" recovers the remainder via multiply-subtract.
    emit((isUnsigned ? "udiv" : "sdiv"), "r4,r0,r1");
    if (op == "%") {
        emit("mul", "r1,r4");     // r4 = r4 * r1 = quotient * rhs
        emit("sub", "r0,r0,r4");  // r0 = old - quotient*rhs = remainder
    } else {
        emit("mov", "r0,r4");
    }
}

// r2 must already hold the resolved address on entry; it's stashed on the stack across e.rhs's
// evaluation, since that can recursively reach arbitrary codegen (calls included) that may
// clobber every other register.
void STM32Codegen::genCompoundThroughR2(const ir::Expr &e) {
    emit("push", "r2");
    genExpr(*e.rhs);              // r0 = rhs
    emit("mov", "r1,r0");    // r1 = rhs, held across the r2 reload below
    emit("pop", "r2");        // r2 = address
    loadOperand("r2", e.type);         // r0 = old
    if (e.yieldOld) emit("push", "r0");
    genCompoundOp(e.op, e.type);          // r0 = new (raw)
    narrowR0To(e.type);
    if (e.yieldOld) {
        emit("mov", "r4,r0");   // r4 = new
        emit("pop", "r0");       // r0 = old
        emit("mov", "r5,r0");     // r5 = old, preserved across the store below
        emit("mov", "r0,r4");      // r0 = new (storeOperand always stores from r0)
        storeOperand("r2", e.type);         // r2 still addresses the target (loadOperand never moves it - e.type is never wide)
        emit("mov", "r0,r5");          // r0 = old (final result)
    } else {
        storeOperand("r2", e.type);  // r0 already = new; r2 still addresses the target
    }
}

// Stores arrayInit into `slot`'s frame slots. Unlike a Global's static trailer, a Local's
// storage is a fresh frame region each call, so this must re-run every time. An element's
// address is i*widthOfType(slot->type) bytes above element 0's.
void STM32Codegen::genLocalArrayInit(const ir::Slot *slot) {
    int w = widthOfType(slot->type);
    for (size_t i = 0; i < slot->arrayInit.size(); i++) {
        const ir::ArrayInitElem &elem = slot->arrayInit[i];
        loadArrayInitElem("r0", elem);
        if (w == 8 && !elem.isSymbol) {
            auto v = static_cast<uint64_t>(elem.value);
            emit("movl", cat(static_cast<int32_t>(static_cast<uint32_t>(v >> 32)), ",r1"));
        }
        loadFrameWordAddr("r2", -ir::localLowOffset(*slot, typeWidths()) + w * static_cast<int>(i));
        storeOperand("r2", slot->type);
    }
}

void STM32Codegen::narrowR0To(Type to) {
    int w = widthOfType(to);
    // Re-truncate+re-extend via `lsl` then `asr`/`lsr` (zero-fill for unsigned, sign-fill for
    // signed).
    if (w == 1) {
        emit("movl", "24,r3");
        emit("lsl", "r0,r0,r3");
        emit((to.isUnsigned ? "lsr" : "asr"), "r0,r0,r3");
    } else if (w == 2) {
        emit("movl", "16,r3");
        emit("lsl", "r0,r0,r3");
        emit((to.isUnsigned ? "lsr" : "asr"), "r0,r0,r3");
    }
}

// `addrReg` holds the already-computed effective address (r2 by convention); loads into r0
// (or r0:r1 for `long long`). `ldrb`/`ldrh` always zero-extend, so a signed source needs an
// additional re-sign-extend via narrowR0To().
void STM32Codegen::loadOperand(const std::string &addrReg, Type type) {
    int w = widthOfType(type);
    if (w == 1) {
        emit("ldrb", cat("r0,[", addrReg, "]"));
        if (!type.isUnsigned) narrowR0To(type);
    } else if (w == 2) {
        emit("ldrh", cat("r0,[", addrReg, "]"));
        if (!type.isUnsigned) narrowR0To(type);
    } else if (w == 4) {
        emit("ldr", cat("r0,[", addrReg, "]"));
    } else {
        emit("ldr", cat("r0,[", addrReg, "]"));
        emit("movl", "4,r3");
        emit("add", cat("r6,", addrReg, ",r3"));
        emit("ldr", "r1,[r6]");
    }
}

void STM32Codegen::storeOperand(const std::string &addrReg, Type type) {
    int w = widthOfType(type);
    if (w == 1) {
        emit("strb", cat("r0,[", addrReg, "]"));
    } else if (w == 2) {
        emit("strh", cat("r0,[", addrReg, "]"));
    } else if (w == 4) {
        emit("str", cat("r0,[", addrReg, "]"));
    } else {
        emit("str", cat("r0,[", addrReg, "]"));
        emit("movl", "4,r3");
        emit("add", cat("r6,", addrReg, ",r3"));
        emit("str", "r1,[r6]");
    }
}

// An implicit or explicit type conversion. IRBuilder already promotes narrower operands before
// Binary/Unary sees them, so the only work here is crossing the int/`long long` boundary or
// renarrowing via narrowR0To().
void STM32Codegen::genConvert(const ir::Expr &e) {
    Type from = e.lhs->type;
    Type to = e.type;
    bool fromWide = isWide(from);
    bool toWide = isWide(to);
    genExpr(*e.lhs);
    if (fromWide && !toWide) {
        narrowR0To(to);  // r1 is simply discarded
    } else if (!fromWide && toWide) {
        if (from.isUnsigned) {
            emit("movl", "0,r1");
        } else {
            emit("mov", "r1,r0");
            emit("movl", "31,r3");
            emit("asr", "r1,r1,r3");
        }
    } else if (!fromWide && !toWide) {
        narrowR0To(to);
    }
    // fromWide && toWide: the bit pattern is already correct, nothing
    // to do.
}

void STM32Codegen::genNegate64() {
    // Two's complement negate of r0:r1 in place.
    emit("mvn", "r0,r0");
    emit("mvn", "r1,r1");
    emit("movl", "1,r3");
    emit("add", "r0,r0,r3");
    emit("movl", "0,r3");
    emit("adc", "r1,r3");
}

void STM32Codegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        loadSlotAddr("r2", operand.slot);
        emit("mov", "r0,r2");
        return;
    }
    if (operand.kind == ir::ExprKind::Index) {
        genIndexAddr(operand.slot, *operand.lhs);
        emit("mov", "r0,r2");
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        // Cortex-M Thumb interworking: a function address must carry the Thumb-select bit
        // (LSB=1) for `blx` to branch into it correctly. Every function label is always even
        // (2-byte aligned), so this is exactly "address + 1".
        emit("lea", cat(label(operand.func), ",r0"));
        emit("movl", "1,r1");
        emit("add", "r0,r0,r1");
        return;
    }
    err(operand.line, "internal: unhandled addressable-operand kind");
}

// `long long` add/sub: evaluate lhs into r0:r1, stash it on the stack while rhs evaluates into
// r4:r5, then ripple the carry/borrow across the word pair with `adc`/`sbc`.
void STM32Codegen::genWideAdd(const ir::Expr &lhs, const ir::Expr &rhs, bool isAdd) {
    genExpr(lhs);
    emit("push", "r1");
    emit("push", "r0");  // stack (top->bottom): lhs_lo, lhs_hi
    genExpr(rhs);
    emit("mov", "r4,r0");
    emit("mov", "r5,r1");  // r4=rhs_lo, r5=rhs_hi
    emit("pop", "r0");
    emit("pop", "r1");        // r0:r1 = lhs restored
    if (isAdd) {
        emit("add", "r0,r0,r4");
        emit("adc", "r1,r5");
    } else {
        emit("sub", "r0,r0,r4");
        emit("sbc", "r1,r5");
    }
}

// 32x32->64 unsigned widening multiply: {r1:r0} = r6(a) * r4(b), both consumed. No widening
// multiply instruction exists, so this is a bit-serial shift-and-add loop: `a` consumed one
// bit at a time (shifted right, carry tested via `bhs`/`blo`); `b`'s doubled value (r4:r5,
// growing to 64 bits) added into the accumulator (r0:r1) whenever that bit is 1. Clobbers r0-r6.
void STM32Codegen::genMul64() {
    emit("movl", "0,r5");  // r5 = b's high word (b, r4, is only ever 32 bits wide)
    emit("movl", "0,r0");
    emit("movl", "0,r1");
    emit("movl", "0,r2");  // r2 = iteration count-up (0..32)
    std::string Lloop = newLabel(), LdoAdd = newLabel(), LskipAdd = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("movl", "32,r3");
    emit("cmp", "r2,r3");
    emit("beq", Ldone);
    emit("movl", "1,r3");
    emit("add", "r2,r2,r3");
    // The carry flag left by shifting out a's low bit IS that bit, tested via `bhs`/`blo`.
    emit("lsr", "r6,r6,r3");
    emit("bhs", LdoAdd);
    emit("b", LskipAdd);
    emitLabel(LdoAdd);
    emit("add", "r0,r0,r4");
    emit("adc", "r1,r5");
    emitLabel(LskipAdd);
    // Double b (r4:r5) via self-add, carrying its old top bit into r5 through `adc`.
    emit("add", "r4,r4,r4");
    emit("adc", "r5,r5");
    emit("b", Lloop);
    emitLabel(Ldone);
}

// `long long` multiply, keeping the low 64 bits of the true 128-bit product: result =
// lo(lhs)*lo(rhs) (via genMul64()) plus the cross terms lhs_hi*rhs_lo/lhs_lo*rhs_hi folded into
// the high word only (truncating `mul` is exactly right for them).
void STM32Codegen::genWideMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    emit("push", "r1");
    emit("push", "r0");  // stash lhs: top->bottom = lhs_lo, lhs_hi
    genExpr(rhs);
    emit("mov", "r6,r0");
    emit("mov", "r5,r1");  // r6=rhs_lo, r5=rhs_hi
    emit("pop", "r4");
    emit("pop", "r2");        // r4=lhs_lo, r2=lhs_hi
    // Cross terms computed now, before genMul64() clobbers r4/r6.
    emit("mov", "r3,r2");
    emit("mul", "r6,r3");  // r3 = lhs_hi*rhs_lo
    emit("mov", "r2,r5");
    emit("mul", "r4,r2");  // r2 = rhs_hi*lhs_lo
    emit("add", "r2,r2,r3");
    emit("push", "r2");  // stash the cross-term sum across genMul64()'s own full clobber
    genMul64();                 // {r1:r0} = r6(rhs_lo) * r4(lhs_lo), the full 64-bit low*low product
    emit("pop", "r2");
    emit("add", "r1,r1,r2");
}

// A `long long` shifted by a runtime count: no instruction here shifts more than 32 bits at
// once, so this is a counted loop of 1-bit shifts (r6 = count). `<<` doubles r0:r1 via the
// self-add carry-chained-doubling trick. `>>` has no `adc` analogue for the downward direction,
// so the bit crossing the r1/r0 boundary is captured into r4 before shifting, then OR'd into
// r0's new top bit via a precomputed 0x80000000 mask (r3); `isUnsigned` picks `asr` vs `lsr`
// for r1's shift.
void STM32Codegen::genWideShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned) {
    genExpr(lhs);
    emit("push", "r1");
    emit("push", "r0");
    genExpr(rhs);  // r0 = shift count (already promoted to a plain int)
    emit("mov", "r6,r0");
    emit("pop", "r0");
    emit("pop", "r1");  // r0:r1 restored = the value being shifted
    emit("movl", "1,r5");            // r5 = the fixed amount (1) every shift/decrement below uses
    std::string Lloop = newLabel(), Ldone = newLabel();
    if (!isLeft) {
        emit("movl", "1,r3");
        emit("movl", "31,r2");
        emit("lsl", "r3,r3,r2");  // r3 = 0x80000000
    }
    emitLabel(Lloop);
    emit("cmpz", "r6");
    emit("beq", Ldone);
    emit("sub", "r6,r6,r5");
    if (isLeft) {
        emit("add", "r0,r0,r0");
        emit("adc", "r1,r1");
    } else {
        std::string LskipOr = newLabel();
        emit("mov", "r4,r1");
        emit("and", "r4,r5");  // r4 = r1's own old bit 0 (the bit crossing into r0)
        emit((isUnsigned ? "lsr" : "asr"), "r1,r1,r5");
        emit("lsr", "r0,r0,r5");
        emit("cmpz", "r4");
        emit("beq", LskipOr);
        emit("orr", "r0,r3");
        emitLabel(LskipOr);
    }
    emit("b", Lloop);
    emitLabel(Ldone);
}

// Bit-serial shift/compare/subtract long division (no instruction here is wide enough for a
// genuine 64/64 divide): r0:r1 start as the dividend and finish as the quotient (restoring
// division, building the quotient into bits vacated as the dividend shifts left); r4:r5 is the
// remainder (starts 0); r2:r3 is the divisor. `isUnsigned` false divides magnitudes (each
// operand's sign bit, read before negating, is stashed and restored afterward per C's
// truncating-division rule).
void STM32Codegen::genWideDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    emit("push", "r1");
    emit("push", "r0");
    genExpr(rhs);
    emit("mov", "r2,r0");
    emit("mov", "r3,r1");  // r2:r3 = divisor
    emit("pop", "r0");
    emit("pop", "r1");        // r0:r1 = dividend

    if (!isUnsigned) {
        emit("movl", "31,r4");
        emit("mov", "r5,r1");
        emit("lsr", "r5,r5,r4");  // r5 = dividend's own sign (0/1)
        emit("mov", "r6,r3");
        emit("lsr", "r6,r6,r4");  // r6 = divisor's own sign (0/1)
        emit("push", "r5");
        emit("push", "r6");          // stack (top->bottom): dividendSign, divisorSign
        std::string LskipNegN = newLabel(), LskipNegD = newLabel();
        emit("cmpz", "r5");
        emit("beq", LskipNegN);
        emit("mvn", "r0,r0");
        emit("mvn", "r1,r1");
        emit("movl", "1,r4");
        emit("add", "r0,r0,r4");
        emit("movl", "0,r4");
        emit("adc", "r1,r4");
        emitLabel(LskipNegN);
        emit("cmpz", "r6");
        emit("beq", LskipNegD);
        emit("mvn", "r2,r2");
        emit("mvn", "r3,r3");
        emit("movl", "1,r4");
        emit("add", "r2,r2,r4");
        emit("movl", "0,r4");
        emit("adc", "r3,r4");
        emitLabel(LskipNegD);
    }

    emit("movl", "0,r4");
    emit("movl", "0,r5");  // remainder = 0
    emit("movl", "64,r6");                 // iteration count-down (64..0), using `cmpz`
                                                   // instead of a second compare register

    std::string Lloop = newLabel(), Ldone = newLabel();
    std::string LhiDiff = newLabel(), LsubYes = newLabel(), LsubNo = newLabel();
    emitLabel(Lloop);
    emit("cmpz", "r6");
    emit("beq", Ldone);
    // No register is spare for a "1" constant - `push r3`/`pop r3` borrows the divisor's high
    // word as a scratch slot, safe since nothing reads it again until this restores it.
    emit("push", "r3");
    emit("movl", "1,r3");
    emit("sub", "r6,r6,r3");
    emit("pop", "r3");

    // Shift {remainder:dividend} left by 1 as one combined value via carry-chained doubling;
    // the bit overflowing out of the dividend (r1's old top bit) lands in the remainder's new
    // bit 0 through the carry chain.
    emit("add", "r0,r0,r0");
    emit("adc", "r1,r1");
    emit("adc", "r4,r4");
    emit("adc", "r5,r5");

    // if (remainder >= divisor) { remainder -= divisor; quotient bit = 1 } - an unsigned
    // magnitude compare, since both operands were already negated non-negative for the signed case.
    emit("cmp", "r5,r3");
    emit("bne", LhiDiff);
    emit("cmp", "r4,r2");
    emit("bhs", LsubYes);
    emit("b", LsubNo);
    emitLabel(LhiDiff);
    emit("bhi", LsubYes);
    emit("b", LsubNo);
    emitLabel(LsubYes);
    emit("sub", "r4,r4,r2");
    emit("sbc", "r5,r3");
    emit("push", "r3");
    emit("movl", "1,r3");
    emit("add", "r0,r0,r3");
    emit("pop", "r3");  // set the vacated quotient bit
    emitLabel(LsubNo);
    emit("b", Lloop);

    emitLabel(Ldone);
    if (!isUnsigned) {
        emit("pop", "r6");  // r6 = divisorSign
        emit("pop", "r2");  // r2 = dividendSign
        std::string LskipNegQ = newLabel(), LskipNegR = newLabel();
        emit("mov", "r3,r6");
        emit("eor", "r3,r2");  // r3 = divisorSign ^ dividendSign (quotient-negate flag)
        emit("cmpz", "r3");
        emit("beq", LskipNegQ);
        emit("mvn", "r0,r0");
        emit("mvn", "r1,r1");
        emit("movl", "1,r6");
        emit("add", "r0,r0,r6");
        emit("movl", "0,r6");
        emit("adc", "r1,r6");
        emitLabel(LskipNegQ);
        emit("cmpz", "r2");
        emit("beq", LskipNegR);
        emit("mvn", "r4,r4");
        emit("mvn", "r5,r5");
        emit("movl", "1,r6");
        emit("add", "r4,r4,r6");
        emit("movl", "0,r6");
        emit("adc", "r5,r6");
        emitLabel(LskipNegR);
    }
    if (wantRemainder) {
        emit("mov", "r0,r4");
        emit("mov", "r1,r5");
    }
}

// `long long` comparison: compare the high words first (per `isUnsigned`, since that's where
// the sign bit lives); only if equal does the low word matter, always compared unsigned.
void STM32Codegen::genWideCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned) {
    genExpr(lhs);
    emit("push", "r1");
    emit("push", "r0");
    genExpr(rhs);
    emit("mov", "r4,r0");
    emit("mov", "r5,r1");  // r4:r5 = rhs
    emit("pop", "r0");
    emit("pop", "r1");        // r0:r1 = lhs
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    if (op == "==" || op == "!=") {
        const char *onEqual = op == "==" ? "1" : "0";
        const char *onDiffer = op == "==" ? "0" : "1";
        emit("cmp", "r0,r4");
        emit("bne", Lfalse);
        emit("cmp", "r1,r5");
        emit("bne", Lfalse);
        emit("movl", cat(onEqual, ",r0"));
        emit("b", Ldone);
        emitLabel(Lfalse);
        emit("movl", cat(onDiffer, ",r0"));
        emitLabel(Ldone);
        return;
    }
    std::string signedCC = op == "<" ? "blt" : op == "<=" ? "ble" : op == ">" ? "bgt" : "bge";
    std::string unsignedCC = op == "<" ? "blo" : op == "<=" ? "bls" : op == ">" ? "bhi" : "bhs";
    std::string hiCC = isUnsigned ? unsignedCC : signedCC;
    std::string LhiDiff = newLabel();
    emit("cmp", "r1,r5");
    emit("bne", LhiDiff);
    emit("cmp", "r0,r4");
    emit(unsignedCC, cat("", Ltrue));
    emit("b", Lfalse);
    emitLabel(LhiDiff);
    emit(hiCC, cat("", Ltrue));
    emit("b", Lfalse);
    emitLabel(Lfalse);
    emit("movl", "0,r0");
    emit("b", Ldone);
    emitLabel(Ltrue);
    emit("movl", "1,r0");
    emitLabel(Ldone);
}

void STM32Codegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    // Arithmetic/bitwise/comparison operands already share one common type (IRBuilder's usual
    // arithmetic conversions), except shift (only the LHS's type matters) and &&/|| (handled
    // below, width-independent).
    Type operandType = e.lhs->type;
    bool wide = isWide(operandType);
    bool isUnsigned = operandType.isUnsigned;
    if (op == "+" || op == "-") {
        if (wide) {
            genWideAdd(*e.lhs, *e.rhs, op == "+");
            return;
        }
        genExpr(*e.lhs);
        emit("push", "r0");
        genExpr(*e.rhs);
        emit("pop", "r1");  // r1 = lhs, r0 = rhs
        if (op == "+") {
            emit("add", "r0,r0,r1");  // r0 = rhs + lhs
        } else {
            emit("sub", "r0,r1,r0");  // r0 = lhs - rhs
        }
        return;
    }
    if (op == "*") {
        if (wide) {
            genWideMul(*e.lhs, *e.rhs);
        } else {
            genMul(*e.lhs, *e.rhs);
        }
        return;
    }
    if (op == "/" || op == "%") {
        if (wide) {
            genWideDivMod(*e.lhs, *e.rhs, /*wantRemainder=*/op == "%", isUnsigned);
        } else {
            genDivMod(*e.lhs, *e.rhs, /*wantRemainder=*/op == "%", isUnsigned);
        }
        return;
    }
    if (op == "&" || op == "|" || op == "^") {
        const char *insn = (op == "&") ? "and" : (op == "|") ? "orr" : "eor";
        if (wide) {
            genExpr(*e.lhs);
            emit("push", "r1");
            emit("push", "r0");
            genExpr(*e.rhs);
            emit("mov", "r4,r0");
            emit("mov", "r5,r1");
            emit("pop", "r0");
            emit("pop", "r1");  // r0:r1 = lhs
            emit(insn, "r0,r4");
            emit(insn, "r1,r5");
            return;
        }
        genExpr(*e.lhs);
        emit("push", "r0");
        genExpr(*e.rhs);
        emit("pop", "r1");  // r1 = lhs, r0 = rhs
        emit(insn, "r0,r1");
        return;
    }
    if (op == "<<" || op == ">>") {
        if (wide) {
            genWideShift(*e.lhs, *e.rhs, op == "<<", isUnsigned);
            return;
        }
        genExpr(*e.lhs);
        emit("push", "r0");
        genExpr(*e.rhs);
        emit("mov", "r1,r0");  // r1 = shift count
        emit("pop", "r0");     // r0 = value to shift
        const char *insn = op == "<<" ? "lsl" : (isUnsigned ? "lsr" : "asr");
        emit(insn, "r0,r0,r1");
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

// Hardware multiply (`muls`): a genuine 32x32->32 product, no operand-width caveat.
void STM32Codegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    emit("push", "r0");
    genExpr(rhs);
    emit("pop", "r1");    // r1 = lhs, r0 = rhs
    emit("mul", "r1,r0");  // r0 = r0 * r1 = rhs * lhs
}

// Hardware divide (`sdiv`/`udiv`): a genuine signed/unsigned integer divide, so no software
// loop is needed; `%` is computed directly via one multiply-subtract.
void STM32Codegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    emit("push", "r0");
    genExpr(rhs);
    emit("pop", "r1");          // r1 = lhs (dividend), r0 = rhs (divisor)
    emit((isUnsigned ? "udiv" : "sdiv"), "r2,r1,r0");  // r2 = lhs / rhs
    if (wantRemainder) {
        emit("mul", "r0,r2");       // r2 = r2 * r0 = quotient * rhs
        emit("sub", "r0,r1,r2");    // r0 = lhs - quotient*rhs = remainder
    } else {
        emit("mov", "r0,r2");
    }
}

void STM32Codegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs) {
    if (isWide(lhs.type)) {
        genWideCompare(op, lhs, rhs, lhs.type.isUnsigned);
        return;
    }
    bool isUnsigned = lhs.type.isUnsigned;
    if (op == "==") return emitCompare(lhs, rhs, "beq", isUnsigned);
    if (op == "!=") return emitCompare(lhs, rhs, "bne", isUnsigned);
    if (op == "<") return emitCompare(lhs, rhs, isUnsigned ? "blo" : "blt", isUnsigned);
    if (op == "<=") return emitCompare(lhs, rhs, isUnsigned ? "bls" : "ble", isUnsigned);
    if (op == ">") return emitCompare(lhs, rhs, isUnsigned ? "bhi" : "bgt", isUnsigned);
    if (op == ">=") return emitCompare(lhs, rhs, isUnsigned ? "bhs" : "bge", isUnsigned);
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

// `cmp` + condition-code branch: flags already encode signed/unsigned ordering correctly, no
// fixup needed. `isUnsigned` is unused here directly (baked into genCompare()'s choice of
// `branchIfTrue`) but documents why.
void STM32Codegen::emitCompare(const ir::Expr &lhs, const ir::Expr &rhs, const std::string &branchIfTrue,
                                 bool isUnsigned) {
    (void)isUnsigned;
    genExpr(lhs);
    emit("push", "r0");
    genExpr(rhs);
    emit("pop", "r1");  // r1 = lhs, r0 = rhs
    emit("cmp", "r1,r0");  // flags = r1 - r0 = lhs - rhs
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit(branchIfTrue, cat("", Ltrue));
    emit("movl", "0,r0");
    emit("b", Ldone);
    emitLabel(Ltrue);
    emit("movl", "1,r0");
    emitLabel(Ldone);
}

// A wide operand's truthiness needs both words: `orr r0,r1` folds them before `cmpz r0`.
void STM32Codegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    if (isWide(lhs.type)) emit("orr", "r0,r1");
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    emit("cmpz", "r0");
    emit("beq", Lfalse);
    genExpr(rhs);
    if (isWide(rhs.type)) emit("orr", "r0,r1");
    emit("cmpz", "r0");
    emit("beq", Lfalse);
    emit("b", Ltrue);
    emitLabel(Lfalse);
    emit("movl", "0,r0");
    emit("b", Ldone);
    emitLabel(Ltrue);
    emit("movl", "1,r0");
    emitLabel(Ldone);
}

void STM32Codegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    if (isWide(lhs.type)) emit("orr", "r0,r1");
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    emit("cmpz", "r0");
    emit("bne", Ltrue);
    genExpr(rhs);
    if (isWide(rhs.type)) emit("orr", "r0,r1");
    emit("cmpz", "r0");
    emit("bne", Ltrue);
    emit("b", Lfalse);
    emitLabel(Ltrue);
    emit("movl", "1,r0");
    emit("b", Ldone);
    emitLabel(Lfalse);
    emit("movl", "0,r0");
    emitLabel(Ldone);
}

void STM32Codegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    if (isWide(rhs.type)) emit("orr", "r0,r1");
    emit("cmpz", "r0");
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit("beq", Ltrue);
    emit("movl", "0,r0");
    emit("b", Ldone);
    emitLabel(Ltrue);
    emit("movl", "1,r0");
    emitLabel(Ldone);
}

std::optional<std::string> STM32Codegen::assembleInProcess(const std::string &asmText,
                                                             const std::string &format) const {
    if (format == "raw") return assembleSTM32(asmText);
    // `coff`/`elf`/`macho`/`aout`/`objraw`: builds one format-neutral ObjectFile, then picks
    // that format's FileFormatWriter to serialize it.
    if (auto kind = fileFormatKindFromName(format)) {
        ObjectFile obj = assembleSTM32Object(asmText);
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

}  // namespace rcc
