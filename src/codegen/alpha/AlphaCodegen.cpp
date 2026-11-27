#include "AlphaCodegen.h"

#include <cstdint>
#include <stdexcept>
#include <tuple>

#include "AlphaAssembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

// Where the whole program loads; the test harness sets the simulated CPU's PC here directly.
constexpr uint32_t kBaseAddress = kAlphaBaseAddress;

// Initial `sp`, comfortably above the loaded program and its data; matches
// AlphaLinkerFactory.cpp's own kInitialSP so a linked program behaves like a single-file one.
constexpr uint32_t kInitialSP = 0x00200000u;

// Splits a 32-bit value into the (hi,lo) halves an `ldah`+`lda` pair needs - see
// AlphaAssembler.h's own comment on why `lo` must be sign-extended before `hi` is derived from
// what's left.
std::pair<int32_t, int32_t> splitHiLo(int32_t v) {
    int32_t lo = static_cast<int32_t>(static_cast<int16_t>(v & 0xFFFF));
    int32_t hi = static_cast<int32_t>((static_cast<int64_t>(v) - lo) >> 16);
    return {hi, lo};
}
}  // namespace

std::string AlphaCodegen::arrayElemText(const ir::ArrayInitElem &e) {
    if (!e.isSymbol) return std::to_string(static_cast<int32_t>(static_cast<uint32_t>(e.value)));
    return e.slot ? label(e.slot) : label(e.func);
}

void AlphaCodegen::declareArray(const std::string &lbl, const std::vector<ir::ArrayInitElem> &values,
                                 Type elemType) {
    if (values.empty()) return;
    int w = widthOfType(elemType);
    // No 64-bit data directive in this dialect (see AlphaAssembler.h) - a `long long` array's
    // literal elements are emitted as two "dw"s (low word, then high) per element; a symbol
    // element is always a pointer-width address, so it still gets one "dw".
    const char *directive = w == 1 ? "db" : w == 2 ? "dh" : "dw";
    std::ostringstream line;
    line << lbl << ": " << directive << " ";
    bool first = true;
    for (const auto &e : values) {
        if (w == 8 && !e.isSymbol) {
            auto v = static_cast<uint64_t>(e.value);
            if (!first) line << ", ";
            line << static_cast<uint32_t>(v) << ", " << static_cast<uint32_t>(v >> 32);
            first = false;
            continue;
        }
        if (!first) line << ", ";
        line << arrayElemText(e);
        first = false;
    }
    trailer_.push_back(line.str());
}

void AlphaCodegen::loadAddress(const std::string &reg, const std::string &lbl) {
    emit("ldah", cat(reg, ",zero,", lbl));
    emit("lda", cat(reg, ",", reg, ",", lbl));
}

void AlphaCodegen::loadImm32(const std::string &reg, int32_t v) {
    if (v >= -32768 && v <= 32767) {
        emit("lda", cat(reg, ",zero,", v));
        return;
    }
    auto [hi, lo] = splitHiLo(v);
    // splitHiLo()'s %hi/%lo split is exactly reversible by one ldah+lda pair except at its
    // own edge: when `v` is large and positive enough that its low 16 bits (interpreted as
    // signed, per `lda`'s own sign extension) come out negative, `hi` can land one past the
    // top of `ldah`'s 16-bit signed field (`32768`, not representable there at all) - split
    // that one extra `0x10000` across two `ldah`s instead, each safely in range.
    if (hi > 32767) {
        emit("ldah", cat(reg, ",zero,16384"));
        emit("ldah", cat(reg, ",", reg, ",16384"));
    } else {
        emit("ldah", cat(reg, ",zero,", hi));
    }
    emit("lda", cat(reg, ",", reg, ",", lo));
}

// Builds the low 32 bits (cleaned to a genuine zero-extended quantity) and the high 32 bits
// separately, then combines them - the one construction a single loadImm32() can't do, since
// its own `ldah`/`lda` pair only ever produces a *sign-extended* 32-bit result.
void AlphaCodegen::loadImm64(const std::string &reg, uint64_t v) {
    int64_t sv = static_cast<int64_t>(v);
    if (sv == static_cast<int64_t>(static_cast<int32_t>(sv))) {
        loadImm32(reg, static_cast<int32_t>(sv));
        return;
    }
    loadImm32(reg, static_cast<int32_t>(static_cast<uint32_t>(v & 0xFFFFFFFFu)));
    emit("sll", cat(reg, ",", reg, ",32"));
    emit("srl", cat(reg, ",", reg, ",32"));
    loadImm32("at", static_cast<int32_t>(static_cast<uint32_t>((v >> 32) & 0xFFFFFFFFu)));
    emit("sll", "at,at,32");
    emit("bis", cat(reg, ",", reg, ",at"));
}

