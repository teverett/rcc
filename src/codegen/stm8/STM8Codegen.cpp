#include "STM8Codegen.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

#include "STM8Assembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

// Program load/entry address, and the real hardware stack's initial top.
constexpr uint32_t kBaseAddress = 0x0100;
constexpr uint32_t kStackTop = 0xFF00;

// Initial top of the software frame stack; sits between the code/data
// region and the real hardware stack, clear of both.
constexpr uint32_t kFrameStackTop = 0xE000;
}  // namespace

void STM8Codegen::declareTemp(const std::string &lbl) { trailer_.push_back(lbl + ": db 0, 0"); }

std::string STM8Codegen::arrayElemText(const ir::ArrayInitElem &e) {
    if (!e.isSymbol) return std::to_string(e.value);
    return e.slot ? label(e.slot) : label(e.func);
}

// `elemType`'s width picks db (char array) vs dw (every other array - a
// long/long long array's elements still only occupy one native word each,
// a known gap, see genIndex()).
void STM8Codegen::declareArray(const std::string &lbl, const std::vector<ir::ArrayInitElem> &values, Type elemType) {
    if (values.empty()) return;
    const char *directive = widthOfType(elemType) == 1 ? "db" : "dw";
    std::ostringstream line;
    line << lbl << ": " << directive << " ";
    for (size_t i = 0; i < values.size(); i++) {
        if (i > 0) line << ", ";
        line << arrayElemText(values[i]);
    }
    trailer_.push_back(line.str());
}

void STM8Codegen::copy16(const std::string &dst, const std::string &src) {
    emit("ldw", "x, " + src);
    emit("ldw", dst + ", x");
}

// Assumes `x` already holds the target address.
void STM8Codegen::storeIndirect(const std::string &src) {
    emit("ld", "a, " + src);
    emit("stxind");
    emit("incw", "x");
    emit("ld", "a, " + src + "+1");
    emit("stxind");
}

void STM8Codegen::loadIndirect(const std::string &dst) {
    emit("ldxind");
    emit("ld", dst + ", a");
    emit("incw", "x");
    emit("ldxind");
    emit("ld", dst + "+1, a");
}

void STM8Codegen::shiftOneBit(const std::string &lbl, bool isLeft) {
    emit("ldw", "x, " + lbl);
    emit(isLeft ? "slaw" : "srlw", "x");
    emit("ldw", lbl + ", x");
}

// STM8 has no sign-extend instruction; tests the byte's top bit instead
// (andi a, 0x80 isolates it, tnza turns that into a zero/nonzero test) and
// branches once. `a` must already hold the byte to extend, and ends up
// holding the fill byte (0xFF or 0x00).
void STM8Codegen::genSignFillByte() {
    emit("andi", "a, 0x80");
    emit("tnza");
    std::string Lneg = newLabel(), Ldone = newLabel();
    emit("jrne", Lneg);
    emit("ldi", "a, 0");
    emit("jp", Ldone);
    emitLabel(Lneg);
    emit("ldi", "a, 0xFF");
    emitLabel(Ldone);
}

// Assumes `x` already holds the effective storage address.
void STM8Codegen::loadOperand(Type type) {
    if (widthOfType(type) == 1) {
        emit("ldxind");
        emit("ld", "ac+1, a");
        if (type.isUnsigned) {
            emit("ldi", "a, 0");
        } else {
            genSignFillByte();  // a is unchanged by "ld ac+1,a" above
        }
        emit("ld", "ac, a");
        return;
    }
    loadAcIndirect();
}

void STM8Codegen::storeOperand(Type type) {
    if (widthOfType(type) == 1) {
        emit("ld", "a, ac+1");
        emit("stxind");
        return;
    }
    storeAcIndirect();
}

// Pops `words` words off the real hardware stack (LSW on top) into
// `role`'s wideScratch() cells.
void STM8Codegen::popWideInto(const char *role, int words) {
    for (int i = 0; i < words; i++) {
        emit("pop", "x");
        emit("ldw", wideScratch(role, i) + ", x");
    }
}

void STM8Codegen::pushWideFrom(const char *role, int words) {
    for (int i = words - 1; i >= 0; i--) {
        emit("ldw", "x, " + wideScratch(role, i));
        emit("push", "x");
    }
}

void STM8Codegen::genWideWordAddr(const ir::Slot *slot, int words, int wordIndex) {
    int byteOff = 2 * (words - 1 - wordIndex);
    if (slot->kind == ir::SlotKind::Global) {
        emit("ldwi", "x, " + label(slot) + (byteOff ? ("+" + std::to_string(byteOff)) : ""));
        return;
    }
    genSlotAddress(slot);
    if (byteOff > 0) {
        emit("ldwi", cat("y, ", byteOff));
        emit("addw", "x, y");
    }
}

// Pushes `slot`'s `words`-word value onto the real hardware stack, word 0
// (least significant) ending up on top.
void STM8Codegen::genWideLoad(const ir::Slot *slot, int words) {
    for (int i = words - 1; i >= 0; i--) {
        genWideWordAddr(slot, words, i);
        loadIndirect("bc");
        emit("ldw", "x, bc");
        emit("push", "x");
    }
}

void STM8Codegen::genWideLoadLiteral(long value, int words) {
    auto uv = static_cast<unsigned long>(value);
    for (int i = words - 1; i >= 0; i--) {
        uint16_t w = static_cast<uint16_t>((uv >> (16 * i)) & 0xFFFF);
        emit("ldwi", cat("x, ", w));
        emit("push", "x");
    }
}

// Pops `words` words off the real hardware stack (least significant on
// top) into `slot`'s storage.
void STM8Codegen::genWideStore(const ir::Slot *slot, int words) {
    for (int i = 0; i < words; i++) {
        emit("pop", "x");
        emit("ldw", "bc, x");
        genWideWordAddr(slot, words, i);
        storeIndirect("bc");
    }
}

// Runtime-address counterpart of genWideWordAddr(): same
// most-significant-word-first layout, base read from `addrCell`.
void STM8Codegen::pushWideFromAddress(const std::string &addrCell, int words) {
    for (int i = words - 1; i >= 0; i--) {
        int byteOff = 2 * (words - 1 - i);
        emit("ldw", "x, " + addrCell);
        if (byteOff > 0) {
            emit("ldwi", cat("y, ", byteOff));
            emit("addw", "x, y");
        }
        loadIndirect("bc");
        emit("ldw", "x, bc");
        emit("push", "x");
    }
}

void STM8Codegen::popWideToAddress(const std::string &addrCell, int words) {
    for (int i = 0; i < words; i++) {
        emit("pop", "x");
        emit("ldw", "bc, x");
        int byteOff = 2 * (words - 1 - i);
        emit("ldw", "x, " + addrCell);
        if (byteOff > 0) {
            emit("ldwi", cat("y, ", byteOff));
            emit("addw", "x, y");
        }
        storeIndirect("bc");
    }
}

