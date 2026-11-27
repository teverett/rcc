#include "Arm64Codegen.h"

#include <algorithm>
#include <tuple>
#include <cstdint>
#include <stdexcept>

#include "Arm64Assembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

// Rounds `n` up to the next multiple of 8, on top of IRBuilder's
// `frameSize`, which only guarantees a 4-byte-aligned result.
int alignUp8(int n) { return (n + 7) & ~7; }
int alignUp16(int n) { return (n + 15) & ~15; }

// Where the whole program is loaded and starts executing: qemu's
// `-kernel` raw-image convention for the `virt` machine's AArch64 boot
// loads it at RAM base + 0x80000 and sets the CPU's PC there directly.
// RAM base on `virt` is a fixed, documented 0x40000000.
constexpr uint64_t kBaseAddress = 0x40080000ull;

// Where the entry sequence points `sp` before calling main() -
// comfortably above the loaded program and its data.
constexpr uint64_t kInitialSP = 0x44000000ull;

// The "w" or "x" view of general register number `n`, per `wide` -
// threading this through genBinary()/genMul()/genDivMod()/genCompare()/
// genLogicalAnd()/Or()/Not() is all a `long long` operand needs; a real
// 64-bit register holds the whole value, no multi-word emulation needed.
std::string reg(int n, bool wide) { return (wide ? "x" : "w") + std::to_string(n); }
}  // namespace

void Arm64Codegen::loadImm32(const std::string &reg, uint32_t uv) {
    std::string w = "w" + reg.substr(1);  // the same physical register's own 32-bit ("w") view
    if (uv <= 0xFFFFu) {
        emit("movz", cat(w, ",", static_cast<int32_t>(uv)));
    } else {
        emit("movz", cat(w, ",", static_cast<int32_t>(uv & 0xFFFFu)));
        emit("movk", cat(w, ",", static_cast<int32_t>((uv >> 16) & 0xFFFFu)));
    }
}

void Arm64Codegen::loadImm64(const std::string &dstReg, uint64_t uv) {
    emit("movz", cat(dstReg, ",", static_cast<int32_t>(uv & 0xFFFFu)));
    uint16_t c1 = static_cast<uint16_t>((uv >> 16) & 0xFFFFu);
    uint16_t c2 = static_cast<uint16_t>((uv >> 32) & 0xFFFFu);
    uint16_t c3 = static_cast<uint16_t>((uv >> 48) & 0xFFFFu);
    bool any = (uv & 0xFFFFu) != 0;
    if (c1 != 0 || (any && (c2 != 0 || c3 != 0))) {
        emit("movk", cat(dstReg, ",", static_cast<int32_t>(c1)));
    }
    if (c2 != 0) emit("movk2", cat(dstReg, ",", static_cast<int32_t>(c2)));
    if (c3 != 0) emit("movk3", cat(dstReg, ",", static_cast<int32_t>(c3)));
}

void Arm64Codegen::narrowW0To(Type to) {
    int w = widthOfType(to);
    // Re-truncate+re-extend by shifting the unwanted high bits out and
    // back in (immediate `lsl`/`lsr`/`asr` - zero-fill for unsigned,
    // sign-fill for signed).
    if (w == 1) {
        emit("lsl", "w0,w0,24");
        emit((to.isUnsigned ? "lsr" : "asr"), "w0,w0,24");
    } else if (w == 2) {
        emit("lsl", "w0,w0,16");
        emit((to.isUnsigned ? "lsr" : "asr"), "w0,w0,16");
    }
}

// `byteReg` is `x4` for an 8-byte destination/source, `w4` otherwise -
// keeping the shift/OR/AND chain's widths consistent with
// `destReg`/`srcReg` matches each instruction's `sf` bit to the width
// the other operand needs.
void Arm64Codegen::loadIntUnaligned(const std::string &addrReg, const std::string &destReg, int nBytes) {
    std::string byteReg = nBytes > 4 ? "x4" : "w4";
    emit("ldindb", cat(addrReg, ",", byteReg));
    emit("mov", cat(destReg, ",", byteReg));
    for (int byteIdx = 1; byteIdx < nBytes; byteIdx++) {
        { emit("mov", cat("x3,", addrReg)); emit("add", cat("x3,x3,", byteIdx)); emit("ldindb", cat("x3,", byteReg)); }
        emit("lsl", cat(byteReg, ",", byteReg, ",", (byteIdx * 8)));
        emit("orr", cat(destReg, ",", destReg, ",", byteReg));
    }
}
void Arm64Codegen::storeIntUnaligned(const std::string &addrReg, const std::string &srcReg, int nBytes) {
    std::string byteReg = nBytes > 4 ? "x4" : "w4";
    emit("stindb", cat(addrReg, ",", srcReg));
    for (int byteIdx = 1; byteIdx < nBytes; byteIdx++) {
        { emit("mov", cat(byteReg, ",", srcReg)); emit("lsr", cat(byteReg, ",", byteReg, ",", (byteIdx * 8))); }
        { emit("mov", cat("x3,", addrReg)); emit("add", cat("x3,x3,", byteIdx)); emit("stindb", cat("x3,", byteReg)); }
    }
}

