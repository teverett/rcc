#include "Z80Codegen.h"

#include <cstdint>
#include <tuple>
#include <stdexcept>

#include "Z80Assembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

// Load/entry address for a whole-program raw binary; must equal
// Z80Assembler.h's kZ80BaseAddress.
constexpr uint32_t kBaseAddress = 0x8000;
// SP is undefined at reset; CALL/RET/PUSH/POP need a real initial value.
constexpr uint32_t kStackTop = 0xFF00;
}  // namespace

void Z80Codegen::declareTemp(const std::string &lbl) { trailer_.push_back(lbl + ": db 0, 0"); }

// A literal element's decimal text, or a symbol element's (dispatch-table
// address-of-function/global) label text.
std::string Z80Codegen::arrayElemText(const ir::ArrayInitElem &e) {
    if (!e.isSymbol) return std::to_string(e.value);
    return e.slot ? label(e.slot) : label(e.func);
}

// `elemType` picks the directive: `db` for char, `dw` for short/int, or
// 2/4 `dw`s (LSW first) for long/long long. A symbol element is always a
// single native-word address, regardless of `elemType`.
void Z80Codegen::declareArray(const std::string &lbl, const std::vector<ir::ArrayInitElem> &values, Type elemType) {
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

void Z80Codegen::copy16(const std::string &dst, const std::string &src) {
    emit("ld", "hl, [" + src + "]");
    emit("ld", "[" + dst + "], hl");
}

void Z80Codegen::loadPair(const std::string &pairReg, const std::string &src) {
    emit("ld", pairReg + ", [" + src + "]");
}

void Z80Codegen::storePair(const std::string &dst, const std::string &pairReg) {
    emit("ld", "[" + dst + "], " + pairReg);
}

// No direct register move exists between HL and IX, only via SP; `ix`
// itself is left untouched, so this is safe to call mid-expression.
void Z80Codegen::loadFrameAddr(const ir::Slot *slot, long extraBytes) {
    emit("push", "ix");
    emit("pop", "hl");
    // Param bytes sit at increasing addresses above `ix`; Local bytes grow
    // down from `ix`, at decreasing addresses as `extraBytes` increases,
    // except a Local array's, which ascend from element 0, its lowest
    // address (see ir::localLowOffset()).
    long off = slot->kind == ir::SlotKind::Param ? (static_cast<long>(slot->frameOffset) + extraBytes)
               : slot->arraySize > 0 ? (-static_cast<long>(ir::localLowOffset(*slot, typeWidths())) + extraBytes)
                                     : (-(static_cast<long>(ir::localOffsetDescendingWide(*slot, typeWidths())) + extraBytes));
    uint16_t off16 = static_cast<uint16_t>(off);
    emit("ld", "de, " + std::to_string(off16));
    emit("add", "hl, de");
}

void Z80Codegen::readWordAt(const std::string &loReg, const std::string &hiReg) {
    emit("ld", loReg + ", m");
    emit("inc", "hl");
    emit("ld", hiReg + ", m");
}

// A Local array's initializer values are stored into this call's own frame
// at the point of declaration, rather than static trailer data like a
// Global array's.
void Z80Codegen::genLocalArrayInit(const ir::Slot *slot) {
    int w = widthOfType(slot->type);
    for (size_t i = 0; i < slot->arrayInit.size(); i++) {
        const ir::ArrayInitElem &elem = slot->arrayInit[i];
        long base = static_cast<long>(i) * w;
        if (w == 1 && !elem.isSymbol) {
            loadFrameAddr(slot, base);
            emit("ld", "a, " + std::to_string(static_cast<int>(static_cast<uint8_t>(elem.value))));
            emit("ld", "m, a");
            continue;
        }
        if (elem.isSymbol || w <= 2) {
            loadFrameAddr(slot, base);
            emit("ld", "de, " + arrayElemText(elem));
            emit("ld", "m, e");
            emit("inc", "hl");
            emit("ld", "m, d");
            continue;
        }
        // A wide element: 2 or 4 words, least significant first.
        auto uv = static_cast<unsigned long>(elem.value);
        int words = w / 2;
        for (int j = 0; j < words; j++) {
            loadFrameAddr(slot, base + 2 * j);
            uint16_t wv = static_cast<uint16_t>((uv >> (16 * j)) & 0xFFFF);
            emit("ld", "de, " + std::to_string(wv));
            emit("ld", "m, e");
            emit("inc", "hl");
            emit("ld", "m, d");
        }
    }
}

void Z80Codegen::storeAcIndirect() {
    // Loads ac's two bytes by name (not through HL) and stores each via (HL), so HL survives.
    emit("ld", "a, [ac]");
    emit("ld", "m, a");
    emit("inc", "hl");
    emit("ld", "a, [ac+1]");
    emit("ld", "m, a");
}

// RL/RR target L/H directly, so this needs only two instructions vs. the
// 8080's six (RAL/RAR only touch the accumulator).
void Z80Codegen::shiftOneBit(const std::string &lbl, bool isLeft) {
    emit("ld", "hl, [" + lbl + "]");
    emit("or", "a");
    if (isLeft) {
        emit("rl", "l");
        emit("rl", "h");
    } else {
        emit("rr", "h");
        emit("rr", "l");
    }
    emit("ld", "[" + lbl + "], hl");
}

int Z80Codegen::widthOfType(Type t) const { return widthOf(t, typeWidths()); }
bool Z80Codegen::isWide(Type t) const { return widthOfType(t) > 2; }
int Z80Codegen::wordCountOf(Type t) const { return widthOfType(t) / 2; }
// HL holds the operand's address. `rla` rotates the loaded byte (in L)
// left through carry, leaving carry = the byte's sign bit; `sbc a,a`
// (a - a - carry) then yields exactly 0x00 or 0xFF - the sign-extension
// byte in one branch-free move. An unsigned char just zero-fills H.
void Z80Codegen::loadOperand(Type type) {
    if (widthOfType(type) == 1) {
        emit("ld", "a, m");
        emit("ld", "l, a");
        if (type.isUnsigned) {
            emit("ld", "h, 0");
        } else {
            emit("rla");
            emit("sbc", "a, a");
            emit("ld", "h, a");
        }
        emit("ld", "[ac], hl");
        return;
    }
    readWordAt("e", "d");
    storePair("ac", "de");
}

void Z80Codegen::storeOperand(Type type) {
    if (widthOfType(type) == 1) {
        emit("ld", "a, [ac]");
        emit("ld", "m, a");
        return;
    }
    storeAcIndirect();
}

void Z80Codegen::popWideInto(const char *role, int words) {
    for (int i = 0; i < words; i++) {
        emit("pop", "hl");
        emit("ld", "[" + wideScratch(role, i) + "], hl");
    }
}

void Z80Codegen::pushWideFrom(const char *role, int words) {
    for (int i = words - 1; i >= 0; i--) {
        emit("ld", "hl, [" + wideScratch(role, i) + "]");
        emit("push", "hl");
    }
}

void Z80Codegen::genWideLoad(const ir::Slot *slot, int words) {
    for (int i = words - 1; i >= 0; i--) {
        if (slot->kind == ir::SlotKind::Global) {
            emit("ld", "hl, " + label(slot) + (i ? ("+" + std::to_string(2 * i)) : ""));
        } else {
            loadFrameAddr(slot, 2 * i);
        }
        emit("ld", "c, m");
        emit("inc", "hl");
        emit("ld", "b, m");
        emit("push", "bc");
    }
}

void Z80Codegen::genWideLoadLiteral(long value, int words) {
    auto uv = static_cast<unsigned long>(value);
    for (int i = words - 1; i >= 0; i--) {
        uint16_t w = static_cast<uint16_t>((uv >> (16 * i)) & 0xFFFF);
        emit("ld", "hl, " + std::to_string(w));
        emit("push", "hl");
    }
}

void Z80Codegen::genWideStore(const ir::Slot *slot, int words) {
    for (int i = 0; i < words; i++) {
        emit("pop", "bc");
        if (slot->kind == ir::SlotKind::Global) {
            emit("ld", "hl, " + label(slot) + (i ? ("+" + std::to_string(2 * i)) : ""));
        } else {
            loadFrameAddr(slot, 2 * i);
        }
        emit("ld", "m, c");
        emit("inc", "hl");
        emit("ld", "m, b");
    }
}

// Word 0 is a plain ADD/SBC HL,DE; later words use ADC/SBC HL,DE, which
// already read the real carry flag, so no separate carry-propagation step
// is needed. Operands are popped into "wa"/"wb" scratch cells first since
// there's no SP-relative addressing to operate on the stack in place.
void Z80Codegen::genWideAdd(const ir::Expr &lhs, const ir::Expr &rhs, bool isAdd, int words) {
    genExpr(lhs);
    genExpr(rhs);
    popWideInto("b", words);
    popWideInto("a", words);
    for (int i = 0; i < words; i++) {
        emit("ld", "hl, [" + wideScratch("a", i) + "]");
        emit("ld", "de, [" + wideScratch("b", i) + "]");
        if (i == 0) {
            if (isAdd) {
                emit("add", "hl, de");
            } else {
                emit("or", "a");
                emit("sbc", "hl, de");
            }
        } else {
            emit(isAdd ? "adc" : "sbc", "hl, de");
        }
        emit("ld", "[" + wideScratch("a", i) + "], hl");
    }
    pushWideFrom("a", words);
}

// Two's-complement negate: complement every word, then add 1 into the
// least significant word and let ADC HL,DE (DE=0 above word 0) carry it
// up through the rest.
void Z80Codegen::genWideNegate(int words) {
    popWideInto("a", words);
    for (int i = 0; i < words; i++) {
        emit("ld", "hl, [" + wideScratch("a", i) + "]");
        emit("ld", "a, l");
        emit("cpl");
        emit("ld", "l, a");
        emit("ld", "a, h");
        emit("cpl");
        emit("ld", "h, a");
        emit("ld", "[" + wideScratch("a", i) + "], hl");
    }
    for (int i = 0; i < words; i++) {
        emit("ld", "hl, [" + wideScratch("a", i) + "]");
        emit("ld", "de, " + std::to_string(i == 0 ? 1 : 0));
        emit(i == 0 ? "add" : "adc", "hl, de");
        emit("ld", "[" + wideScratch("a", i) + "], hl");
    }
    pushWideFrom("a", words);
}

void Z80Codegen::genWideBitwise(const ir::Expr &lhs, const ir::Expr &rhs, const std::string &op, int words) {
    genExpr(lhs);
    genExpr(rhs);
    popWideInto("b", words);
    popWideInto("a", words);
    const char *insn = op == "&" ? "and" : op == "|" ? "or" : "xor";
    for (int i = 0; i < words; i++) {
        emit("ld", "hl, [" + wideScratch("a", i) + "]");
        emit("ld", "de, [" + wideScratch("b", i) + "]");
        emit("ld", "a, l");
        emit(insn, "e");
        emit("ld", "l, a");
        emit("ld", "a, h");
        emit(insn, "d");
        emit("ld", "h, a");
        emit("ld", "[" + wideScratch("a", i) + "], hl");
    }
    pushWideFrom("a", words);
}

void Z80Codegen::genWideEqualityCompare(const ir::Expr &lhs, const ir::Expr &rhs, bool wantEqual, int words) {
    genExpr(lhs);
    genExpr(rhs);
    popWideInto("b", words);
    popWideInto("a", words);
    emit("ld", "hl, 0");
    emit("ld", "[wacc], hl");
    for (int i = 0; i < words; i++) {
        emit("ld", "hl, [" + wideScratch("a", i) + "]");
        emit("ld", "de, [" + wideScratch("b", i) + "]");
        emit("ld", "a, l");
        emit("xor", "e");
        emit("ld", "l, a");
        emit("ld", "a, h");
        emit("xor", "d");
        emit("ld", "h, a");
        emit("ld", "de, [wacc]");
        emit("ld", "a, l");
        emit("or", "e");
        emit("ld", "l, a");
        emit("ld", "a, h");
        emit("or", "d");
        emit("ld", "h, a");
        emit("ld", "[wacc], hl");
    }
    emit("ld", "hl, [wacc]");
    emit("ld", "a, l");
    emit("or", "h");
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit("jp", (wantEqual ? std::string("z") : std::string("nz")) + ", " + Ltrue);
    emit("ld", "hl, 0");
    emit("ld", "[ac], hl");
    emit("jp", Ldone);
    emitLabel(Ltrue);
    emit("ld", "hl, 1");
    emit("ld", "[ac], hl");
    emitLabel(Ldone);
}

// Same sign-bias trick as emitLessCompare(), compared most-significant word
// first - only that word needs the signed/unsigned distinction, since once
// two words differ every lower word is a pure unsigned magnitude.
void Z80Codegen::genWideLessCompare(const ir::Expr &a, const ir::Expr &b, bool invert, bool isUnsigned, int words) {
    genExpr(a);
    genExpr(b);
    popWideInto("b", words);
    popWideInto("a", words);
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    for (int i = words - 1; i >= 0; i--) {
        emit("ld", "hl, [" + wideScratch("a", i) + "]");
        emit("ld", "de, [" + wideScratch("b", i) + "]");
        if (i == words - 1 && !isUnsigned) {
            emit("ld", "a, h");
            emit("xor", "0x80");
            emit("ld", "h, a");
            emit("ld", "a, d");
            emit("xor", "0x80");
            emit("ld", "d, a");
        }
        emit("or", "a");
        emit("sbc", "hl, de");
        if (i == 0) {
            emit("jp", (invert ? std::string("nc") : std::string("c")) + ", " + Ltrue);
            emit("jp", Lfalse);
        } else {
            std::string Lcontinue = newLabel();
            emit("jp", "z, " + Lcontinue);
            emit("jp", (invert ? std::string("nc") : std::string("c")) + ", " + Ltrue);
            emit("jp", Lfalse);
            emitLabel(Lcontinue);
        }
    }
    emitLabel(Lfalse);
    emit("ld", "hl, 0");
    emit("ld", "[ac], hl");
    emit("jp", Ldone);
    emitLabel(Ltrue);
    emit("ld", "hl, 1");
    emit("ld", "[ac], hl");
    emitLabel(Ldone);
}

void Z80Codegen::genWideCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned,
                                  int words) {
    if (op == "==") return genWideEqualityCompare(lhs, rhs, /*wantEqual=*/true, words);
    if (op == "!=") return genWideEqualityCompare(lhs, rhs, /*wantEqual=*/false, words);
    if (op == "<") return genWideLessCompare(lhs, rhs, /*invert=*/false, isUnsigned, words);
    if (op == ">") return genWideLessCompare(rhs, lhs, /*invert=*/false, isUnsigned, words);
    if (op == "<=") return genWideLessCompare(rhs, lhs, /*invert=*/true, isUnsigned, words);
    if (op == ">=") return genWideLessCompare(lhs, rhs, /*invert=*/true, isUnsigned, words);
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

// A long/long long shifted by a runtime count, looped one bit at a time
// across every byte, most-significant byte first. No variable-count shift
// exists even for a single word on this chip (see genShift()).
void Z80Codegen::genWideShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned, int words) {
    genExpr(lhs);
    popWideInto("a", words);
    genExpr(rhs);
    emit("ld", "a, [ac]");
    emit("ld", "b, a");
    std::vector<std::string> bytesMsbFirst;  // high byte of the top word .. low byte of word 0
    for (int i = words - 1; i >= 0; i--) {
        bytesMsbFirst.push_back(wideScratch("a", i) + "+1");
        bytesMsbFirst.push_back(wideScratch("a", i));
    }
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("ld", "a, b");
    emit("or", "a");
    emit("jp", "z, " + Ldone);
    if (isLeft) {
        emit("or", "a");  // clear carry - '<<' always shifts in a 0 bit, regardless of signedness
        for (size_t idx = bytesMsbFirst.size(); idx-- > 0;) {
            emit("ld", "a, [" + bytesMsbFirst[idx] + "]");
            emit("rl", "a");
            emit("ld", "[" + bytesMsbFirst[idx] + "], a");
        }
    } else {
        for (size_t idx = 0; idx < bytesMsbFirst.size(); idx++) {
            // The first byte is the value's top byte: sra/srl read its own
            // top bit directly (signed keeps it, unsigned zero-fills), no
            // "clear carry" step needed like '<<'. Every byte after rotates
            // in the carry that just set.
            const char *instr = idx == 0 ? (isUnsigned ? "srl" : "sra") : "rr";
            emit("ld", "a, [" + bytesMsbFirst[idx] + "]");
            emit(instr, "a");
            emit("ld", "[" + bytesMsbFirst[idx] + "], a");
        }
    }
    emit("dec", "b");
    emit("jp", Lloop);
    emitLabel(Ldone);
    pushWideFrom("a", words);
}