// Every byte-at-a-time wide helper below first pops both operands into the
// fixed "wa"/"wb" scratch cells, then walks every byte from least to most
// significant, chaining the carry/borrow flag through adc/sbc.
namespace {
// `role`'s `words`-word value as individual byte labels, least significant
// byte first; since each cell stores its high byte first, within one word
// the low byte (+1) comes before its own high byte in this ordering.
std::vector<std::string> wideByteLabelsLsbFirst(const char *role, int words) {
    std::vector<std::string> out;
    for (int i = 0; i < words; i++) {
        out.push_back(STM8Codegen::wideScratch(role, i) + "+1");
        out.push_back(STM8Codegen::wideScratch(role, i));
    }
    return out;
}
}  // namespace

void STM8Codegen::genWideAdd(const ir::Expr &lhs, const ir::Expr &rhs, bool isAdd, int words) {
    genExpr(lhs);
    genExpr(rhs);
    popWideInto("b", words);
    popWideInto("a", words);
    auto aBytes = wideByteLabelsLsbFirst("a", words);
    auto bBytes = wideByteLabelsLsbFirst("b", words);
    for (size_t k = 0; k < aBytes.size(); k++) {
        emit("ld", "a, " + aBytes[k]);
        if (isAdd) {
            emit(k == 0 ? "add" : "adc", "a, " + bBytes[k]);
        } else {
            emit(k == 0 ? "sub" : "sbc", "a, " + bBytes[k]);
        }
        emit("ld", aBytes[k] + ", a");
    }
    pushWideFrom("a", words);
}

// Two's-complement negation in place: complement every byte, then add 1
// into the least significant byte and fold the carry through the rest via
// adc a, zero.
void STM8Codegen::genWideNegate(int words) {
    popWideInto("a", words);
    auto bytes = wideByteLabelsLsbFirst("a", words);
    for (const auto &b : bytes) {
        emit("ld", "a, " + b);
        emit("cpl", "a");
        emit("ld", b + ", a");
    }
    for (size_t k = 0; k < bytes.size(); k++) {
        emit("ld", "a, " + bytes[k]);
        if (k == 0) {
            emit("addi", "a, 1");
        } else {
            emit("adc", "a, zero");
        }
        emit("ld", bytes[k] + ", a");
    }
    pushWideFrom("a", words);
}

void STM8Codegen::genWideBitwise(const ir::Expr &lhs, const ir::Expr &rhs, const std::string &op, int words) {
    genExpr(lhs);
    genExpr(rhs);
    popWideInto("b", words);
    popWideInto("a", words);
    const char *insn = op == "&" ? "and" : op == "|" ? "or" : "xor";
    auto aBytes = wideByteLabelsLsbFirst("a", words);
    auto bBytes = wideByteLabelsLsbFirst("b", words);
    for (size_t k = 0; k < aBytes.size(); k++) {
        emit("ld", "a, " + aBytes[k]);
        emit(insn, "a, " + bBytes[k]);
        emit("ld", aBytes[k] + ", a");
    }
    pushWideFrom("a", words);
}

void STM8Codegen::genWideEqualityCompare(const ir::Expr &lhs, const ir::Expr &rhs, bool wantEqual, int words) {
    genExpr(lhs);
    genExpr(rhs);
    popWideInto("b", words);
    popWideInto("a", words);
    auto aBytes = wideByteLabelsLsbFirst("a", words);
    auto bBytes = wideByteLabelsLsbFirst("b", words);
    emit("ldi", "a, 0");
    emit("ld", "lc, a");
    for (size_t k = 0; k < aBytes.size(); k++) {
        emit("ld", "a, " + aBytes[k]);
        emit("xor", "a, " + bBytes[k]);
        emit("or", "a, lc");
        emit("ld", "lc, a");
    }
    emit("ld", "a, lc");
    emit("tnza");
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit(wantEqual ? "jreq" : "jrne", Ltrue);
    emit("ldwi", "x, 0");
    emit("ldw", "ac, x");
    emit("jp", Ldone);
    emitLabel(Ltrue);
    emit("ldwi", "x, 1");
    emit("ldw", "ac, x");
    emitLabel(Ldone);
}

// Compares most-significant byte first; only that byte needs the
// signed/unsigned sign-bit bias, since once a byte differs every
// byte after it is comparing a pure unsigned magnitude.
void STM8Codegen::genWideLessCompare(const ir::Expr &a, const ir::Expr &b, bool invert, bool isUnsigned, int words) {
    genExpr(a);
    genExpr(b);
    popWideInto("b", words);
    popWideInto("a", words);
    auto aBytes = wideByteLabelsLsbFirst("a", words);
    auto bBytes = wideByteLabelsLsbFirst("b", words);
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    for (size_t idx = aBytes.size(); idx-- > 0;) {
        bool top = idx == aBytes.size() - 1;
        emit("ld", "a, " + bBytes[idx]);
        if (top && !isUnsigned) emit("xori", "a, 0x80");
        emit("ld", "lc, a");
        emit("ld", "a, " + aBytes[idx]);
        if (top && !isUnsigned) emit("xori", "a, 0x80");
        emit("sub", "a, lc");
        if (idx == 0) {
            emit("jrult", invert ? Lfalse : Ltrue);
            emit("jp", invert ? Ltrue : Lfalse);
        } else {
            std::string Lcontinue = newLabel();
            emit("jreq", Lcontinue);
            emit("jrult", invert ? Lfalse : Ltrue);
            emit("jp", invert ? Ltrue : Lfalse);
            emitLabel(Lcontinue);
        }
    }
    emitLabel(Lfalse);
    emit("ldwi", "x, 0");
    emit("ldw", "ac, x");
    emit("jp", Ldone);
    emitLabel(Ltrue);
    emit("ldwi", "x, 1");
    emit("ldw", "ac, x");
    emitLabel(Ldone);
}

void STM8Codegen::genWideCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned,
                                   int words) {
    if (op == "==") return genWideEqualityCompare(lhs, rhs, /*wantEqual=*/true, words);
    if (op == "!=") return genWideEqualityCompare(lhs, rhs, /*wantEqual=*/false, words);
    if (op == "<") return genWideLessCompare(lhs, rhs, /*invert=*/false, isUnsigned, words);
    if (op == ">") return genWideLessCompare(rhs, lhs, /*invert=*/false, isUnsigned, words);
    if (op == "<=") return genWideLessCompare(rhs, lhs, /*invert=*/true, isUnsigned, words);
    if (op == ">=") return genWideLessCompare(lhs, rhs, /*invert=*/true, isUnsigned, words);
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

// One bit at a time, most significant byte first - rlc/rrc rotate `a`
// through the carry flag, chaining it byte to byte. A signed right shift's
// top byte needs its sign bit preserved: rlc (copies the sign bit into
// carry) then rrc on the same original byte rotates that bit back in as
// the new top bit while still shifting right and producing the correct
// carry-out for the next byte's plain rrc.
void STM8Codegen::genWideShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned, int words) {
    genExpr(lhs);
    popWideInto("a", words);
    genExpr(rhs);
    emit("ld", "a, ac+1");
    emit("ld", "lc, a");
    auto bytesMsbFirst = wideByteLabelsLsbFirst("a", words);
    std::reverse(bytesMsbFirst.begin(), bytesMsbFirst.end());
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("ld", "a, lc");
    emit("tnza");
    emit("jreq", Ldone);
    if (isLeft) {
        emit("rcf");
        for (size_t idx = bytesMsbFirst.size(); idx-- > 0;) {
            emit("ld", "a, " + bytesMsbFirst[idx]);
            emit("rlc", "a");
            emit("ld", bytesMsbFirst[idx] + ", a");
        }
    } else {
        for (size_t idx = 0; idx < bytesMsbFirst.size(); idx++) {
            if (idx == 0 && !isUnsigned) {
                emit("ld", "a, " + bytesMsbFirst[idx]);
                emit("rlc", "a");
                emit("ld", "a, " + bytesMsbFirst[idx]);
                emit("rrc", "a");
            } else {
                if (idx == 0) emit("rcf");
                emit("ld", "a, " + bytesMsbFirst[idx]);
                emit("rrc", "a");
            }
            emit("ld", bytesMsbFirst[idx] + ", a");
        }
    }
    emit("ld", "a, lc");
    emit("dec", "a");
    emit("ld", "lc, a");
    emit("jp", Lloop);
    emitLabel(Ldone);
    pushWideFrom("a", words);
}

