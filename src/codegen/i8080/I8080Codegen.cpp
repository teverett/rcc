#include "I8080Codegen.h"

#include <algorithm>
#include <tuple>
#include <cstdint>
#include <stdexcept>

#include "I8080Assembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

// Where the whole program is loaded and starts executing - above the conventional
// RST-vector/boot area (0x0000-0x00FF), though nothing here uses interrupts or RST.
constexpr uint32_t kBaseAddress = 0x0100;
// Initial SP: the 8080's SP is undefined at reset (unlike the 6502's fixed $0100 stack page).
constexpr uint32_t kStackTop = 0xFF00;
}  // namespace

void I8080Codegen::declareTemp(const std::string &lbl) { trailer_.push_back(lbl + ": db 0, 0"); }

// A literal element's decimal text, or a symbol element's (a dispatch-table
// address-of-function/global) label text.
std::string I8080Codegen::arrayElemText(const ir::ArrayInitElem &e) {
    if (!e.isSymbol) return std::to_string(e.value);
    return e.slot ? label(e.slot) : label(e.func);
}

// `elemType` picks the data directive.
void I8080Codegen::declareArray(const std::string &lbl, const std::vector<ir::ArrayInitElem> &values, Type elemType) {
    if (values.empty()) return;
    int w = widthOfType(elemType);
    const char *directive = w == 1 ? "db" : "dw";
    std::ostringstream line;
    line << lbl << ": " << directive << " ";
    bool first = true;
    for (const auto &e : values) {
        int words = w <= 2 ? 1 : w / 2;
        if (words > 1 && !e.isSymbol) {
            auto uv = static_cast<unsigned long>(e.value);
            for (int i = 0; i < words; i++) {
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

void I8080Codegen::copy16(const std::string &dst, const std::string &src) {
    emit("lhld", src);
    emit("shld", dst);
}

void I8080Codegen::loadPair(const std::string &pairReg, const std::string &src) {
    emit("lhld", src);
    emit("push", "h");
    emit("pop", pairReg);
}

void I8080Codegen::storePair(const std::string &dst, const std::string &pairReg) {
    emit("push", pairReg);
    emit("pop", "h");
    emit("shld", dst);
}

void I8080Codegen::storeAcIndirect() {
    // `lda`/`sta` address `ac` directly by name, never touching HL, so the caller's address in
    // HL survives across both byte stores.
    emit("lda", "ac");
    emit("mov", "m,a");
    emit("inx", "h");
    emit("lda", "ac+1");
    emit("mov", "m,a");
}

// One in-place 1-bit rotate of the 16-bit memory cell `lbl`. `ana a` clears carry without
// disturbing A (the 8080 has no explicit clear-carry instruction), giving a known 0 carry-in to
// shift into the vacated bit.
void I8080Codegen::shiftOneBit(const std::string &lbl, bool isLeft) {
    emit("lhld", lbl);
    emit("ana", "a");
    if (isLeft) {
        emit("mov", "a,l");
        emit("ral");
        emit("mov", "l,a");
        emit("mov", "a,h");
        emit("ral");
        emit("mov", "h,a");
    } else {
        // Logical (unsigned) shift.
        emit("mov", "a,h");
        emit("rar");
        emit("mov", "h,a");
        emit("mov", "a,l");
        emit("rar");
        emit("mov", "l,a");
    }
    emit("shld", lbl);
}

int I8080Codegen::widthOfType(Type t) const { return widthOf(t, typeWidths()); }
bool I8080Codegen::isWide(Type t) const { return widthOfType(t) > 2; }
int I8080Codegen::wordCountOf(Type t) const { return widthOfType(t) / 2; }

// HL must hold the operand's address. Sign-extension idiom: `ral` rotates L's bit 7 into carry,
// then `sbb a` (A-A-CY) yields 0x00 or 0xFF - the sign-extension byte, in three instructions
// with no branch. An unsigned `char` just zero-fills H instead.
void I8080Codegen::loadOperand(Type type) {
    if (widthOfType(type) == 1) {
        emit("mov", "a,m");
        emit("mov", "l,a");
        if (type.isUnsigned) {
            emit("mvi", "h, 0");
        } else {
            emit("ral");
            emit("sbb", "a");
            emit("mov", "h,a");
        }
        emit("shld", "ac");
        return;
    }
    emit("mov", "e,m");
    emit("inx", "h");
    emit("mov", "d,m");
    storePair("ac", "d");
}

void I8080Codegen::storeOperand(Type type) {
    if (widthOfType(type) == 1) {
        emit("lda", "ac");
        emit("mov", "m,a");
        return;
    }
    storeAcIndirect();
}

void I8080Codegen::popWideInto(const char *role, int words) {
    for (int i = 0; i < words; i++) {
        emit("pop", "h");
        emit("shld", wideScratch(role, i));
    }
}

void I8080Codegen::pushWideFrom(const char *role, int words) {
    for (int i = words - 1; i >= 0; i--) {
        emit("lhld", wideScratch(role, i));
        emit("push", "h");
    }
}

void I8080Codegen::genWideLoad(const ir::Slot *slot, int words) {
    for (int i = words - 1; i >= 0; i--) {
        if (slot->kind == ir::SlotKind::Global) {
            emit("lxi", "h, " + label(slot) + (i ? ("+" + std::to_string(2 * i)) : ""));
        } else {
            genFrameAddr(slot, 2 * i);
        }
        emit("mov", "e,m");
        emit("inx", "h");
        emit("mov", "d,m");
        emit("push", "d");
    }
}

void I8080Codegen::genWideLoadLiteral(long value, int words) {
    auto uv = static_cast<unsigned long>(value);
    for (int i = words - 1; i >= 0; i--) {
        uint16_t w = static_cast<uint16_t>((uv >> (16 * i)) & 0xFFFF);
        emit("lxi", "h, " + std::to_string(w));
        emit("push", "h");
    }
}

void I8080Codegen::genWideStore(const ir::Slot *slot, int words) {
    for (int i = 0; i < words; i++) {
        // Popped into BC, not DE - genFrameAddr() below uses DE as scratch for a Local/Param's
        // offset, which would clobber a value popped into DE before it's stored.
        emit("pop", "b");
        if (slot->kind == ir::SlotKind::Global) {
            emit("lxi", "h, " + label(slot) + (i ? ("+" + std::to_string(2 * i)) : ""));
        } else {
            genFrameAddr(slot, 2 * i);
        }
        emit("mov", "m,c");
        emit("inx", "h");
        emit("mov", "m,b");
    }
}

// Multi-byte add/sub/negate/bitwise/compare all work byte-at-a-time, LSB first, chaining the
// carry/borrow flag through ADC/SBB (no 16-bit ADC/SBB on this chip). Every wide helper below
// first pops both operands into the fixed "wa"/"wb" scratch cells, then walks every byte of
// every word from least to most significant.
namespace {
// `role`'s `words`-word value, as individual byte labels, least significant byte first.
std::vector<std::string> wideByteLabelsLsbFirst(const char *role, int words) {
    std::vector<std::string> out;
    for (int i = 0; i < words; i++) {
        out.push_back(I8080Codegen::wideScratch(role, i));
        out.push_back(I8080Codegen::wideScratch(role, i) + "+1");
    }
    return out;
}
}  // namespace

void I8080Codegen::genWideAdd(const ir::Expr &lhs, const ir::Expr &rhs, bool isAdd, int words) {
    genExpr(lhs);
    genExpr(rhs);
    popWideInto("b", words);
    popWideInto("a", words);
    auto aBytes = wideByteLabelsLsbFirst("a", words);
    auto bBytes = wideByteLabelsLsbFirst("b", words);
    for (size_t k = 0; k < aBytes.size(); k++) {
        emit("lda", bBytes[k]);
        emit("mov", "b,a");
        emit("lda", aBytes[k]);
        if (isAdd) {
            emit(k == 0 ? "add" : "adc", "b");
        } else {
            emit(k == 0 ? "sub" : "sbb", "b");
        }
        emit("sta", aBytes[k]);
    }
    pushWideFrom("a", words);
}

void I8080Codegen::genWideNegate(int words) {
    popWideInto("a", words);
    auto aBytes = wideByteLabelsLsbFirst("a", words);
    for (const auto &b : aBytes) {
        emit("lda", b);
        emit("cma");
        emit("sta", b);
    }
    for (size_t k = 0; k < aBytes.size(); k++) {
        emit("lda", aBytes[k]);
        emit(k == 0 ? "adi" : "aci", k == 0 ? 1 : 0);
        emit("sta", aBytes[k]);
    }
    pushWideFrom("a", words);
}

void I8080Codegen::genWideBitwise(const ir::Expr &lhs, const ir::Expr &rhs, const std::string &op, int words) {
    genExpr(lhs);
    genExpr(rhs);
    popWideInto("b", words);
    popWideInto("a", words);
    const char *insn = op == "&" ? "ana" : op == "|" ? "ora" : "xra";
    auto aBytes = wideByteLabelsLsbFirst("a", words);
    auto bBytes = wideByteLabelsLsbFirst("b", words);
    for (size_t k = 0; k < aBytes.size(); k++) {
        emit("lda", bBytes[k]);
        emit("mov", "b,a");
        emit("lda", aBytes[k]);
        emit(insn, "b");
        emit("sta", aBytes[k]);
    }
    pushWideFrom("a", words);
}

void I8080Codegen::genWideEqualityCompare(const ir::Expr &lhs, const ir::Expr &rhs, bool wantEqual, int words) {
    genExpr(lhs);
    genExpr(rhs);
    popWideInto("b", words);
    popWideInto("a", words);
    auto aBytes = wideByteLabelsLsbFirst("a", words);
    auto bBytes = wideByteLabelsLsbFirst("b", words);
    emit("mvi", "c, 0");
    for (size_t k = 0; k < aBytes.size(); k++) {
        emit("lda", bBytes[k]);
        emit("mov", "d,a");
        emit("lda", aBytes[k]);
        emit("xra", "d");
        emit("ora", "c");
        emit("mov", "c,a");
    }
    emit("mov", "a,c");
    emit("ora", "a");
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit(wantEqual ? "jz" : "jnz", Ltrue);
    emit("lxi", "h, 0");
    emit("shld", "ac");
    emit("jmp", Ldone);
    emitLabel(Ltrue);
    emit("lxi", "h, 1");
    emit("shld", "ac");
    emitLabel(Ldone);
}

// Compares most-significant byte first - only that byte needs the signed/unsigned bias (see
// emitLessCompare()); once a byte differs, every byte after it compares a plain unsigned
// magnitude.
void I8080Codegen::genWideLessCompare(const ir::Expr &a, const ir::Expr &b, bool invert, bool isUnsigned, int words) {
    genExpr(a);
    genExpr(b);
    popWideInto("b", words);
    popWideInto("a", words);
    auto aBytes = wideByteLabelsLsbFirst("a", words);
    auto bBytes = wideByteLabelsLsbFirst("b", words);
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    for (size_t idx = aBytes.size(); idx-- > 0;) {
        bool top = idx == aBytes.size() - 1;
        emit("lda", aBytes[idx]);
        if (top && !isUnsigned) emit("xri", "0x80");
        emit("mov", "d,a");
        emit("lda", bBytes[idx]);
        if (top && !isUnsigned) emit("xri", "0x80");
        emit("mov", "e,a");
        emit("mov", "a,d");
        emit("sub", "e");
        if (idx == 0) {
            emit("jc", invert ? Lfalse : Ltrue);
            emit("jmp", invert ? Ltrue : Lfalse);
        } else {
            std::string Lcontinue = newLabel();
            emit("jz", Lcontinue);
            emit("jc", invert ? Lfalse : Ltrue);
            emit("jmp", invert ? Ltrue : Lfalse);
            emitLabel(Lcontinue);
        }
    }
    emitLabel(Lfalse);
    emit("lxi", "h, 0");
    emit("shld", "ac");
    emit("jmp", Ldone);
    emitLabel(Ltrue);
    emit("lxi", "h, 1");
    emit("shld", "ac");
    emitLabel(Ldone);
}

void I8080Codegen::genWideCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned,
                                     int words) {
    if (op == "==") return genWideEqualityCompare(lhs, rhs, /*wantEqual=*/true, words);
    if (op == "!=") return genWideEqualityCompare(lhs, rhs, /*wantEqual=*/false, words);
    if (op == "<") return genWideLessCompare(lhs, rhs, /*invert=*/false, isUnsigned, words);
    if (op == ">") return genWideLessCompare(rhs, lhs, /*invert=*/false, isUnsigned, words);
    if (op == "<=") return genWideLessCompare(rhs, lhs, /*invert=*/true, isUnsigned, words);
    if (op == ">=") return genWideLessCompare(lhs, rhs, /*invert=*/true, isUnsigned, words);
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

// One bit at a time, most significant byte first, chaining the carry flag from one byte to the
// next via `ral`/`rar`. A signed right shift needs the top byte's sign bit preserved
// (arithmetic, not logical): `mov b,a`/`ral`/`mov a,b`/`rar` computes a software SRA by first
// copying the sign bit into carry, then rotating it back in from the *original* byte while
// shifting right.
void I8080Codegen::genWideShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned, int words) {
    genExpr(lhs);
    popWideInto("a", words);
    genExpr(rhs);
    emit("lhld", "ac");
    emit("mov", "c,l");
    auto bytesMsbFirst = wideByteLabelsLsbFirst("a", words);
    std::reverse(bytesMsbFirst.begin(), bytesMsbFirst.end());
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("mov", "a,c");
    emit("ora", "a");
    emit("jz", Ldone);
    if (isLeft) {
        emit("ana", "a");  // clear carry - '<<' always shifts in a 0 bit
        for (size_t idx = bytesMsbFirst.size(); idx-- > 0;) {
            emit("lda", bytesMsbFirst[idx]);
            emit("ral");
            emit("sta", bytesMsbFirst[idx]);
        }
    } else {
        for (size_t idx = 0; idx < bytesMsbFirst.size(); idx++) {
            if (idx == 0 && !isUnsigned) {
                emit("lda", bytesMsbFirst[idx]);
                emit("mov", "b,a");
                emit("ral");
                emit("mov", "a,b");
                emit("rar");
            } else {
                if (idx == 0) emit("ana", "a");  // unsigned top byte: logical, zero-fill, clear carry first
                emit("lda", bytesMsbFirst[idx]);
                emit("rar");
            }
            emit("sta", bytesMsbFirst[idx]);
        }
    }
    emit("dcr", "c");
    emit("jmp", Lloop);
    emitLabel(Ldone);
    pushWideFrom("a", words);
}

// Unimplemented: no simulator in this project's test harness to verify a hand-written
// multi-word multiply/divide routine against, so failing loudly is safer than shipping an
// unverifiable one.
void I8080Codegen::genWideMul(const ir::Expr &lhs, const ir::Expr &rhs, int) {
    err(lhs.line, "multiplying two 'long'/'long long' values isn't supported on this target yet - "
                   "see I8080Codegen.cpp's own genWideMul() comment");
    (void)rhs;
}

void I8080Codegen::genWideDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool, int) {
    err(lhs.line, "dividing two 'long'/'long long' values isn't supported on this target yet - "
                   "see I8080Codegen.cpp's own genWideDivMod() comment");
    (void)rhs;
}

// An implicit or explicit conversion.
void I8080Codegen::genConvert(const ir::Expr &e) {
    Type from = e.lhs->type;
    Type to = e.type;
    bool fromWide = isWide(from);
    bool toWide = isWide(to);
    if (!fromWide && !toWide) {
        genExpr(*e.lhs);
        if (widthOfType(to) == 1) {
            emit("lhld", "ac");
            if (to.isUnsigned) {
                emit("mvi", "h, 0");
            } else {
                emit("mov", "a,l");
                emit("ral");
                emit("sbb", "a");
                emit("mov", "h,a");
            }
            emit("shld", "ac");
        }
        return;
    }
    if (fromWide && !toWide) {
        int words = wordCountOf(from);
        genExpr(*e.lhs);
        popWideInto("a", words);
        emit("lhld", wideScratch("a", 0));
        emit("shld", "ac");
        if (widthOfType(to) == 1) {
            emit("lhld", "ac");
            if (to.isUnsigned) {
                emit("mvi", "h, 0");
            } else {
                emit("mov", "a,l");
                emit("ral");
                emit("sbb", "a");
                emit("mov", "h,a");
            }
            emit("shld", "ac");
        }
        return;
    }
    if (!fromWide && toWide) {
        // Every word is written into the "wa" scratch cells and pushed via pushWideFrom() (LSW
        // on top) rather than pushing word 0 directly followed by the extension word(s), which
        // would leave an extension word on top instead of the least significant one.
        int toWords = wordCountOf(to);
        genExpr(*e.lhs);  // ac already holds the native-width value, sign/zero-extended to 16 bits
        emit("lhld", "ac");
        emit("shld", wideScratch("a", 0));
        for (int i = 1; i < toWords; i++) {
            if (from.isUnsigned) {
                emit("lxi", "h, 0");
            } else {
                emit("mov", "a,h");
                emit("ral");
                emit("sbb", "a");
                emit("mov", "h,a");
                emit("mov", "l,a");
            }
            emit("shld", wideScratch("a", i));
        }
        pushWideFrom("a", toWords);
        return;
    }
    // fromWide && toWide: pad/truncate word count, preserving sign.
    int fromWords = wordCountOf(from);
    int toWords = wordCountOf(to);
    genExpr(*e.lhs);
    popWideInto("a", fromWords);
    if (toWords > fromWords) {
        emit("lhld", wideScratch("a", fromWords - 1));
        if (from.isUnsigned) {
            emit("lxi", "h, 0");
        } else {
            emit("mov", "a,h");
            emit("ral");
            emit("sbb", "a");
            emit("mov", "h,a");
            emit("mov", "l,a");
        }
        for (int i = fromWords; i < toWords; i++) emit("shld", wideScratch("a", i));
    }
    pushWideFrom("a", toWords);
}

std::string I8080Codegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    // Real separate compilation means `prog` may not define `main` at all. `objectMode` means
    // no object file emits this entry sequence itself; I8080LinkerFactory.cpp synthesizes
    // exactly one once every object is merged and `main`'s linked address is resolvable. An
    // object file's base is `org 0` - placing it is the linker's job.
    emitRaw("org " + std::to_string(objectMode ? 0 : kBaseAddress));
    emitRaw("");
    if (!objectMode) {
        emitLabel("_start");
        emit("lxi", "sp, " + std::to_string(kStackTop));
        emit("call", label(mainFn));
        // The program's final result sits in `ac` once execution reaches `halt` below; this
        // extra `lhld` also leaves it in HL for a quick look via register dump.
        emit("lhld", "ac");
        emitLabel("halt");
        emit("jmp", "halt");
        emitRaw("");
    }

    // The fixed memory pseudo-registers. Every genExpr() call in the whole linked program
    // shares the same `ac`, so unlike every other symbol this backend emits, these five can't
    // be redeclared per-object: in whole-program mode they're declared once here as ordinary
    // temps; in object mode this file never declares them, so every reference is deferred as an
    // external relocation against the definition I8080LinkerFactory.cpp's synthesized entry
    // object provides.
    if (!objectMode) {
        declareTemp("ac");
        declareTemp("bc");
        declareTemp("cx");
        declareTemp("dx");
        declareTemp("ptr");
        // The current function's software frame pointer, shared the same way as the cells
        // above since a callee's prologue and its caller must agree on where it lives.
        declareTemp("fp");
        // Wide (`long`/`long long`) scratch cells.
        for (int i = 0; i < 4; i++) declareTemp(wideScratch("a", i));
        for (int i = 0; i < 4; i++) declareTemp(wideScratch("b", i));
        for (int i = 0; i < 4; i++) declareTemp(wideScratch("ret", i));
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
        // An external global isn't defined in this file - it must never get a local storage
        // word here, only be referenced by name, letting the assembler defer it to a relocation.
        if (slot->isExternal) continue;
        if (debugMapEnabled_) debugSymbols_.push_back({label(slot.get()), /*isFunc=*/false});
        size_t gTrailerStart = trailer_.size();
        const std::string lbl = label(slot.get());
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
            int w = widthOfType(slot->type);
            if (w == 1) {
                trailer_.push_back(lbl + ": db " + std::to_string(static_cast<int>(static_cast<uint8_t>(v))) +
                                    "  ; " + slot->sourceName);
            } else if (w == 2) {
                uint16_t v16 = static_cast<uint16_t>(v);
                trailer_.push_back(lbl + ": db " + std::to_string(v16 & 0xFF) + "  ; " + slot->sourceName);
                trailer_.push_back("db " + std::to_string((v16 >> 8) & 0xFF));
            } else {
                auto uv = static_cast<unsigned long>(v);
                std::ostringstream line;
                line << lbl << ": dw ";
                for (int i = 0; i < w / 2; i++) {
                    if (i > 0) line << ", ";
                    line << static_cast<uint16_t>((uv >> (16 * i)) & 0xFFFF);
                }
                line << "  ; " << slot->sourceName;
                trailer_.push_back(line.str());
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

// The whole generated text is attributed to whichever file defines `main`.
std::map<std::string, std::string> I8080Codegen::perFileAsm() const { return perFileAsm_; }

// Every activation gets its own real stack frame: `lhld fp`/`push h` saves the caller's frame
// pointer, `lxi h,0`/`dad sp` reads SP into HL (there's no direct "HL=SP" instruction) right
// after that push - exactly the address of the just-pushed word - and `shld fp` makes that this
// call's new frame base. `dad d` (with the locals' negative total byte size in DE) then `sphl`
// moves SP down below them in one step, reserving that memory before anything later in this
// function can land on top of it.
void I8080Codegen::genFunction(const ir::FunctionBody &fn) {
    lastDebugLine_ = 0;
    if (debugMapEnabled_) debugSymbols_.push_back({label(fn.func), /*isFunc=*/true});
    emitLabel(label(fn.func));
    emit("lhld", "fp");
    emit("push", "h");
    emit("lxi", "h, 0");
    emit("dad", "sp");
    emit("shld", "fp");
    // `fn.func->frameSize` alone can fall up to `intUnits-1` bytes short of the deepest byte a
    // Local slot needs below `fp`; padding by that much covers the worst case.
    long reserveBytes = static_cast<long>(fn.func->frameSize) + (typeWidths().intUnits - 1);
    if (reserveBytes > 0) {
        emit("lxi", "d, " + std::to_string(-reserveBytes));
        emit("dad", "d");
        emit("sphl");
    }
    genStmt(*fn.body);
    emitEpilogue();
    emitRaw("");
}

// Undoes genFunction()'s prologue: `lhld fp`/`sphl` snaps SP back to this call's frame base,
// dropping the locals' reserved region; `pop h`/`shld fp` restores the caller's `fp`; `ret`
// returns.
void I8080Codegen::emitEpilogue() {
    emit("lhld", "fp");
    emit("sphl");
    emit("pop", "h");
    emit("shld", "fp");
    emit("ret");
}

// A Local/Param slot's effective address, computed fresh every time rather than cached - this
// target has no addressing mode that could do it as part of a single instruction.
void I8080Codegen::genFrameAddr(const ir::Slot *slot, long extraBytes) {
    emit("lhld", "fp");
    emit("lxi", "d, " + std::to_string(frameByteOffset(slot, extraBytes)));
    emit("dad", "d");
}

void I8080Codegen::genStmt(const ir::Stmt &s) {
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
            emit("jz", s.elseStmt ? Lelse : Ldone);
            genStmt(*s.thenStmt);
            if (s.elseStmt) {
                emit("jmp", Ldone);
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
            emit("jz", Lend);
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
            if (s.initOrReturnOrExpr) {
                genExpr(*s.initOrReturnOrExpr);
                // A wide value sits on the real stack (LSW on top) after genExpr(); pop it into
                // the "wret" scratch cells since emitEpilogue()'s `sphl` is about to reset SP
                // back to `fp`, abandoning whatever's above it. genExpr()'s Call case reads it
                // back out of "wret" the same way.
                if (isWide(s.initOrReturnOrExpr->type)) {
                    int words = wordCountOf(s.initOrReturnOrExpr->type);
                    for (int i = 0; i < words; i++) {
                        emit("pop", "h");
                        emit("shld", wideScratch("ret", i));
                    }
                }
            }
            // An early return bypasses genFunction()'s fall-through epilogue, so it needs this
            // same frame teardown inline.
            emitEpilogue();
            return;
        }
        case ir::StmtKind::LocalDecl: {
            if (s.declSlot->arraySize > 0) {
                genLocalArrayInit(s.declSlot);
            } else if (s.initOrReturnOrExpr) {
                if (isWide(s.declSlot->type)) {
                    genExpr(*s.initOrReturnOrExpr);  // pushes `wordCountOf()` words
                    genWideStore(s.declSlot, wordCountOf(s.declSlot->type));
                    return;
                }
                genExpr(*s.initOrReturnOrExpr);
                genFrameAddr(s.declSlot);
                storeOperand(s.declSlot->type);
            }
            // No "else" case for a bare, uninitialized scalar: its frame word already exists
            // (genFunction()'s prologue reserved it), so its value is whatever was already in
            // that stack slot until something assigns it.
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

void I8080Codegen::genExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit: {
            if (isWide(e.type)) {
                genWideLoadLiteral(e.intValue, wordCountOf(e.type));
                return;
            }
            uint16_t v16 = static_cast<uint16_t>(e.intValue);
            emit("lxi", "h, " + std::to_string(v16));
            emit("shld", "ac");
            return;
        }
        case ir::ExprKind::Var: {
            if (isWide(e.slot->type)) {
                genWideLoad(e.slot, wordCountOf(e.slot->type));
                return;
            }
            if (e.slot->kind == ir::SlotKind::Global) {
                emit("lxi", "h, " + label(e.slot));
            } else {
                genFrameAddr(e.slot);
            }
            loadOperand(e.slot->type);
            return;
        }
        case ir::ExprKind::Index: {
            genIndex(e);
            return;
        }
        case ir::ExprKind::Assign: {
            if (e.lhs) {  // slot[idx] = rhs
                genIndexAddr(e.slot, *e.lhs);  // ptr = target address
                if (isWide(e.slot->type)) {
                    int words = wordCountOf(e.slot->type);
                    // `ptr` is a single shared scratch cell - evaluating `rhs` might itself
                    // index another array and clobber it, so the element address is saved on
                    // the real stack first; rhs's pushed words land on top of it and are popped
                    // into "wb" before the address underneath is restored.
                    emit("lhld", "ptr");
                    emit("push", "h");
                    genExpr(*e.rhs);
                    popWideInto("b", words);
                    emit("pop", "h");
                    emit("shld", "ptr");
                    genWideStoreAtPtr("b", words);
                    return;
                }
                emit("lhld", "ptr");
                emit("push", "h");
                genExpr(*e.rhs);
                emit("pop", "h");
                storeOperand(e.slot->type);
                return;
            }
            if (isWide(e.slot->type)) {
                genExpr(*e.rhs);  // pushes `wordCountOf()` words
                genWideStore(e.slot, wordCountOf(e.slot->type));
                return;
            }
            genExpr(*e.rhs);
            if (e.slot->kind == ir::SlotKind::Global) {
                emit("lxi", "h, " + label(e.slot));
            } else {
                genFrameAddr(e.slot);
            }
            storeOperand(e.slot->type);
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            if (e.lhs) {  // slot[idx] op= rhs (including slot[idx]++/--)
                genIndexAddr(e.slot, *e.lhs);  // ptr = address
                genCompoundThroughPtr(e);
                return;
            }
            genCompoundScalar(e);
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
            testTruthy(*e.lhs);
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            emit("jz", Lelse);
            genExpr(*e.rhs);
            emit("jmp", Ldone);
            emitLabel(Lelse);
            genExpr(*e.elseExpr);
            emitLabel(Ldone);
            return;
        }
        case ir::ExprKind::DerefAssign: {
            // Stores at e.type's width - the pointee type of a typed pointer, or native-word
            // width for the untyped-int-as-address style. A wide pointee mirrors the indexed
            // Assign's wide branch above, except `ptr` comes from evaluating e.lhs (the
            // pointer, always narrow) instead of genIndexAddr().
            genExpr(*e.lhs);  // ac = pointer value
            copy16("ptr", "ac");
            if (isWide(e.type)) {
                int words = wordCountOf(e.type);
                emit("lhld", "ptr");
                emit("push", "h");
                genExpr(*e.rhs);
                popWideInto("b", words);
                emit("pop", "h");
                emit("shld", "ptr");
                genWideStoreAtPtr("b", words);
                return;
            }
            emit("lhld", "ptr");
            emit("push", "h");
            genExpr(*e.rhs);
            emit("pop", "h");
            storeOperand(e.type);
            return;
        }
        case ir::ExprKind::Unary: {
            bool wide = isWide(e.rhs->type);
            if (e.op == "-") {
                genExpr(*e.rhs);
                if (wide) {
                    genWideNegate(wordCountOf(e.rhs->type));
                    return;
                }
                emit("lhld", "ac");
                emit("xra", "a");
                emit("sub", "l");
                emit("mov", "l,a");
                emit("mvi", "a, 0");
                emit("sbb", "h");
                emit("mov", "h,a");
                emit("shld", "ac");
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                // Loads at e.type's width - see DerefAssign above.
                genExpr(*e.rhs);  // ac = pointer value
                copy16("ptr", "ac");
                if (isWide(e.type)) {
                    genWideLoadAtPtr(wordCountOf(e.type));
                    return;
                }
                emit("lhld", "ptr");
                loadOperand(e.type);
            } else if (e.op == "~") {
                if (wide) {
                    genExpr(*e.rhs);
                    int words = wordCountOf(e.rhs->type);
                    popWideInto("a", words);
                    for (int i = 0; i < words; i++) {
                        emit("lhld", wideScratch("a", i));
                        emit("mov", "a,l");
                        emit("cma");
                        emit("mov", "l,a");
                        emit("mov", "a,h");
                        emit("cma");
                        emit("mov", "h,a");
                        emit("shld", wideScratch("a", i));
                    }
                    pushWideFrom("a", words);
                    return;
                }
                genExpr(*e.rhs);
                emit("lhld", "ac");
                emit("mov", "a,l");
                emit("cma");
                emit("mov", "l,a");
                emit("mov", "a,h");
                emit("cma");
                emit("mov", "h,a");
                emit("shld", "ac");
            } else {  // "&"
                genAddrOf(*e.rhs);
            }
            return;
        }
        case ir::ExprKind::Call: {
            // Each argument is pushed last-declared first, so argument 0 ends up closest to the
            // return address `call` is about to push (paramIndex 0 is `fp+4`). Each value is
            // computed into `ac` and transited through HL, since `PUSH` only takes a register
            // pair, never a memory operand.
            int totalWords = 0;
            for (size_t i = e.args.size(); i-- > 0;) {
                genExpr(*e.args[i]);
                if (isWide(e.args[i]->type)) {
                    totalWords += wordCountOf(e.args[i]->type);
                } else {
                    emit("lhld", "ac");
                    emit("push", "h");
                    totalWords += 1;
                }
            }
            emit("call", label(e.func));
            // The caller cleans its own pushed arguments off afterward (cdecl convention),
            // needing no cooperation from the callee.
            if (totalWords > 0) {
                emit("lxi", "h, " + std::to_string(totalWords * 2));
                emit("dad", "sp");
                emit("sphl");
            }
            if (isWide(e.type)) {
                // The callee left its wide return value in the "wret" scratch cells; push it
                // back as this expression's result, most significant word first so the least
                // significant ends up on top.
                int words = wordCountOf(e.type);
                for (int i = words - 1; i >= 0; i--) {
                    emit("lhld", wideScratch("ret", i));
                    emit("push", "h");
                }
            }
            return;
        }
        case ir::ExprKind::CallPtr: {
            // The 8080 has no indirect-CALL instruction, only `PCHL` (jump to the address in
            // HL), so a real CALL is emulated: push the return address, then `PCHL`. The
            // callee's `ret` pops that address back into PC, landing right after `pchl` below.
            genExpr(*e.lhs);  // ac = the callee's own entry address
            std::string Lret = newLabel();
            emit("lhld", "ac");  // HL = callee's address - pchl's implicit target
            emit("lxi", "d, " + Lret);
            emit("push", "d");
            emit("pchl");
            emitLabel(Lret);
            return;
        }
        case ir::ExprKind::Binary: {
            genBinary(e);
            return;
        }
        case ir::ExprKind::Intrinsic: {
            // Real 8080 interrupt enable/disable: EI/DI.
            emit(e.op == "ion" ? "ei" : "di");
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

// Leaves slot[idxExpr]'s effective address in the `ptr` memory cell. `idxExpr` is evaluated
// first and alone, since genExpr() may itself need `ptr`/`bc`/`cx`/`dx` for nested
// subexpressions.
//
// `slot->arraySize > 0` means `slot` is itself the array; otherwise `slot` is a pointer
// variable and the base comes from reading its stored value. A Global's base is a compile-
// time-known label, or for a pointer variable a direct read of it (loadPair()); a Local/Param's
// base is runtime-relative to `fp` (genFrameAddr()), and for a pointer variable means
// dereferencing that frame address.
//
// Both a Global and a Local array's element i sits at base+2*i; genFrameAddr() gives a Local
// array's element 0, its lowest address.
void I8080Codegen::genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
    genExpr(idxExpr);  // ac = index
    // ptr = index * widthOfType(slot->type): the width is a compile-time constant, so this is a
    // fully unrolled shift-and-add multiply (double `bc`, add into `ac` for each set bit of the
    // width) rather than a runtime loop - see Z80Codegen.cpp's own genIndexAddr() for the same
    // idea on that backend, including why this replaced a plain doubling count derived from
    // log2(width) that was only ever correct for a width of exactly 1/2/4/8 bytes. Stashed in
    // `ptr` immediately since loading the base below goes through HL too.
    emit("lhld", "ac");
    emit("shld", "bc");  // bc = index (current doubling value)
    emit("lxi", "h, 0");
    emit("shld", "ac");  // ac = running product, starts at 0
    for (int w = widthOfType(slot->type); w > 0; w >>= 1) {
        if (w & 1) {
            emit("lhld", "bc");
            emit("xchg");
            emit("lhld", "ac");
            emit("dad", "d");
            emit("shld", "ac");
        }
        if (w >> 1) {
            emit("lhld", "bc");
            emit("dad", "h");
            emit("shld", "bc");
        }
    }
    emit("lhld", "ac");
    emit("shld", "ptr");
    const bool isGlobal = slot->kind == ir::SlotKind::Global;
    if (isGlobal) {
        if (slot->arraySize > 0) {
            emit("lxi", "d, " + label(slot));
        } else {
            loadPair("d", label(slot));
        }
    } else {
        genFrameAddr(slot);  // HL = slot's own frame address
        if (slot->arraySize > 0) {
            emit("push", "h");
            emit("pop", "d");  // DE = that address (the array's own base)
        } else {
            // slot is a pointer variable living in this frame - DE needs its stored runtime
            // value, not its own address.
            emit("mov", "e,m");
            emit("inx", "h");
            emit("mov", "d,m");
        }
    }
    emit("lhld", "ptr");
    emit("dad", "d");
    emit("shld", "ptr");  // ptr = index*2 + base
}

// `ptr` already holds the element's address (genIndexAddr()). Reads its `words` 16-bit words,
// most significant first, pushing each onto the real stack (LSW on top), recomputing
// `ptr+2*i` fresh for each word via BC.
void I8080Codegen::genWideLoadAtPtr(int words) {
    for (int i = words - 1; i >= 0; i--) {
        emit("lhld", "ptr");
        if (i > 0) {
            emit("lxi", "b, " + std::to_string(2 * i));
            emit("dad", "b");
        }
        emit("mov", "e,m");
        emit("inx", "h");
        emit("mov", "d,m");
        emit("push", "d");
    }
}

// The mirror image for a write: `wideScratch(role,i)` already holds word `i` of the value to
// store, reached one byte at a time via `lda` without needing a register pair that
// `ptr+2*i`'s address computation is also using.
void I8080Codegen::genWideStoreAtPtr(const char *role, int words) {
    for (int i = 0; i < words; i++) {
        emit("lhld", "ptr");
        if (i > 0) {
            emit("lxi", "b, " + std::to_string(2 * i));
            emit("dad", "b");
        }
        emit("lda", wideScratch(role, i));
        emit("mov", "m,a");
        emit("inx", "h");
        emit("lda", wideScratch(role, i) + "+1");
        emit("mov", "m,a");
    }
}

void I8080Codegen::genIndex(const ir::Expr &e) {
    genIndexAddr(e.slot, *e.lhs);  // ptr = element address
    if (isWide(e.slot->type)) {
        genWideLoadAtPtr(wordCountOf(e.slot->type));
        return;
    }
    emit("lhld", "ptr");
    loadOperand(e.slot->type);
}

// Entry convention: HL = old value, DE = rhs value, both already at `type`'s width/signedness.
// `+` is a single `dad d`. `-`/`&`/`|`/`^` reuse genBinary()'s byte-wise instruction shapes with
// HL/DE's roles swapped. `*`/`/`/`%`/`<<`/`>>` seed `bc`/`dx` from HL/DE and run genMul()'s/
// genDivMod()'s/genShift()'s loop bodies unchanged.
void I8080Codegen::genCompoundOp(const std::string &op, Type type) {
    bool isUnsigned = type.isUnsigned;
    if (op == "+") {
        emit("dad", "d");
        emit("shld", "ac");
        return;
    }
    if (op == "-") {
        emit("mov", "a,l");
        emit("sub", "e");
        emit("mov", "l,a");
        emit("mov", "a,h");
        emit("sbb", "d");
        emit("mov", "h,a");
        emit("shld", "ac");
        return;
    }
    if (op == "&" || op == "|" || op == "^") {
        const char *insn = (op == "&") ? "ana" : (op == "|") ? "ora" : "xra";
        emit("mov", "a,l");
        emit(insn, "e");
        emit("mov", "l,a");
        emit("mov", "a,h");
        emit(insn, "d");
        emit("mov", "h,a");
        emit("shld", "ac");
        return;
    }
    if (op == "*") {
        emit("shld", "bc");  // bc = old (multiplicand) - HL already holds it
        storePair("dx", "d");       // dx = rhs (multiplier)
        emit("lxi", "h, 0");
        emit("shld", "ac");
        emit("mvi", "c, 16");
        std::string Lloop = newLabel(), Ldone = newLabel(), Lskipadd = newLabel();
        emitLabel(Lloop);
        emit("mov", "a,c");
        emit("ora", "a");
        emit("jz", Ldone);
        emit("lda", "dx");
        emit("ani", 1);
        emit("jz", Lskipadd);
        loadPair("d", "bc");
        emit("lhld", "ac");
        emit("dad", "d");
        emit("shld", "ac");
        emitLabel(Lskipadd);
        shiftOneBit("bc", /*isLeft=*/true);
        shiftOneBit("dx", /*isLeft=*/false);
        emit("dcr", "c");
        emit("jmp", Lloop);
        emitLabel(Ldone);
        return;
    }
    if (op == "<<" || op == ">>") {
        emit("shld", "bc");  // bc = old (value) - HL already holds it
        emit("mov", "c, e");  // c = shift count (low byte of rhs)
        std::string Lloop = newLabel(), Ldone = newLabel();
        emitLabel(Lloop);
        emit("mov", "a,c");
        emit("ora", "a");
        emit("jz", Ldone);
        if (op == "<<" || isUnsigned) {
            shiftOneBit("bc", /*isLeft=*/op == "<<");
        } else {
            emit("lhld", "bc");
            emit("mov", "a,h");
            emit("mov", "b,a");
            emit("ral");
            emit("mov", "a,b");
            emit("rar");
            emit("mov", "h,a");
            emit("mov", "a,l");
            emit("rar");
            emit("mov", "l,a");
            emit("shld", "bc");
        }
        emit("dcr", "c");
        emit("jmp", Lloop);
        emitLabel(Ldone);
        copy16("ac", "bc");
        return;
    }
    // "/" or "%"
    emit("shld", "dx");  // dx = old (dividend) - HL already holds it
    storePair("bc", "d");       // bc = rhs (divisor)
    emit("lxi", "h, 0");
    emit("shld", "ac");
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    loadPair("d", "dx");
    emit("lhld", "bc");
    emit("mov", "a,e");
    emit("sub", "l");
    emit("mov", "c,a");
    emit("mov", "a,d");
    emit("sbb", "h");
    emit("jc", Ldone);
    emit("mov", "d,a");
    emit("mov", "e,c");
    storePair("dx", "d");
    emit("lhld", "ac");
    emit("inx", "h");
    emit("shld", "ac");
    emit("jmp", Lloop);
    emitLabel(Ldone);
    if (op == "%") copy16("ac", "dx");
}

// The target address is computed once before loading `old`, then again fresh right before the
// final store; only `old` (and, for a postfix `++`/`--`, a second copy for the final result)
// needs the real stack to survive evaluating `rhs`.
void I8080Codegen::genCompoundScalar(const ir::Expr &e) {
    auto genAddr = [&] {
        if (e.slot->kind == ir::SlotKind::Global) {
            emit("lxi", "h, " + label(e.slot));
        } else {
            genFrameAddr(e.slot);
        }
    };
    genAddr();
    loadOperand(e.type);  // ac = old
    emit("lhld", "ac");
    emit("push", "h");  // stash old across rhs's own evaluation
    genExpr(*e.rhs);            // ac = rhs
    loadPair("d", "ac");          // de = rhs
    emit("pop", "h");        // hl = old
    if (e.yieldOld) {
        emit("push", "h");  // re-stash old for the final result
    }
    genCompoundOp(e.op, e.type);                // ac = new (raw)
    narrowAcTo(e.type);
    if (e.yieldOld) {
        emit("pop", "h");  // hl = old
        emit("shld", "cx");
    }
    genAddr();
    storeOperand(e.type);  // stores ac(=new); ac already = new if !yieldOld
    if (e.yieldOld) {
        emit("lhld", "cx");
        emit("shld", "ac");  // ac = old (final result)
    }
}

// `ptr` must already hold the resolved address on entry. The address is shuttled through the
// real hardware stack across evaluating `rhs`, since `rhs` may recurse into codegen that reuses
// `ptr` (e.g. a nested compound assignment). After `rhs` is evaluated nothing here recurses
// further, so `cx` is safe as a transient `yieldOld` stash.
void I8080Codegen::genCompoundThroughPtr(const ir::Expr &e) {
    emit("lhld", "ptr");
    loadOperand(e.type);  // ac = old
    emit("lhld", "ptr");
    emit("push", "h");  // stash address
    emit("lhld", "ac");
    emit("push", "h");  // stash old (ac is about to be overwritten by rhs)
    genExpr(*e.rhs);            // ac = rhs
    loadPair("d", "ac");          // de = rhs
    emit("pop", "h");        // hl = old
    if (e.yieldOld) {
        emit("push", "h");  // re-stash old for the final result
    }
    genCompoundOp(e.op, e.type);                // ac = new (raw)
    narrowAcTo(e.type);
    if (e.yieldOld) {
        emit("pop", "h");  // hl = old
        emit("shld", "cx");
        emit("pop", "h");   // hl = address
        storeOperand(e.type);      // stores ac(=new) through hl
        emit("lhld", "cx");
        emit("shld", "ac");    // ac = old (final result)
    } else {
        emit("pop", "h");  // hl = address
        storeOperand(e.type);      // ac already = new
    }
}

// A no-op unless `to` is a `char`.
void I8080Codegen::narrowAcTo(Type to) {
    if (widthOfType(to) != 1) return;
    emit("lhld", "ac");
    if (to.isUnsigned) {
        emit("mvi", "h, 0");
    } else {
        emit("mov", "a,l");
        emit("ral");
        emit("sbb", "a");
        emit("mov", "h,a");
    }
    emit("shld", "ac");
}

void I8080Codegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        if (operand.slot->kind == ir::SlotKind::Global) {
            emit("lxi", "h, " + label(operand.slot));
            emit("shld", "ac");
        } else {
            genFrameAddr(operand.slot);
            emit("shld", "ac");
        }
        return;
    }
    if (operand.kind == ir::ExprKind::Index) {
        genIndexAddr(operand.slot, *operand.lhs);
        copy16("ac", "ptr");
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        emit("lxi", "h, " + label(operand.func));
        emit("shld", "ac");
        return;
    }
    err(operand.line, "internal: unhandled addressable-operand kind");
}

// Stores arrayInit into `slot`'s frame slots; every call re-runs this since a Local's storage
// is a fresh frame region each activation, not pre-set like a Global's static trailer.
// arrayInit is always fully populated up to arraySize (zero-padded), leaving the whole array in
// a known state.
void I8080Codegen::genLocalArrayInit(const ir::Slot *slot) {
    int w = widthOfType(slot->type);
    for (size_t i = 0; i < slot->arrayInit.size(); i++) {
        const ir::ArrayInitElem &elem = slot->arrayInit[i];
        long base = static_cast<long>(i) * w;
        if (w == 1 && !elem.isSymbol) {
            genFrameAddr(slot, base);
            emit("mvi", "a, " + std::to_string(static_cast<int>(static_cast<uint8_t>(elem.value))));
            emit("mov", "m,a");
            continue;
        }
        if (elem.isSymbol || w <= 2) {
            emit("lxi", "h, " + arrayElemText(elem));
            emit("shld", "ac");
            genFrameAddr(slot, base);
            storeAcIndirect();
            continue;
        }
        // A wide (`long`/`long long`) element - 2 or 4 words, least significant first.
        auto uv = static_cast<unsigned long>(elem.value);
        int words = w / 2;
        for (int j = 0; j < words; j++) {
            uint16_t wv = static_cast<uint16_t>((uv >> (16 * j)) & 0xFFFF);
            emit("lxi", "h, " + std::to_string(wv));
            emit("shld", "ac");
            genFrameAddr(slot, base + 2 * j);
            storeAcIndirect();
        }
    }
}

// Every binary op saves its left operand across evaluating the right one on the real 8080
// hardware stack (`push h`/`pop d`), never in a named cell like `bc` - the right operand's
// genExpr() might itself be another binary op (or genMul()/genDivMod()/genShift(), which share
// the same `bc`/`cx`/`dx` cells), which would clobber a named "other operand" cell.
void I8080Codegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    // Every operand here shares one common type (usual arithmetic conversions), except shift
    // (only the LHS's type matters) and &&/|| (width-independent).
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
        emit("lhld", "ac");
        emit("push", "h");  // save lhs on the real stack across evaluating rhs
        genExpr(*e.rhs);                        // ac = rhs
        emit("pop", "d");                  // DE = lhs (popped only now, after rhs is fully evaluated)
        if (op == "+") {
            emit("lhld", "ac");
            emit("dad", "d");
            emit("shld", "ac");
        } else {
            emit("lhld", "ac");  // HL = rhs
            emit("mov", "a,e");
            emit("sub", "l");
            emit("mov", "e,a");
            emit("mov", "a,d");
            emit("sbb", "h");
            emit("mov", "d,a");
            storePair("ac", "d");
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
            genWideDivMod(*e.lhs, *e.rhs, /*wantRemainder=*/op == "%", words);
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
        emit("lhld", "ac");
        emit("push", "h");
        genExpr(*e.rhs);
        emit("pop", "d");    // DE = lhs
        emit("lhld", "ac");  // HL = rhs
        const char *insn = (op == "&") ? "ana" : (op == "|") ? "ora" : "xra";
        emit("mov", "a,l");
        emit(insn, "e");
        emit("mov", "l,a");
        emit("mov", "a,h");
        emit(insn, "d");
        emit("mov", "h,a");
        emit("shld", "ac");
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

// No variable-count shift instruction - loop `rhs` times, one bit at a time, via shiftOneBit().
// `isUnsigned` only matters for `>>`, using the same `mov b,a`/`ral`/`mov a,b`/`rar` software-
// SRA idiom for the top byte as genWideShift().
void I8080Codegen::genShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned) {
    genExpr(lhs);                          // ac = value to shift
    emit("lhld", "ac");
    emit("push", "h");  // saved on the real stack across evaluating the count
    genExpr(rhs);                           // ac = shift count
    emit("lhld", "ac");
    emit("mov", "c,l");  // c = shift count (always small; low byte is enough)
    emit("pop", "h");
    emit("shld", "bc");     // bc = value (restored only now, after the count is safely in c)
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("mov", "a,c");
    emit("ora", "a");
    emit("jz", Ldone);
    if (isLeft || isUnsigned) {
        shiftOneBit("bc", isLeft);
    } else {
        emit("lhld", "bc");
        emit("mov", "a,h");
        emit("mov", "b,a");
        emit("ral");
        emit("mov", "a,b");
        emit("rar");
        emit("mov", "h,a");
        emit("mov", "a,l");
        emit("rar");
        emit("mov", "l,a");
        emit("shld", "bc");
    }
    emit("dcr", "c");
    emit("jmp", Lloop);
    emitLabel(Ldone);
    copy16("ac", "bc");
}

// Textbook shift-and-add multiply (no hardware multiply): `bc` is the multiplicand (doubled
// each iteration), `dx` the multiplier (halved each iteration), `ac` the accumulating product.
// Only verified against non-negative operands.
void I8080Codegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);                          // ac = multiplicand
    emit("lhld", "ac");
    emit("push", "h");  // saved on the real stack across evaluating the multiplier
    genExpr(rhs);
    emit("pop", "h");
    emit("shld", "bc");  // bc = multiplicand (restored only now)
    copy16("dx", "ac");                     // dx = multiplier
    emit("lxi", "h, 0");
    emit("shld", "ac");  // ac = product = 0
    emit("mvi", "c, 16");               // loop counter, in the real register C
    std::string Lloop = newLabel(), Ldone = newLabel(), Lskipadd = newLabel();
    emitLabel(Lloop);
    emit("mov", "a,c");
    emit("ora", "a");
    emit("jz", Ldone);
    emit("lda", "dx");
    emit("ani", 1);
    emit("jz", Lskipadd);
    loadPair("d", "bc");
    emit("lhld", "ac");
    emit("dad", "d");
    emit("shld", "ac");
    emitLabel(Lskipadd);
    shiftOneBit("bc", /*isLeft=*/true);
    shiftOneBit("dx", /*isLeft=*/false);
    emit("dcr", "c");
    emit("jmp", Lloop);
    emitLabel(Ldone);
}

// Repeated subtraction (no hardware divide): `dx` is the running remainder (starts as the
// dividend), `bc` the fixed divisor, `ac` the quotient, counting how many times `bc` was
// subtracted out of `dx` before it would go negative. The core loop only handles non-negative
// operands, so a signed operand is divided via its absolute value: `ptr`'s low/high byte holds
// the dividend's/divisor's original sign while the unsigned core runs, then the quotient's sign
// (1 iff the two signs differed) and the remainder's sign (the dividend's) are restored after.
void I8080Codegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);                          // ac = dividend
    emit("lhld", "ac");
    emit("push", "h");  // saved on the real stack across evaluating the divisor
    genExpr(rhs);
    emit("pop", "h");
    emit("shld", "dx");  // dx = dividend (restored only now)
    copy16("bc", "ac");                     // bc = divisor
    auto isNeg = [&](const char *cell, const char *dst) {
        emit("lda", std::string(cell) + "+1");
        emit("ani", 128);
        std::string Lnonneg = newLabel(), Ldone = newLabel();
        emit("jz", Lnonneg);
        emit("mvi", "a, 1");
        emit("sta", dst);
        emit("jmp", Ldone);
        emitLabel(Lnonneg);
        emit("mvi", "a, 0");
        emit("sta", dst);
        emitLabel(Ldone);
    };
    auto negate16 = [&](const char *cell) {
        emit("lhld", cell);
        emit("xra", "a");
        emit("sub", "l");
        emit("mov", "l,a");
        emit("mvi", "a, 0");
        emit("sbb", "h");
        emit("mov", "h,a");
        emit("shld", cell);
    };
    auto negateIf = [&](const char *flagCell, const char *cell) {
        emit("lda", flagCell);
        emit("ora", "a");  // LDA alone leaves flags unaffected on the 8080 - this sets Z from A's own value
        std::string Lskip = newLabel();
        emit("jz", Lskip);
        negate16(cell);
        emitLabel(Lskip);
    };
    if (!isUnsigned) {
        isNeg("dx", "ptr");
        isNeg("bc", "ptr+1");
        negateIf("ptr", "dx");
        negateIf("ptr+1", "bc");
    }
    // Non-restoring division, with divisor/quotient pinned in registers for the loop's whole
    // run (HL = running remainder, DE = divisor, BC = quotient) - no memory access and no
    // register-pair shuffling through the stack at all inside the loop itself, unlike the
    // load-from-memory-every-iteration version this replaced. Each round speculatively does
    // HL -= DE; a final borrow means that went negative, so it's undone (`dad d`, real 8080
    // 16-bit add) and the loop stops; otherwise it's kept and the quotient (BC) counts it. This
    // keeps the common-case iteration to 9 instructions (`mov`/`sub`/`sbb`/`jnc`/`inx`/`jmp`)
    // instead of the previous 19 (three lhld/shld round trips through memory plus a push/pop
    // register-pair move) - division by a large dividend needs one iteration per unit of
    // quotient, so that difference is what used to run a real test past the test harness's own
    // step-count ceiling (see docs/known-bugs.md).
    emit("lhld", "bc");
    emit("mov", "d,h");
    emit("mov", "e,l");  // de = divisor (fixed for the loop)
    emit("lhld", "dx");                            // hl = running remainder
    emit("lxi", "b, 0");                           // bc = quotient = 0
    std::string Lloop = newLabel(), Lcontinue = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("mov", "a,l");
    emit("sub", "e");
    emit("mov", "l,a");
    emit("mov", "a,h");
    emit("sbb", "d");
    emit("mov", "h,a");
    emit("jnc", Lcontinue);
    emit("dad", "d");  // undo: hl was about to go negative, add the divisor back
    emit("jmp", Ldone);
    emitLabel(Lcontinue);
    emit("inx", "b");
    emit("jmp", Lloop);
    emitLabel(Ldone);
    emit("shld", "dx");                            // dx = remainder
    emit("push", "b");
    emit("pop", "h");
    emit("shld", "ac");     // ac = quotient
    if (!isUnsigned) {
        // Quotient is negative iff the two original signs differed; the remainder always takes
        // the dividend's original sign.
        emit("lda", "ptr");
        emit("mov", "b,a");
        emit("lda", "ptr+1");
        emit("xra", "b");
        std::string Lskip = newLabel();
        emit("jz", Lskip);
        negate16("ac");
        emitLabel(Lskip);
        negateIf("ptr", "dx");
    }
    if (wantRemainder) copy16("ac", "dx");
}