// A long/long long multiply/divide needs a bit-serial multi-word routine,
// and this target has no simulator to verify a hand-written one against -
// failing loudly here is safer than shipping an unverifiable one.
void Z80Codegen::genWideMul(const ir::Expr &lhs, const ir::Expr &rhs, int) {
    err(lhs.line, "multiplying two 'long'/'long long' values isn't supported on this target yet - "
                   "see Z80Codegen.cpp's own genWideMul() comment");
    (void)rhs;
}

void Z80Codegen::genWideDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool, int) {
    err(lhs.line, "dividing two 'long'/'long long' values isn't supported on this target yet - "
                   "see Z80Codegen.cpp's own genWideDivMod() comment");
    (void)rhs;
}

// Every arithmetic operand narrower than int is already promoted by
// IRBuilder, so the real work is crossing the native-word/wide boundary
// and re-narrowing a char result.
void Z80Codegen::genConvert(const ir::Expr &e) {
    Type from = e.lhs->type;
    Type to = e.type;
    bool fromWide = isWide(from);
    bool toWide = isWide(to);
    if (!fromWide && !toWide) {
        genExpr(*e.lhs);  // ac
        if (widthOfType(to) == 1) {
            emit("ld", "hl, [ac]");
            if (to.isUnsigned) {
                emit("ld", "h, 0");
            } else {
                emit("ld", "a, l");
                emit("rla");
                emit("sbc", "a, a");
                emit("ld", "h, a");
            }
            emit("ld", "[ac], hl");
        }
        return;
    }
    if (fromWide && !toWide) {
        // No SP-relative addressing, so every popped word must land
        // somewhere real first; only the least significant one is kept.
        int words = wordCountOf(from);
        genExpr(*e.lhs);
        popWideInto("a", words);
        emit("ld", "hl, [" + wideScratch("a", 0) + "]");
        emit("ld", "[ac], hl");
        if (widthOfType(to) == 1) {
            if (to.isUnsigned) {
                emit("ld", "h, 0");
            } else {
                emit("ld", "a, l");
                emit("rla");
                emit("sbc", "a, a");
                emit("ld", "h, a");
            }
            emit("ld", "[ac], hl");
        }
        return;
    }
    if (!fromWide && toWide) {
        // Every extension word must land below word 0 (LSW stays on top),
        // so all words are written into "wa" first and pushed via
        // pushWideFrom(), rather than pushing word 0 directly.
        int toWords = wordCountOf(to);
        genExpr(*e.lhs);  // ac already holds the native-width value, correctly sign/zero-extended to 16 bits
        emit("ld", "hl, [ac]");
        emit("ld", "[" + wideScratch("a", 0) + "], hl");
        for (int i = 1; i < toWords; i++) {
            if (from.isUnsigned) {
                emit("ld", "hl, 0");
            } else {
                emit("ld", "a, h");
                emit("rla");
                emit("sbc", "a, a");
                emit("ld", "h, a");
                emit("ld", "l, a");
            }
            emit("ld", "[" + wideScratch("a", i) + "], hl");
        }
        pushWideFrom("a", toWords);
        return;
    }
    // fromWide && toWide: pad/truncate word count, preserving sign. Words
    // are popped into "wa"; any new sign-extension words are computed from
    // the old most-significant word, then exactly toWords are pushed back.
    int fromWords = wordCountOf(from);
    int toWords = wordCountOf(to);
    genExpr(*e.lhs);
    popWideInto("a", fromWords);
    if (toWords > fromWords) {
        emit("ld", "hl, [" + wideScratch("a", fromWords - 1) + "]");
        if (from.isUnsigned) {
            emit("ld", "hl, 0");
        } else {
            emit("ld", "a, h");
            emit("rla");
            emit("sbc", "a, a");
            emit("ld", "h, a");
            emit("ld", "l, a");
        }
        for (int i = fromWords; i < toWords; i++) emit("ld", "[" + wideScratch("a", i) + "], hl");
    }
    pushWideFrom("a", toWords);
}