// No simulator exists here to verify a hand-written multi-byte multiply/
// divide routine against, so failing loudly is safer than an unverifiable one.
void STM8Codegen::genWideMul(const ir::Expr &lhs, const ir::Expr &rhs, int) {
    err(lhs.line, "multiplying two 'long'/'long long' values isn't supported on this target yet - "
                  "see STM8Codegen.cpp's own genWideMul() comment");
    (void)rhs;
}

void STM8Codegen::genWideDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool, int) {
    err(lhs.line, "dividing two 'long'/'long long' values isn't supported on this target yet - "
                  "see STM8Codegen.cpp's own genWideDivMod() comment");
    (void)rhs;
}

// The real work here is crossing the native-word/long-or-long-long
// boundary (adding words of sign/zero extension, or dropping down to the
// low word) and re-narrowing a char result.
void STM8Codegen::genConvert(const ir::Expr &e) {
    Type from = e.lhs->type;
    Type to = e.type;
    bool fromWide = isWide(from);
    bool toWide = isWide(to);
    if (!fromWide && !toWide) {
        genExpr(*e.lhs);
        if (widthOfType(to) == 1) {
            if (to.isUnsigned) {
                emit("ldi", "a, 0");
                emit("ld", "ac, a");
            } else {
                emit("ld", "a, ac+1");
                genSignFillByte();
                emit("ld", "ac, a");
            }
        }
        return;
    }
    if (fromWide && !toWide) {
        int words = wordCountOf(from);
        genExpr(*e.lhs);
        popWideInto("a", words);
        emit("ldw", "x, " + wideScratch("a", 0));
        emit("ldw", "ac, x");
        if (widthOfType(to) == 1) {
            if (to.isUnsigned) {
                emit("ldi", "a, 0");
                emit("ld", "ac, a");
            } else {
                emit("ld", "a, ac+1");
                genSignFillByte();
                emit("ld", "ac, a");
            }
        }
        return;
    }
    if (!fromWide && toWide) {
        int toWords = wordCountOf(to);
        genExpr(*e.lhs);
        emit("ldw", "x, ac");
        emit("ldw", wideScratch("a", 0) + ", x");
        // A pointer value is always zero-extended: from.isUnsigned would
        // otherwise describe the pointee's signedness, not the pointer's,
        // and a pointer has no sign to extend regardless.
        bool extendUnsigned = isPointer(from) || from.isUnsigned;
        for (int i = 1; i < toWords; i++) {
            if (extendUnsigned) {
                emit("ldwi", "x, 0");
                emit("ldw", wideScratch("a", i) + ", x");
            } else {
                emit("ld", "a, ac");
                genSignFillByte();
                emit("ld", wideScratch("a", i) + ", a");
                emit("ld", wideScratch("a", i) + "+1, a");
            }
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
        emit("ld", "a, " + wideScratch("a", fromWords - 1));
        if (from.isUnsigned) {
            emit("ldi", "a, 0");
        } else {
            genSignFillByte();
        }
        for (int i = fromWords; i < toWords; i++) {
            emit("ld", wideScratch("a", i) + ", a");
            emit("ld", wideScratch("a", i) + "+1, a");
        }
    }
    pushWideFrom("a", toWords);
}

std::string STM8Codegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    // Real separate compilation means `prog` may not define `main` at all;
    // `objectMode` means no object file emits this entry sequence itself -
    // STM8LinkerFactory.cpp synthesizes exactly one once every object is
    // merged and `main`'s linked address is resolvable. An object file's
    // own base is `org 0`; placing it is the linker's job.
    emitRaw("org " + std::to_string(objectMode ? 0 : kBaseAddress));
    emitRaw("");
    if (!objectMode) {
        emitLabel("_start");
        emit("ldwi", cat("sp, ", kStackTop));
        emit("ldwi", cat("x, ", kFrameStackTop));
        emit("ldw", "fsp, x");
        emit("ldwi", "x, 0");
        emit("ldw", "fp, x");
        emit("call", label(mainFn));
        // The result sits in `ac` once execution reaches `halt`; also left
        // in `x` for a quick look via a register dump.
        emit("ldw", "x, ac");
        emitLabel("halt");
        emit("jp", "halt");
        emitRaw("");
    }

    // Every genExpr() call in the whole linked program shares these cells,
    // so unlike other symbols they can't be redeclared per-object: declared
    // once here in whole-program mode; in object mode every reference is
    // deferred as an external relocation against STM8LinkerFactory.cpp's
    // synthesized entry object.
    if (!objectMode) {
        declareTemp("ac");
        declareTemp("bc");
        declareTemp("cx");
        declareTemp("dx");
        declareTemp("ptr");
        declareTemp("fp");
        declareTemp("fsp");
        declareTemp("mc");  // genNarrowMul()'s own scratch cell, never aliased with `cx`
        trailer_.push_back("lc: db 0");
        // Permanent zero byte: adc a, zero folds a pending carry into a
        // byte with nothing else added (see genWideNegate()).
        trailer_.push_back("zero: db 0");
        for (int i = 0; i < 4; i++) declareTemp(wideScratch("a", i));
        for (int i = 0; i < 4; i++) declareTemp(wideScratch("b", i));
    }

    for (const auto &fb : prog.functionBodies) genFunction(fb);

    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        // An external global must never get storage here; only referenced
        // by name, so the assembler's undefined-symbol handling defers it
        // to a relocation.
        if (slot->isExternal) continue;
        const std::string lbl = label(slot.get());
        if (slot->arraySize > 0) {
            declareArray(lbl, slot->arrayInit, slot->type);
        } else if (slot->type.base == BaseType::Struct) {
            // No struct initializer exists in this subset, so this is
            // always zero-filled; reuses declareArray's own element-list
            // emission (one zero "db" per byte) rather than the width-
            // bucketed logic below, which only handles even widths.
            declareArray(lbl, std::vector<ir::ArrayInitElem>(slot->type.structSize), Type{BaseType::Char, false});
        } else {
            long v = slot->hasInit ? slot->initValue : 0;
            int w = widthOfType(slot->type);
            if (w == 1) {
                trailer_.push_back(lbl + ": db " + std::to_string(static_cast<int>(static_cast<uint8_t>(v))) +
                                    "  ; " + slot->sourceName);
            } else if (w == 2) {
                uint16_t v16 = static_cast<uint16_t>(v);
                // High byte first, low byte second (big-endian).
                trailer_.push_back(lbl + ": db " + std::to_string((v16 >> 8) & 0xFF) + "  ; " + slot->sourceName);
                trailer_.push_back("db " + std::to_string(v16 & 0xFF));
            } else {
                // Each word emitted most significant first, least
                // significant word last.
                auto uv = static_cast<unsigned long>(v);
                std::ostringstream line;
                line << lbl << ": db ";
                bool first = true;
                for (int i = w / 2 - 1; i >= 0; i--) {
                    uint16_t word = static_cast<uint16_t>((uv >> (16 * i)) & 0xFFFF);
                    if (!first) line << ", ";
                    line << ((word >> 8) & 0xFF) << ", " << (word & 0xFF);
                    first = false;
                }
                line << "  ; " << slot->sourceName;
                trailer_.push_back(line.str());
            }
        }
    }

    std::ostringstream out;
    out << code_.str();
    for (const auto &line : trailer_) out << line << "\n";
    return out.str();
}