void I8080Codegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned) {
    if (op == "==") return emitEqualityCompare(lhs, rhs, /*wantEqual=*/true);
    if (op == "!=") return emitEqualityCompare(lhs, rhs, /*wantEqual=*/false);
    if (op == "<") return emitLessCompare(lhs, rhs, /*invert=*/false, isUnsigned);
    if (op == ">") return emitLessCompare(rhs, lhs, /*invert=*/false, isUnsigned);
    if (op == "<=") return emitLessCompare(rhs, lhs, /*invert=*/true, isUnsigned);
    if (op == ">=") return emitLessCompare(lhs, rhs, /*invert=*/true, isUnsigned);
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

// `==`/`!=` need no sign handling: a-b (16 bits, borrow chained low into high byte) is zero
// iff a==b.
void I8080Codegen::emitEqualityCompare(const ir::Expr &lhs, const ir::Expr &rhs, bool wantEqual) {
    genExpr(lhs);
    emit("lhld", "ac");
    emit("push", "h");  // saved on the real stack across evaluating rhs
    genExpr(rhs);
    emit("pop", "d");    // DE = lhs
    emit("lhld", "ac");  // HL = rhs
    emit("mov", "a,e");
    emit("sub", "l");
    emit("mov", "c,a");
    emit("mov", "a,d");
    emit("sbb", "h");
    emit("ora", "c");
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit(wantEqual ? "jz" : "jnz", Ltrue);
    emit("lxi", "h, 0");
    emit("shld", "ac");
    emit("jmp", Ldone);
    emitLabel(Ltrue);
    emit("lxi", "h, 1");
    emit("shld", "ac");
    emitLabel(Ldone);
}

// Signed 16-bit "a < b" via the bias trick: flipping the top bit of each operand's high byte
// maps the signed range onto the unsigned one while preserving order, leaving a plain unsigned
// compare (the final borrow) to do.
void I8080Codegen::emitLessCompare(const ir::Expr &a, const ir::Expr &b, bool invert, bool isUnsigned) {
    genExpr(a);
    emit("lhld", "ac");
    emit("push", "h");  // saved on the real stack across evaluating b
    genExpr(b);
    emit("pop", "d");    // DE = a
    emit("lhld", "ac");  // HL = b
    if (!isUnsigned) {
        emit("mov", "a,d");
        emit("xri", "0x80");
        emit("mov", "d,a");  // D = a's high byte, sign flipped
        emit("mov", "a,h");
        emit("xri", "0x80");
        emit("mov", "h,a");  // H = b's high byte, sign flipped
    }
    emit("mov", "a,e");
    emit("sub", "l");                  // a'_lo - b'_lo, CY = borrow
    emit("mov", "a,d");
    emit("sbb", "h");                  // a'_hi - b'_hi - borrow; CY now = (a < b) unbiased
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit(invert ? "jnc" : "jc", Ltrue);
    emit("lxi", "h, 0");
    emit("shld", "ac");
    emit("jmp", Ldone);
    emitLabel(Ltrue);
    emit("lxi", "h, 1");
    emit("shld", "ac");
    emitLabel(Ldone);
}

void I8080Codegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    genExpr(lhs);
    testTruthy(lhs);
    emit("jz", Lfalse);
    genExpr(rhs);
    testTruthy(rhs);
    emit("jz", Lfalse);
    emitLabel(Ltrue);
    emit("lxi", "h, 1");
    emit("shld", "ac");
    emit("jmp", Ldone);
    emitLabel(Lfalse);
    emit("lxi", "h, 0");
    emit("shld", "ac");
    emitLabel(Ldone);
}