std::string Z80Codegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    // Real separate compilation means `prog` may not define `main` at all.
    // In `objectMode`, no object file emits this entry sequence, even one
    // that defines `main` - Z80Linker.cpp synthesizes exactly one once
    // every object is merged. An object file's own base is `org 0`;
    // placing it is the linker's job.
    emitRaw("org " + std::to_string(objectMode ? 0 : kBaseAddress));
    emitRaw("");
    if (!objectMode) {
        emitLabel("_start");
        emit("ld", "sp, " + std::to_string(kStackTop));
        emit("call", label(mainFn));
        // The program's result sits in `ac` once execution reaches `halt` -
        // this extra load also leaves it in HL for a quick register-dump look.
        emit("ld", "hl, [ac]");
        emitLabel("halt");
        emit("jp", "halt");
        emitRaw("");
    }

    // The fixed pseudo-registers are shared across the whole linked
    // program, not redeclared per-object: in whole-program mode they're
    // declared once, here; in object mode this file never declares them,
    // so every reference becomes a relocation resolved against
    // Z80Linker.cpp's synthesized entry object.
    if (!objectMode) {
        declareTemp("ac");
        declareTemp("_bc");
        declareTemp("cx");
        declareTemp("dx");
        declareTemp("ptr");
        // Wide (long/long long) scratch cells.
        for (int i = 0; i < 4; i++) declareTemp(wideScratch("a", i));
        for (int i = 0; i < 4; i++) declareTemp(wideScratch("b", i));
        for (int i = 0; i < 4; i++) declareTemp(wideScratch("ret", i));
        declareTemp("wacc");
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
        // An external global isn't defined in this file; it must never get
        // local storage here, only be referenced by name so the assembler
        // defers it as a relocation.
        if (slot->isExternal) continue;
        size_t gTrailerStart = trailer_.size();
        const std::string lbl = label(slot.get());
        if (debugMapEnabled_) debugSymbols_.push_back({lbl, /*isFunc=*/false});
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

// Single-file-only - the whole generated text is attributed to whichever
// file defines `main`.
std::map<std::string, std::string> Z80Codegen::perFileAsm() const { return perFileAsm_; }

// Every call gets its own real frame, built around `ix`. On entry SP
// points at the return address CALL just pushed; `push ix` saves the
// caller's frame pointer, then `ix` is set to that SP (no direct `ld
// ix,sp` exists, so this zeroes ix and adds SP in). From `ix`: `ix+0` is
// the saved caller ix, `ix+2` the return address, `ix+4+2*i` argument i
// (pushed by the caller in reverse order, so argument 0 is closest to the
// return address). `frameReserveBytes_` single-byte `dec sp`s then carve
// out this function's Locals below `ix` (no instruction moves a computed
// value into SP directly). Every Return undoes this with matching `inc
// sp`s before restoring `ix` and returning.
void Z80Codegen::genFunction(const ir::FunctionBody &fn) {
    lastDebugLine_ = 0;
    if (debugMapEnabled_) debugSymbols_.push_back({label(fn.func), /*isFunc=*/true});
    emitLabel(label(fn.func));
    emit("push", "ix");
    emit("ld", "ix, 0");
    emit("add", "ix, sp");
    // `frameSize` is rounded up to the native int width as a whole, which
    // can fall up to `intUnits-1` bytes short of the deepest byte an
    // individual Local actually needs (e.g. an int then two trailing chars
    // needs frameOffset 5 but frameSize 4) - padding by `intUnits-1` always
    // covers that worst case, at the cost of some extra stack space.
    frameReserveBytes_ = fn.func->frameSize + (typeWidths().intUnits - 1);
    for (int i = 0; i < frameReserveBytes_; i++) emit("dec", "sp");

    genStmt(*fn.body);

    for (int i = 0; i < frameReserveBytes_; i++) emit("inc", "sp");
    emit("pop", "ix");
    emit("ret");
    emitRaw("");
}

void Z80Codegen::genStmt(const ir::Stmt &s) {
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
            emit("jp", "z, " + (s.elseStmt ? Lelse : Ldone));
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
            emit("jp", "z, " + Lend);
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
                // A wide value sits on the real stack (LSW on top) after
                // genExpr() - pop it into "wret" first since the stack
                // unwind below is about to move SP past it. genExpr()'s own
                // Call case reads it back out of "wret".
                if (isWide(s.initOrReturnOrExpr->type)) {
                    int words = wordCountOf(s.initOrReturnOrExpr->type);
                    for (int i = 0; i < words; i++) {
                        emit("pop", "hl");
                        emit("ld", "[" + wideScratch("ret", i) + "], hl");
                    }
                }
            }
            for (int i = 0; i < frameReserveBytes_; i++) emit("inc", "sp");
            emit("pop", "ix");
            emit("ret");
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
                // The target address must survive evaluating the
                // initializer, which might itself index an array and reuse
                // `ptr` - so it's saved on the real stack instead.
                loadFrameAddr(s.declSlot);
                emit("push", "hl");
                genExpr(*s.initOrReturnOrExpr);
                emit("pop", "hl");
                storeOperand(s.declSlot->type);
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

void Z80Codegen::genExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit: {
            if (isWide(e.type)) {
                genWideLoadLiteral(e.intValue, wordCountOf(e.type));
                return;
            }
            uint16_t v16 = static_cast<uint16_t>(e.intValue);
            emit("ld", "hl, " + std::to_string(v16));
            emit("ld", "[ac], hl");
            return;
        }
        case ir::ExprKind::Var: {
            if (isWide(e.slot->type)) {
                genWideLoad(e.slot, wordCountOf(e.slot->type));
                return;
            }
            if (e.slot->kind == ir::SlotKind::Global) {
                emit("ld", "hl, " + label(e.slot));
            } else {
                loadFrameAddr(e.slot);
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
                    // `ptr` is shared scratch - evaluating rhs might itself
                    // index another array, clobbering it - so the address is
                    // saved on the real stack across that call; rhs's pushed
                    // words land on top, so they're popped into "wb" before
                    // the address underneath is restored.
                    emit("ld", "hl, [ptr]");
                    emit("push", "hl");
                    genExpr(*e.rhs);
                    popWideInto("b", words);
                    emit("pop", "hl");
                    emit("ld", "[ptr], hl");
                    genWideStoreAtPtr("b", words);
                    return;
                }
                emit("ld", "hl, [ptr]");
                emit("push", "hl");
                genExpr(*e.rhs);
                emit("pop", "hl");
                storeOperand(e.slot->type);
                return;
            }
            if (isWide(e.slot->type)) {
                genExpr(*e.rhs);  // pushes `wordCountOf()` words
                genWideStore(e.slot, wordCountOf(e.slot->type));
                return;
            }
            if (e.slot->kind == ir::SlotKind::Global) {
                genExpr(*e.rhs);
                emit("ld", "hl, " + label(e.slot));
                storeOperand(e.slot->type);
                return;
            }
            // `ptr` isn't safe to park the address in across evaluating
            // `rhs` (rhs might reuse `ptr` itself indexing an array) - the
            // real stack is, same as the indexed-Assign case above.
            loadFrameAddr(e.slot);
            emit("push", "hl");
            genExpr(*e.rhs);
            emit("pop", "hl");
            storeOperand(e.slot->type);
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            if (e.lhs) {  // slot[idx] op= rhs (including slot[idx]++/--)
                genIndexAddr(e.slot, *e.lhs);  // ptr = address
            } else if (e.slot->kind == ir::SlotKind::Global) {
                emit("ld", "hl, " + label(e.slot));
                emit("ld", "[ptr], hl");
            } else {
                loadFrameAddr(e.slot);
                emit("ld", "[ptr], hl");
            }
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
            testTruthy(*e.lhs);
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            emit("jp", "z, " + Lelse);
            genExpr(*e.rhs);
            emit("jp", Ldone);
            emitLabel(Lelse);
            genExpr(*e.elseExpr);
            emitLabel(Ldone);
            return;
        }
        case ir::ExprKind::DerefAssign: {
            // Stores through e.type's own width - the pointee type, or
            // plain native-word width for untyped-int-as-address. A wide
            // pointee mirrors the indexed Assign's wide branch above.
            genExpr(*e.lhs);  // ac = pointer value
            copy16("ptr", "ac");
            if (isWide(e.type)) {
                int words = wordCountOf(e.type);
                emit("ld", "hl, [ptr]");
                emit("push", "hl");
                genExpr(*e.rhs);
                popWideInto("b", words);
                emit("pop", "hl");
                emit("ld", "[ptr], hl");
                genWideStoreAtPtr("b", words);
                return;
            }
            emit("ld", "hl, [ptr]");
            emit("push", "hl");
            genExpr(*e.rhs);
            emit("pop", "hl");
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
                emit("ld", "hl, [ac]");
                emit("xor", "a");
                emit("sub", "l");
                emit("ld", "l, a");
                emit("ld", "a, 0");
                emit("sbc", "a, h");
                emit("ld", "h, a");
                emit("ld", "[ac], hl");
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                // Loads at e.type's own width - see DerefAssign's
                // identical comment above.
                genExpr(*e.rhs);  // ac = pointer value
                copy16("ptr", "ac");
                if (isWide(e.type)) {
                    genWideLoadAtPtr(wordCountOf(e.type));
                    return;
                }
                emit("ld", "hl, [ptr]");
                loadOperand(e.type);
            } else if (e.op == "~") {
                if (wide) {
                    genExpr(*e.rhs);
                    int words = wordCountOf(e.rhs->type);
                    popWideInto("a", words);
                    for (int i = 0; i < words; i++) {
                        emit("ld", "hl, [" + wideScratch("a", i) + "]");
                        emit("ld", "a, l");
                        emit("cpl");
                        emit("ld", "l, a");
                        emit("ld", "a, h");
                        emit("cpl");
                        emit("ld", "h, a");
                        emit("ld", "[" + wideScratch("a", i) + "], hl");
                    }
                    pushWideFrom("a", words);
                    return;
                }
                genExpr(*e.rhs);
                emit("ld", "hl, [ac]");
                emit("ld", "a, l");
                emit("cpl");
                emit("ld", "l, a");
                emit("ld", "a, h");
                emit("cpl");
                emit("ld", "h, a");
                emit("ld", "[ac], hl");
            } else {  // "&"
                genAddrOf(*e.rhs);
            }
            return;
        }
        case ir::ExprKind::Call: {
            // Arguments are pushed right-to-left so argument 0 ends up
            // closest to the return address (see genFunction()). The callee
            // never cleans these up (no `ret n` on this chip), so the caller
            // pops them back off after the call returns.
            int totalWords = 0;
            for (size_t i = e.args.size(); i-- > 0;) {
                genExpr(*e.args[i]);
                if (isWide(e.args[i]->type)) {
                    totalWords += wordCountOf(e.args[i]->type);
                } else {
                    emit("ld", "hl, [ac]");
                    emit("push", "hl");
                    totalWords += 1;
                }
            }
            emit("call", label(e.func));
            for (int i = 0; i < totalWords; i++) emit("pop", "hl");
            if (isWide(e.type)) {
                // The callee left its wide return value in "wret" - push it
                // back as this expression's result, MSW first so LSW ends up on top.
                int words = wordCountOf(e.type);
                for (int i = words - 1; i >= 0; i--) {
                    emit("ld", "hl, [" + wideScratch("ret", i) + "]");
                    emit("push", "hl");
                }
            }
            return;
        }
        case ir::ExprKind::CallPtr: {
            // No indirect-CALL instruction exists, only `JP (HL)` ("jp
            // [hl]"), identical to the 8080's PCHL. A real CALL is emulated
            // by pushing the return address, then jumping through HL.
            genExpr(*e.lhs);  // ac = the callee's own entry address
            std::string Lret = newLabel();
            emit("ld", "hl, [ac]");  // HL = callee's address - jp [hl]'s own implicit target
            emit("ld", "de, " + Lret);
            emit("push", "de");
            emit("jp", "[hl]");
            emitLabel(Lret);
            return;
        }
        case ir::ExprKind::Binary: {
            genBinary(e);
            return;
        }
        case ir::ExprKind::Intrinsic: {
            // Real interrupt enable/disable, same mnemonics as the 8080's EI/DI.
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

// Leaves slot[idxExpr]'s effective address in `ptr`. `idxExpr` is always
// evaluated first, alone. `slot->arraySize > 0` means the base is a
// compile-time label; otherwise `slot` is a pointer variable and the base
// is read from its stored value via a single real instruction, unlike the
// 8080's stack round-trip.
void Z80Codegen::genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
    // The product computed below already scales by the element's full width, so this is valid
    // for a wide element too; only callers reading/writing through `ptr` need their own
    // wide-aware path.
    genExpr(idxExpr);  // ac = index
    // ptr = index * widthOfType(slot->type): the width is a compile-time constant, so this is a
    // fully unrolled shift-and-add multiply (double `_bc`, add into `ac` for each set bit of the
    // width) rather than a runtime loop - genMul()'s own bit-serial loop exists because *both*
    // operands there are only known at runtime; one side here never is. Iterating over the
    // width's actual set bits (rather than a fixed doubling count derived from log2(width))
    // keeps this correct for any element width, not just 1/2/4/8 bytes.
    emit("ld", "hl, [ac]");
    emit("ld", "[_bc], hl");  // _bc = index (current doubling value)
    emit("ld", "hl, 0");
    emit("ld", "[ac], hl");  // ac = running product, starts at 0
    for (int w = widthOfType(slot->type); w > 0; w >>= 1) {
        if (w & 1) {
            emit("ld", "hl, [ac]");
            emit("ld", "de, [_bc]");
            emit("add", "hl, de");
            emit("ld", "[ac], hl");
        }
        if (w >> 1) {
            emit("ld", "hl, [_bc]");
            emit("add", "hl, hl");
            emit("ld", "[_bc], hl");
        }
    }
    emit("ld", "hl, [ac]");
    emit("ld", "[ptr], hl");
    if (slot->arraySize > 0) {
        if (slot->kind == ir::SlotKind::Global) {
            emit("ld", "de, " + label(slot));
            emit("ld", "hl, [ptr]");
            emit("add", "hl, de");
            emit("ld", "[ptr], hl");
        } else {
            loadFrameAddr(slot);      // hl = array's element 0, its lowest address
            emit("ld", "de, [ptr]");  // de = scaled index, saved above
            emit("add", "hl, de");
            emit("ld", "[ptr], hl");
        }
        return;
    }
    if (slot->kind == ir::SlotKind::Global) {
        loadPair("de", label(slot));
    } else {
        loadFrameAddr(slot);
        readWordAt("e", "d");
    }
    emit("ld", "hl, [ptr]");
    emit("add", "hl, de");
    emit("ld", "[ptr], hl");
}