std::map<std::string, std::string> STM8Codegen::perFileAsm() const { return perFileAsm_; }

void STM8Codegen::genFunction(const ir::FunctionBody &fn) {
    emitLabel(label(fn.func));
    currentFunction_ = fn.func;
    genPrologue(*fn.func);
    genStmt(*fn.body);
    genEpilogue(*fn.func);
    currentFunction_ = nullptr;
    emitRaw("");
}

// --- Per-call frame management ------------------------------------------
//
// Layout, growing down from `fp` (the saved caller's `fp`, the first word
// pushed for this call):
//
//   fp+2*numParams .. fp+2   incoming arguments (param 0 closest to `fp`;
//                            pushed in reverse order, so param 0 is pushed last)
//   fp                       this call's saved caller's `fp`
//   fp-2 .. fp-2*frameSize   this function's Local storage, declaration order
//
// `fp`/`fsp` are ordinary named memory cells, not real registers: `x`/`y`
// are already committed as ephemeral transit registers, so there's no
// spare register to hold a live frame pointer. They're independent of the
// real hardware `sp` (used only for call/ret and genBinary()'s push/pop scratch).
void STM8Codegen::pushFrameWord(const std::string &cell) {
    emit("ldw", "x, fsp");
    emit("ldwi", "y, 2");
    emit("subw", "x, y");
    emit("ldw", "fsp, x");
    storeIndirect(cell);  // x still holds the just-decremented fsp
}

void STM8Codegen::genPrologue(const ir::Function &fn) {
    pushFrameWord("fp");
    emit("ldw", "x, fsp");
    emit("ldw", "fp, x");
    if (fn.frameSize > 0) {
        emit("ldw", "x, fp");
        emit("ldwi", cat("y, ", fn.frameSize));
        emit("subw", "x, y");
        emit("ldw", "fsp, x");
    }
}

// Resets `fsp` past this call's incoming arguments too, so the callee
// cleans up its own argument area and the caller needs no adjustment after
// `call`. Uses `bc` as scratch, never `ac`, since `ac` already holds the
// live return value by the time this runs.
void STM8Codegen::genEpilogue(const ir::Function &fn) {
    emit("ldw", "x, fp");
    loadIndirect("bc");  // bc = the caller's saved fp
    int numParams = static_cast<int>(fn.params.size());
    emit("ldw", "x, fp");
    emit("ldwi", cat("y, ", (numParams + 1) * 2));
    emit("addw", "x, y");
    emit("ldw", "fsp, x");
    emit("ldw", "x, bc");
    emit("ldw", "fp, x");
    emit("ret");
}

void STM8Codegen::genSlotAddress(const ir::Slot *slot) {
    if (slot->kind == ir::SlotKind::Global) {
        emit("ldwi", "x, " + label(slot));
        return;
    }
    if (slot->kind == ir::SlotKind::Param) {
        emit("ldw", "x, fp");
        emit("ldwi", cat("y, ", slot->frameOffset));
        emit("addw", "x, y");
        return;
    }
    int below = ir::localOffsetDescendingWide(*slot, typeWidths());
    emit("ldw", "x, fp");
    emit("ldwi", cat("y, ", below));
    emit("subw", "x, y");
}