void I8080Codegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    genExpr(lhs);
    testTruthy(lhs);
    emit("jnz", Ltrue);
    genExpr(rhs);
    testTruthy(rhs);
    emit("jnz", Ltrue);
    emitLabel(Lfalse);
    emit("lxi", "h, 0");
    emit("shld", "ac");
    emit("jmp", Ldone);
    emitLabel(Ltrue);
    emit("lxi", "h, 1");
    emit("shld", "ac");
    emitLabel(Ldone);
}

void I8080Codegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    testTruthy(rhs);
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit("jz", Ltrue);
    emit("lxi", "h, 0");
    emit("shld", "ac");
    emit("jmp", Ldone);
    emitLabel(Ltrue);
    emit("lxi", "h, 1");
    emit("shld", "ac");
    emitLabel(Ldone);
}

// After genExpr(e) leaves `e`'s value - `ac` if narrow, or `wordCountOf(e.type)` words on the
// real stack (LSW on top) if wide - this sets Z exactly as a plain `ora h` would, with any wide
// stack space released, ready for an immediate `jz`/`jnz`.
void I8080Codegen::testTruthy(const ir::Expr &e) {
    if (!isWide(e.type)) {
        emit("lhld", "ac");
        emit("mov", "a,l");
        emit("ora", "h");
        return;
    }
    int words = wordCountOf(e.type);
    popWideInto("a", words);
    auto bytes = wideByteLabelsLsbFirst("a", words);
    emit("mvi", "c, 0");
    for (const auto &b : bytes) {
        emit("lda", b);
        emit("ora", "c");
        emit("mov", "c,a");
    }
    emit("mov", "a,c");
    emit("ora", "a");
}