// A Param lives at `fp+frameOffset` (the caller's pushed argument space, see genFunction()'s
// own comment); a Local's lowest address is `fp-localLowOffset()`. Either way one `lda`
// suffices - Alpha's own address-arithmetic instruction takes a base register and a 16-bit
// displacement directly, no separate add step needed the way a target with no such
// instruction (e.g. MIPS's `addiu`) still manages fine, but Alpha's real `lda` is exactly
// this operation already.
void AlphaCodegen::loadSlotAddr(const std::string &reg, const ir::Slot *slot) {
    if (slot->kind == ir::SlotKind::Global) {
        loadAddress(reg, label(slot));
        return;
    }
    if (slot->kind == ir::SlotKind::Param) {
        emit("lda", cat(reg, ",fp,", slot->frameOffset));
        return;
    }
    emit("lda", cat(reg, ",fp,-", ir::localLowOffset(*slot, typeWidths())));
}

std::string AlphaCodegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    // In objectMode, `prog` may not define `main`; the entry sequence below is skipped since
    // AlphaLinkerFactory.cpp synthesizes it once every object is merged and `main` is
    // resolvable. An object's base is `org 0`; the linker places it.
    emitRaw("org " + std::to_string(objectMode ? 0 : kBaseAddress));
    emitRaw("");
    if (!objectMode) {
        auto [spHi, spLo] = splitHiLo(static_cast<int32_t>(kInitialSP));
        emitLabel("_start");
        emit("ldah", cat("sp,zero,", spHi));
        emit("lda", cat("sp,sp,", spLo));
        loadAddress("t12", label(mainFn));
        emit("jsr", "ra,(t12)");
        // main's result sits in t0 once execution reaches halt; there is no way to stop the
        // processor from here (see AlphaCodegen.h's own header comment), so this spins.
        emitLabel("halt");
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

    // A `ldindq`/`stindq` faults on a misaligned address, so a global wider than one byte
    // needs padding ahead of it to reach its own natural alignment (capped at 8, the widest
    // transfer this backend ever makes).
    size_t dataOffset = 0;
    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        if (slot->isExternal) continue;
        if (debugMapEnabled_) debugSymbols_.push_back({label(slot.get()), /*isFunc=*/false});
        size_t gTrailerStart = trailer_.size();
        const std::string lbl = label(slot.get());
        int elemW = widthOfType(slot->type);
        size_t totalBytes = slot->arraySize > 0 ? static_cast<size_t>(slot->arraySize) * elemW
                                                  : static_cast<size_t>(elemW);
        size_t align = static_cast<size_t>(elemW) < 8 ? static_cast<size_t>(elemW) : 8;
        while (align > 1 && (dataOffset % align) != 0) {
            trailer_.push_back("db 0  ; alignment padding");
            dataOffset += 1;
        }
        dataOffset += totalBytes;
        if (slot->arraySize > 0) {
            declareArray(lbl, slot->arrayInit, slot->type);
        } else if (slot->type.base == BaseType::Struct) {
            // No struct initializer exists in this subset, so this is
            // always zero-filled; reuses declareArray's own element-list
            // emission (one zero "db" per byte) rather than a new
            // directive, since this assembler dialect has no "reserve N
            // zero bytes" form of its own.
            declareArray(lbl, std::vector<ir::ArrayInitElem>(slot->type.structSize), Type{BaseType::Char, false});
        } else {
            long v = slot->hasInit ? slot->initValue : 0;
            if (elemW == 8) {
                auto uv = static_cast<uint64_t>(v);
                trailer_.push_back(lbl + ": dw " + std::to_string(static_cast<uint32_t>(uv)) + ", " +
                                    std::to_string(static_cast<uint32_t>(uv >> 32)) + "  ; " + slot->sourceName);
            } else {
                const char *directive = elemW == 1 ? "db" : elemW == 2 ? "dh" : "dw";
                trailer_.push_back(lbl + ": " + directive + " " +
                                    std::to_string(static_cast<int32_t>(static_cast<uint32_t>(v))) + "  ; " +
                                    slot->sourceName);
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

std::map<std::string, std::string> AlphaCodegen::perFileAsm() const { return perFileAsm_; }

// `fp`/`ra` are saved/restored as plain 32-bit values (`stindl`/`ldindl`, sign-extending back
// out on reload) rather than full 64-bit quadwords: every address in this backend's universe
// fits in 32 bits (kAlphaBaseAddress plus whatever this program's own code+data+stack need),
// so the low 32 bits already reconstruct the exact original value - and it keeps this
// reservation at exactly IR.h's own assumed "two native ints" (8 bytes) ahead of a Param's
// frameOffset=0 point, with no extra per-target adjustment the way a genuine 64-bit save would
// need (see Arm64Codegen.h's own `currentParamBase_`/`kParamExtra` for what that adjustment
// looks like when it can't be avoided this way).
void AlphaCodegen::genFunction(const ir::FunctionBody &fn) {
    lastDebugLine_ = 0;
    if (debugMapEnabled_) debugSymbols_.push_back({label(fn.func), /*isFunc=*/true});
    emitLabel(label(fn.func));
    emit("lda", "sp,sp,-8");
    emit("stindl", "sp,fp,0");
    emit("stindl", "sp,ra,4");
    emit("bis", "fp,sp,zero");
    if (fn.func->frameSize > 0) emit("lda", cat("sp,sp,-", fn.func->frameSize));

    genStmt(*fn.body);

    genEpilogue();
    emitRaw("");
}

void AlphaCodegen::genEpilogue() {
    emit("bis", "sp,fp,zero");
    emit("ldindl", "sp,at,0");
    emit("ldindl", "sp,ra,4");
    emit("lda", "sp,sp,8");
    emit("bis", "fp,at,zero");
    emit("ret", "zero,(ra)");
}

void AlphaCodegen::genStmt(const ir::Stmt &s) {
    emitSourceLine(s.line);
    switch (s.kind) {
        case ir::StmtKind::Block: {
            for (const auto &child : s.body) genStmt(*child);
            return;
        }
        case ir::StmtKind::If: {
            genExpr(*s.cond);
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            emit("beq", cat("t0,", (s.elseStmt ? Lelse : Ldone)));
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
            std::string Lstart = newLabel();
            std::string Lend = newLabel();
            std::string Lcont = s.postStmt ? newLabel() : Lstart;
            emitLabel(Lstart);
            genExpr(*s.cond);
            emit("beq", cat("t0,", Lend));
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
            if (s.initOrReturnOrExpr) genExpr(*s.initOrReturnOrExpr);
            genEpilogue();
            return;
        }
        case ir::StmtKind::LocalDecl: {
            if (s.declSlot->arraySize > 0) {
                genLocalArrayInit(s.declSlot);
            } else if (s.initOrReturnOrExpr) {
                genExpr(*s.initOrReturnOrExpr);
                loadSlotAddr("t2", s.declSlot);
                storeOperand("t2", s.declSlot->type);
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
            emit("br", irLabel(s.labelId));
            return;
        }
    }
}

void AlphaCodegen::genExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit: {
            if (isWide(e.type)) {
                loadImm64("t0", static_cast<uint64_t>(e.intValue));
            } else {
                // loadImm32() always sign-extends (the same `ldah`+`lda` sequence any signed
                // 32-bit value would use); an unsigned literal whose own bit 31 is set (e.g.
                // `2147483648u`) needs narrowT0To() to turn that back into a zero-extended
                // canonical representation.
                loadImm32("t0", static_cast<int32_t>(static_cast<uint32_t>(e.intValue)));
                narrowT0To(e.type);
            }
            return;
        }
        case ir::ExprKind::Var: {
            loadSlotAddr("t2", e.slot);
            loadOperand("t2", e.slot->type);
            return;
        }
        case ir::ExprKind::Index: {
            genIndex(e);
            return;
        }
        case ir::ExprKind::Assign: {
            if (e.lhs) {  // slot[idx] = rhs
                genIndexAddr(e.slot, *e.lhs);  // t2 = target address
                emit("lda", "sp,sp,-8");
                emit("stindq", "sp,t2,0");
                genExpr(*e.rhs);
                emit("ldindq", "sp,t2,0");
                emit("lda", "sp,sp,8");
                storeOperand("t2", e.slot->type);
                return;
            }
            genExpr(*e.rhs);
            loadSlotAddr("t2", e.slot);
            storeOperand("t2", e.slot->type);
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            if (e.lhs) {  // slot[idx] op= rhs (including slot[idx]++/--)
                genIndexAddr(e.slot, *e.lhs);
                genCompoundThroughT2(e);
                return;
            }
            loadSlotAddr("t2", e.slot);
            genCompoundThroughT2(e);
            return;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            genExpr(*e.lhs);  // t0 = pointer value
            emit("bis", "t2,t0,zero");
            genCompoundThroughT2(e);
            return;
        }
        case ir::ExprKind::Ternary: {
            genExpr(*e.lhs);
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            emit("beq", cat("t0,", Lelse));
            genExpr(*e.rhs);
            emit("br", Ldone);
            emitLabel(Lelse);
            genExpr(*e.elseExpr);
            emitLabel(Ldone);
            return;
        }
        case ir::ExprKind::DerefAssign: {
            genExpr(*e.lhs);  // t0 = pointer value
            emit("lda", "sp,sp,-8");
            emit("stindq", "sp,t0,0");
            genExpr(*e.rhs);
            emit("ldindq", "sp,t2,0");
            emit("lda", "sp,sp,8");
            storeOperand("t2", e.type);
            return;
        }
        case ir::ExprKind::Unary: {
            if (e.op == "-") {
                genExpr(*e.rhs);
                emit("subq", "t0,zero,t0");
                narrowT0To(e.rhs->type);
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                genExpr(*e.rhs);  // t0 = pointer value
                emit("bis", "t2,t0,zero");
                loadOperand("t2", e.type);
            } else if (e.op == "~") {
                genExpr(*e.rhs);
                emit("ornot", "t0,zero,t0");
                narrowT0To(e.rhs->type);
            } else {  // "&"
                genAddrOf(*e.rhs);
            }
            return;
        }
        case ir::ExprKind::Convert: {
            genConvert(e);
            return;
        }
        case ir::ExprKind::Call: {
            // Caller pushes right to left (arg 0 lands closest to the callee's own prologue
            // reservation, matching IR.h's ascending Param::frameOffset - see
            // genFunction()'s own comment), caller cleans up afterward. The callee's entry
            // address always goes through `t12`/`pv`, the real Alpha calling convention's own
            // "procedure value" register - `jsr`'s target is always a register, never an
            // immediate.
            int totalBytes = 0;
            for (size_t i = e.args.size(); i-- > 0;) {
                genExpr(*e.args[i]);
                if (isWide(e.args[i]->type)) {
                    emit("lda", "sp,sp,-8");
                    emit("stindq", "sp,t0,0");
                    totalBytes += 8;
                } else {
                    emit("lda", "sp,sp,-4");
                    emit("stindl", "sp,t0,0");
                    totalBytes += 4;
                }
            }
            loadAddress("t12", label(e.func));
            emit("jsr", "ra,(t12)");
            if (totalBytes > 0) emit("lda", cat("sp,sp,", totalBytes));
            return;
        }
        case ir::ExprKind::CallPtr: {
            genExpr(*e.lhs);  // t0 = the callee's own entry address
            emit("bis", "t12,t0,zero");
            emit("jsr", "ra,(t12)");
            return;
        }
        case ir::ExprKind::Binary: {
            genBinary(e);
            return;
        }
        case ir::ExprKind::Intrinsic: {
            // No exception/interrupt setup on this bare-metal target; no-op.
            (void)e;
            return;
        }
        case ir::ExprKind::Func:
            err(e.line, "internal: Func reached genExpr - should only ever appear as '&'s operand");
    }
}

// t2 ends up holding the computed address. arraySize > 0 means a compile-time-known array
// base (Global label or Local `fp`-relative); otherwise `slot` is a pointer variable, so the
// base is read from its own stored (pointer-width) value first.
void AlphaCodegen::genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
    genExpr(idxExpr);  // t0 = index
    int shift = elemShift(slot->type);
    if (shift > 0) {
        emit("sll", cat("t0,t0,", shift));
    } else if (shift < 0) {
        // Non-power-of-two element width (a struct's): scale by a real multiply instead.
        loadImm32("t3", widthOfType(slot->type));
        emit("mulq", "t0,t3,t0");
    }
    if (slot->arraySize > 0) {
        loadSlotAddr("t2", slot);
        emit("addq", "t2,t2,t0");
    } else {
        loadSlotAddr("t1", slot);
        emit("ldindl", "t1,t1,0");  // t1 = the pointer variable's own stored value
        emit("addq", "t2,t1,t0");
    }
}

void AlphaCodegen::genIndex(const ir::Expr &e) {
    genIndexAddr(e.slot, *e.lhs);
    loadOperand("t2", e.slot->type);
}

void AlphaCodegen::genLocalArrayInit(const ir::Slot *slot) {
    int w = widthOfType(slot->type);
    for (size_t i = 0; i < slot->arrayInit.size(); i++) {
        const ir::ArrayInitElem &elem = slot->arrayInit[i];
        int offset = -ir::localLowOffset(*slot, typeWidths()) + w * static_cast<int>(i);
        if (elem.isSymbol) {
            loadAddress("t0", elem.slot ? label(elem.slot) : label(elem.func));
            emit("lda", cat("t2,fp,", offset));
            emit("stindl", "t2,t0,0");
            continue;
        }
        if (w == 8) {
            loadImm64("t0", static_cast<uint64_t>(elem.value));
        } else {
            loadImm32("t0", static_cast<int32_t>(static_cast<uint32_t>(elem.value)));
        }
        emit("lda", cat("t2,fp,", offset));
        if (w == 1) {
            emit("stindb", "t2,t0,0");
        } else if (w == 2) {
            emit("stindh", "t2,t0,0");
        } else if (w == 4) {
            emit("stindl", "t2,t0,0");
        } else {
            emit("stindq", "t2,t0,0");
        }
    }
}

void AlphaCodegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        loadSlotAddr("t2", operand.slot);
        emit("bis", "t0,t2,zero");
        return;
    }
    if (operand.kind == ir::ExprKind::Index) {
        genIndexAddr(operand.slot, *operand.lhs);
        emit("bis", "t0,t2,zero");
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        loadAddress("t2", label(operand.func));
        emit("bis", "t0,t2,zero");
        return;
    }
    err(operand.line, "internal: unhandled addressable-operand kind");
}