// `addrReg` holds the already-computed 64-bit effective address; loads
// into w0 (or x0, for a `long long`), sign- or zero-extending a
// narrower-than-native-int value per its signedness.
void Arm64Codegen::loadOperand(const std::string &addrReg, Type type, bool mayBeUnaligned) {
    int w = widthOfType(type);
    if (w == 1) {
        emit(cat((type.isUnsigned ? "ldindb " : "ldindsb "), addrReg, ",w0"));
    } else if (w == 2) {
        if (mayBeUnaligned) {
            loadIntUnaligned(addrReg, "w0", 2);
            if (!type.isUnsigned) { emit("lsl", "w0,w0,16"); emit("asr", "w0,w0,16"); }
        } else {
            emit(cat((type.isUnsigned ? "ldindh " : "ldindsh "), addrReg, ",w0"));
        }
    } else if (w == 4) {
        if (mayBeUnaligned) {
            loadIntUnaligned(addrReg, "w0", 4);
        } else {
            emit("ldind", cat(addrReg, ",w0"));
        }
    } else if (mayBeUnaligned) {
        loadIntUnaligned(addrReg, "x0", 8);
    } else {
        emit("ldind", cat(addrReg, ",x0"));
    }
}

void Arm64Codegen::storeOperand(const std::string &addrReg, Type type, bool mayBeUnaligned) {
    int w = widthOfType(type);
    if (w == 1) {
        emit("stindb", cat(addrReg, ",w0"));
    } else if (w == 2) {
        if (mayBeUnaligned) {
            storeIntUnaligned(addrReg, "w0", 2);
        } else {
            emit("stindh", cat(addrReg, ",w0"));
        }
    } else if (w == 4) {
        if (mayBeUnaligned) {
            storeIntUnaligned(addrReg, "w0", 4);
        } else {
            emit("stind", cat(addrReg, ",w0"));
        }
    } else if (mayBeUnaligned) {
        storeIntUnaligned(addrReg, "x0", 8);
    } else {
        emit("stind", cat(addrReg, ",x0"));
    }
}

// An implicit or explicit type conversion. Arithmetic/comparison
// operands narrower than `int` are already promoted by IRBuilder, so the
// only real work here is crossing the native-int/`long long` boundary,
// or re-truncating+re-extending w0 for a narrower target - see narrowW0To().
void Arm64Codegen::genConvert(const ir::Expr &e) {
    Type from = e.lhs->type;
    Type to = e.type;
    bool fromWide = isWide(from);
    bool toWide = isWide(to);
    genExpr(*e.lhs);
    if (fromWide && !toWide) {
        narrowW0To(to);  // the extra 32 bits in x0 are simply ignored from here on
    } else if (!fromWide && toWide) {
        if (from.isUnsigned) {
            // w0's value is already correct in x0's low 32 bits,
            // zero-extended by the architectural W-form guarantee.
        } else {
            // Sign-extend w0 into x0: shift its value up into the
            // x-register's top half, then shift back down arithmetically,
            // sign-filling from what is now bit63 (w0's original sign bit).
            { emit("lsl", "x0,x0,32"); emit("asr", "x0,x0,32"); }
        }
    } else if (!fromWide && !toWide) {
        narrowW0To(to);
    }
    // fromWide && toWide: the bit pattern is already correct, nothing
    // to do.
}

void Arm64Codegen::loadSlotAddr(const std::string &reg, const ir::Slot *slot) {
    if (slot->kind == ir::SlotKind::Global) {
        loadAddress(reg, label(slot));
        return;
    }
    emit("mov", cat(reg, ",x29"));
    if (darwinHosted_ && slot->kind == ir::SlotKind::Param) {
        emit("sub", cat(reg, ",", reg, ",", (currentParamBase_ + (slot->paramIndex + 1) * 8)));
        return;
    }
    // `frameOffset` is a byte offset from IRBuilder, which reserves a
    // flat two-native-`int`-widths (8 bytes) below a Param's
    // frameOffset=0 point. This prologue actually reserves 32 bytes
    // (two 16-byte-aligned pushes, since AArch64 wants `sp`
    // 16-byte-aligned) - `kParamExtra` makes up the difference so a
    // Param's address lands on the caller's pushed argument.
    constexpr int kParamExtra = 32 - 8;
    if (slot->kind == ir::SlotKind::Param) {
        emit("add", cat(reg, ",", reg, ",", (slot->frameOffset + kParamExtra)));
    } else {
        emit("sub", cat(reg, ",", reg, ",", ir::localLowOffset(*slot, typeWidths())));
    }
}

// A literal element is its own decimal text; a symbol element (a
// dispatch-table address-of-function/global) is the referenced Slot's
// or Function's label, written out bare - Arm64Assembler.cpp's `dw`
// evaluator resolves it the same way a label operand resolves for
// MOVZ/MOVK.
std::string Arm64Codegen::arrayElemText(const ir::ArrayInitElem &e) {
    if (!e.isSymbol) return std::to_string(static_cast<int32_t>(static_cast<uint32_t>(e.value)));
    return e.slot ? label(e.slot) : label(e.func);
}

