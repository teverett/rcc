#include "M6809Codegen.h"

#include <cstdint>
#include <stdexcept>

#include "M6809Assembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

// Load/entry address for a whole-program raw binary; must equal
// M6809Assembler.h's kM6809BaseAddress.
constexpr uint32_t kBaseAddress = 0x3000;
// 0x7F00: matches M6809LinkerFactory.cpp's own entry sequence - leaves the
// stack clear of a stock 64K map's ROM window (conventionally $8000-$FFFF).
constexpr uint32_t kStackTop = 0x7F00;

// True iff `e` is a promoted `unsigned char` operand - an IR
// ExprKind::Convert node (every arithmetic operand narrower than `int` is
// already wrapped in one by IRBuilder) whose own operand's pre-promotion
// type is `unsigned char`. Used by genMul() to pick the real hardware MUL
// fast path (see its own comment).
bool isUnsignedByteOperand(const ir::Expr &e) {
    return e.kind == ir::ExprKind::Convert && e.lhs && e.lhs->type.base == BaseType::Char &&
           e.lhs->type.isUnsigned && e.lhs->type.ptrDepth == 0;
}
}  // namespace

void M6809Codegen::declareTemp(const std::string &lbl) { trailer_.push_back(lbl + ": dw 0"); }

std::string M6809Codegen::arrayElemText(const ir::ArrayInitElem &e) {
    if (!e.isSymbol) return std::to_string(e.value);
    return e.slot ? label(e.slot) : label(e.func);
}