// `ptr` already holds the element's address. Reads `words` 16-bit words,
// most significant first, pushing each (LSW ends up on top).
void Z80Codegen::genWideLoadAtPtr(int words) {
    for (int i = words - 1; i >= 0; i--) {
        emit("ld", "hl, [ptr]");
        if (i > 0) {
            emit("ld", "bc, " + std::to_string(2 * i));
            emit("add", "hl, bc");
        }
        emit("ld", "c, m");
        emit("inc", "hl");
        emit("ld", "b, m");
        emit("push", "bc");
    }
}

// Mirror image for a write. DE (not BC) carries the address offset here,
// since BC holds the value being stored.
void Z80Codegen::genWideStoreAtPtr(const char *role, int words) {
    for (int i = 0; i < words; i++) {
        emit("ld", "hl, [ptr]");
        if (i > 0) {
            emit("ld", "de, " + std::to_string(2 * i));
            emit("add", "hl, de");
        }
        emit("ld", "bc, [" + wideScratch(role, i) + "]");
        emit("ld", "m, c");
        emit("inc", "hl");
        emit("ld", "m, b");
    }
}

void Z80Codegen::genIndex(const ir::Expr &e) {
    genIndexAddr(e.slot, *e.lhs);  // ptr = element address
    if (isWide(e.slot->type)) {
        genWideLoadAtPtr(wordCountOf(e.slot->type));
        return;
    }
    emit("ld", "hl, [ptr]");
    loadOperand(e.slot->type);
}