void STM8Codegen::genStmt(const ir::Stmt &s) {
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
            emit("jreq", s.elseStmt ? Lelse : Ldone);
            genStmt(*s.thenStmt);
            if (s.elseStmt) {
                emit("jp", Ldone);
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
            emit("jreq", Lend);
            loopLabels_.push_back({Lcont, Lend});
            genStmt(*s.loopBody);
            loopLabels_.pop_back();
            if (s.postStmt) {
                emitLabel(Lcont);
                genStmt(*s.postStmt);
            }
            emit("jp", Lstart);
            emitLabel(Lend);
            return;
        }
        case ir::StmtKind::Break: {
            emit("jp", loopLabels_.back().second);
            return;
        }
        case ir::StmtKind::Continue: {
            emit("jp", loopLabels_.back().first);
            return;
        }
        case ir::StmtKind::Return: {
            if (s.initOrReturnOrExpr) {
                genExpr(*s.initOrReturnOrExpr);
                // A wide value sits on the hardware stack (LSW on top) after
                // genExpr(); pop it into the "a" wideScratch() cells since
                // genEpilogue() never touches that stack (see the matching
                // push-back in genExpr()'s Call case).
                if (isWide(s.initOrReturnOrExpr->type)) popWideInto("a", wordCountOf(s.initOrReturnOrExpr->type));
            }
            genEpilogue(*currentFunction_);
            return;
        }
        case ir::StmtKind::LocalDecl: {
            // A Local's words are already reserved by genPrologue(); an
            // array initializer still runs its per-element stores every
            // time this declaration executes (not once, statically) since
            // a fresh call gets a fresh frame.
            if (s.declSlot->arraySize > 0) {
                int stride = widthOfType(s.declSlot->type) == 1 ? 1 : 2;
                for (size_t i = 0; i < s.declSlot->arrayInit.size(); i++) {
                    const ir::ArrayInitElem &elem = s.declSlot->arrayInit[i];
                    if (elem.isSymbol && elem.slot && elem.slot->kind != ir::SlotKind::Global) {
                        // A Local's/Param's address only exists at runtime
                        // (relative to this call's `fp`), unlike a Global's
                        // or Function's compile-time constant address.
                        genSlotAddress(elem.slot);
                        emit("ldw", "ac, x");
                    } else {
                        emit("ldwi", "x, " + arrayElemText(elem));
                        emit("ldw", "ac, x");
                    }
                    genSlotAddress(s.declSlot);  // x = address of element 0
                    if (i > 0) {
                        // Element i sits above element 0, the array's
                        // lowest address.
                        emit("ldwi", cat("y, ", i * stride));
                        emit("addw", "x, y");
                    }
                    storeOperand(s.declSlot->type);
                }
            } else if (s.initOrReturnOrExpr) {
                genExpr(*s.initOrReturnOrExpr);
                if (isWide(s.declSlot->type)) {
                    genWideStore(s.declSlot, wordCountOf(s.declSlot->type));
                } else {
                    genSlotAddress(s.declSlot);
                    storeOperand(s.declSlot->type);
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
            emit("jp", irLabel(s.labelId));
            return;
        }
    }
}

void STM8Codegen::genExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit: {
            if (isWide(e.type)) {
                genWideLoadLiteral(e.intValue, wordCountOf(e.type));
                return;
            }
            uint16_t v16 = static_cast<uint16_t>(e.intValue);
            emit("ldwi", cat("x, ", v16));
            emit("ldw", "ac, x");
            return;
        }
        case ir::ExprKind::Var: {
            if (isWide(e.slot->type)) {
                genWideLoad(e.slot, wordCountOf(e.slot->type));
                return;
            }
            if (e.slot->kind == ir::SlotKind::Global) {
                copy16("ac", label(e.slot));
            } else {
                genSlotAddress(e.slot);
                loadOperand(e.slot->type);
            }
            return;
        }
        case ir::ExprKind::Index: {
            genIndex(e);
            return;
        }
        case ir::ExprKind::Assign: {
            if (e.lhs) {  // slot[idx] = rhs - never wide (see genIndex()'s known limitation)
                genIndexAddr(e.slot, *e.lhs);  // ptr = target address
                emit("ldw", "x, ptr");
                emit("push", "x");
                genExpr(*e.rhs);
                emit("pop", "x");
                storeOperand(e.slot->type);
                return;
            }
            if (isWide(e.slot->type)) {
                genExpr(*e.rhs);
                genWideStore(e.slot, wordCountOf(e.slot->type));
                return;
            }
            genExpr(*e.rhs);
            if (e.slot->kind == ir::SlotKind::Global) {
                copy16(label(e.slot), "ac");
            } else {
                genSlotAddress(e.slot);
                storeOperand(e.slot->type);
            }
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            if (e.lhs) {  // slot[idx] op= rhs (including slot[idx]++/--)
                genIndexAddr(e.slot, *e.lhs);  // ptr = target address
                genCompoundThroughPtr(e);
                return;
            }
            // Plain scalar target: a Global's label or a Local/Param's
            // fp-relative address has no side effects, so it's simply
            // recomputed after evaluating rhs rather than held across it.
            genExpr(*e.rhs);
            copy16("dx", "ac");  // dx = rhs, held across loading old
            if (e.slot->kind == ir::SlotKind::Global) {
                copy16("ac", label(e.slot));
            } else {
                genSlotAddress(e.slot);
                loadOperand(e.slot->type);
            }
            copy16("bc", "ac");  // bc = old, ac = rhs - genCompoundOp()'s preconditions
            copy16("ac", "dx");
            if (e.yieldOld) copy16("cx", "bc");  // cx = old, held for the final result
            genCompoundOp(e.op, e.type);
            narrowAcTo(e.type);
            if (e.slot->kind == ir::SlotKind::Global) {
                copy16(label(e.slot), "ac");
            } else {
                genSlotAddress(e.slot);
                storeOperand(e.slot->type);
            }
            if (e.yieldOld) copy16("ac", "cx");
            return;
        }
        case ir::ExprKind::Ternary: {
            genExpr(*e.lhs);
            testTruthy(*e.lhs);
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            emit("jreq", Lelse);
            genExpr(*e.rhs);
            emit("jp", Ldone);
            emitLabel(Lelse);
            genExpr(*e.elseExpr);
            emitLabel(Ldone);
            return;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            genExpr(*e.lhs);              // ac = pointer value
            emit("ldw", "ptr, ac");
            genCompoundThroughPtr(e);
            return;
        }
        case ir::ExprKind::DerefAssign: {
            // Stores at e.type's width/signedness: the pointee type of a
            // typed pointer, or a plain native word for untyped-int-as-address.
            genExpr(*e.lhs);  // ac = pointer value
            if (isWide(e.type)) {
                emit("ldw", "ptr, ac");
                genExpr(*e.rhs);
                popWideToAddress("ptr", wordCountOf(e.type));
                return;
            }
            emit("ldw", "x, ac");
            emit("push", "x");
            genExpr(*e.rhs);
            emit("pop", "x");
            storeOperand(e.type);
            return;
        }
        case ir::ExprKind::Unary: {
            if (e.op == "-") {
                genExpr(*e.rhs);
                if (isWide(e.rhs->type)) {
                    genWideNegate(wordCountOf(e.rhs->type));
                } else {
                    emit("ldw", "x, ac");
                    emit("negw", "x");
                    emit("ldw", "ac, x");
                }
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                // Loads at e.type's width/signedness - see DerefAssign above.
                genExpr(*e.rhs);  // ac = pointer value
                if (isWide(e.type)) {
                    emit("ldw", "ptr, ac");
                    pushWideFromAddress("ptr", wordCountOf(e.type));
                } else {
                    emit("ldw", "x, ac");
                    loadOperand(e.type);
                }
            } else if (e.op == "~") {
                genExpr(*e.rhs);
                if (isWide(e.rhs->type)) {
                    int words = wordCountOf(e.rhs->type);
                    popWideInto("a", words);
                    for (int i = 0; i < words; i++) {
                        std::string cell = wideScratch("a", i);
                        emit("ldw", "x, " + cell);
                        emit("cplw", "x");
                        emit("ldw", cell + ", x");
                    }
                    pushWideFrom("a", words);
                } else {
                    emit("ldw", "x, ac");
                    emit("cplw", "x");
                    emit("ldw", "ac, x");
                }
            } else {  // "&"
                genAddrOf(*e.rhs);
            }
            return;
        }
        case ir::ExprKind::Call: {
            // Pushed in reverse (last argument first), so param 0 ends up
            // pushed last, at fp+2 - matching genSlotAddress()'s
            // fp+(paramIndex+1)*2 formula. A wide argument's words
            // (LSW on top) are popped and frame-pushed one at a time, least
            // significant first, landing the most significant word at the
            // lowest address - where genWideWordAddr() expects it.
            for (size_t i = e.args.size(); i-- > 0;) {
                genExpr(*e.args[i]);
                if (isWide(e.args[i]->type)) {
                    int words = wordCountOf(e.args[i]->type);
                    for (int w = 0; w < words; w++) {
                        emit("pop", "x");
                        emit("ldw", "ac, x");
                        pushFrameWord("ac");
                    }
                } else {
                    pushFrameWord("ac");
                }
            }
            emit("call", label(e.func));
            if (isWide(e.type)) {
                // Callee left its wide return value in the "wa" scratch
                // cells; push it back as this expression's result.
                pushWideFrom("a", wordCountOf(e.type));
            }
            return;
        }
        case ir::ExprKind::CallPtr: {
            // `CALL [mem]` fetches the entry address from the word stored
            // at mem rather than treating mem itself as the callee, so the
            // computed callee address is spilled to a fresh temp cell first.
            genExpr(*e.lhs);  // ac = the callee's own entry address
            std::string T = allocTemp();
            declareTemp(T);
            copy16(T, "ac");
            emit("callind", T);
            return;
        }
        case ir::ExprKind::Binary: {
            genBinary(e);
            return;
        }
        case ir::ExprKind::Intrinsic: {
            // Real STM8 interrupt enable/disable (RIM/SIM).
            emit(e.op == "ion" ? "rim" : "sim");
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

// Leaves slot[idxExpr]'s effective address in the `ptr` memory cell;
// idxExpr is evaluated first and alone, since no named cell survives a
// general recursive genExpr() call.
void STM8Codegen::genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
    genExpr(idxExpr);  // ac = index
    emit("ldw", "x, ac");
    for (int i = 0; i < elemShift(slot->type); i++) emit("slaw", "x");  // x *= widthOf(slot->type)
    emit("ldw", "ptr, x");
    genSlotAddress(slot);  // x = slot's own storage address (Global/Local/Param alike)
    if (slot->arraySize > 0) {
        emit("ldw", "y, ptr");  // ptr = base + index*2
        emit("addw", "x, y");
        emit("ldw", "ptr, x");
        return;
    }
    // Ordinary pointer arithmetic: base-plus-scaled-index regardless of
    // where the pointer variable itself lives.
    loadIndirect("bc");
    emit("ldw", "y, bc");
    emit("ldw", "x, ptr");
    emit("addw", "x, y");
    emit("ldw", "ptr, x");
}

// Every array element reads only one native word regardless of its real
// width - a known gap for long/long long array elements (the index is
// scaled correctly by elemShift(), but a multi-word element's remaining
// words are never read/written). Scalars of every width work fully.
void STM8Codegen::genIndex(const ir::Expr &e) {
    genIndexAddr(e.slot, *e.lhs);
    emit("ldw", "x, ptr");
    loadOperand(e.slot->type);
}

void STM8Codegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        genSlotAddress(operand.slot);
        emit("ldw", "ac, x");
        return;
    }
    if (operand.kind == ir::ExprKind::Index) {
        genIndexAddr(operand.slot, *operand.lhs);
        copy16("ac", "ptr");
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        emit("ldwi", "x, " + label(operand.func));
        emit("ldw", "ac, x");
        return;
    }
    err(operand.line, "internal: unhandled addressable-operand kind");
}