void AlphaCodegen::loadOperand(const std::string &addrReg, Type type) {
    int w = widthOfType(type);
    if (w == 1) {
        emit("ldindbu", cat(addrReg, ",t0,0"));
    } else if (w == 2) {
        emit("ldindhu", cat(addrReg, ",t0,0"));
    } else if (w == 4) {
        emit("ldindl", cat(addrReg, ",t0,0"));
    } else {
        emit("ldindq", cat(addrReg, ",t0,0"));
        return;
    }
    narrowT0To(type);
}

void AlphaCodegen::storeOperand(const std::string &addrReg, Type type) {
    int w = widthOfType(type);
    if (w == 1) {
        emit("stindb", cat(addrReg, ",t0,0"));
    } else if (w == 2) {
        emit("stindh", cat(addrReg, ",t0,0"));
    } else if (w == 4) {
        emit("stindl", cat(addrReg, ",t0,0"));
    } else {
        emit("stindq", cat(addrReg, ",t0,0"));
    }
}

void AlphaCodegen::narrowT0To(Type to) {
    int w = widthOfType(to);
    if (w == 8) return;
    int shiftAmt = 64 - 8 * w;
    emit("sll", cat("t0,t0,", shiftAmt));
    emit(to.isUnsigned ? "srl" : "sra", cat("t0,t0,", shiftAmt));
}