void M6809Codegen::declareArray(const std::string &lbl, const std::vector<ir::ArrayInitElem> &values,
                                 Type elemType) {
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

void M6809Codegen::copy16(const std::string &dst, const std::string &src) {
    emit("ldd", cat("[", src, "]"));
    emit("std", cat("[", dst, "]"));
}

void M6809Codegen::loadAddrX(const std::string &cell) { emit("ldx", cat("[", cell, "]")); }

// `leax <off>,u` - real indexed addressing reaches a Local/Param directly
// off the fixed frame pointer `u`, unlike Z80's push/pop-through-ix dance
// or the 8080's software frame pointer. A Local lives at
// u-(frameOffset+extraBytes) (frame grows down from u); a Param at
// u+frameOffset+extraBytes. A Local array's address is its element 0, its
// lowest, with `extraBytes` counting upward from there.
void M6809Codegen::loadFrameAddr(const ir::Slot *slot, long extraBytes) {
    long off = slot->kind == ir::SlotKind::Param
                   ? (static_cast<long>(slot->frameOffset) + extraBytes)
                   : slot->arraySize > 0
                         ? (-static_cast<long>(ir::localLowOffset(*slot, typeWidths())) + extraBytes)
                         : (-(static_cast<long>(ir::localOffsetDescendingWide(*slot, typeWidths())) + extraBytes));
    emit("leax", cat(off, ",u"));
}

// One bit of a logical shift on the 16-bit cell `lbl`, via D: ASLB sets
// carry from B's own top bit (no need to clear it first - ASL always
// shifts in a 0), then ROLA chains that carry up into A for a left shift;
// the mirror image (LSRA/RORB) does the same chaining down for a right
// shift, carry-in to A's top bit also always 0.
void M6809Codegen::shiftOneBit(const std::string &lbl, bool isLeft) {
    emit("ldd", cat("[", lbl, "]"));
    if (isLeft) {
        emit("aslb");
        emit("rola");
    } else {
        emit("lsra");
        emit("rorb");
    }
    emit("std", cat("[", lbl, "]"));
}

int M6809Codegen::widthOfType(Type t) const { return widthOf(t, typeWidths()); }
bool M6809Codegen::isWide(Type t) const { return widthOfType(t) > 2; }
int M6809Codegen::wordCountOf(Type t) const { return widthOfType(t) / 2; }

// X holds the operand's address. SEX sign-extends B (the loaded byte) into
// A in one instruction for a signed char; an unsigned char just zeroes A.
void M6809Codegen::loadOperand(Type type) {
    if (widthOfType(type) == 1) {
        emit("ldb", "0,x");
        if (type.isUnsigned) {
            emit("clra");
        } else {
            emit("sex");
        }
        emit("std", "[ac]");
        return;
    }
    emit("ldd", "0,x");
    emit("std", "[ac]");
}

void M6809Codegen::storeOperand(Type type) {
    if (widthOfType(type) == 1) {
        // `ac`'s own cell is big-endian (see M6809Assembler.h), so the
        // actual value byte - the one a `char` store writes - is at ac+1,
        // not ac itself.
        emit("ldb", "[ac+1]");
        emit("stb", "0,x");
        return;
    }
    emit("ldd", "[ac]");
    emit("std", "0,x");
}

void M6809Codegen::popWideInto(const char *role, int words) {
    for (int i = 0; i < words; i++) {
        emit("puls", "a,b");
        emit("std", cat("[", wideScratch(role, i), "]"));
    }
}

void M6809Codegen::pushWideFrom(const char *role, int words) {
    for (int i = words - 1; i >= 0; i--) {
        emit("ldd", cat("[", wideScratch(role, i), "]"));
        emit("pshs", "a,b");
    }
}

void M6809Codegen::genWideLoad(const ir::Slot *slot, int words) {
    for (int i = words - 1; i >= 0; i--) {
        if (slot->kind == ir::SlotKind::Global) {
            emit("ldd", cat("[", label(slot), (i ? ("+" + std::to_string(2 * i)) : ""), "]"));
        } else {
            loadFrameAddr(slot, 2 * i);
            emit("ldd", "0,x");
        }
        emit("pshs", "a,b");
    }
}

void M6809Codegen::genWideLoadLiteral(long value, int words) {
    auto uv = static_cast<unsigned long>(value);
    for (int i = words - 1; i >= 0; i--) {
        uint16_t w = static_cast<uint16_t>((uv >> (16 * i)) & 0xFFFF);
        emit("ldd", w);
        emit("pshs", "a,b");
    }
}

void M6809Codegen::genWideStore(const ir::Slot *slot, int words) {
    for (int i = 0; i < words; i++) {
        emit("puls", "a,b");
        if (slot->kind == ir::SlotKind::Global) {
            emit("std", cat("[", label(slot), (i ? ("+" + std::to_string(2 * i)) : ""), "]"));
        } else {
            loadFrameAddr(slot, 2 * i);
            emit("std", "0,x");
        }
    }
}

// No 16-bit add/sub-with-carry exists on real 6809 hardware (ADDD/SUBD
// alone can't chain a carry into the next word): word 0 uses ADDD/SUBD
// directly (its own carry-out already reflects the bit-15/16 boundary),
// every later word's low byte then high byte continues that same chain via
// ADCB/SBCB then ADCA/SBCA (see M6809Assembler.cpp's own comment on why
// these were added).
void M6809Codegen::genWideAdd(const ir::Expr &lhs, const ir::Expr &rhs, bool isAdd, int words) {
    genExpr(lhs);
    genExpr(rhs);
    popWideInto("b", words);
    popWideInto("a", words);
    for (int i = 0; i < words; i++) {
        if (i == 0) {
            emit("ldd", cat("[", wideScratch("a", i), "]"));
            emit((isAdd ? "addd" : "subd"), cat("[", wideScratch("b", i), "]"));
            emit("std", cat("[", wideScratch("a", i), "]"));
        } else {
            emit("ldb", cat("[", wideScratch("a", i), "+1]"));
            emit((isAdd ? "adcb" : "sbcb"), cat("[", wideScratch("b", i), "+1]"));
            emit("stb", cat("[", wideScratch("a", i), "+1]"));
            emit("lda", cat("[", wideScratch("a", i), "]"));
            emit((isAdd ? "adca" : "sbca"), cat("[", wideScratch("b", i), "]"));
            emit("sta", cat("[", wideScratch("a", i), "]"));
        }
    }
    pushWideFrom("a", words);
}

// Two's-complement negate: complement every byte (COMA/COMB), then add 1
// into the least significant word and let the same ADCB/ADCA carry chain
// genWideAdd() uses propagate it up through the rest.
void M6809Codegen::genWideNegate(int words) {
    popWideInto("a", words);
    for (int i = 0; i < words; i++) {
        emit("lda", cat("[", wideScratch("a", i), "]"));
        emit("coma");
        emit("sta", cat("[", wideScratch("a", i), "]"));
        emit("ldb", cat("[", wideScratch("a", i), "+1]"));
        emit("comb");
        emit("stb", cat("[", wideScratch("a", i), "+1]"));
    }
    for (int i = 0; i < words; i++) {
        if (i == 0) {
            emit("ldd", cat("[", wideScratch("a", i), "]"));
            emit("addd", "1");
            emit("std", cat("[", wideScratch("a", i), "]"));
        } else {
            emit("ldb", cat("[", wideScratch("a", i), "+1]"));
            emit("adcb", "0");
            emit("stb", cat("[", wideScratch("a", i), "+1]"));
            emit("lda", cat("[", wideScratch("a", i), "]"));
            emit("adca", "0");
            emit("sta", cat("[", wideScratch("a", i), "]"));
        }
    }
    pushWideFrom("a", words);
}

// No 16-bit AND/OR/XOR exists either - every word is combined byte by byte
// via the A/B-only forms.
void M6809Codegen::genWideBitwise(const ir::Expr &lhs, const ir::Expr &rhs, const std::string &op, int words) {
    genExpr(lhs);
    genExpr(rhs);
    popWideInto("b", words);
    popWideInto("a", words);
    const char *ainsn = op == "&" ? "anda" : op == "|" ? "ora" : "eora";
    const char *binsn = op == "&" ? "andb" : op == "|" ? "orb" : "eorb";
    for (int i = 0; i < words; i++) {
        emit("lda", cat("[", wideScratch("a", i), "]"));
        emit(ainsn, cat("[", wideScratch("b", i), "]"));
        emit("sta", cat("[", wideScratch("a", i), "]"));
        emit("ldb", cat("[", wideScratch("a", i), "+1]"));
        emit(binsn, cat("[", wideScratch("b", i), "+1]"));
        emit("stb", cat("[", wideScratch("a", i), "+1]"));
    }
    pushWideFrom("a", words);
}

void M6809Codegen::genWideEqualityCompare(const ir::Expr &lhs, const ir::Expr &rhs, bool wantEqual, int words) {
    genExpr(lhs);
    genExpr(rhs);
    popWideInto("b", words);
    popWideInto("a", words);
    emit("ldd", "0");
    emit("std", "[wacc]");
    for (int i = 0; i < words; i++) {
        emit("lda", cat("[", wideScratch("a", i), "]"));
        emit("eora", cat("[", wideScratch("b", i), "]"));
        emit("ora", "[wacc]");
        emit("sta", "[wacc]");
        emit("ldb", cat("[", wideScratch("a", i), "+1]"));
        emit("eorb", cat("[", wideScratch("b", i), "+1]"));
        emit("orb", "[wacc+1]");
        emit("stb", "[wacc+1]");
    }
    emit("ldd", "[wacc]");  // LDD sets Z from the whole 16-bit result: Z=1 iff every word matched
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit((wantEqual ? "lbeq" : "lbne"), Ltrue);
    emit("ldd", "0");
    emit("std", "[ac]");
    emit("lbra", Ldone);
    emitLabel(Ltrue);
    emit("ldd", "1");
    emit("std", "[ac]");
    emitLabel(Ldone);
}

// Compared most-significant word first, using real N/V/C flags directly -
// no sign-bias trick needed, unlike Z80/8080 (see M6809Codegen.h). Only the
// top word's compare needs the signed/unsigned distinction; once two words
// differ every lower word is a pure unsigned magnitude.
void M6809Codegen::genWideLessCompare(const ir::Expr &a, const ir::Expr &b, bool invert, bool isUnsigned,
                                       int words) {
    genExpr(a);
    genExpr(b);
    popWideInto("b", words);
    popWideInto("a", words);
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    for (int i = words - 1; i >= 0; i--) {
        emit("ldd", cat("[", wideScratch("a", i), "]"));
        emit("subd", cat("[", wideScratch("b", i), "]"));
        bool topWord = (i == words - 1);
        const char *ltMnem = (topWord && !isUnsigned) ? (invert ? "lbge" : "lblt") : (invert ? "lbhs" : "lblo");
        if (i == 0) {
            emit(ltMnem, Ltrue);
            emit("lbra", Lfalse);
        } else {
            std::string Lcontinue = newLabel();
            emit("lbeq", Lcontinue);
            emit(ltMnem, Ltrue);
            emit("lbra", Lfalse);
            emitLabel(Lcontinue);
        }
    }
    emitLabel(Lfalse);
    emit("ldd", "0");
    emit("std", "[ac]");
    emit("lbra", Ldone);
    emitLabel(Ltrue);
    emit("ldd", "1");
    emit("std", "[ac]");
    emitLabel(Ldone);
}

void M6809Codegen::genWideCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned,
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
// across every byte, most-significant byte first - no variable-count
// shift exists even for a single word on this chip (see genShift()). The
// byte list is built most-significant-byte-first (word i's high byte, then
// its low byte, for i descending) to match this cell layout's big-endian
// byte order within each word.
void M6809Codegen::genWideShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned, int words) {
    genExpr(lhs);
    popWideInto("a", words);
    genExpr(rhs);
    emit("ldb", "[ac+1]");
    emit("stb", "[cx+1]");
    std::vector<std::string> bytesMsbFirst;
    for (int i = words - 1; i >= 0; i--) {
        bytesMsbFirst.push_back(wideScratch("a", i));
        bytesMsbFirst.push_back(wideScratch("a", i) + "+1");
    }
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("ldb", "[cx+1]");
    emit("lbeq", Ldone);
    if (isLeft) {
        for (size_t idx = bytesMsbFirst.size(); idx-- > 0;) {
            bool first = idx == bytesMsbFirst.size() - 1;
            emit("ldb", cat("[", bytesMsbFirst[idx], "]"));
            emit((first ? "aslb" : "rolb"));
            emit("stb", cat("[", bytesMsbFirst[idx], "]"));
        }
    } else {
        for (size_t idx = 0; idx < bytesMsbFirst.size(); idx++) {
            const char *insn = idx == 0 ? (isUnsigned ? "lsrb" : "asrb") : "rorb";
            emit("ldb", cat("[", bytesMsbFirst[idx], "]"));
            emit(insn);
            emit("stb", cat("[", bytesMsbFirst[idx], "]"));
        }
    }
    emit("ldb", "[cx+1]");
    emit("decb");
    emit("stb", "[cx+1]");
    emit("lbra", Lloop);
    emitLabel(Ldone);
    pushWideFrom("a", words);
}