// Every binary op saves its left operand across evaluating the right one
// on the real hardware stack (push x/pop x), never in a named memory cell,
// since a nested binary op might reuse that cell.
void STM8Codegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    // Shares one common type via e.lhs->type, except shift (only LHS's
    // type matters) and &&/|| (width-independent).
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
        emit("ldw", "x, ac");
        emit("push", "x");
        genExpr(*e.rhs);  // ac = rhs
        emit("ldw", "y, ac");
        emit("pop", "x");  // y = rhs, x = lhs (popped only now)
        if (op == "+") {
            emit("addw", "x, y");
        } else {
            emit("subw", "x, y");
        }
        emit("ldw", "ac, x");
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
        genBitwise(op, *e.lhs, *e.rhs);
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

// No register-register bitwise op on `x`/`y` (STM8 AND/OR/XOR only touch
// `a`), so this goes byte-at-a-time through `a` against each cell's named
// byte addresses. `lhs` still survives evaluating `rhs` on the hardware
// stack rather than in `bc` directly, since a nested `&`/`|`/`^` also reuses `bc`.
void STM8Codegen::genBitwise(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    emit("ldw", "x, ac");
    emit("push", "x");
    genExpr(rhs);  // ac = rhs
    emit("pop", "x");
    emit("ldw", "bc, x");  // bc = lhs
    const char *insn = (op == "&") ? "and" : (op == "|") ? "or" : "xor";
    emit("ld", "a, bc");
    emit(insn, "a, ac");
    emit("ld", "ac, a");
    emit("ld", "a, bc+1");
    emit(insn, "a, ac+1");
    emit("ld", "ac+1, a");
}

// No variable-count shift instruction - loop `rhs` times instead, one bit
// at a time, via shiftOneBit(). The loop count lives in `lc` (a plain
// memory byte, not `a`): STM8 has no dec/tnz on an arbitrary memory byte
// directly, so `a` is still needed as scratch to move it in and out.
void STM8Codegen::genShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned) {
    genExpr(lhs);  // ac = value to shift
    emit("ldw", "x, ac");
    emit("push", "x");
    genExpr(rhs);  // ac = shift count
    emit("ld", "a, ac+1");
    emit("ld", "lc, a");  // lc = shift count (low byte is enough)
    emit("pop", "x");
    emit("ldw", "bc, x");  // bc = value
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("ld", "a, lc");
    emit("tnza");
    emit("jreq", Ldone);
    if (isLeft) {
        shiftOneBit("bc", /*isLeft=*/true);
    } else if (isUnsigned) {
        shiftOneBit("bc", /*isLeft=*/false);
    } else {
        // SRAW: arithmetic (sign-preserving) counterpart to shiftOneBit()'s logical SRLW.
        emit("ldw", "x, bc");
        emit("sraw", "x");
        emit("ldw", "bc, x");
    }
    emit("ld", "a, lc");
    emit("deca");
    emit("ld", "lc, a");
    emit("jp", Lloop);
    emitLabel(Ldone);
    copy16("ac", "bc");
}

// `bc` is the multiplicand, `dx` the multiplier; `genNarrowMul()` turns
// them into a 16x16->16 product via hardware `MUL X,A`. `lhs` survives
// evaluating `rhs` on the hardware stack rather than an early
// copy16("bc", ...), since `rhs` may itself be or contain a call that reuses `bc`.
void STM8Codegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    emit("ldw", "x, ac");
    emit("push", "x");
    genExpr(rhs);
    copy16("dx", "ac");  // dx = multiplier
    emit("pop", "x");
    emit("ldw", "bc, x");  // bc = multiplicand
    genNarrowMul();
}

// `MUL X,A` multiplies `a` by `x`'s low byte, leaving the full 16-bit
// result in `x`; the real STM8's own `x` is only a transit register here
// (every live operand lives in a named cell), so each byte operand is
// routed through `x`'s low byte via a scratch cell (`mc`, dedicated to this
// function alone - `cx` is reserved for genCompoundOp()'s callers to stash
// a yieldOld result across it) whose low byte is set first.
//
// A 16x16 product's bcH*dxH term only affects bits >=16, so it's dropped;
// of the two cross terms (bcH*dxL, bcL*dxH), only their low bytes still
// land inside the 16-bit result (each cross term's own high byte would
// land at bit 16 or above), so those two low bytes are summed (the 8-bit
// add's carry-out is itself bit 16 and is discarded) and added into the
// low*low product's high byte (again discarding any carry past bit 15).
void STM8Codegen::genNarrowMul() {
    emit("ldw", "x, bc");  // x = bcL * dxL
    emit("ld", "a, dx+1");
    emit("mul", "x, a");
    emit("ldw", "ac, x");
    emit("ld", "a, bc");  // x = bcH * dxL
    emit("ld", "mc+1, a");
    emit("ldw", "x, mc");
    emit("ld", "a, dx+1");
    emit("mul", "x, a");
    emit("ldw", "mc, x");
    emit("ld", "a, mc+1");
    emit("ld", "lc, a");
    emit("ldw", "x, bc");  // x = bcL * dxH
    emit("ld", "a, dx");
    emit("mul", "x, a");
    emit("ldw", "mc, x");
    emit("ld", "a, mc+1");
    emit("add", "a, lc");
    emit("ld", "lc, a");
    emit("ld", "a, ac");
    emit("add", "a, lc");
    emit("ld", "ac, a");
}