// Entry: HL = old value, DE = rhs value (both already at `type`'s
// width/signedness). `+`/`-`/`&`/`|`/`^` reuse genBinary()'s own HL/DE
// tail; `*`/`/`/`%`/`<<`/`>>` seed `_bc`/`dx` (or `_bc`/`b`) and reuse
// genMul()'s/genDivMod()'s/genShift()'s own loop bodies.
void Z80Codegen::genCompoundOp(const std::string &op, Type type) {
    bool isUnsigned = type.isUnsigned;
    if (op == "+") {
        emit("add", "hl, de");
        emit("ld", "[ac], hl");
        return;
    }
    if (op == "-") {
        emit("or", "a");
        emit("sbc", "hl, de");
        emit("ld", "[ac], hl");
        return;
    }
    if (op == "&" || op == "|" || op == "^") {
        const char *insn = (op == "&") ? "and" : (op == "|") ? "or" : "xor";
        emit("ld", "a, l");
        emit(insn, "e");
        emit("ld", "l, a");
        emit("ld", "a, h");
        emit(insn, "d");
        emit("ld", "h, a");
        emit("ld", "[ac], hl");
        return;
    }
    if (op == "*") {
        emit("ld", "[_bc], hl");  // bc = old (multiplicand)
        emit("ld", "[dx], de");   // dx = rhs (multiplier)
        emit("ld", "hl, 0");
        emit("ld", "[ac], hl");
        emit("ld", "b, 16");
        std::string Lloop = newLabel(), Ldone = newLabel(), Lskipadd = newLabel();
        emitLabel(Lloop);
        emit("ld", "a, b");
        emit("or", "a");
        emit("jp", "z, " + Ldone);
        emit("ld", "a, [dx]");
        emit("and", "1");
        emit("jp", "z, " + Lskipadd);
        emit("ld", "hl, [ac]");
        emit("ld", "de, [_bc]");
        emit("add", "hl, de");
        emit("ld", "[ac], hl");
        emitLabel(Lskipadd);
        shiftOneBit("_bc", /*isLeft=*/true);
        shiftOneBit("dx", /*isLeft=*/false);
        emit("dec", "b");
        emit("jp", Lloop);
        emitLabel(Ldone);
        return;
    }
    if (op == "<<" || op == ">>") {
        emit("ld", "[_bc], hl");  // bc = old (value)
        emit("ld", "a, e");
        emit("ld", "b, a");  // b = shift count (low byte of rhs)
        std::string Lloop = newLabel(), Ldone = newLabel();
        emitLabel(Lloop);
        emit("ld", "a, b");
        emit("or", "a");
        emit("jp", "z, " + Ldone);
        if (op == "<<" || isUnsigned) {
            shiftOneBit("_bc", /*isLeft=*/op == "<<");
        } else {
            emit("ld", "hl, [_bc]");
            emit("ld", "a, h");
            emit("sra", "a");
            emit("ld", "h, a");
            emit("rr", "l");
            emit("ld", "[_bc], hl");
        }
        emit("dec", "b");
        emit("jp", Lloop);
        emitLabel(Ldone);
        copy16("ac", "_bc");
        return;
    }
    // "/" or "%"
    emit("ld", "[dx], hl");   // dx = old (dividend)
    emit("ld", "[_bc], de");  // bc = rhs (divisor)
    emit("ld", "hl, 0");
    emit("ld", "[ac], hl");
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("ld", "hl, [dx]");
    emit("ld", "de, [_bc]");
    emit("or", "a");
    emit("sbc", "hl, de");
    emit("jp", "c, " + Ldone);
    emit("ld", "[dx], hl");
    emit("ld", "hl, [ac]");
    emit("inc", "hl");
    emit("ld", "[ac], hl");
    emit("jp", Lloop);
    emitLabel(Ldone);
    if (op == "%") copy16("ac", "dx");
}