// A long/long long multiply/divide needs a bit-serial multi-word routine,
// and this target has no simulator to verify a hand-written one against -
// failing loudly here is safer than shipping an unverifiable one (same
// rationale as Z80Codegen.cpp's/I8080Codegen.h's own genWideMul()).
void M6809Codegen::genWideMul(const ir::Expr &lhs, const ir::Expr &rhs, int) {
    err(lhs.line, "multiplying two 'long'/'long long' values isn't supported on this target yet - "
                  "see M6809Codegen.cpp's own genWideMul() comment");
    (void)rhs;
}

void M6809Codegen::genWideDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool, int) {
    err(lhs.line, "dividing two 'long'/'long long' values isn't supported on this target yet - "
                  "see M6809Codegen.cpp's own genWideDivMod() comment");
    (void)rhs;
}

// Every arithmetic operand narrower than int is already promoted by
// IRBuilder, so the real work is crossing the native-word/wide boundary
// and re-narrowing a char result.
void M6809Codegen::genConvert(const ir::Expr &e) {
    Type from = e.lhs->type;
    Type to = e.type;
    bool fromWide = isWide(from);
    bool toWide = isWide(to);

    // D = 0xFFFF or 0x0000 depending on `highByteAddr`'s own bit 7 (the
    // overall sign bit of the word it's the high byte of) - used to
    // materialize a signed extension word.
    auto signExtendWord = [&](const std::string &highByteAddr) {
        std::string Lskip = newLabel();
        emit("ldb", cat("[", highByteAddr, "]"));
        emit("andb", "128");
        emit("ldd", "0");
        emit("lbeq", Lskip);
        emit("ldd", "65535");
        emitLabel(Lskip);
    };

    if (!fromWide && !toWide) {
        genExpr(*e.lhs);  // ac
        if (widthOfType(to) == 1) {
            emit("ldb", "[ac+1]");
            emitVerbatim((to.isUnsigned ? "    clra\n" : "    sex\n"));
            emit("std", "[ac]");
        }
        return;
    }
    if (fromWide && !toWide) {
        int words = wordCountOf(from);
        genExpr(*e.lhs);
        popWideInto("a", words);
        emit("ldd", cat("[", wideScratch("a", 0), "]"));
        emit("std", "[ac]");
        if (widthOfType(to) == 1) {
            emit("ldb", "[ac+1]");
            emitVerbatim((to.isUnsigned ? "    clra\n" : "    sex\n"));
            emit("std", "[ac]");
        }
        return;
    }
    if (!fromWide && toWide) {
        int toWords = wordCountOf(to);
        genExpr(*e.lhs);  // ac already holds the native-width value, correctly sign/zero-extended to 16 bits
        emit("ldd", "[ac]");
        emit("std", cat("[", wideScratch("a", 0), "]"));
        for (int i = 1; i < toWords; i++) {
            if (from.isUnsigned) {
                emit("ldd", "0");
            } else {
                signExtendWord("ac");
            }
            emit("std", cat("[", wideScratch("a", i), "]"));
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
        for (int i = fromWords; i < toWords; i++) {
            if (from.isUnsigned) {
                emit("ldd", "0");
            } else {
                signExtendWord(wideScratch("a", fromWords - 1));
            }
            emit("std", cat("[", wideScratch("a", i), "]"));
        }
    }
    pushWideFrom("a", toWords);
}

std::string M6809Codegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    // Real separate compilation means `prog` may not define `main` at all.
    // In `objectMode`, no object file emits this entry sequence, even one
    // that defines `main` - M6809LinkerFactory.cpp synthesizes exactly one
    // once every object is merged. An object file's own base is `org 0`;
    // placing it is the linker's job.
    emitRaw(cat("org ", (objectMode ? 0 : kBaseAddress)));
    emitRaw("");
    if (!objectMode) {
        emitLabel("_start");
        emit("lds", kStackTop);
        emit("jsr", cat("[", label(mainFn), "]"));
        // The program's result sits in `ac` once execution reaches `halt` -
        // this extra load also leaves it in D for a quick register-dump look.
        emit("ldd", "[ac]");
        emitLabel("halt");
        emit("jmp", "[halt]");
        emitRaw("");
    }

    // The fixed pseudo-registers are shared across the whole linked
    // program, not redeclared per-object: in whole-program mode they're
    // declared once, here; in object mode this file never declares them,
    // so every reference becomes a relocation resolved against
    // M6809LinkerFactory.cpp's synthesized entry object.
    if (!objectMode) {
        declareTemp("ac");
        declareTemp("_bc");
        declareTemp("cx");
        declareTemp("dx");
        declareTemp("ptr");
        for (int i = 0; i < 4; i++) declareTemp(wideScratch("a", i));
        for (int i = 0; i < 4; i++) declareTemp(wideScratch("b", i));
        for (int i = 0; i < 4; i++) declareTemp(wideScratch("ret", i));
        declareTemp("wacc");
    }

    for (const auto &fb : prog.functionBodies) genFunction(fb);

    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        if (slot->isExternal) continue;
        if (debugMapEnabled_) debugSymbols_.push_back({label(slot.get()), /*isFunc=*/false});
        const std::string lbl = label(slot.get());
        if (slot->arraySize > 0) {
            declareArray(lbl, slot->arrayInit, slot->type);
        } else if (slot->type.base == BaseType::Struct) {
            declareArray(lbl, std::vector<ir::ArrayInitElem>(slot->type.structSize), Type{BaseType::Char, false});
        } else {
            long v = slot->hasInit ? slot->initValue : 0;
            int w = widthOfType(slot->type);
            if (w == 1) {
                trailer_.push_back(lbl + ": db " + std::to_string(static_cast<int>(static_cast<uint8_t>(v))) +
                                    "  ; " + slot->sourceName);
            } else if (w == 2) {
                uint16_t v16 = static_cast<uint16_t>(v);
                trailer_.push_back(lbl + ": dw " + std::to_string(v16) + "  ; " + slot->sourceName);
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
    }

    std::ostringstream out;
    out << code_.str();
    for (const auto &line : trailer_) out << line << "\n";
    return out.str();
}

// Every call gets its own real frame, built around `u`. On entry S points
// at the return address JSR just pushed; `pshs u` saves the caller's frame
// pointer, `tfr s,u` sets `u` to that S - unlike Z80's `ix` (no direct
// move to/from HL, so it goes through the real stack) this is one plain
// register transfer. From `u`: `u+0` the saved caller u, `u+2` the return
// address, `u+4+2*i` argument i (pushed by the caller in reverse order, so
// argument 0 is closest to the return address). `frameReserveBytes_` bytes
// of locals are then carved out from `s` in one `leas -n,s` - no dec-sp
// loop needed, unlike Z80. Every Return undoes this with a matching
// `leas n,s` before restoring `u` and returning.
void M6809Codegen::genFunction(const ir::FunctionBody &fn) {
    lastDebugLine_ = 0;
    if (debugMapEnabled_) debugSymbols_.push_back({label(fn.func), /*isFunc=*/true});
    emitLabel(label(fn.func));
    emit("pshs", "u");
    emit("tfr", "s,u");
    // See Z80Codegen.cpp's identical comment: `frameSize` is rounded up to
    // the native int width as a whole, padding by `intUnits-1` to always
    // cover a Local whose own frameOffset falls short of its deepest byte.
    frameReserveBytes_ = fn.func->frameSize + (typeWidths().intUnits - 1);
    if (frameReserveBytes_ > 0) emit("leas", cat("-", frameReserveBytes_, ",s"));

    genStmt(*fn.body);

    if (frameReserveBytes_ > 0) emit("leas", cat(frameReserveBytes_, ",s"));
    emit("puls", "u");
    emit("rts");
    emitRaw("");
}

void M6809Codegen::genStmt(const ir::Stmt &s) {
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
            emit("lbeq", (s.elseStmt ? Lelse : Ldone));
            genStmt(*s.thenStmt);
            if (s.elseStmt) {
                emit("lbra", Ldone);
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
            emit("lbeq", Lend);
            loopLabels_.push_back({Lcont, Lend});
            genStmt(*s.loopBody);
            loopLabels_.pop_back();
            if (s.postStmt) {
                emitLabel(Lcont);
                genStmt(*s.postStmt);
            }
            emit("lbra", Lstart);
            emitLabel(Lend);
            return;
        }
        case ir::StmtKind::Break: {
            emit("lbra", loopLabels_.back().second);
            return;
        }
        case ir::StmtKind::Continue: {
            emit("lbra", loopLabels_.back().first);
            return;
        }
        case ir::StmtKind::Return: {
            if (s.initOrReturnOrExpr) {
                genExpr(*s.initOrReturnOrExpr);
                if (isWide(s.initOrReturnOrExpr->type)) {
                    int words = wordCountOf(s.initOrReturnOrExpr->type);
                    for (int i = 0; i < words; i++) {
                        emit("puls", "a,b");
                        emit("std", cat("[", wideScratch("ret", i), "]"));
                    }
                }
            }
            if (frameReserveBytes_ > 0) emit("leas", cat(frameReserveBytes_, ",s"));
            emit("puls", "u");
            emit("rts");
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
                emit("pshs", "x");
                genExpr(*s.initOrReturnOrExpr);
                emit("puls", "x");
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
            if (!s.asmText.empty() && s.asmText.back() != '\n') emitRaw("");
            return;
        }
        case ir::StmtKind::Label: {
            emitLabel(irLabel(s.labelId));
            return;
        }
        case ir::StmtKind::Goto: {
            emit("lbra", irLabel(s.labelId));
            return;
        }
    }
}