void Arm64Codegen::declareArray(const std::string &lbl, const std::vector<ir::ArrayInitElem> &values, Type elemType) {
    if (values.empty()) return;
    int w = widthOfType(elemType);
    // No 64-bit data directive in this dialect - a `long long` array's
    // literal elements are emitted as two "dw"s (low word, then high) per
    // element; a symbol element is always a native-int-width address, so
    // it still gets one "dw".
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

void Arm64Codegen::loadAddress(const std::string &reg, const std::string &lbl) {
    if (darwinHosted_) {
        emit("adrp", cat(reg, ",", lbl));
        emit("addlo", cat(reg, ",", reg, ",", lbl));
        return;
    }
    emit("movz", cat(reg, ",", lbl));
    emit("movk", cat(reg, ",", lbl));
}

std::string Arm64Codegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    // Separate compilation means `prog` may not define `main` at all.
    // `objectMode` means no entry sequence is emitted here -
    // Arm64LinkerFactory.cpp synthesizes one once every object is merged
    // and `main`'s linked address is resolvable. An object file's base is
    // `org 0`; placing it is the linker's job.
    { emitRaw(cat("org ", (objectMode ? 0 : kBaseAddress))); emitRaw(""); }
    if (!objectMode) {
        emitLabel("_start");
        emit("movz", cat("x9,", static_cast<int32_t>(kInitialSP & 0xFFFFu)));
        emit("movk", cat("x9,", static_cast<int32_t>((kInitialSP >> 16) & 0xFFFFu)));
        emit("add", "sp,x9,0");
        emit("bl", label(mainFn));
        // The result sits in w0 the moment execution reaches `halt`.
        { emitLabel("halt"); emit("b", "halt"); emitRaw(""); }
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

    // Running byte offset within the data segment (8-byte aligned from
    // the start), used to insert alignment padding ahead of a global
    // wider than 1 byte: a misaligned `ldindh`/`stindh`/`ldind`/`stind`
    // is UNPREDICTABLE on this core and would clobber whatever previous
    // global sits there. A `long long` is accessed with one 8-byte
    // `ldind`/`stind`, so it needs 8-byte alignment.
    trailer_.push_back("    align 8");
    size_t dataOffset = 0;
    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        // An external global isn't defined in this file - it must never
        // get a local storage word, only be referenced by name, letting
        // the assembler's undefined-symbol handling defer it to a relocation.
        if (slot->isExternal) continue;
        if (debugMapEnabled_) debugSymbols_.push_back({label(slot.get()), /*isFunc=*/false});
        size_t gTrailerStart = trailer_.size();
        int align = std::min(widthOfType(slot->type), 8);
        size_t padding = (align - (dataOffset % align)) % align;
        if (padding > 0) {
            std::string fill = "0";
            for (size_t i = 1; i < padding; i++) fill += ", 0";
            trailer_.push_back("db " + fill);
            dataOffset += padding;
        }
        const std::string lbl = label(slot.get());
        if (slot->arraySize > 0) {
            declareArray(lbl, slot->arrayInit, slot->type);
            dataOffset += static_cast<size_t>(widthOfType(slot->type)) * static_cast<size_t>(slot->arraySize);
        } else if (slot->type.base == BaseType::Struct) {
            // No struct initializer exists in this subset, so this is
            // always zero-filled; reuses declareArray's own element-list
            // emission (one zero "db" per byte) rather than a new
            // directive, since this assembler dialect has no "reserve N
            // zero bytes" form of its own.
            declareArray(lbl, std::vector<ir::ArrayInitElem>(slot->type.structSize), Type{BaseType::Char, false});
            dataOffset += static_cast<size_t>(slot->type.structSize);
        } else {
            long v = slot->hasInit ? slot->initValue : 0;
            int w = widthOfType(slot->type);
            if (w == 8) {
                auto uv = static_cast<uint64_t>(v);
                trailer_.push_back(lbl + ": dw " + std::to_string(static_cast<uint32_t>(uv)) + ", " +
                                    std::to_string(static_cast<uint32_t>(uv >> 32)) + "  ; " + slot->sourceName);
            } else {
                const char *directive = w == 1 ? "db" : w == 2 ? "dh" : "dw";
                trailer_.push_back(lbl + ": " + std::string(directive) + " " +
                                    std::to_string(static_cast<int32_t>(static_cast<uint32_t>(v))) + "  ; " +
                                    slot->sourceName);
            }
            dataOffset += static_cast<size_t>(w);
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
    if (objectMode) out << "__rcc_macho_data_start:\n";
    for (const auto &line : trailer_) out << line << "\n";
    return out.str();
}

std::map<std::string, std::string> Arm64Codegen::perFileAsm() const { return perFileAsm_; }

// Every function saves/restores `x30` (link register) and `x29` (frame
// pointer) around its body, whether or not it calls anything else: `bl`
// sets `x30` unconditionally, so a non-leaf function's call overwrites
// it. Each save/restore only moves the register's low 32 bits through
// `stind`/`ldind`, safe only because every address in this backend's
// universe fits in 32 bits, so the reload's automatic zero-extension
// reconstructs the exact original 64-bit value.
//
// The prologue pushes the caller's `x30` then its `x29`, each in its own
// 16-byte-aligned block, then points the new `x29` at the saved-old-`x29`
// word - so the caller's pushed arguments sit 32 bytes above the new
// `x29`, and this call's Locals are reserved immediately below it by
// `sub sp,sp,frameSize*4` (see loadSlotAddr() for the full address
// arithmetic). The epilogue (here, and inline in the `Return` case,
// since an early return bypasses this fall-through epilogue) reverses
// all of this in the opposite order.
void Arm64Codegen::genFunction(const ir::FunctionBody &fn) {
    lastDebugLine_ = 0;
    if (debugMapEnabled_) debugSymbols_.push_back({label(fn.func), /*isFunc=*/true});
    emitLabel(label(fn.func));
    if (darwinHosted_) {
        { emit("sub", "sp,sp,16"); emit("stind", "sp,x29"); }
        { emit("add", "x9,sp,8"); emit("stind", "x9,x30"); }
        emit("add", "x29,sp,0");
        currentParamBase_ = alignUp8(fn.func->frameSize);
        currentFrameSize_ = alignUp16(currentParamBase_ + static_cast<int>(fn.func->params.size()) * 8);
        if (currentFrameSize_ > 0) emit("sub", cat("sp,sp,", currentFrameSize_));
        for (size_t i = 0; i < fn.func->params.size(); i++) {
            const ir::Slot *param = fn.func->params[i];
            std::string valueReg = (isWide(param->type) ? "x" : "w") + std::to_string(i < 8 ? i : 0);
            if (i >= 8) {
                emit("add", cat("x9,x29,", (16 + (i - 8) * 8)));
                emit("ldind", cat("x9,", valueReg));
            }
            int offset = currentParamBase_ + static_cast<int>(i + 1) * 8;
            emit("sub", cat("x10,x29,", offset));
            emit("stind", cat("x10,", valueReg));
        }
        genStmt(*fn.body);
        if (currentFrameSize_ > 0) emit("add", cat("sp,sp,", currentFrameSize_));
        { emit("ldind", "sp,x29"); emit("add", "x9,sp,8"); emit("ldind", "x9,x30"); emit("add", "sp,sp,16"); }
        { emit("ret"); emitRaw(""); }
        return;
    }
    { emit("sub", "sp,sp,16"); emit("stind", "sp,w30"); }
    { emit("sub", "sp,sp,16"); emit("stind", "sp,w29"); }
    // `add x29,sp,0`, not `mov x29,sp`: this dialect's `mov` alias is
    // `orr rd,<zr>,rn`, and a real ORR's register fields mean the zero
    // register for operand 31, never `sp` - `sp` is only addressable
    // through an instruction whose encoding gives register 31 that
    // meaning, e.g. `add`/`sub` immediate.
    emit("add", "x29,sp,0");
    // IRBuilder's `frameSize` is only rounded up to the native `int`'s
    // width, fine for a Local's frameOffset, but `sp` itself must stay
    // 8-byte aligned at all times on this target (every access runs
    // against Device memory, which enforces natural alignment
    // unconditionally). `x29` is always 16-byte aligned already, so
    // rounding this `sub`/`add sp` amount up to a multiple of 8 keeps
    // `sp` 8-aligned for the entire body.
    int spFrameSize = alignUp8(fn.func->frameSize);
    if (spFrameSize > 0) emit("sub", cat("sp,sp,", spFrameSize));
    currentFrameSize_ = spFrameSize;
    genStmt(*fn.body);
    if (spFrameSize > 0) emit("add", cat("sp,sp,", spFrameSize));
    { emit("ldind", "sp,w29"); emit("add", "sp,sp,16"); }
    { emit("ldind", "sp,w30"); emit("add", "sp,sp,16"); }
    { emit("ret"); emitRaw(""); }
}

void Arm64Codegen::genStmt(const ir::Stmt &s) {
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
            emit("cbz", cat(reg(0, isWide(s.cond->type)), ",", (s.elseStmt ? Lelse : Ldone)));
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
            emit("cbz", cat(reg(0, isWide(s.cond->type)), ",", Lend));
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
            // An early return bypasses genFunction()'s fall-through
            // epilogue, so it needs this same teardown inline, in the
            // opposite order from the prologue.
            if (currentFrameSize_ > 0) emit("add", cat("sp,sp,", currentFrameSize_));
            if (darwinHosted_) {
                { emit("ldind", "sp,x29"); emit("add", "x9,sp,8"); emit("ldind", "x9,x30"); emit("add", "sp,sp,16"); }
            } else {
                { emit("ldind", "sp,w29"); emit("add", "sp,sp,16"); }
                { emit("ldind", "sp,w30"); emit("add", "sp,sp,16"); }
            }
            emit("ret");
            return;
        }
        case ir::StmtKind::LocalDecl: {
            if (s.declSlot->arraySize > 0) {
                genLocalArrayInit(s.declSlot);
            } else if (s.initOrReturnOrExpr) {
                genExpr(*s.initOrReturnOrExpr);
                loadSlotAddr("x2", s.declSlot);
                storeOperand("x2", s.declSlot->type);
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
            emitRaw(s.asmText);
            if (!s.asmText.empty() && s.asmText.back() != '\n') emitRaw("");
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

void Arm64Codegen::genExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit: {
            if (isWide(e.type)) {
                loadImm64("x0", static_cast<uint64_t>(e.intValue));
            } else {
                loadImm32("x0", static_cast<uint32_t>(e.intValue));
            }
            return;
        }
        case ir::ExprKind::Var: {
            loadSlotAddr("x2", e.slot);
            loadOperand("x2", e.slot->type);
            return;
        }
        case ir::ExprKind::Index: {
            genIndex(e);
            return;
        }
        case ir::ExprKind::Assign: {
            if (e.lhs) {  // slot[idx] = rhs
                genIndexAddr(e.slot, *e.lhs);  // x2 = target address
                { emit("sub", "sp,sp,16"); emit("stind", "sp,x2"); }
                genExpr(*e.rhs);
                { emit("ldind", "sp,x2"); emit("add", "sp,sp,16"); }
                storeOperand("x2", e.slot->type, /*mayBeUnaligned=*/e.slot->arraySize == 0);
                return;
            }
            genExpr(*e.rhs);
            loadSlotAddr("x2", e.slot);
            storeOperand("x2", e.slot->type);
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            if (e.lhs) {  // slot[idx] op= rhs (including slot[idx]++/--)
                genIndexAddr(e.slot, *e.lhs);  // x2 = target address
                genCompoundThroughX2(e, /*mayBeUnaligned=*/e.slot->arraySize == 0);
                return;
            }
            // A plain scalar target still needs its address computed
            // (A64 has no absolute-addressing memory operand), so it
            // goes through the same held-address tail as the indexed form.
            loadSlotAddr("x2", e.slot);
            genCompoundThroughX2(e);
            return;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            genExpr(*e.lhs);  // w0 = pointer value
            emit("mov", "x2,x0");  // x2 = address
            genCompoundThroughX2(e, /*mayBeUnaligned=*/true);
            return;
        }
        case ir::ExprKind::Ternary: {
            genExpr(*e.lhs);
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            emit("cbz", cat(reg(0, isWide(e.lhs->type)), ",", Lelse));
            genExpr(*e.rhs);
            emit("b", Ldone);
            emitLabel(Lelse);
            genExpr(*e.elseExpr);
            emitLabel(Ldone);
            return;
        }
        case ir::ExprKind::DerefAssign: {
            // Stores at e.type's width/signedness - the pointee type of a
            // typed pointer, or native-int-width for untyped-int-as-address.
            genExpr(*e.lhs);  // w0 = pointer value
            { emit("sub", "sp,sp,16"); emit("stind", "sp,w0"); }
            genExpr(*e.rhs);  // w0, or x0 if wide
            { emit("ldind", "sp,w2"); emit("add", "sp,sp,16"); }
            storeOperand("x2", e.type, /*mayBeUnaligned=*/true);
            return;
        }
        case ir::ExprKind::Unary: {
            bool wide = isWide(e.rhs->type);
            if (e.op == "-") {
                genExpr(*e.rhs);
                emit("sub", cat(reg(0, wide), ",xzr,", reg(0, wide)));
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                // Loads at e.type's width/signedness - see DerefAssign above.
                genExpr(*e.rhs);  // w0 = pointer value
                emit("mov", "x2,x0");
                loadOperand("x2", e.type, /*mayBeUnaligned=*/true);
            } else if (e.op == "~") {
                genExpr(*e.rhs);
                emit("mvn", cat(reg(0, wide), ",", reg(0, wide)));
            } else {  // "&"
                genAddrOf(*e.rhs);
            }
            return;
        }
        case ir::ExprKind::Call: {
            if (darwinHosted_) {
                size_t registerArgs = std::min<size_t>(8, e.args.size());
                size_t stackArgs = e.args.size() > 8 ? (e.args.size() - 8) * 8 : 0;
                int callFrameSize = alignUp16(static_cast<int>(stackArgs + registerArgs * 8));
                if (callFrameSize > 0) emit("sub", cat("sp,sp,", callFrameSize));
                for (size_t i = e.args.size(); i-- > 0;) {
                    genExpr(*e.args[i]);
                    size_t offset = i < 8 ? stackArgs + i * 8 : (i - 8) * 8;
                    emit("add", cat("x2,sp,", offset));
                    emit("stind", cat("x2,", (isWide(e.args[i]->type) ? "x0" : "w0")));
                }
                for (size_t i = 0; i < registerArgs; i++) {
                    emit("add", cat("x9,sp,", (stackArgs + i * 8)));
                    emit("ldind", cat("x9,", (isWide(e.args[i]->type) ? "x" : "w"), i));
                }
                emit("bl", label(e.func));
                if (callFrameSize > 0) emit("add", cat("sp,sp,", callFrameSize));
                return;
            }
            // Pushes each argument right-to-left so arg 0 ends up closest
            // to the top of the pushed-argument space. The caller cleans
            // the pushed words up once `bl` returns. Every argument
            // reserves at least one native word, and a wide (`long long`)
            // argument pushes a full 8 bytes via `x0`.
            //
            // The pushed bytes (4 per plain arg, 8 per wide one) can add
            // up to a total that isn't itself a multiple of 8, but `sp`
            // must stay a multiple of 8 at all times. So any padding
            // needed is reserved first, before any argument is pushed:
            // that leaves arg 0 still the last thing pushed, landing
            // exactly `x29 + 32` bytes up regardless of padding - unlike
            // padding added after the arguments, which would shift every
            // one away from the fixed offset IRBuilder assigned it.
            int totalBytes = 0;
            for (const auto &arg : e.args) totalBytes += isWide(arg->type) ? 8 : 4;
            int padding = (8 - totalBytes % 8) % 8;
            if (padding > 0) emit("sub", cat("sp,sp,", padding));
            for (size_t i = e.args.size(); i-- > 0;) {
                genExpr(*e.args[i]);
                if (isWide(e.args[i]->type)) {
                    { emit("sub", "sp,sp,8"); emit("stind", "sp,x0"); }
                } else {
                    { emit("sub", "sp,sp,4"); emit("stind", "sp,w0"); }
                }
            }
            emit("bl", label(e.func));
            if (totalBytes + padding > 0) emit("add", cat("sp,sp,", (totalBytes + padding)));
            return;
        }
        case ir::ExprKind::CallPtr: {
            // e.args is always empty here - IRBuilder.cpp rejects any
            // argument on this kind, since the callee isn't known at
            // compile time. `blr x2` is a near call through whatever
            // address x2 holds, setting `x30` like `bl` does.
            genExpr(*e.lhs);  // w0 = the callee's entry address
            emit("mov", "x2,x0");
            emit("blr", "x2");
            return;
        }
        case ir::ExprKind::Binary: {
            genBinary(e);
            return;
        }
        case ir::ExprKind::Intrinsic: {
            // No portable interrupt enable/disable on this bare-metal
            // core with no exception/interrupt setup - treated as a no-op.
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

// `x2` holds the computed address. `arraySize > 0` means the base is the
// array's own first-word address (loadSlotAddr() dispatches Global vs.
// Local); otherwise `slot` is a plain pointer variable, so the base comes
// from reading that variable's stored value first.
//
// Both a Global and a Local array's element addresses increase with the
// index; loadSlotAddr() points `x2` at element 0, the lowest address.
//
// Multiplying the index by a fixed element width uses `mul` against a
// loaded constant rather than a shift, since this dialect has no
// dedicated shift-by-immediate mnemonic.
void Arm64Codegen::genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
    genExpr(idxExpr);  // w0 = index
    int shift = elemShift(slot->type);
    if (shift != 0) {
        // shift > 0: a power-of-two width, loaded as 1<<shift. shift < 0: a non-power-of-two
        // width (a struct's) - loaded directly, since this always goes through a real `mul`
        // regardless.
        int width = shift > 0 ? (1 << shift) : widthOfType(slot->type);
        { emit("movz", cat("w1,", width)); emit("mul", "w0,w0,w1"); }  // w0 *= the element type's own width
    }
    if (slot->arraySize > 0) {
        loadSlotAddr("x2", slot);
        // `x0`'s upper 32 bits are already zero (the `mul` above is a
        // 32-bit "W-form" op, an architectural guarantee), so `x0` is
        // already a valid 64-bit operand with no explicit extension.
        emit("add", "x2,x2,x0");
    } else {
        loadSlotAddr("x1", slot);
        emit("ldind", "x1,w1");  // w1 = the pointer variable's own stored value (x1 zero-extends too)
        emit("add", "x2,x1,x0");
    }
}

void Arm64Codegen::genIndex(const ir::Expr &e) {
    genIndexAddr(e.slot, *e.lhs);
    loadOperand("x2", e.slot->type, /*mayBeUnaligned=*/e.slot->arraySize == 0);
}

// Stores `slot`'s arrayInit values into its per-call frame slots, at
// runtime, every time this LocalDecl executes - unlike a Global array,
// a Local array's storage is a fresh `x29`-relative frame slot on every
// call, so its initial values are written freshly each time too.
void Arm64Codegen::genLocalArrayInit(const ir::Slot *slot) {
    int w = widthOfType(slot->type);
    for (size_t i = 0; i < slot->arrayInit.size(); i++) {
        const ir::ArrayInitElem &elem = slot->arrayInit[i];
        if (elem.isSymbol) {
            loadAddress("x0", elem.slot ? label(elem.slot) : label(elem.func));
        } else if (w == 8) {
            loadImm64("x0", static_cast<uint64_t>(elem.value));
        } else {
            loadImm32("x0", static_cast<uint32_t>(elem.value));
        }
        { emit("mov", "x2,x29"); emit("sub", cat("x2,x2,", (ir::localLowOffset(*slot, typeWidths()) - static_cast<int>(i) * w))); }
        storeOperand("x2", slot->type);
    }
}

void Arm64Codegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        loadSlotAddr("x2", operand.slot);
        emit("mov", "x0,x2");
        return;
    }
    if (operand.kind == ir::ExprKind::Index) {
        genIndexAddr(operand.slot, *operand.lhs);
        emit("mov", "x0,x2");
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        loadAddress("x2", label(operand.func));
        emit("mov", "x0,x2");
        return;
    }
    err(operand.line, "internal: unhandled addressable-operand kind");
}

// Entry: w0 = old, w1 = rhs (both at `type`'s width/signedness; never wide).
void Arm64Codegen::genCompoundOp(const std::string &op, Type type) {
    bool isUnsigned = type.isUnsigned;
    if (op == "+") { emit("add", "w0,w0,w1"); return; }
    if (op == "-") { emit("sub", "w0,w0,w1"); return; }
    if (op == "*") { emit("mul", "w0,w0,w1"); return; }
    if (op == "&") { emit("and", "w0,w0,w1"); return; }
    if (op == "|") { emit("orr", "w0,w0,w1"); return; }
    if (op == "^") { emit("eor", "w0,w0,w1"); return; }
    if (op == "<<" || op == ">>") {
        const char *insn = op == "<<" ? "lslv" : (isUnsigned ? "lsrv" : "asrv");
        emit(insn, "w0,w0,w1");
        return;
    }
    // "/" or "%" - w3 is the quotient scratch the remainder form needs,
    // since old and rhs both have to stay live in w0/w1 for msub.
    const char *divInsn = isUnsigned ? "udiv" : "sdiv";
    if (op == "%") {
        emit(divInsn, "w3,w0,w1");
        emit("msub", "w0,w3,w1,w0");  // w0 = w0 - w3*w1 = old - quotient*rhs
    } else {
        emit(divInsn, "w0,w0,w1");
    }
}

// x2 must already hold the resolved address on entry. The address is
// stashed on the stack across evaluating e.rhs (which can reach
// arbitrary nested codegen using w0/w1/w2/w3) and never recomputed, so
// an indexed/pointer target's address sub-expression is evaluated exactly once.
void Arm64Codegen::genCompoundThroughX2(const ir::Expr &e, bool mayBeUnaligned) {
    { emit("sub", "sp,sp,16"); emit("stind", "sp,x2"); }  // stash address
    genExpr(*e.rhs);                                   // w0 = rhs
    emit("mov", "w1,w0");                          // w1 = rhs
    { emit("ldind", "sp,x2"); emit("add", "sp,sp,16"); }       // x2 = address (restored)
    loadOperand("x2", e.type, mayBeUnaligned);              // w0 = old
    if (e.yieldOld) { emit("sub", "sp,sp,16"); emit("stind", "sp,w0"); }  // stash old
    genCompoundOp(e.op, e.type);                              // w0 = new (raw)
    narrowW0To(e.type);
    if (e.yieldOld) {
        emit("mov", "w3,w0");                             // w3 = new
        { emit("ldind", "sp,w0"); emit("add", "sp,sp,16"); }           // w0 = old
        emit("mov", "w1,w0");                                // w1 = old, preserved across the store below
        emit("mov", "w0,w3");                                 // w0 = new - storeOperand always stores from w0
        storeOperand("x2", e.type, mayBeUnaligned);
        emit("mov", "w0,w1");                                  // w0 = old (final result)
    } else {
        storeOperand("x2", e.type, mayBeUnaligned);  // w0 already = new
    }
}

void Arm64Codegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    // Every arithmetic/bitwise/comparison operand here shares one common
    // type (IRBuilder's usual-arithmetic-conversion pass), except shift
    // (only the LHS's type matters) and &&/|| (width-independent). A real
    // 64-bit register holds a `long long` value directly - `reg(0,wide)`
    // etc. just pick "x" over "w".
    Type operandType = e.lhs->type;
    bool wide = isWide(operandType);
    bool isUnsigned = operandType.isUnsigned;
    if (op == "+" || op == "-") {
        genExpr(*e.lhs);
        { emit("sub", "sp,sp,16"); emit("stind", cat("sp,", reg(0, wide))); }
        genExpr(*e.rhs);
        { emit("ldind", cat("sp,", reg(1, wide))); emit("add", "sp,sp,16"); }  // r1 = lhs, r0 = rhs
        emit(cat((op == "+" ? "add " : "sub "), reg(0, wide), ",", reg(1, wide), ",", reg(0, wide)));
        return;
    }
    if (op == "*") {
        genMul(*e.lhs, *e.rhs);
        return;
    }
    if (op == "/" || op == "%") {
        genDivMod(*e.lhs, *e.rhs, /*wantRemainder=*/op == "%", isUnsigned);
        return;
    }
    if (op == "&" || op == "|" || op == "^") {
        const char *insn = (op == "&") ? "and" : (op == "|") ? "orr" : "eor";
        genExpr(*e.lhs);
        { emit("sub", "sp,sp,16"); emit("stind", cat("sp,", reg(0, wide))); }
        genExpr(*e.rhs);
        { emit("ldind", cat("sp,", reg(1, wide))); emit("add", "sp,sp,16"); }
        emit(insn, cat(reg(0, wide), ",", reg(1, wide), ",", reg(0, wide)));
        return;
    }
    if (op == "<<" || op == ">>") {
        genExpr(*e.lhs);
        { emit("sub", "sp,sp,16"); emit("stind", cat("sp,", reg(0, wide))); }
        genExpr(*e.rhs);  // w0 = shift count (always plain int)
        emit("mov", "w1,w0");
        { emit("ldind", cat("sp,", reg(0, wide))); emit("add", "sp,sp,16"); }  // r0 = value to shift
        const char *insn = op == "<<" ? "lslv" : (isUnsigned ? "lsrv" : "asrv");
        emit(insn, cat(reg(0, wide), ",", reg(0, wide), ",w1"));
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

// Real hardware multiply (`mul`, a full NxN->N product, N per `wide`).
void Arm64Codegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    bool wide = isWide(lhs.type);
    genExpr(lhs);
    { emit("sub", "sp,sp,16"); emit("stind", cat("sp,", reg(0, wide))); }
    genExpr(rhs);
    { emit("ldind", cat("sp,", reg(1, wide))); emit("add", "sp,sp,16"); }  // r1 = lhs, r0 = rhs
    emit("mul", cat(reg(0, wide), ",", reg(1, wide), ",", reg(0, wide)));
}

// Real hardware divide (`sdiv`/`udiv`) plus `msub` (`rd = ra - rn*rm`,
// one instruction) for the remainder.
void Arm64Codegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    bool wide = isWide(lhs.type);
    genExpr(lhs);
    { emit("sub", "sp,sp,16"); emit("stind", cat("sp,", reg(0, wide))); }
    genExpr(rhs);
    { emit("ldind", cat("sp,", reg(1, wide))); emit("add", "sp,sp,16"); }  // r1 = lhs (dividend), r0 = rhs (divisor)
    const char *divInsn = isUnsigned ? "udiv" : "sdiv";
    if (wantRemainder) {
        emit(divInsn, cat(reg(2, wide), ",", reg(1, wide), ",", reg(0, wide)));
        emit("msub", cat(reg(0, wide), ",", reg(2, wide), ",", reg(0, wide), ",", reg(1, wide)));
    } else {
        emit(divInsn, cat(reg(0, wide), ",", reg(1, wide), ",", reg(0, wide)));
    }
}

// `cmp` (`subs` with a discarded result) plus a dedicated `cset<cc>`
// per comparison operator - each directly produces the boolean this
// language needs (1 or 0), no branch needed. `isUnsigned` picks the
// HS/LO/HI/LS family instead of LT/GE/LE/GT for the ordered operators.
void Arm64Codegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs) {
    bool wide = isWide(lhs.type);
    bool isUnsigned = lhs.type.isUnsigned;
    genExpr(lhs);
    { emit("sub", "sp,sp,16"); emit("stind", cat("sp,", reg(0, wide))); }
    genExpr(rhs);
    { emit("ldind", cat("sp,", reg(1, wide))); emit("add", "sp,sp,16"); }  // r1 = lhs, r0 = rhs
    emit("cmp", cat(reg(1, wide), ",", reg(0, wide)));  // flags = lhs - rhs
    if (op == "<") {
        emit((isUnsigned ? "csetlo" : "csetlt"), "w0");
    } else if (op == ">") {
        emit((isUnsigned ? "csethi" : "csetgt"), "w0");
    } else if (op == "<=") {
        emit((isUnsigned ? "csetls" : "csetle"), "w0");
    } else if (op == ">=") {
        emit((isUnsigned ? "cseths" : "csetge"), "w0");
    } else if (op == "==") {
        emit("cseteq", "w0");
    } else if (op == "!=") {
        emit("csetne", "w0");
    } else {
        err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
    }
}