// Repeated subtraction (no hardware 16/16 divide): `dx` is the running
// remainder (starts as the dividend), `bc` the fixed divisor, `ac` counts
// how many times `bc` was subtracted via CPW+jrult, with no separate loop
// counter needed. Only divides non-negative magnitudes, so a signed
// division/modulo first records each operand's sign as a 0/1 flag, negates
// to a non-negative magnitude, divides via the loop, then restores the
// quotient's sign (negative iff the signs disagreed) and the remainder's
// sign (always the dividend's, matching C's truncating-toward-zero convention).
void STM8Codegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    emit("ldw", "x, ac");
    emit("push", "x");
    genExpr(rhs);
    copy16("bc", "ac");  // bc = divisor
    emit("pop", "x");
    emit("ldw", "dx, x");  // dx = dividend
    std::string divisorNeg = allocTemp();
    declareTemp(divisorNeg);
    if (!isUnsigned) {
        std::string Ld = newLabel(), Lr = newLabel();
        emit("ldi", "a, 0");
        emit("ld", "lc, a");  // lc = dividend-negative flag
        emit("ld", "a, dx");
        emit("andi", "a, 0x80");
        emit("tnza");
        emit("jreq", Ld);
        emit("ldw", "x, dx");
        emit("negw", "x");
        emit("ldw", "dx, x");
        emit("ldi", "a, 1");
        emit("ld", "lc, a");
        emitLabel(Ld);
        emit("ldi", "a, 0");
        emit("ld", divisorNeg + "+1, a");
        emit("ld", "a, bc");
        emit("andi", "a, 0x80");
        emit("tnza");
        emit("jreq", Lr);
        emit("ldw", "x, bc");
        emit("negw", "x");
        emit("ldw", "bc, x");
        emit("ldi", "a, 1");
        emit("ld", divisorNeg + "+1, a");
        emitLabel(Lr);
    }
    emit("ldwi", "x, 0");
    emit("ldw", "ac, x");  // ac = quotient = 0
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("ldw", "x, dx");
    emit("ldw", "y, bc");
    emit("cpw", "x, y");
    emit("jrult", Ldone);  // dx < bc: stop
    emit("subw", "x, y");
    emit("ldw", "dx, x");
    emit("ldw", "x, ac");
    emit("incw", "x");
    emit("ldw", "ac, x");
    emit("jp", Lloop);
    emitLabel(Ldone);
    if (wantRemainder) {
        copy16("ac", "dx");
        if (!isUnsigned) {
            std::string Lskip = newLabel();
            emit("ld", "a, lc");
            emit("tnza");
            emit("jreq", Lskip);
            emit("ldw", "x, ac");
            emit("negw", "x");
            emit("ldw", "ac, x");
            emitLabel(Lskip);
        }
        return;
    }
    if (!isUnsigned) {
        std::string Lskip = newLabel();
        emit("ld", "a, lc");
        emit("xor", "a, " + divisorNeg + "+1");
        emit("tnza");
        emit("jreq", Lskip);
        emit("ldw", "x, ac");
        emit("negw", "x");
        emit("ldw", "ac, x");
        emitLabel(Lskip);
    }
}

// Precondition: `bc` holds `old`, `ac` holds `rhs`. Each operator's body
// below mirrors its genBinary() helper's post-setup logic on the same
// named cells. Leaves the raw new value in `ac`.
void STM8Codegen::genCompoundOp(const std::string &op, Type type) {
    bool isUnsigned = type.isUnsigned;
    if (op == "+" || op == "-") {
        emit("ldw", "x, bc");
        emit("ldw", "y, ac");
        emit(op == "+" ? "addw" : "subw", "x, y");
        emit("ldw", "ac, x");
        return;
    }
    if (op == "*") {
        copy16("dx", "ac");  // dx = multiplier (rhs) - bc already holds the multiplicand (old)
        genNarrowMul();
        return;
    }
    if (op == "/" || op == "%") {
        emit("ldw", "x, bc");
        emit("ldw", "dx, x");  // dx = dividend (old)
        copy16("bc", "ac");    // bc = divisor (rhs)
        std::string divisorNeg = allocTemp();
        declareTemp(divisorNeg);
        if (!isUnsigned) {
            std::string Ld = newLabel(), Lr = newLabel();
            emit("ldi", "a, 0");
            emit("ld", "lc, a");  // lc = dividend-negative flag
            emit("ld", "a, dx");
            emit("andi", "a, 0x80");
            emit("tnza");
            emit("jreq", Ld);
            emit("ldw", "x, dx");
            emit("negw", "x");
            emit("ldw", "dx, x");
            emit("ldi", "a, 1");
            emit("ld", "lc, a");
            emitLabel(Ld);
            emit("ldi", "a, 0");
            emit("ld", divisorNeg + "+1, a");
            emit("ld", "a, bc");
            emit("andi", "a, 0x80");
            emit("tnza");
            emit("jreq", Lr);
            emit("ldw", "x, bc");
            emit("negw", "x");
            emit("ldw", "bc, x");
            emit("ldi", "a, 1");
            emit("ld", divisorNeg + "+1, a");
            emitLabel(Lr);
        }
        emit("ldwi", "x, 0");
        emit("ldw", "ac, x");  // ac = quotient = 0
        std::string Lloop = newLabel(), Ldone = newLabel();
        emitLabel(Lloop);
        emit("ldw", "x, dx");
        emit("ldw", "y, bc");
        emit("cpw", "x, y");
        emit("jrult", Ldone);
        emit("subw", "x, y");
        emit("ldw", "dx, x");
        emit("ldw", "x, ac");
        emit("incw", "x");
        emit("ldw", "ac, x");
        emit("jp", Lloop);
        emitLabel(Ldone);
        if (op == "%") {
            copy16("ac", "dx");
            if (!isUnsigned) {
                std::string Lskip = newLabel();
                emit("ld", "a, lc");
                emit("tnza");
                emit("jreq", Lskip);
                emit("ldw", "x, ac");
                emit("negw", "x");
                emit("ldw", "ac, x");
                emitLabel(Lskip);
            }
            return;
        }
        if (!isUnsigned) {
            std::string Lskip = newLabel();
            emit("ld", "a, lc");
            emit("xor", "a, " + divisorNeg + "+1");
            emit("tnza");
            emit("jreq", Lskip);
            emit("ldw", "x, ac");
            emit("negw", "x");
            emit("ldw", "ac, x");
            emitLabel(Lskip);
        }
        return;
    }
    if (op == "&" || op == "|" || op == "^") {
        const char *insn = (op == "&") ? "and" : (op == "|") ? "or" : "xor";
        emit("ld", "a, bc");
        emit(insn, "a, ac");
        emit("ld", "ac, a");
        emit("ld", "a, bc+1");
        emit(insn, "a, ac+1");
        emit("ld", "ac+1, a");
        return;
    }
    // "<<" or ">>"
    emit("ld", "a, ac+1");
    emit("ld", "lc, a");  // lc = shift count; bc already holds the value to shift (old)
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("ld", "a, lc");
    emit("tnza");
    emit("jreq", Ldone);
    if (op == "<<") {
        shiftOneBit("bc", /*isLeft=*/true);
    } else if (isUnsigned) {
        shiftOneBit("bc", /*isLeft=*/false);
    } else {
        emit("ldw", "x, bc");
        emit("sraw", "x");
        emit("ldw", "bc, x");
    }
    emit("ld", "a, lc");
    emit("deca");
    emit("ld", "lc, a");
    emit("jp", Lloop);
    emitLabel(Ldone);
    copy16("ac", "bc");
}