void M6809Codegen::genExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit: {
            if (isWide(e.type)) {
                genWideLoadLiteral(e.intValue, wordCountOf(e.type));
                return;
            }
            uint16_t v16 = static_cast<uint16_t>(e.intValue);
            emit("ldd", v16);
            emit("std", "[ac]");
            return;
        }
        case ir::ExprKind::Var: {
            if (isWide(e.slot->type)) {
                genWideLoad(e.slot, wordCountOf(e.slot->type));
                return;
            }
            if (e.slot->kind == ir::SlotKind::Global) {
                emit("ldx", label(e.slot));  // X = the global's own address, as data
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
                    emit("ldd", "[ptr]");
                    emit("pshs", "a,b");
                    genExpr(*e.rhs);
                    popWideInto("b", words);
                    emit("puls", "a,b");
                    emit("std", "[ptr]");
                    genWideStoreAtPtr("b", words);
                    return;
                }
                emit("ldd", "[ptr]");
                emit("pshs", "a,b");
                genExpr(*e.rhs);
                emit("puls", "a,b");
                emit("std", "[ptr]");
                loadAddrX("ptr");
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
                emit("ldx", label(e.slot));
                storeOperand(e.slot->type);
                return;
            }
            loadFrameAddr(e.slot);
            emit("pshs", "x");
            genExpr(*e.rhs);
            emit("puls", "x");
            storeOperand(e.slot->type);
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            if (e.lhs) {  // slot[idx] op= rhs (including slot[idx]++/--)
                genIndexAddr(e.slot, *e.lhs);  // ptr = address
            } else if (e.slot->kind == ir::SlotKind::Global) {
                emit("ldd", label(e.slot));
                emit("std", "[ptr]");
            } else {
                loadFrameAddr(e.slot);
                emit("stx", "[ptr]");
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
            emit("lbeq", Lelse);
            genExpr(*e.rhs);
            emit("lbra", Ldone);
            emitLabel(Lelse);
            genExpr(*e.elseExpr);
            emitLabel(Ldone);
            return;
        }
        case ir::ExprKind::DerefAssign: {
            genExpr(*e.lhs);  // ac = pointer value
            copy16("ptr", "ac");
            if (isWide(e.type)) {
                int words = wordCountOf(e.type);
                emit("ldd", "[ptr]");
                emit("pshs", "a,b");
                genExpr(*e.rhs);
                popWideInto("b", words);
                emit("puls", "a,b");
                emit("std", "[ptr]");
                genWideStoreAtPtr("b", words);
                return;
            }
            emit("ldd", "[ptr]");
            emit("pshs", "a,b");
            genExpr(*e.rhs);
            emit("puls", "a,b");
            emit("std", "[ptr]");
            loadAddrX("ptr");
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
                emit("ldd", "0");
                emit("subd", "[ac]");
                emit("std", "[ac]");
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                genExpr(*e.rhs);  // ac = pointer value
                copy16("ptr", "ac");
                if (isWide(e.type)) {
                    genWideLoadAtPtr(wordCountOf(e.type));
                    return;
                }
                loadAddrX("ptr");
                loadOperand(e.type);
            } else if (e.op == "~") {
                if (wide) {
                    genExpr(*e.rhs);
                    int words = wordCountOf(e.rhs->type);
                    popWideInto("a", words);
                    for (int i = 0; i < words; i++) {
                        emit("lda", cat("[", wideScratch("a", i), "]"));
                        emit("coma");
                        emit("sta", cat("[", wideScratch("a", i), "]"));
                        emit("ldb", cat("[", wideScratch("a", i), "+1]"));
                        emit("comb");
                        emit("stb", cat("[", wideScratch("a", i), "+1]"));
                    }
                    pushWideFrom("a", words);
                    return;
                }
                genExpr(*e.rhs);
                emit("lda", "[ac]");
                emit("coma");
                emit("sta", "[ac]");
                emit("ldb", "[ac+1]");
                emit("comb");
                emit("stb", "[ac+1]");
            } else {  // "&"
                genAddrOf(*e.rhs);
            }
            return;
        }
        case ir::ExprKind::Call: {
            // Arguments are pushed right-to-left so argument 0 ends up
            // closest to the return address (see genFunction()). The callee
            // never cleans these up, so the caller pops them back off after
            // the call returns.
            int totalWords = 0;
            for (size_t i = e.args.size(); i-- > 0;) {
                genExpr(*e.args[i]);
                if (isWide(e.args[i]->type)) {
                    totalWords += wordCountOf(e.args[i]->type);
                } else {
                    emit("ldd", "[ac]");
                    emit("pshs", "a,b");
                    totalWords += 1;
                }
            }
            emit("jsr", cat("[", label(e.func), "]"));
            for (int i = 0; i < totalWords; i++) emit("puls", "a,b");
            if (isWide(e.type)) {
                // The callee left its wide return value in "wret" - push it
                // back as this expression's result, MSW first so LSW ends up on top.
                int words = wordCountOf(e.type);
                for (int i = words - 1; i >= 0; i--) {
                    emit("ldd", cat("[", wideScratch("ret", i), "]"));
                    emit("pshs", "a,b");
                }
            }
            return;
        }
        case ir::ExprKind::CallPtr: {
            // JMP's indexed form transfers control directly to the computed
            // address (see M6809Assembler.h), so a real CALL is emulated the
            // same way Z80's "jp [hl]" trick does: push the return address,
            // then jump through the register holding the callee's address.
            genExpr(*e.lhs);  // ac = the callee's own entry address
            std::string Lret = newLabel();
            loadAddrX("ac");  // X = callee's address
            emit("ldd", Lret);
            emit("pshs", "a,b");
            emit("jmp", "0,x");
            emitLabel(Lret);
            return;
        }
        case ir::ExprKind::Binary: {
            genBinary(e);
            return;
        }
        case ir::ExprKind::Intrinsic: {
            // No dedicated EI/DI on this chip - ANDCC/ORCC mask/unmask IRQ
            // via the I flag directly (see M6809Assembler.h).
            emit((e.op == "ion" ? "andcc 0xEF" : "orcc 0x10"));
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

// Leaves slot[idxExpr]'s effective address in `ptr`. The scaling multiply
// (index * widthOfType(slot->type)) is a compile-time-constant-width
// shift-and-add, fully unrolled over the width's own set bits (see
// Z80Codegen.cpp's identical comment), so a non-power-of-two element width
// (e.g. 3, for a 3-byte struct) scales correctly, unlike a naive
// power-of-two doubling.
void M6809Codegen::genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
    genExpr(idxExpr);  // ac = index
    emit("ldd", "[ac]");
    emit("std", "[_bc]");  // _bc = index (current doubling value)
    emit("ldd", "0");
    emit("std", "[ac]");       // ac = running product, starts at 0
    for (int w = widthOfType(slot->type); w > 0; w >>= 1) {
        if (w & 1) {
            emit("ldd", "[ac]");
            emit("addd", "[_bc]");
            emit("std", "[ac]");
        }
        if (w >> 1) {
            emit("ldd", "[_bc]");
            emit("addd", "[_bc]");
            emit("std", "[_bc]");
        }
    }
    emit("ldd", "[ac]");
    emit("std", "[ptr]");
    if (slot->arraySize > 0) {
        if (slot->kind == ir::SlotKind::Global) {
            emit("ldd", "[ptr]");
            emit("addd", label(slot));
            emit("std", "[ptr]");
        } else {
            loadFrameAddr(slot);  // X = array's element 0, its lowest address
            emit("stx", "[cx]");
            emit("ldd", "[ptr]");
            emit("addd", "[cx]");
            emit("std", "[ptr]");
        }
        return;
    }
    if (slot->kind == ir::SlotKind::Global) {
        emit("ldd", cat("[", label(slot), "]"));  // D = the pointer variable's own stored value
    } else {
        loadFrameAddr(slot);  // X = address of the pointer-variable's own storage
        emit("ldd", "0,x");
    }
    emit("addd", "[ptr]");
    emit("std", "[ptr]");
}