// `ptr` must already hold the resolved address. The address is shuttled
// through the real stack across evaluating `rhs` (ptr is a single shared
// cell, and rhs's own evaluation may recurse into something that reuses
// it). `cx` stashes `yieldOld`'s value afterward.
void Z80Codegen::genCompoundThroughPtr(const ir::Expr &e) {
    emit("ld", "hl, [ptr]");
    loadOperand(e.type);  // ac = old
    emit("ld", "hl, [ptr]");
    emit("push", "hl");  // stash address
    emit("ld", "hl, [ac]");
    emit("push", "hl");  // stash old (ac is about to be overwritten by rhs)
    genExpr(*e.rhs);      // ac = rhs
    emit("ld", "de, [ac]");
    emit("pop", "hl");  // hl = old
    if (e.yieldOld) {
        emit("push", "hl");  // re-stash old for the final result
    }
    genCompoundOp(e.op, e.type);  // ac = new (raw)
    narrowAcTo(e.type);
    if (e.yieldOld) {
        emit("pop", "hl");  // hl = old
        emit("ld", "[cx], hl");
        emit("pop", "hl");  // hl = address
        storeOperand(e.type);  // stores ac(=new) through hl
        emit("ld", "hl, [cx]");
        emit("ld", "[ac], hl");  // ac = old (final result)
    } else {
        emit("pop", "hl");  // hl = address
        storeOperand(e.type);  // ac already = new
    }
}

// No-op unless `to` is a `char`.
void Z80Codegen::narrowAcTo(Type to) {
    if (widthOfType(to) != 1) return;
    emit("ld", "hl, [ac]");
    if (to.isUnsigned) {
        emit("ld", "h, 0");
    } else {
        emit("ld", "a, l");
        emit("rla");
        emit("sbc", "a, a");
        emit("ld", "h, a");
    }
    emit("ld", "[ac], hl");
}

void Z80Codegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        if (operand.slot->kind == ir::SlotKind::Global) {
            emit("ld", "hl, " + label(operand.slot));
            emit("ld", "[ac], hl");
        } else {
            loadFrameAddr(operand.slot);
            emit("ld", "[ac], hl");
        }
        return;
    }
    if (operand.kind == ir::ExprKind::Index) {
        genIndexAddr(operand.slot, *operand.lhs);
        copy16("ac", "ptr");
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        emit("ld", "hl, " + label(operand.func));
        emit("ld", "[ac], hl");
        return;
    }
    err(operand.line, "internal: unhandled addressable-operand kind");
}