// Crosses the native-`int`/`long` boundary (sign/zero-extending t0's low 32 bits across the
// rest of the register, or just re-narrowing them), or re-narrows a same-side Convert. Unlike
// every 32-bit-register backend in this project, the "wide && wide" case here genuinely is a
// complete no-op (the bit pattern needs no reshaping at all, since there's only ever one
// register to begin with).
void AlphaCodegen::genConvert(const ir::Expr &e) {
    Type from = e.lhs->type;
    Type to = e.type;
    bool fromWide = isWide(from);
    bool toWide = isWide(to);
    genExpr(*e.lhs);
    if (fromWide && !toWide) {
        narrowT0To(to);
    } else if (!fromWide && toWide) {
        emit("sll", "t0,t0,32");
        emit(from.isUnsigned ? "srl" : "sra", "t0,t0,32");
    } else if (!fromWide && !toWide) {
        narrowT0To(to);
    }
    // fromWide && toWide: nothing to do.
}

void AlphaCodegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    Type operandType = e.lhs->type;
    bool wide = isWide(operandType);
    bool isUnsigned = operandType.isUnsigned;
    if (op == "+" || op == "-") {
        genExpr(*e.lhs);
        emit("lda", "sp,sp,-8");
        emit("stindq", "sp,t0,0");
        genExpr(*e.rhs);
        emit("ldindq", "sp,t1,0");
        emit("lda", "sp,sp,8");  // t1 = lhs, t0 = rhs
        emit(op == "+" ? "addq" : "subq", "t0,t1,t0");
        narrowT0To(operandType);
        return;
    }
    if (op == "*") {
        genMul(*e.lhs, *e.rhs);
        narrowT0To(operandType);
        return;
    }
    if (op == "/" || op == "%") {
        genDivMod(*e.lhs, *e.rhs, /*wantRemainder=*/op == "%", isUnsigned, wide ? 64 : 32);
        return;
    }
    if (op == "&" || op == "|" || op == "^") {
        const char *insn = op == "&" ? "and" : op == "|" ? "bis" : "xor";
        genExpr(*e.lhs);
        emit("lda", "sp,sp,-8");
        emit("stindq", "sp,t0,0");
        genExpr(*e.rhs);
        emit("ldindq", "sp,t1,0");
        emit("lda", "sp,sp,8");
        emit(insn, "t0,t1,t0");
        narrowT0To(operandType);
        return;
    }
    if (op == "<<" || op == ">>") {
        genExpr(*e.lhs);
        emit("lda", "sp,sp,-8");
        emit("stindq", "sp,t0,0");
        genExpr(*e.rhs);  // t0 = shift count
        emit("bis", "t1,t0,zero");
        emit("ldindq", "sp,t0,0");
        emit("lda", "sp,sp,8");  // t0 = value to shift
        const char *insn = op == "<<" ? "sll" : (isUnsigned ? "srl" : "sra");
        emit(insn, "t0,t0,t1");
        narrowT0To(operandType);
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

// A real hardware 64-bit multiply (`mulq`) directly, for every width: the low 64 bits of a
// product only ever depend on the low 64 bits of each operand (two's-complement
// multiplication is modular), so running it on the already sign/zero-extended `int` operands
// gives the exact right low-32-bit `int*int` result too - narrowT0To() (called by the caller,
// genBinary()) just re-trims it.
void AlphaCodegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    emit("lda", "sp,sp,-8");
    emit("stindq", "sp,t0,0");
    genExpr(rhs);
    emit("ldindq", "sp,t1,0");
    emit("lda", "sp,sp,8");  // t1 = lhs, t0 = rhs
    emit("mulq", "t0,t1,t0");
}

void AlphaCodegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned,
                             int bits) {
    genExpr(lhs);
    emit("lda", "sp,sp,-8");
    emit("stindq", "sp,t0,0");
    genExpr(rhs);
    emit("bis", "t1,t0,zero");  // t1 = divisor
    emit("ldindq", "sp,t0,0");
    emit("lda", "sp,sp,8");  // t0 = dividend
    genDivModCore(wantRemainder, isUnsigned, bits);
}

// Alpha has no integer divide instruction at all (a genuine architectural omission - see
// AlphaCodegen.h's own header comment), so `/`/`%` always run this bit-serial restoring
// division: `bits` iterations of "shift the next dividend bit into the remainder, then
// subtract the divisor out of it if that doesn't go negative". The remainder is only ever
// compared once it's known to still fit in `bits` bits; the one bit that could carry it past
// that (t9, captured from the remainder's own top bit just before each shift) forces the
// subtraction unconditionally instead of running a (64-bit-register-only) comparison that
// would otherwise silently miss an actual `bits`-wide overflow.
void AlphaCodegen::genDivModCore(bool wantRemainder, bool isUnsigned, int bits) {
    std::string LskipNegN = newLabel(), LskipNegD = newLabel();
    if (!isUnsigned) {
        emit("srl", cat("t6,t0,", (bits - 1)));
        emit("and", "t6,t6,1");  // t6 = dividend's sign
        emit("srl", cat("t7,t1,", (bits - 1)));
        emit("and", "t7,t7,1");  // t7 = divisor's sign
        emit("beq", cat("t6,", LskipNegN));
        emit("subq", "t0,zero,t0");
        if (bits == 32) { emit("sll", "t0,t0,32"); emit("srl", "t0,t0,32"); }
        emitLabel(LskipNegN);
        emit("beq", cat("t7,", LskipNegD));
        emit("subq", "t1,zero,t1");
        if (bits == 32) { emit("sll", "t1,t1,32"); emit("srl", "t1,t1,32"); }
        emitLabel(LskipNegD);
    }

    emit("bis", "t3,zero,zero");  // t3 = remainder
    emit("bis", "t4,zero,zero");  // t4 = quotient
    emit("lda", cat("t5,zero,", bits));

    std::string Lloop = newLabel(), Ldone = newLabel(), LsubYes = newLabel(), LsubNo = newLabel(), Lcont = newLabel();
    emitLabel(Lloop);
    emit("beq", cat("t5,", Ldone));
    emit("subq", "t5,t5,1");
    emit("srl", cat("t8,t0,", (bits - 1)));
    emit("and", "t8,t8,1");  // t8 = dividend's own top bit
    emit("sll", "t0,t0,1");
    emit("srl", cat("t9,t3,", (bits - 1)));
    emit("and", "t9,t9,1");  // t9 = remainder's own top bit (overflow)
    emit("sll", "t3,t3,1");
    emit("bis", "t3,t3,t8");  // remainder = (remainder<<1)|dividendBit
    emit("bne", cat("t9,", LsubYes));
    emit("cmpult", "at,t3,t1");
    emit("bne", cat("at,", LsubNo));
    emitLabel(LsubYes);
    emit("subq", "t3,t3,t1");
    emit("sll", "t4,t4,1");
    emit("bis", "t4,t4,1");
    emit("br", Lcont);
    emitLabel(LsubNo);
    emit("sll", "t4,t4,1");
    emitLabel(Lcont);
    emit("br", Lloop);
    emitLabel(Ldone);

    if (!isUnsigned) {
        std::string LnoNegQ = newLabel(), LnoNegR = newLabel();
        emit("xor", "at,t6,t7");
        emit("beq", cat("at,", LnoNegQ));
        emit("subq", "t4,zero,t4");
        emitLabel(LnoNegQ);
        emit("beq", cat("t6,", LnoNegR));  // remainder's sign follows the dividend's
        emit("subq", "t3,zero,t3");
        emitLabel(LnoNegR);
    }
    emit("bis", cat("t0,", (wantRemainder ? "t3" : "t4"), ",zero"));
    if (bits == 32) {
        emit("sll", "t0,t0,32");
        emit(isUnsigned ? "srl" : "sra", "t0,t0,32");
    }
}