// `ptr` already holds the element's address. Reads `words` 16-bit words,
// most significant first, pushing each (LSW ends up on top) - real indexed
// addressing lets every word be read with one `ldd <2*i>,x`, no per-word
// address arithmetic needed (unlike Z80's own version of this).
void M6809Codegen::genWideLoadAtPtr(int words) {
    for (int i = words - 1; i >= 0; i--) {
        emit("ldx", "[ptr]");
        emit("ldd", cat((2 * i), ",x"));
        emit("pshs", "a,b");
    }
}

// Mirror image for a write; `role`'s scratch cells must already hold the
// words to store (see its Z80Codegen.cpp counterpart's identical contract).
void M6809Codegen::genWideStoreAtPtr(const char *role, int words) {
    for (int i = 0; i < words; i++) {
        emit("ldx", "[ptr]");
        emit("ldd", cat("[", wideScratch(role, i), "]"));
        emit("std", cat((2 * i), ",x"));
    }
}

void M6809Codegen::genIndex(const ir::Expr &e) {
    genIndexAddr(e.slot, *e.lhs);  // ptr = element address
    if (isWide(e.slot->type)) {
        genWideLoadAtPtr(wordCountOf(e.slot->type));
        return;
    }
    loadAddrX("ptr");
    loadOperand(e.slot->type);
}