// Saves the left operand across evaluating the right one on the real
// stack (push hl/pop de), never in a named cell: the right operand's own
// genExpr() might recurse into genMul()/genDivMod()/genShift(), which
// share bc/cx/dx.
void Z80Codegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    // Operands share one common type (usual arithmetic conversions), except
    // shift (only lhs's type matters) and &&/|| (width-independent).
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
        emit("ld", "hl, [ac]");
        emit("push", "hl");
        genExpr(*e.rhs);
        emit("ld", "de, [ac]");
        emit("pop", "hl");  // HL = lhs, DE = rhs
        if (op == "+") {
            emit("add", "hl, de");
        } else {
            emit("or", "a");
            emit("sbc", "hl, de");  // HL = lhs - rhs (real 16-bit subtract-with-carry)
        }
        emit("ld", "[ac], hl");
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
        emit("ld", "hl, [ac]");
        emit("push", "hl");
        genExpr(*e.rhs);
        emit("ld", "de, [ac]");
        emit("pop", "hl");  // HL = lhs, DE = rhs
        const char *insn = (op == "&") ? "and" : (op == "|") ? "or" : "xor";
        emit("ld", "a, l");
        emit(insn, "e");
        emit("ld", "l, a");
        emit("ld", "a, h");
        emit(insn, "d");
        emit("ld", "h, a");
        emit("ld", "[ac], hl");
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

// No variable-count shift instruction - loops `rhs` times via
// shiftOneBit(). A signed right shift refills the vacated top bit with the
// sign bit (arithmetic) instead of 0 (logical), via a sign-copy into the
// top byte before an `rr`/`rr` chain.
void Z80Codegen::genShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned) {
    genExpr(lhs);  // ac = value to shift
    emit("ld", "hl, [ac]");
    emit("push", "hl");  // saved on the real stack across evaluating the count
    genExpr(rhs);         // ac = shift count
    emit("ld", "a, [ac]");
    emit("ld", "b, a");  // b = shift count (always small; low byte is enough)
    emit("pop", "hl");
    emit("ld", "[_bc], hl");  // bc = value (restored only now)
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("ld", "a, b");
    emit("or", "a");
    emit("jp", "z, " + Ldone);
    if (isLeft || isUnsigned) {
        shiftOneBit("_bc", isLeft);
    } else {
        emit("ld", "hl, [_bc]");
        emit("ld", "a, h");
        emit("sra", "a");  // top byte: arithmetic (sign-preserving)
        emit("ld", "h, a");
        emit("rr", "l");  // low byte: carry (the bit sra shifted out) rolls in
        emit("ld", "[_bc], hl");
    }
    emit("dec", "b");
    emit("jp", Lloop);
    emitLabel(Ldone);
    copy16("ac", "_bc");
}

// Shift-and-add multiply (no hardware multiply): bc doubles each
// iteration, dx halves, ac accumulates. Only verified against
// non-negative operands.
void Z80Codegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);  // ac = multiplicand
    emit("ld", "hl, [ac]");
    emit("push", "hl");  // saved across evaluating the multiplier
    genExpr(rhs);
    emit("pop", "hl");
    emit("ld", "[_bc], hl");  // bc = multiplicand (restored only now)
    copy16("dx", "ac");       // dx = multiplier
    emit("ld", "hl, 0");
    emit("ld", "[ac], hl");
    emit("ld", "b, 16");
    std::string Lloop = newLabel(), Ldone = newLabel(), Lskipadd = newLabel();
    emitLabel(Lloop);
    emit("ld", "a, b");
    emit("or", "a");
    emit("jp", "z, " + Ldone);
    emit("ld", "a, [dx]");
    emit("and", "1");
    emit("jp", "z, " + Lskipadd);
    emit("ld", "hl, [ac]");
    emit("ld", "de, [_bc]");
    emit("add", "hl, de");
    emit("ld", "[ac], hl");
    emitLabel(Lskipadd);
    shiftOneBit("_bc", /*isLeft=*/true);
    shiftOneBit("dx", /*isLeft=*/false);
    emit("dec", "b");
    emit("jp", Lloop);
    emitLabel(Ldone);
}

// Repeated subtraction (no hardware divide): dx is the running remainder,
// bc the fixed divisor, ac the quotient. SBC HL,DE both tests and commits
// the subtraction in one instruction. The core loop only handles
// non-negative operands, so a signed operand is divided via its absolute
// value, with `ptr`'s low/high byte holding the original signs while it
// runs; the quotient's sign is restored (negative iff the signs differed),
// and the remainder always takes the dividend's own sign.
void Z80Codegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);  // ac = dividend
    emit("ld", "hl, [ac]");
    emit("push", "hl");  // saved across evaluating the divisor
    genExpr(rhs);
    emit("pop", "hl");
    emit("ld", "[dx], hl");  // dx = dividend (restored only now)
    copy16("_bc", "ac");     // bc = divisor
    auto isNeg = [&](const char *cell, const char *dst) {
        emit("ld", "a, [" + std::string(cell) + "+1]");
        emit("and", "128");
        std::string Lnonneg = newLabel(), Ldone = newLabel();
        emit("jp", "z, " + Lnonneg);
        emit("ld", "a, 1");
        emit("ld", "[" + std::string(dst) + "], a");
        emit("jp", Ldone);
        emitLabel(Lnonneg);
        emit("ld", "a, 0");
        emit("ld", "[" + std::string(dst) + "], a");
        emitLabel(Ldone);
    };
    auto negate16 = [&](const char *cell) {
        emit("ld", "hl, [" + std::string(cell) + "]");
        emit("xor", "a");
        emit("sub", "l");
        emit("ld", "l, a");
        emit("ld", "a, 0");
        emit("sbc", "a, h");
        emit("ld", "h, a");
        emit("ld", "[" + std::string(cell) + "], hl");
    };
    auto negateIf = [&](const char *flagCell, const char *cell) {
        emit("ld", "a, [" + std::string(flagCell) + "]");
        std::string Lskip = newLabel();
        emit("or", "a");
        emit("jp", "z, " + Lskip);
        negate16(cell);
        emitLabel(Lskip);
    };
    if (!isUnsigned) {
        isNeg("dx", "ptr");
        isNeg("_bc", "ptr+1");
        negateIf("ptr", "dx");
        negateIf("ptr+1", "_bc");
    }
    // Non-restoring division, with divisor/quotient pinned in registers for the loop's whole
    // run (HL = running remainder, DE = divisor, BC = quotient) - no memory access at all
    // inside the loop itself, unlike the load-from-memory-every-iteration version this
    // replaced (same fix as I8080Codegen::genDivMod() - see that function's own comment and
    // docs/known-bugs.md for the 8080 case this was originally found on). Each round
    // speculatively does HL -= DE; a final borrow means that went negative, so it's undone
    // (`add hl, de`) and the loop stops; otherwise it's kept and the quotient (BC) counts it.
    // This cuts the common-case iteration from 10 instructions (two 16-bit memory loads plus a
    // memory round trip for the quotient) down to 5 (`or a`/`sbc hl,de`/`jp c`/`inc bc`/`jp`).
    emit("ld", "de, [_bc]");  // de = divisor (fixed for the loop)
    emit("ld", "hl, [dx]");   // hl = running remainder
    emit("ld", "bc, 0");      // bc = quotient = 0
    std::string Lloop = newLabel(), Lrestore = newLabel();
    emitLabel(Lloop);
    emit("or", "a");
    emit("sbc", "hl, de");
    emit("jp", "c, " + Lrestore);
    emit("inc", "bc");
    emit("jp", Lloop);
    emitLabel(Lrestore);
    emit("add", "hl, de");  // undo: hl was about to go negative, add the divisor back
    emit("ld", "[dx], hl");  // dx = remainder
    emit("ld", "[ac], bc");  // ac = quotient
    if (!isUnsigned) {
        // Quotient is negative iff the two original signs differed; the
        // remainder keeps the dividend's sign.
        emit("ld", "a, [ptr]");
        emit("ld", "b, a");
        emit("ld", "a, [ptr+1]");
        emit("xor", "b");
        std::string Lskip = newLabel();
        emit("jp", "z, " + Lskip);
        negate16("ac");
        emitLabel(Lskip);
        negateIf("ptr", "dx");
    }
    if (wantRemainder) copy16("ac", "dx");
}