// Entry: t0 = old, t1 = rhs (both at `type`'s width/signedness). `genCompoundThroughT2()`
// never touches t2 while this runs, so the address it holds survives.
void AlphaCodegen::genCompoundOp(const std::string &op, Type type) {
    bool isUnsigned = type.isUnsigned;
    if (op == "+") {
        emit("addq", "t0,t0,t1");
        return;
    }
    if (op == "-") {
        emit("subq", "t0,t0,t1");
        return;
    }
    if (op == "*") {
        emit("mulq", "t0,t0,t1");
        return;
    }
    if (op == "&") {
        emit("and", "t0,t0,t1");
        return;
    }
    if (op == "|") {
        emit("bis", "t0,t0,t1");
        return;
    }
    if (op == "^") {
        emit("xor", "t0,t0,t1");
        return;
    }
    if (op == "<<" || op == ">>") {
        const char *insn = op == "<<" ? "sll" : (isUnsigned ? "srl" : "sra");
        emit(insn, "t0,t0,t1");
        return;
    }
    // "/" or "%"
    genDivModCore(op == "%", isUnsigned, isWide(type) ? 64 : 32);
}

// t2 must already hold the resolved address on entry; it's stashed on the stack across
// evaluating e.rhs (which may clobber t0-t9) and never recomputed, so the target's address
// sub-expression runs exactly once.
void AlphaCodegen::genCompoundThroughT2(const ir::Expr &e) {
    emit("lda", "sp,sp,-8");
    emit("stindq", "sp,t2,0");  // stash address
    genExpr(*e.rhs);                                      // t0 = rhs
    emit("bis", "t1,t0,zero");  // t1 = rhs
    emit("ldindq", "sp,t2,0");
    emit("lda", "sp,sp,8");  // t2 = address (restored)
    loadOperand("t2", e.type);                                   // t0 = old
    if (e.yieldOld) {
        emit("lda", "sp,sp,-8");
        emit("stindq", "sp,t0,0");  // stash old
    }
    genCompoundOp(e.op, e.type);                                   // t0 = new (raw)
    narrowT0To(e.type);
    if (e.yieldOld) {
        emit("bis", "t1,t0,zero");  // t1 = new
        emit("ldindq", "sp,t0,0");
        emit("lda", "sp,sp,8");  // t0 = old
        emit("bis", "t3,t0,zero");  // t3 = old, preserved across the store
        emit("bis", "t0,t1,zero");  // t0 = new - storeOperand stores from t0
        storeOperand("t2", e.type);
        emit("bis", "t0,t3,zero");  // t0 = old (final result)
    } else {
        storeOperand("t2", e.type);  // t0 already = new
    }
}