// Element stores go straight through X's indexed form - one `ldd`/`std` per
// element (or `ldb`/`stb` for a `char`), no inc-pointer dance like Z80's.
void M6809Codegen::genLocalArrayInit(const ir::Slot *slot) {
    int w = widthOfType(slot->type);
    for (size_t i = 0; i < slot->arrayInit.size(); i++) {
        const ir::ArrayInitElem &elem = slot->arrayInit[i];
        long base = static_cast<long>(i) * w;
        if (w == 1 && !elem.isSymbol) {
            loadFrameAddr(slot, base);
            emit("ldb", static_cast<int>(static_cast<uint8_t>(elem.value)));
            emit("stb", "0,x");
            continue;
        }
        if (elem.isSymbol || w <= 2) {
            loadFrameAddr(slot, base);
            emit("ldd", arrayElemText(elem));
            emit("std", "0,x");
            continue;
        }
        auto uv = static_cast<unsigned long>(elem.value);
        int words = w / 2;
        for (int j = 0; j < words; j++) {
            loadFrameAddr(slot, base + 2 * j);
            uint16_t wv = static_cast<uint16_t>((uv >> (16 * j)) & 0xFFFF);
            emit("ldd", wv);
            emit("std", "0,x");
        }
    }
}

// Entry: `_bc` = old value, `ac` = rhs value (both already at `type`'s
// width/signedness). `+`/`-`/`&`/`|`/`^` end with the raw new value in
// `ac`; `*`/`/`/`%`/`<<`/`>>` reuse genMul()'s/genDivMod()'s/genShift()'s
// own loop shapes with `_bc`/`dx`/`cx+1` as the working cells.
void M6809Codegen::genCompoundOp(const std::string &op, Type type) {
    bool isUnsigned = type.isUnsigned;
    if (op == "+") {
        emit("ldd", "[_bc]");
        emit("addd", "[ac]");
        emit("std", "[ac]");
        return;
    }
    if (op == "-") {
        emit("ldd", "[_bc]");
        emit("subd", "[ac]");
        emit("std", "[ac]");
        return;
    }
    if (op == "&" || op == "|" || op == "^") {
        const char *ainsn = op == "&" ? "anda" : op == "|" ? "ora" : "eora";
        const char *binsn = op == "&" ? "andb" : op == "|" ? "orb" : "eorb";
        emit("lda", "[_bc]");
        emit(ainsn, "[ac]");
        emit("sta", "[ac]");
        emit("ldb", "[_bc+1]");
        emit(binsn, "[ac+1]");
        emit("stb", "[ac+1]");
        return;
    }
    if (op == "*") {
        emit("ldd", "[ac]");
        emit("std", "[dx]");  // dx = rhs (multiplier); _bc already = old (multiplicand)
        emit("ldd", "0");
        emit("std", "[ac]");
        emit("ldb", "16");
        emit("stb", "[cx+1]");
        std::string Lloop = newLabel(), Ldone = newLabel(), Lskipadd = newLabel();
        emitLabel(Lloop);
        emit("ldb", "[cx+1]");
        emit("lbeq", Ldone);
        emit("ldb", "[dx+1]");
        emit("andb", "1");
        emit("lbeq", Lskipadd);
        emit("ldd", "[ac]");
        emit("addd", "[_bc]");
        emit("std", "[ac]");
        emitLabel(Lskipadd);
        shiftOneBit("_bc", /*isLeft=*/true);
        shiftOneBit("dx", /*isLeft=*/false);
        emit("ldb", "[cx+1]");
        emit("decb");
        emit("stb", "[cx+1]");
        emit("lbra", Lloop);
        emitLabel(Ldone);
        return;
    }
    if (op == "<<" || op == ">>") {
        emit("ldb", "[ac+1]");
        emit("stb", "[cx+1]");  // cx+1 = shift count; _bc already = old (value)
        std::string Lloop = newLabel(), Ldone = newLabel();
        emitLabel(Lloop);
        emit("ldb", "[cx+1]");
        emit("lbeq", Ldone);
        if (op == "<<" || isUnsigned) {
            shiftOneBit("_bc", /*isLeft=*/op == "<<");
        } else {
            emit("lda", "[_bc]");
            emit("asra");
            emit("sta", "[_bc]");
            emit("ldb", "[_bc+1]");
            emit("rorb");
            emit("stb", "[_bc+1]");
        }
        emit("ldb", "[cx+1]");
        emit("decb");
        emit("stb", "[cx+1]");
        emit("lbra", Lloop);
        emitLabel(Ldone);
        copy16("ac", "_bc");
        return;
    }
    // "/" or "%"
    emit("ldd", "[_bc]");
    emit("std", "[dx]");  // dx = old (dividend)
    emit("ldd", "[ac]");
    emit("std", "[_bc]");  // _bc = rhs (divisor)
    emit("ldd", "0");
    emit("std", "[ac]");
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("ldd", "[dx]");
    emit("subd", "[_bc]");
    emit("lblo", Ldone);
    emit("std", "[dx]");
    emit("ldd", "[ac]");
    emit("addd", "1");
    emit("std", "[ac]");
    emit("lbra", Lloop);
    emitLabel(Ldone);
    if (op == "%") copy16("ac", "dx");
}

// `ptr` must already hold the resolved address. The address and the old
// value are both shuttled through the real stack across evaluating `rhs`
// (ptr/ac/_bc are shared cells, and rhs's own evaluation may recurse into
// something that reuses them, and genCompoundOp() itself may clobber `_bc`
// for `*`/`<<`/`>>`).
void M6809Codegen::genCompoundThroughPtr(const ir::Expr &e) {
    loadAddrX("ptr");
    loadOperand(e.type);  // ac = old
    emit("ldx", "[ptr]");
    emit("pshs", "x");  // stash address
    emit("ldd", "[ac]");
    emit("pshs", "a,b");  // stash old (ac is about to be overwritten by rhs)
    genExpr(*e.rhs);                           // ac = rhs
    emit("puls", "a,b");
    emit("std", "[_bc]");  // _bc = old
    if (e.yieldOld) emit("pshs", "a,b");  // re-stash old (D still holds it) for the final result
    genCompoundOp(e.op, e.type);                // ac = new (raw)
    narrowAcTo(e.type);
    if (e.yieldOld) {
        emit("puls", "a,b");
        emit("std", "[cx]");  // cx = old
        emit("puls", "x");                    // X = address
        storeOperand(e.type);                        // stores ac(=new) through x
        emit("ldd", "[cx]");
        emit("std", "[ac]");    // ac = old (final result)
    } else {
        emit("puls", "x");  // X = address
        storeOperand(e.type);      // ac already = new
    }
}

// No-op unless `to` is a `char`.
void M6809Codegen::narrowAcTo(Type to) {
    if (widthOfType(to) != 1) return;
    emit("ldb", "[ac+1]");
    emitVerbatim((to.isUnsigned ? "    clra\n" : "    sex\n"));
    emit("std", "[ac]");
}