// `ptr` holds the resolved address on entry; stashed on the hardware stack
// while `rhs` is evaluated, since `rhs` may itself be or contain an
// arbitrarily deep expression free to reuse every named cell here.
void STM8Codegen::genCompoundThroughPtr(const ir::Expr &e) {
    emit("ldw", "x, ptr");
    emit("push", "x");  // stash address
    genExpr(*e.rhs);    // ac = rhs
    copy16("dx", "ac");
    emit("pop", "x");
    emit("ldw", "ptr, x");  // ptr = address, restored
    emit("ldw", "x, ptr");
    loadOperand(e.type);  // ac = old
    copy16("bc", "ac");    // bc = old, ac = rhs - genCompoundOp()'s preconditions
    copy16("ac", "dx");
    if (e.yieldOld) copy16("cx", "bc");  // cx = old, held for the final result
    genCompoundOp(e.op, e.type);
    narrowAcTo(e.type);
    emit("ldw", "x, ptr");
    storeOperand(e.type);
    if (e.yieldOld) copy16("ac", "cx");  // ac = old (final result)
}

void STM8Codegen::narrowAcTo(Type to) {
    if (widthOfType(to) != 1) return;
    if (to.isUnsigned) {
        emit("ldi", "a, 0");
        emit("ld", "ac, a");
    } else {
        emit("ld", "a, ac+1");
        genSignFillByte();
        emit("ld", "ac, a");
    }
}

void STM8Codegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned) {
    if (op == "==") return emitEqualityCompare(lhs, rhs, /*wantEqual=*/true);
    if (op == "!=") return emitEqualityCompare(lhs, rhs, /*wantEqual=*/false);
    if (op == "<") return emitLessCompare(lhs, rhs, /*invert=*/false, isUnsigned);
    if (op == ">") return emitLessCompare(rhs, lhs, /*invert=*/false, isUnsigned);
    if (op == "<=") return emitLessCompare(rhs, lhs, /*invert=*/true, isUnsigned);
    if (op == ">=") return emitLessCompare(lhs, rhs, /*invert=*/true, isUnsigned);
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

// CPW x,y after loading both operands sets Z iff they're equal, full 16
// bits at once - no sign handling needed. `lhs` survives evaluating `rhs`
// on the hardware stack rather than an early copy16("bc", ...), since
// `rhs` may itself be or contain a call that reuses `bc`.
void STM8Codegen::emitEqualityCompare(const ir::Expr &lhs, const ir::Expr &rhs, bool wantEqual) {
    genExpr(lhs);
    emit("ldw", "x, ac");
    emit("push", "x");
    genExpr(rhs);  // ac = rhs
    emit("ldw", "y, ac");
    emit("pop", "x");
    emit("cpw", "x, y");
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit(wantEqual ? "jreq" : "jrne", Ltrue);
    emit("ldwi", "x, 0");
    emit("ldw", "ac, x");
    emit("jp", Ldone);
    emitLabel(Ltrue);
    emit("ldwi", "x, 1");
    emit("ldw", "ac, x");
    emitLabel(Ldone);
}

// Signed 16-bit "a < b" via the sign-bit bias trick: flipping just the top
// bit of each operand's high byte maps the signed range onto the unsigned
// one while preserving order, so a plain unsigned CPW+jrult/jruge suffices.
// `a` survives evaluating `b` on the hardware stack rather than an early
// copy16("bc", ...), since `b` may itself be or contain a call that reuses `bc`.
void STM8Codegen::emitLessCompare(const ir::Expr &a, const ir::Expr &b, bool invert, bool isUnsigned) {
    genExpr(a);
    emit("ldw", "x, ac");
    emit("push", "x");
    genExpr(b);  // ac = b
    emit("pop", "x");
    emit("ldw", "bc, x");  // bc = a
    if (!isUnsigned) {
        emit("ld", "a, bc");
        emit("xori", "a, 0x80");
        emit("ld", "bc, a");
        emit("ld", "a, ac");
        emit("xori", "a, 0x80");
        emit("ld", "ac, a");
    }
    emit("ldw", "x, bc");
    emit("ldw", "y, ac");
    emit("cpw", "x, y");
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit(invert ? "jruge" : "jrult", Ltrue);
    emit("ldwi", "x, 0");
    emit("ldw", "ac, x");
    emit("jp", Ldone);
    emitLabel(Ltrue);
    emit("ldwi", "x, 1");
    emit("ldw", "ac, x");
    emitLabel(Ldone);
}

// A wide value (words on the hardware stack, LSW on top) is ORed together
// into `lc` first, since it's true iff any word is nonzero; a narrow value
// is just tnzw-tested directly, leaving the zero/nonzero result in the flags.
void STM8Codegen::testTruthy(const ir::Expr &e) {
    if (!isWide(e.type)) {
        emit("ldw", "x, ac");
        emit("tnzw", "x");
        return;
    }
    int words = wordCountOf(e.type);
    popWideInto("a", words);
    auto bytes = wideByteLabelsLsbFirst("a", words);
    emit("ldi", "a, 0");
    emit("ld", "lc, a");
    for (const auto &b : bytes) {
        emit("ld", "a, " + b);
        emit("or", "a, lc");
        emit("ld", "lc, a");
    }
    emit("ld", "a, lc");
    emit("tnza");
}

void STM8Codegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    genExpr(lhs);
    testTruthy(lhs);
    emit("jreq", Lfalse);
    genExpr(rhs);
    testTruthy(rhs);
    emit("jreq", Lfalse);
    emitLabel(Ltrue);
    emit("ldwi", "x, 1");
    emit("ldw", "ac, x");
    emit("jp", Ldone);
    emitLabel(Lfalse);
    emit("ldwi", "x, 0");
    emit("ldw", "ac, x");
    emitLabel(Ldone);
}

void STM8Codegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    genExpr(lhs);
    testTruthy(lhs);
    emit("jrne", Ltrue);
    genExpr(rhs);
    testTruthy(rhs);
    emit("jrne", Ltrue);
    emitLabel(Lfalse);
    emit("ldwi", "x, 0");
    emit("ldw", "ac, x");
    emit("jp", Ldone);
    emitLabel(Ltrue);
    emit("ldwi", "x, 1");
    emit("ldw", "ac, x");
    emitLabel(Ldone);
}

void STM8Codegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    testTruthy(rhs);
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit("jreq", Ltrue);
    emit("ldwi", "x, 0");
    emit("ldw", "ac, x");
    emit("jp", Ldone);
    emitLabel(Ltrue);
    emit("ldwi", "x, 1");
    emit("ldw", "ac, x");
    emitLabel(Ldone);
}

std::optional<std::string> STM8Codegen::assembleInProcess(const std::string &asmText,
                                                           const std::string &format) const {
    if (format == "raw") return assembleSTM8(asmText);
    if (auto kind = fileFormatKindFromName(format)) {
        ObjectFile obj = assembleSTM8Object(asmText);
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

}  // namespace rcc