std::optional<std::string> I8080Codegen::assembleInProcess(const std::string &asmText,
                                                             const std::string &format) const {
    if (format == "raw") return assembleI8080(asmText);
    // `coff`/`elf`/`macho`/`aout`/`objraw`: builds one format-neutral ObjectFile, then picks
    // that format's FileFormatWriter to serialize it.
    if (auto kind = fileFormatKindFromName(format)) {
        ObjectFile obj = assembleI8080Object(asmText);
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

std::optional<std::string> I8080Codegen::debugMap(const std::string &asmText, const std::string &format,
        const std::string &sourcePath) const {
    (void)format;
    return assembleI8080DebugMap(asmText, sourcePath, debugSymbols_);
}

std::optional<std::string> I8080Codegen::debugMapPreLink(const std::string &asmText,
                                                    const std::string &sourcePath) const {
    return assembleI8080DebugMap(asmText, sourcePath, debugSymbols_, /*allowExternal=*/true);
}

// Only emits a marker when `debugMapEnabled_` (set via setDebugMapEnabled(),
// before generate() runs) and `line` actually differs from the last one
// emitted - see Pdp7Codegen.cpp's identical emitSourceLine(), which this
// mirrors exactly.
void I8080Codegen::emitSourceLine(int line) {
    if (!debugMapEnabled_ || line == 0 || line == lastDebugLine_) return;
    emitRaw(cat("__line ", line));
    lastDebugLine_ = line;
}

}  // namespace rcc