void M6809Codegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        if (operand.slot->kind == ir::SlotKind::Global) {
            emit("ldd", label(operand.slot));
            emit("std", "[ac]");
        } else {
            loadFrameAddr(operand.slot);
            emit("stx", "[ac]");
        }
        return;
    }
    if (operand.kind == ir::ExprKind::Index) {
        genIndexAddr(operand.slot, *operand.lhs);
        copy16("ac", "ptr");
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        emit("ldd", label(operand.func));
        emit("std", "[ac]");
        return;
    }
    err(operand.line, "internal: unhandled addressable-operand kind");
}

// Saves the left operand across evaluating the right one on the real
// stack (pshs a,b/puls a,b), never in a named cell: the right operand's own
// genExpr() might recurse into genMul()/genDivMod()/genShift(), which
// share _bc/cx/dx.
void M6809Codegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
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
        emit("ldd", "[ac]");
        emit("std", "[_bc]");
        genExpr(*e.rhs);
        emit("ldd", "[_bc]");
        emit((op == "+" ? "addd" : "subd"), "[ac]");
        emit("std", "[ac]");
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
        emit("ldd", "[ac]");
        emit("std", "[_bc]");
        genExpr(*e.rhs);
        const char *ainsn = op == "&" ? "anda" : op == "|" ? "ora" : "eora";
        const char *binsn = op == "&" ? "andb" : op == "|" ? "orb" : "eorb";
        emit("lda", "[_bc]");
        emit(ainsn, "[ac]");
        emit("sta", "[ac]");
        emit("ldb", "[_bc+1]");
        emit(binsn, "[ac+1]");
        emit("stb", "[ac+1]");
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
// sign bit (arithmetic ASRA) instead of 0 (logical LSRA).
void M6809Codegen::genShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned) {
    genExpr(lhs);
    emit("ldd", "[ac]");
    emit("std", "[_bc]");  // _bc = value
    genExpr(rhs);
    emit("ldb", "[ac+1]");
    emit("stb", "[cx+1]");  // cx+1 = shift count (always small; low byte is enough)
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("ldb", "[cx+1]");
    emit("lbeq", Ldone);
    if (isLeft || isUnsigned) {
        shiftOneBit("_bc", isLeft);
    } else {
        emit("lda", "[_bc]");
        emit("asra");
        emit("sta", "[_bc]");
        emit("ldb", "[_bc+1]");
        emit("rorb");
        emit("stb", "[_bc+1]");
    }
    emit("ldb", "[cx+1]");
    emit("decb");
    emit("stb", "[cx+1]");
    emit("lbra", Lloop);
    emitLabel(Ldone);
    copy16("ac", "_bc");
}

// Real hardware MUL (8x8->16 unsigned) is used directly when both operands
// are statically `unsigned char` - the one case that's correctness-free to
// fast-path, since the chip itself computes the product, not a hand-written
// algorithm. Every other `*` (both signed-byte and any int-width operand)
// falls back to the same shift-and-add loop Z80/8080 use: a correct
// 16x16->16 multiply built from three partial 8x8 MULs plus carry handling
// is new, unverified logic, not a safe template port, and a signed-byte MUL
// needs its own post-hoc sign correction this pass deliberately skips.
void M6809Codegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    if (isUnsignedByteOperand(lhs) && isUnsignedByteOperand(rhs)) {
        genExpr(lhs);
        emit("ldb", "[ac+1]");
        emit("stb", "[_bc+1]");  // _bc's low byte = lhs's own byte value
        genExpr(rhs);
        emit("lda", "[_bc+1]");
        emit("ldb", "[ac+1]");
        emit("mul");
        emit("std", "[ac]");
        return;
    }
    genExpr(lhs);                                // ac = multiplicand
    emit("ldd", "[ac]");
    emit("std", "[_bc]");    // saved across evaluating the multiplier
    genExpr(rhs);
    emit("ldd", "[ac]");
    emit("std", "[dx]");      // dx = multiplier
    emit("ldd", "0");
    emit("std", "[ac]");
    emit("ldb", "16");
    emit("stb", "[cx+1]");
    std::string Lloop = newLabel(), Ldone = newLabel(), Lskipadd = newLabel();
    emitLabel(Lloop);
    emit("ldb", "[cx+1]");
    emit("lbeq", Ldone);
    emit("ldb", "[dx+1]");
    emit("andb", "1");
    emit("lbeq", Lskipadd);
    emit("ldd", "[ac]");
    emit("addd", "[_bc]");
    emit("std", "[ac]");
    emitLabel(Lskipadd);
    shiftOneBit("_bc", /*isLeft=*/true);
    shiftOneBit("dx", /*isLeft=*/false);
    emit("ldb", "[cx+1]");
    emit("decb");
    emit("stb", "[cx+1]");
    emit("lbra", Lloop);
    emitLabel(Ldone);
}

// Repeated subtraction (no hardware divide): dx is the running remainder,
// _bc the fixed divisor, ac the quotient. SUBD both tests and computes the
// subtracted result in one instruction (its carry = unsigned borrow,
// checked directly via LBLO - no sign-bias trick needed). The core loop
// only handles non-negative operands, so a signed operand is divided via
// its absolute value, with `ptr`'s high/low bytes holding the original
// signs while it runs; the quotient's sign is restored (negative iff the
// signs differed), and the remainder always takes the dividend's own sign.
void M6809Codegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);                                // ac = dividend
    emit("ldd", "[ac]");
    emit("std", "[dx]");    // saved across evaluating the divisor
    genExpr(rhs);
    emit("ldd", "[ac]");
    emit("std", "[_bc]");    // bc = divisor
    auto isNeg = [&](const char *cell, const char *dstByteAddr) {
        emit("ldb", cat("[", cell, "]"));
        emit("andb", "128");  // ANDB sets Z from the masked result
        std::string Lnonneg = newLabel(), Ldone2 = newLabel();
        emit("lbeq", Lnonneg);
        emit("ldb", "1");
        emit("stb", cat("[", dstByteAddr, "]"));
        emit("lbra", Ldone2);
        emitLabel(Lnonneg);
        emit("ldb", "0");
        emit("stb", cat("[", dstByteAddr, "]"));
        emitLabel(Ldone2);
    };
    auto negate16 = [&](const char *cell) { emit("ldd", "0");
emit("subd", cat("[", cell, "]"));
emit("std", cat("[", cell, "]")); };
    auto negateIf = [&](const char *flagByteAddr, const char *cell) {
        emit("ldb", cat("[", flagByteAddr, "]"));  // LDB sets Z from the loaded value directly
        std::string Lskip = newLabel();
        emit("lbeq", Lskip);
        negate16(cell);
        emitLabel(Lskip);
    };
    if (!isUnsigned) {
        isNeg("dx", "ptr");
        isNeg("_bc", "ptr+1");
        negateIf("ptr", "dx");
        negateIf("ptr+1", "_bc");
    }
    emit("ldd", "0");
    emit("std", "[ac]");
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("ldd", "[dx]");
    emit("subd", "[_bc]");  // D = dx - _bc; C = borrow (dx < _bc)
    emit("lblo", Ldone);
    emit("std", "[dx]");  // commit: dx -= _bc (D already holds the subtracted result)
    emit("ldd", "[ac]");
    emit("addd", "1");
    emit("std", "[ac]");
    emit("lbra", Lloop);
    emitLabel(Ldone);
    if (!isUnsigned) {
        // Quotient is negative iff the two original signs differed; the
        // remainder keeps the dividend's sign.
        emit("ldb", "[ptr]");
        emit("stb", "[cx]");
        emit("ldb", "[ptr+1]");
        emit("eorb", "[cx]");
        std::string Lskip = newLabel();
        emit("lbeq", Lskip);
        negate16("ac");
        emitLabel(Lskip);
        negateIf("ptr", "dx");
    }
    if (wantRemainder) copy16("ac", "dx");
}