void Arm64Codegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    emit("cbz", cat(reg(0, isWide(lhs.type)), ",", Lfalse));
    genExpr(rhs);
    emit("cbz", cat(reg(0, isWide(rhs.type)), ",", Lfalse));
    emit("b", Ltrue);
    { emitLabel(Lfalse); emit("movz", "w0,0"); emit("b", Ldone); }
    { emitLabel(Ltrue); emit("movz", "w0,1"); }
    emitLabel(Ldone);
}

void Arm64Codegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    emit("cbnz", cat(reg(0, isWide(lhs.type)), ",", Ltrue));
    genExpr(rhs);
    emit("cbnz", cat(reg(0, isWide(rhs.type)), ",", Ltrue));
    emit("b", Lfalse);
    { emitLabel(Ltrue); emit("movz", "w0,1"); emit("b", Ldone); }
    { emitLabel(Lfalse); emit("movz", "w0,0"); }
    emitLabel(Ldone);
}

// `cmp w0,wzr` + `cseteq` is exactly "w0 = (w0 == 0)" - no branch needed.
void Arm64Codegen::genLogicalNot(const ir::Expr &rhs) {
    bool wide = isWide(rhs.type);
    genExpr(rhs);
    { emit("cmp", cat(reg(0, wide), ",", (wide ? "xzr" : "wzr"))); emit("cseteq", "w0"); }
}