// Real Alpha has no "not equal"/"greater"/"greater-or-equal" compare, only cmpeq/cmplt/cmple
// (and their unsigned cmpult/cmpule counterparts), each directly a 0/1 boolean (mirroring
// MIPS's SLT): the missing ones are derived by swapping operands (`>` is `<` reversed) or
// XOR-with-1 (`!=`/`<=`/`>=` are each the complement of one of the three real compares) -
// `xor`'s own literal form takes the `1` directly, no register needed to hold it first.
void AlphaCodegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs) {
    bool isUnsigned = lhs.type.isUnsigned;
    const char *ltInsn = isUnsigned ? "cmpult" : "cmplt";
    genExpr(lhs);
    emit("lda", "sp,sp,-8");
    emit("stindq", "sp,t0,0");
    genExpr(rhs);
    emit("ldindq", "sp,t1,0");
    emit("lda", "sp,sp,8");  // t1 = lhs, t0 = rhs
    if (op == "<") {
        emit(ltInsn, "t0,t1,t0");  // t0 = (lhs < rhs)
        return;
    }
    if (op == ">") {
        emit(ltInsn, "t0,t0,t1");  // t0 = (rhs < lhs) = (lhs > rhs)
        return;
    }
    if (op == "<=") {
        emit(ltInsn, "at,t0,t1");  // at = (rhs < lhs)
        emit("xor", "t0,at,1");  // t0 = !(rhs<lhs) = (lhs<=rhs)
        return;
    }
    if (op == ">=") {
        emit(ltInsn, "at,t1,t0");  // at = (lhs < rhs)
        emit("xor", "t0,at,1");  // t0 = !(lhs<rhs) = (lhs>=rhs)
        return;
    }
    if (op == "==") {
        emit("cmpeq", "t0,t1,t0");
        return;
    }
    if (op == "!=") {
        emit("cmpeq", "at,t1,t0");
        emit("xor", "t0,at,1");
        return;
    }
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

void AlphaCodegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    emit("beq", cat("t0,", Lfalse));
    genExpr(rhs);
    emit("beq", cat("t0,", Lfalse));
    emit("br", Ltrue);
    emitLabel(Lfalse);
    emit("lda", "t0,zero,0");
    emit("br", Ldone);
    emitLabel(Ltrue);
    emit("lda", "t0,zero,1");
    emitLabel(Ldone);
}

void AlphaCodegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    emit("bne", cat("t0,", Ltrue));
    genExpr(rhs);
    emit("bne", cat("t0,", Ltrue));
    emit("br", Lfalse);
    emitLabel(Ltrue);
    emit("lda", "t0,zero,1");
    emit("br", Ldone);
    emitLabel(Lfalse);
    emit("lda", "t0,zero,0");
    emitLabel(Ldone);
}

// `cmpeq t0,t0,zero` computes `(t0 == 0) ? 1 : 0` directly - logical not - in one instruction;
// `zero` is an ordinary operand register here (real register 31), not special syntax.
void AlphaCodegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    emit("cmpeq", "t0,t0,zero");
}

std::optional<std::string> AlphaCodegen::assembleInProcess(const std::string &asmText,
                                                             const std::string &format) const {
    if (format == "raw") return assembleAlpha(asmText);
    if (auto kind = fileFormatKindFromName(format)) {
        ObjectFile obj = assembleAlphaObject(asmText);
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

std::optional<std::string> AlphaCodegen::debugMap(const std::string &asmText, const std::string &format,
        const std::string &sourcePath) const {
    (void)format;
    return assembleAlphaDebugMap(asmText, sourcePath, debugSymbols_);
}

std::optional<std::string> AlphaCodegen::debugMapPreLink(const std::string &asmText,
                                                    const std::string &sourcePath) const {
    return assembleAlphaDebugMap(asmText, sourcePath, debugSymbols_, /*allowExternal=*/true);
}

// Only emits a marker when `debugMapEnabled_` (set via setDebugMapEnabled(),
// before generate() runs) and `line` actually differs from the last one
// emitted - see Pdp7Codegen.cpp's identical emitSourceLine(), which this
// mirrors exactly.
void AlphaCodegen::emitSourceLine(int line) {
    if (!debugMapEnabled_ || line == 0 || line == lastDebugLine_) return;
    emitRaw(cat("__line ", line));
    lastDebugLine_ = line;
}

}  // namespace rcc