void Z80Codegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned) {
    if (op == "==") return emitEqualityCompare(lhs, rhs, /*wantEqual=*/true);
    if (op == "!=") return emitEqualityCompare(lhs, rhs, /*wantEqual=*/false);
    if (op == "<") return emitLessCompare(lhs, rhs, /*invert=*/false, isUnsigned);
    if (op == ">") return emitLessCompare(rhs, lhs, /*invert=*/false, isUnsigned);
    if (op == "<=") return emitLessCompare(rhs, lhs, /*invert=*/true, isUnsigned);
    if (op == ">=") return emitLessCompare(lhs, rhs, /*invert=*/true, isUnsigned);
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

// `==`/`!=` reduce to one SBC HL,DE: the Z flag directly means lhs == rhs.
void Z80Codegen::emitEqualityCompare(const ir::Expr &lhs, const ir::Expr &rhs, bool wantEqual) {
    genExpr(lhs);
    emit("ld", "hl, [ac]");
    emit("push", "hl");
    genExpr(rhs);
    emit("ld", "de, [ac]");
    emit("pop", "hl");  // HL = lhs, DE = rhs
    emit("or", "a");
    emit("sbc", "hl, de");
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit("jp", (wantEqual ? std::string("z") : std::string("nz")) + ", " + Ltrue);
    emit("ld", "hl, 0");
    emit("ld", "[ac], hl");
    emit("jp", Ldone);
    emitLabel(Ltrue);
    emit("ld", "hl, 1");
    emit("ld", "[ac], hl");
    emitLabel(Ldone);
}

// Signed "a < b" via the sign-bias trick: flipping each operand's
// high-byte sign bit maps the signed range onto the unsigned one, then one
// SBC HL,DE does the rest.
void Z80Codegen::emitLessCompare(const ir::Expr &a, const ir::Expr &b, bool invert, bool isUnsigned) {
    genExpr(a);
    emit("ld", "hl, [ac]");
    emit("push", "hl");
    genExpr(b);
    emit("ld", "de, [ac]");
    emit("pop", "hl");  // HL = a, DE = b
    if (!isUnsigned) {
        emit("ld", "a, h");
        emit("xor", "0x80");
        emit("ld", "h, a");  // H = a's high byte, sign flipped
        emit("ld", "a, d");
        emit("xor", "0x80");
        emit("ld", "d, a");  // D = b's high byte, sign flipped
    }
    emit("or", "a");
    emit("sbc", "hl, de");  // CY = (a < b)
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit("jp", (invert ? std::string("nc") : std::string("c")) + ", " + Ltrue);
    emit("ld", "hl, 0");
    emit("ld", "[ac], hl");
    emit("jp", Ldone);
    emitLabel(Ltrue);
    emit("ld", "hl, 1");
    emit("ld", "[ac], hl");
    emitLabel(Ldone);
}

void Z80Codegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    genExpr(lhs);
    testTruthy(lhs);
    emit("jp", "z, " + Lfalse);
    genExpr(rhs);
    testTruthy(rhs);
    emit("jp", "z, " + Lfalse);
    emitLabel(Ltrue);
    emit("ld", "hl, 1");
    emit("ld", "[ac], hl");
    emit("jp", Ldone);
    emitLabel(Lfalse);
    emit("ld", "hl, 0");
    emit("ld", "[ac], hl");
    emitLabel(Ldone);
}

void Z80Codegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    genExpr(lhs);
    testTruthy(lhs);
    emit("jp", "nz, " + Ltrue);
    genExpr(rhs);
    testTruthy(rhs);
    emit("jp", "nz, " + Ltrue);
    emitLabel(Lfalse);
    emit("ld", "hl, 0");
    emit("ld", "[ac], hl");
    emit("jp", Ldone);
    emitLabel(Ltrue);
    emit("ld", "hl, 1");
    emit("ld", "[ac], hl");
    emitLabel(Ldone);
}

void Z80Codegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    testTruthy(rhs);
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit("jp", "z, " + Ltrue);
    emit("ld", "hl, 0");
    emit("ld", "[ac], hl");
    emit("jp", Ldone);
    emitLabel(Ltrue);
    emit("ld", "hl, 1");
    emit("ld", "[ac], hl");
    emitLabel(Ldone);
}

// Leaves the real Z flag set as `or a`/`or h` would, with any wide stack
// space already released, ready for an immediate `jp z`/`jp nz`.
void Z80Codegen::testTruthy(const ir::Expr &e) {
    if (!isWide(e.type)) {
        emit("ld", "hl, [ac]");
        emit("ld", "a, l");
        emit("or", "h");
        return;
    }
    int words = wordCountOf(e.type);
    popWideInto("a", words);
    emit("ld", "a, 0");
    for (int i = 0; i < words; i++) {
        emit("ld", "hl, [" + wideScratch("a", i) + "]");
        emit("or", "l");
        emit("or", "h");
    }
}

std::optional<std::string> Z80Codegen::assembleInProcess(const std::string &asmText,
                                                           const std::string &format) const {
    if (format == "raw") return assembleZ80(asmText);
    // coff/elf/macho/aout: builds one format-neutral ObjectFile, then the
    // requested format's own FileFormatWriter serializes it.
    if (auto kind = fileFormatKindFromName(format)) {
        ObjectFile obj = assembleZ80Object(asmText);
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

std::optional<std::string> Z80Codegen::debugMap(const std::string &asmText, const std::string &format,
                                                  const std::string &sourcePath) const {
    if (format != "raw") return std::nullopt;
    return assembleZ80DebugMap(asmText, sourcePath, debugSymbols_);
}

std::optional<std::string> Z80Codegen::debugMapPreLink(const std::string &asmText,
                                                    const std::string &sourcePath) const {
    return assembleZ80DebugMap(asmText, sourcePath, debugSymbols_, /*allowExternal=*/true);
}

// Only emits a marker when `debugMapEnabled_` (set via setDebugMapEnabled(),
// before generate() runs) and `line` actually differs from the last one
// emitted - see Pdp7Codegen.cpp's identical emitSourceLine(), which this
// mirrors exactly.
void Z80Codegen::emitSourceLine(int line) {
    if (!debugMapEnabled_ || line == 0 || line == lastDebugLine_) return;
    emitRaw(cat("__line ", line));
    lastDebugLine_ = line;
}

}  // namespace rcc