std::optional<std::string> Arm64Codegen::assembleInProcess(const std::string &asmText,
                                                             const std::string &format) const {
    if (format == "raw") return assembleArm64(asmText);
    // `coff`/`elf`/`macho`/`aout`/`objraw`: assembleArm64Object() builds
    // one format-neutral ObjectFile, and the requested format just picks
    // that format's own FileFormatWriter to serialize it.
    if (auto kind = fileFormatKindFromName(format)) {
        ObjectFile obj = assembleArm64Object(asmText, darwinHosted_ && format == "macho");
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

std::optional<std::string> Arm64Codegen::debugMap(const std::string &asmText, const std::string &format,
        const std::string &sourcePath) const {
    (void)format;
    return assembleArm64DebugMap(asmText, sourcePath, debugSymbols_);
}

std::optional<std::string> Arm64Codegen::debugMapPreLink(const std::string &asmText,
                                                    const std::string &sourcePath) const {
    return assembleArm64DebugMap(asmText, sourcePath, debugSymbols_, /*allowExternal=*/true);
}

// Only emits a marker when `debugMapEnabled_` (set via setDebugMapEnabled(),
// before generate() runs) and `line` actually differs from the last one
// emitted - see Pdp7Codegen.cpp's identical emitSourceLine(), which this
// mirrors exactly.
void Arm64Codegen::emitSourceLine(int line) {
    if (!debugMapEnabled_ || line == 0 || line == lastDebugLine_) return;
    emitRaw(cat("__line ", line));
    lastDebugLine_ = line;
}

}  // namespace rcc