void M6809Codegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned) {
    if (op == "==") return emitEqualityCompare(lhs, rhs, /*wantEqual=*/true);
    if (op == "!=") return emitEqualityCompare(lhs, rhs, /*wantEqual=*/false);
    if (op == "<") return emitLessCompare(lhs, rhs, /*invert=*/false, isUnsigned);
    if (op == ">") return emitLessCompare(rhs, lhs, /*invert=*/false, isUnsigned);
    if (op == "<=") return emitLessCompare(rhs, lhs, /*invert=*/true, isUnsigned);
    if (op == ">=") return emitLessCompare(lhs, rhs, /*invert=*/true, isUnsigned);
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

// `==`/`!=` reduce to one CMPD: the Z flag directly means lhs == rhs.
void M6809Codegen::emitEqualityCompare(const ir::Expr &lhs, const ir::Expr &rhs, bool wantEqual) {
    genExpr(lhs);
    emit("ldd", "[ac]");
    emit("std", "[_bc]");
    genExpr(rhs);
    emit("ldd", "[_bc]");
    emit("cmpd", "[ac]");
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit((wantEqual ? "lbeq" : "lbne"), Ltrue);
    emit("ldd", "0");
    emit("std", "[ac]");
    emit("lbra", Ldone);
    emitLabel(Ltrue);
    emit("ldd", "1");
    emit("std", "[ac]");
    emitLabel(Ldone);
}

// Real N/V/C flags from one SUBD give the answer directly - no sign-bias
// trick needed (unlike Z80/8080; see M6809Codegen.h): LBLT/LBGE for signed,
// LBLO/LBHS for unsigned.
void M6809Codegen::emitLessCompare(const ir::Expr &a, const ir::Expr &b, bool invert, bool isUnsigned) {
    genExpr(a);
    emit("ldd", "[ac]");
    emit("std", "[_bc]");
    genExpr(b);
    emit("ldd", "[_bc]");
    emit("subd", "[ac]");  // D = a - b
    const char *mnem = isUnsigned ? (invert ? "lbhs" : "lblo") : (invert ? "lbge" : "lblt");
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit(mnem, Ltrue);
    emit("ldd", "0");
    emit("std", "[ac]");
    emit("lbra", Ldone);
    emitLabel(Ltrue);
    emit("ldd", "1");
    emit("std", "[ac]");
    emitLabel(Ldone);
}

void M6809Codegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    genExpr(lhs);
    testTruthy(lhs);
    emit("lbeq", Lfalse);
    genExpr(rhs);
    testTruthy(rhs);
    emit("lbeq", Lfalse);
    emitLabel(Ltrue);
    emit("ldd", "1");
    emit("std", "[ac]");
    emit("lbra", Ldone);
    emitLabel(Lfalse);
    emit("ldd", "0");
    emit("std", "[ac]");
    emitLabel(Ldone);
}

void M6809Codegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    genExpr(lhs);
    testTruthy(lhs);
    emit("lbne", Ltrue);
    genExpr(rhs);
    testTruthy(rhs);
    emit("lbne", Ltrue);
    emitLabel(Lfalse);
    emit("ldd", "0");
    emit("std", "[ac]");
    emit("lbra", Ldone);
    emitLabel(Ltrue);
    emit("ldd", "1");
    emit("std", "[ac]");
    emitLabel(Ldone);
}

void M6809Codegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    testTruthy(rhs);
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit("lbeq", Ltrue);
    emit("ldd", "0");
    emit("std", "[ac]");
    emit("lbra", Ldone);
    emitLabel(Ltrue);
    emit("ldd", "1");
    emit("std", "[ac]");
    emitLabel(Ldone);
}

// Leaves the real Z flag set from the value genExpr(e) just left behind,
// ready for an immediate `lbeq`/`lbne`. A non-wide value's own LDD already
// sets it; a wide value needs its words OR-ed together first.
void M6809Codegen::testTruthy(const ir::Expr &e) {
    if (!isWide(e.type)) {
        emit("ldd", "[ac]");
        return;
    }
    int words = wordCountOf(e.type);
    popWideInto("a", words);
    emit("ldd", "0");
    emit("std", "[wacc]");
    for (int i = 0; i < words; i++) {
        emit("lda", cat("[", wideScratch("a", i), "]"));
        emit("ora", "[wacc]");
        emit("sta", "[wacc]");
        emit("ldb", cat("[", wideScratch("a", i), "+1]"));
        emit("orb", "[wacc+1]");
        emit("stb", "[wacc+1]");
    }
    emit("ldd", "[wacc]");
}

std::optional<std::string> M6809Codegen::assembleInProcess(const std::string &asmText,
                                                             const std::string &format) const {
    if (format == "raw") return assembleM6809(asmText);
    if (auto kind = fileFormatKindFromName(format)) {
        ObjectFile obj = assembleM6809Object(asmText);
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

std::optional<std::string> M6809Codegen::debugMap(const std::string &asmText, const std::string &format,
                                                    const std::string &sourcePath) const {
    if (format != "raw") return std::nullopt;
    return assembleM6809DebugMap(asmText, sourcePath, debugSymbols_);
}

std::optional<std::string> M6809Codegen::debugMapPreLink(const std::string &asmText,
                                                    const std::string &sourcePath) const {
    return assembleM6809DebugMap(asmText, sourcePath, debugSymbols_, /*allowExternal=*/true);
}

// Only emits a marker when `debugMapEnabled_` (set via setDebugMapEnabled(),
// before generate() runs) and `line` actually differs from the last one
// emitted - see Pdp7Codegen.cpp's identical emitSourceLine(), which this
// mirrors exactly.
void M6809Codegen::emitSourceLine(int line) {
    if (!debugMapEnabled_ || line == 0 || line == lastDebugLine_) return;
    emitRaw(cat("__line ", line));
    lastDebugLine_ = line;
}

}  // namespace rcc
