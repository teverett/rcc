#include "ArmCodegen.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <tuple>

#include "ArmAssembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

// Where the whole program is loaded and starts executing - the test
// harness sets the simulated CPU's PC here directly.
constexpr uint32_t kBaseAddress = 0x1000u;

// Where the entry sequence points `sp` before calling main() -
// comfortably above the loaded program and its data.
constexpr uint32_t kInitialSP = 0x100000u;
}  // namespace

// A literal element is its own decimal text; a symbol element (a
// dispatch-table address-of-function/global) is the referenced Slot's or
// Function's label, written out bare - ArmAssembler.cpp's `dw` evaluator
// resolves it the same way a label operand resolves for MOVW/MOVT.
std::string ArmCodegen::arrayElemText(const ir::ArrayInitElem &e) {
    if (!e.isSymbol) return std::to_string(static_cast<int32_t>(static_cast<uint32_t>(e.value)));
    return e.slot ? label(e.slot) : label(e.func);
}

void ArmCodegen::declareArray(const std::string &lbl, const std::vector<ir::ArrayInitElem> &values, Type elemType) {
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

void ArmCodegen::loadAddress(const std::string &reg, const std::string &lbl) {
    emit("movw", cat(reg, ",", lbl));
    emit("movt", cat(reg, ",", lbl));
}

void ArmCodegen::emitFrameOffset(const std::string &reg, int offsetBytes, bool isAdd) {
    if (offsetBytes == 0) return;
    if (offsetBytes <= 255) {
        emit((isAdd ? "add" : "sub"), cat("", reg, ",", reg, ",", offsetBytes));
        return;
    }
    std::string scratch = (reg == "r1") ? "r3" : "r1";
    uint32_t uv = static_cast<uint32_t>(offsetBytes);
    emit("movw", cat(scratch, ",", static_cast<int32_t>(uv & 0xFFFFu)));
    if ((uv >> 16) != 0) emit("movt", cat(scratch, ",", static_cast<int32_t>((uv >> 16) & 0xFFFFu)));
    emit((isAdd ? "add" : "sub"), cat("", reg, ",", reg, ",", scratch));
}

void ArmCodegen::loadFrameAddr(const std::string &reg, const ir::Slot *slot, int elemIndex) {
    emit("mov", cat(reg, ",fp"));
    // `frameOffset` is already a byte offset (from the slot's declared
    // type's width); `elemIndex` is scaled by the array's element width.
    if (slot->kind == ir::SlotKind::Param) {
        emitFrameOffset(reg, slot->frameOffset, /*isAdd=*/true);
    } else {
        emitFrameOffset(reg, ir::localLowOffset(*slot, typeWidths()) - elemIndex * widthOfType(slot->type),
                        /*isAdd=*/false);
    }
}

void ArmCodegen::loadSlotAddr(const std::string &reg, const ir::Slot *slot) {
    if (slot->kind == ir::SlotKind::Global) {
        loadAddress(reg, label(slot));
    } else {
        loadFrameAddr(reg, slot, 0);
    }
}

void ArmCodegen::loadArrayInitElem(const std::string &reg, const ir::ArrayInitElem &e) {
    if (e.isSymbol) {
        if (e.slot) {
            loadSlotAddr(reg, e.slot);
        } else {
            loadAddress(reg, label(e.func));
        }
        return;
    }
    uint32_t uv = static_cast<uint32_t>(e.value);
    emit("movw", cat(reg, ",", static_cast<int32_t>(uv & 0xFFFFu)));
    if ((uv >> 16) != 0) emit("movt", cat(reg, ",", static_cast<int32_t>((uv >> 16) & 0xFFFFu)));
}

void ArmCodegen::genLocalArrayInit(const ir::Slot *slot) {
    int w = widthOfType(slot->type);
    for (size_t i = 0; i < slot->arrayInit.size(); i++) {
        const ir::ArrayInitElem &e = slot->arrayInit[i];
        loadArrayInitElem("r0", e);
        if (w == 8 && !e.isSymbol) {
            // loadArrayInitElem() only fills r0; fill r1 (the high word)
            // here too for a long long literal element. A symbol element
            // is always a native-int-width address, so stays single-word.
            auto v = static_cast<uint64_t>(e.value);
            uint32_t hi = static_cast<uint32_t>(v >> 32);
            emit("movw", cat("r1,", static_cast<int32_t>(hi & 0xFFFFu)));
            if ((hi >> 16) != 0) emit("movt", cat("r1,", static_cast<int32_t>((hi >> 16) & 0xFFFFu)));
        }
        loadFrameAddr("r2", slot, static_cast<int>(i));
        storeOperand("r2", slot->type);
    }
}

void ArmCodegen::narrowR0To(Type to) {
    int w = widthOfType(to);
    // Re-truncate+re-extend by shifting the unwanted high bits out and
    // back in - zero-fill (`lsr`) for unsigned, sign-fill (`asr`) for
    // signed. ARM has no dedicated sign/zero-extend instruction here.
    if (w == 1) {
        emit("lsl", "r0,r0,24");
        emit((to.isUnsigned ? "lsr" : "asr"), "r0,r0,24");
    } else if (w == 2) {
        emit("lsl", "r0,r0,16");
        emit((to.isUnsigned ? "lsr" : "asr"), "r0,r0,16");
    }
}

// A byte-at-a-time word load, little-endian, tolerating any byte
// boundary - LDRB/STRB never fault on alignment, unlike LDR/STR. `r3`
// holds the running byte address (`addrReg` is read, never written), `r4`
// the byte just transferred; `destReg` is assembled one OR per byte.
void ArmCodegen::loadWordUnaligned(const std::string &addrReg, const std::string &destReg) {
    emit("ldindb", cat(addrReg, ",r4"));
    emit("mov", cat(destReg, ",r4"));
    for (int byteIdx = 1; byteIdx < 4; byteIdx++) {
        emit("mov", cat("r3,", addrReg));
        emit("add", cat("r3,r3,", byteIdx));
        emit("ldindb", "r3,r4");
        emit("lsl", cat("r4,r4,", (byteIdx * 8)));
        emit("orr", cat(destReg, ",", destReg, ",r4"));
    }
}
void ArmCodegen::storeWordUnaligned(const std::string &addrReg, const std::string &srcReg) {
    emit("stindb", cat(addrReg, ",", srcReg));
    for (int byteIdx = 1; byteIdx < 4; byteIdx++) {
        emit("mov", cat("r4,", srcReg));
        emit("lsr", cat("r4,r4,", (byteIdx * 8)));
        emit("mov", cat("r3,", addrReg));
        emit("add", cat("r3,r3,", byteIdx));
        emit("stindb", "r3,r4");
    }
}
void ArmCodegen::loadHalfUnaligned(const std::string &addrReg, const std::string &destReg, bool isUnsigned) {
    emit("ldindb", cat(addrReg, ",", destReg));
    emit("mov", cat("r3,", addrReg));
    emit("add", "r3,r3,1");
    emit("ldindb", "r3,r4");
    emit("lsl", "r4,r4,8");
    emit("orr", cat(destReg, ",", destReg, ",r4"));
    if (!isUnsigned) {
        emit("lsl", cat(destReg, ",", destReg, ",16"));
        emit("asr", cat(destReg, ",", destReg, ",16"));
    }
}
void ArmCodegen::storeHalfUnaligned(const std::string &addrReg, const std::string &srcReg) {
    emit("stindb", cat(addrReg, ",", srcReg));
    emit("mov", cat("r4,", srcReg));
    emit("lsr", "r4,r4,8");
    emit("mov", cat("r3,", addrReg));
    emit("add", "r3,r3,1");
    emit("stindb", "r3,r4");
}

// `addrReg` holds the already-computed effective address (r2, by
// convention); loads into r0 (or r0:r1, for a `long long`), sign- or
// zero-extending a narrower-than-native-int value per its signedness.
void ArmCodegen::loadOperand(const std::string &addrReg, Type type, bool mayBeUnaligned) {
    int w = widthOfType(type);
    if (w == 1) {
        emit((type.isUnsigned ? "ldindb " : "ldindsb "), cat(addrReg, ",r0"));
    } else if (w == 2) {
        if (mayBeUnaligned) {
            loadHalfUnaligned(addrReg, "r0", type.isUnsigned);
        } else {
            emit((type.isUnsigned ? "ldindh " : "ldindsh "), cat(addrReg, ",r0"));
        }
    } else if (w == 4) {
        if (mayBeUnaligned) {
            loadWordUnaligned(addrReg, "r0");
        } else {
            emit("ldind", cat(addrReg, ",r0"));
        }
    } else if (mayBeUnaligned) {
        loadWordUnaligned(addrReg, "r0");
        emit("add", cat(addrReg, ",", addrReg, ",4"));
        loadWordUnaligned(addrReg, "r1");
        emit("sub", cat(addrReg, ",", addrReg, ",4"));
    } else {
        emit("ldind", cat(addrReg, ",r0"));
        emit("add", cat(addrReg, ",", addrReg, ",4"));
        emit("ldind", cat(addrReg, ",r1"));
        emit("sub", cat(addrReg, ",", addrReg, ",4"));
    }
}

void ArmCodegen::storeOperand(const std::string &addrReg, Type type, bool mayBeUnaligned) {
    int w = widthOfType(type);
    if (w == 1) {
        emit("stindb", cat(addrReg, ",r0"));
    } else if (w == 2) {
        if (mayBeUnaligned) {
            storeHalfUnaligned(addrReg, "r0");
        } else {
            emit("stindh", cat(addrReg, ",r0"));
        }
    } else if (w == 4) {
        if (mayBeUnaligned) {
            storeWordUnaligned(addrReg, "r0");
        } else {
            emit("stind", cat(addrReg, ",r0"));
        }
    } else if (mayBeUnaligned) {
        storeWordUnaligned(addrReg, "r0");
        emit("add", cat(addrReg, ",", addrReg, ",4"));
        storeWordUnaligned(addrReg, "r1");
        emit("sub", cat(addrReg, ",", addrReg, ",4"));
    } else {
        emit("stind", cat(addrReg, ",r0"));
        emit("add", cat(addrReg, ",", addrReg, ",4"));
        emit("stind", cat(addrReg, ",r1"));
        emit("sub", cat(addrReg, ",", addrReg, ",4"));
    }
}

// An implicit or explicit type conversion. Arithmetic/comparison operands
// narrower than `int` are already promoted by IRBuilder, so the only
// real work here is crossing the native-int/`long long` boundary, or
// re-truncating+re-extending r0 for a narrower target - see narrowR0To().
void ArmCodegen::genConvert(const ir::Expr &e) {
    Type from = e.lhs->type;
    Type to = e.type;
    bool fromWide = isWide(from);
    bool toWide = isWide(to);
    genExpr(*e.lhs);
    if (fromWide && !toWide) {
        narrowR0To(to);  // r1 is simply discarded
    } else if (!fromWide && toWide) {
        if (from.isUnsigned) {
            emit("mov", "r1,0");
        } else {
            emit("asr", "r1,r0,31");
        }
    } else if (!fromWide && !toWide) {
        narrowR0To(to);
    }
    // fromWide && toWide: the bit pattern is already correct, nothing
    // to do.
}

void ArmCodegen::genNegate64() {
    // Two's complement negate of r0:r1 in place - `adc`'s immediate-0
    // operand2 reads the carry out of the preceding `adds`.
    emit("mvn", "r0,r0");
    emit("mvn", "r1,r1");
    emit("adds", "r0,r0,1");
    emit("adc", "r1,r1,0");
}

// `long long` add/sub (lhs OP rhs) - evaluate both into r0:r1, stashing
// lhs's two words on the stack while rhs evaluates, then ripple the
// carry/borrow across the word pair with adc/sbc.
void ArmCodegen::genWideAdd(const ir::Expr &lhs, const ir::Expr &rhs, bool isAdd) {
    genExpr(lhs);
    emit("sub", "sp,sp,4");
    emit("stind", "sp,r1");
    emit("sub", "sp,sp,4");
    emit("stind", "sp,r0");  // stack (top->bottom): lhs_lo, lhs_hi
    genExpr(rhs);
    emit("mov", "r4,r0");
    emit("mov", "r5,r1");  // r4=rhs_lo, r5=rhs_hi
    emit("ldind", "sp,r0");
    emit("add", "sp,sp,4");
    emit("ldind", "sp,r1");
    emit("add", "sp,sp,4");  // r0:r1 = lhs restored
    if (isAdd) {
        emit("adds", "r0,r0,r4");
        emit("adc", "r1,r1,r5");
    } else {
        emit("subs", "r0,r0,r4");
        emit("sbc", "r1,r1,r5");
    }
}

// `long long` multiply, keeping the low 64 bits of the true 128-bit
// product: result_lo = lo(lhs)*lo(rhs) mod 2^32; result_hi =
// hi32(lo(lhs)*lo(rhs)) + lo(lhs)*hi(rhs) + hi(lhs)*lo(rhs), all mod 2^32.
// Only the low*low term needs a widening multiply (`umull`) - the other
// two only ever contribute to result_hi, so `mul`'s truncating 32x32->32
// result is exactly right for them.
void ArmCodegen::genWideMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    emit("sub", "sp,sp,4");
    emit("stind", "sp,r1");
    emit("sub", "sp,sp,4");
    emit("stind", "sp,r0");
    genExpr(rhs);
    emit("mov", "r6,r0");
    emit("mov", "r7,r1");      // r6=rhs_lo, r7=rhs_hi
    emit("ldind", "sp,r4");
    emit("add", "sp,sp,4");  // r4 = lhs_lo
    emit("ldind", "sp,r5");
    emit("add", "sp,sp,4");  // r5 = lhs_hi
    emit("mul", "r8,r4,r7");                   // r8 = lhs_lo*rhs_hi
    emit("mul", "r9,r5,r6");                   // r9 = lhs_hi*rhs_lo
    emit("umull", "r0,r1,r4,r6");               // r0:r1 = lhs_lo*rhs_lo (full 64-bit)
    emit("add", "r1,r1,r8");
    emit("add", "r1,r1,r9");
}

// Bit-serial shift/compare/subtract long division (no A32 instruction is
// wide enough for a 64/64 divide): r0:r1 = dividend (consumed,
// shifted left MSB-first) -> final quotient, r2:r3 = divisor, r4:r5 =
// remainder, r6:r7 = quotient (being built), r8 = iteration countdown,
// r9/r10/r12 = scratch. `isUnsigned` false divides magnitudes: each
// operand's sign bit is stashed on the stack across the loop and
// restored on the result afterward per C's truncating-division rule.
void ArmCodegen::genWideDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    emit("sub", "sp,sp,4");
    emit("stind", "sp,r1");
    emit("sub", "sp,sp,4");
    emit("stind", "sp,r0");
    genExpr(rhs);
    emit("mov", "r2,r0");
    emit("mov", "r3,r1");      // r2:r3 = divisor
    emit("ldind", "sp,r0");
    emit("add", "sp,sp,4");
    emit("ldind", "sp,r1");
    emit("add", "sp,sp,4");  // r0:r1 = dividend

    std::string LskipNegN, LskipNegD;
    if (!isUnsigned) {
        emit("lsr", "r9,r1,31");   // r9 = dividend's own sign
        emit("lsr", "r10,r3,31");  // r10 = divisor's own sign
        emit("sub", "sp,sp,4");
        emit("stind", "sp,r9");
        emit("sub", "sp,sp,4");
        emit("stind", "sp,r10");  // stack (top->bottom): divisorSign, dividendSign
        LskipNegN = newLabel();
        emit("cmp", "r9,0");
        emit("beq", LskipNegN);
        emit("mvn", "r0,r0");
        emit("mvn", "r1,r1");
        emit("adds", "r0,r0,1");
        emit("adc", "r1,r1,0");
        emitLabel(LskipNegN);
        LskipNegD = newLabel();
        emit("cmp", "r10,0");
        emit("beq", LskipNegD);
        emit("mvn", "r2,r2");
        emit("mvn", "r3,r3");
        emit("adds", "r2,r2,1");
        emit("adc", "r3,r3,0");
        emitLabel(LskipNegD);
    }

    emit("mov", "r4,0");
    emit("mov", "r5,0");
    emit("mov", "r6,0");
    emit("mov", "r7,0");
    emit("mov", "r8,64");

    std::string Lloop = newLabel(), Ldone = newLabel();
    std::string LhiDiff = newLabel(), LsubYes = newLabel(), LsubNo = newLabel(), LshiftQ = newLabel();
    emitLabel(Lloop);
    emit("cmp", "r8,0");
    emit("beq", Ldone);
    emit("sub", "r8,r8,1");

    // dividend <<= 1 (0-filled), capturing its own old top bit (r9).
    emit("lsr", "r9,r1,31");
    emit("lsr", "r10,r0,31");
    emit("lsl", "r0,r0,1");
    emit("lsl", "r1,r1,1");
    emit("orr", "r1,r1,r10");

    // remainder = (remainder << 1) | (dividend's old top bit, r9).
    emit("lsr", "r10,r4,31");
    emit("lsl", "r4,r4,1");
    emit("orr", "r4,r4,r9");
    emit("lsl", "r5,r5,1");
    emit("orr", "r5,r5,r10");

    // if (remainder >= divisor) { remainder -= divisor; quotient bit = 1 } else quotient bit = 0
    // (always an unsigned magnitude compare - both operands were
    // already negated to non-negative above for the signed case).
    emit("cmp", "r5,r3");
    emit("bne", LhiDiff);
    emit("cmp", "r4,r2");
    emit("bhs", LsubYes);
    emit("b", LsubNo);
    emitLabel(LhiDiff);
    emit("bhs", LsubYes);
    emit("b", LsubNo);
    emitLabel(LsubYes);
    emit("subs", "r4,r4,r2");
    emit("sbc", "r5,r5,r3");
    emit("mov", "r12,1");
    emit("b", LshiftQ);
    emitLabel(LsubNo);
    emit("mov", "r12,0");
    emitLabel(LshiftQ);

    // quotient = (quotient << 1) | quotient bit (r12).
    emit("lsr", "r10,r6,31");
    emit("lsl", "r6,r6,1");
    emit("orr", "r6,r6,r12");
    emit("lsl", "r7,r7,1");
    emit("orr", "r7,r7,r10");
    emit("b", Lloop);

    emitLabel(Ldone);
    if (!isUnsigned) {
        emit("ldind", "sp,r10");
        emit("add", "sp,sp,4");  // r10 = divisorSign
        emit("ldind", "sp,r9");
        emit("add", "sp,sp,4");   // r9 = dividendSign
        std::string LskipNegQ = newLabel(), LskipNegR = newLabel();
        emit("eor", "r12,r9,r10");
        emit("cmp", "r12,0");
        emit("beq", LskipNegQ);
        emit("mvn", "r6,r6");
        emit("mvn", "r7,r7");
        emit("adds", "r6,r6,1");
        emit("adc", "r7,r7,0");
        emitLabel(LskipNegQ);
        emit("cmp", "r9,0");
        emit("beq", LskipNegR);
        emit("mvn", "r4,r4");
        emit("mvn", "r5,r5");
        emit("adds", "r4,r4,1");
        emit("adc", "r5,r5,0");
        emitLabel(LskipNegR);
    }
    if (wantRemainder) {
        emit("mov", "r0,r4");
        emit("mov", "r1,r5");
    } else {
        emit("mov", "r0,r6");
        emit("mov", "r1,r7");
    }
}

// A `long long` shifted by a runtime count - no A32 instruction shifts
// more than 32 bits at once, so this is a counted loop of 1-bit shifts,
// each carrying the bit that crosses the word boundary by hand.
// `isUnsigned` only matters for `>>`: each step's high-word shift is
// `asr` (sign-preserving) or `lsr` (zero-filling) accordingly.
void ArmCodegen::genWideShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned) {
    genExpr(lhs);
    emit("sub", "sp,sp,4");
    emit("stind", "sp,r1");
    emit("sub", "sp,sp,4");
    emit("stind", "sp,r0");
    genExpr(rhs);  // r0 = shift count (already promoted to a plain int)
    emit("mov", "r4,r0");
    emit("ldind", "sp,r0");
    emit("add", "sp,sp,4");
    emit("ldind", "sp,r1");
    emit("add", "sp,sp,4");  // r0:r1 restored = the value being shifted
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("cmp", "r4,0");
    emit("beq", Ldone);
    emit("sub", "r4,r4,1");
    if (isLeft) {
        emit("lsr", "r5,r0,31");  // r5 = r0's old top bit, carries into r1
        emit("lsl", "r0,r0,1");
        emit("lsl", "r1,r1,1");
        emit("orr", "r1,r1,r5");
    } else {
        emit("lsl", "r5,r1,31");  // r5 = (r1&1)<<31, carries into r0's top
        emit("lsr", "r0,r0,1");
        emit("orr", "r0,r0,r5");
        emit((isUnsigned ? "lsr" : "asr"), "r1,r1,1");
    }
    emit("b", Lloop);
    emitLabel(Ldone);
}

// A `long long` comparison - compare the high words first (signed or
// unsigned condition per `isUnsigned`, since that's where the sign bit
// lives); only if equal does the low word matter, and that comparison is
// always unsigned regardless of the overall type's signedness.
void ArmCodegen::genWideCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned) {
    genExpr(lhs);
    emit("sub", "sp,sp,4");
    emit("stind", "sp,r1");
    emit("sub", "sp,sp,4");
    emit("stind", "sp,r0");
    genExpr(rhs);
    emit("mov", "r4,r0");
    emit("mov", "r5,r1");      // r4:r5 = rhs
    emit("ldind", "sp,r0");
    emit("add", "sp,sp,4");
    emit("ldind", "sp,r1");
    emit("add", "sp,sp,4");  // r0:r1 = lhs
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    if (op == "==" || op == "!=") {
        const char *onEqual = op == "==" ? "1" : "0";
        const char *onDiffer = op == "==" ? "0" : "1";
        emit("cmp", "r0,r4");
        emit("bne", Lfalse);
        emit("cmp", "r1,r5");
        emit("bne", Lfalse);
        emit("mov", cat("r0,", onEqual));
        emit("b", Ldone);
        emitLabel(Lfalse);
        emit("mov", cat("r0,", onDiffer));
        emitLabel(Ldone);
        return;
    }
    std::string signedCC = op == "<" ? "lt" : op == "<=" ? "le" : op == ">" ? "gt" : "ge";
    std::string unsignedCC = op == "<" ? "lo" : op == "<=" ? "ls" : op == ">" ? "hi" : "hs";
    std::string hiCC = isUnsigned ? unsignedCC : signedCC;
    std::string LhiDiff = newLabel();
    emit("cmp", "r1,r5");
    emit("bne", LhiDiff);
    emit("cmp", "r0,r4");
    emit(cat("b", unsignedCC), Ltrue);
    emit("b", Lfalse);
    emitLabel(LhiDiff);
    emit(cat("b", hiCC), Ltrue);
    emit("b", Lfalse);
    emitLabel(Lfalse);
    emit("mov", "r0,0");
    emit("b", Ldone);
    emitLabel(Ltrue);
    emit("mov", "r0,1");
    emitLabel(Ldone);
}

std::string ArmCodegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    // Separate compilation means `prog` may not define `main` at all.
    // `objectMode` means no entry sequence is emitted here -
    // ArmLinkerFactory.cpp synthesizes one once every object is merged
    // and `main`'s linked address is resolvable. An object file's base is
    // `org 0`; placing it is the linker's job.
    emitRaw(cat("org ", (objectMode ? 0 : kBaseAddress)));
    emitRaw("");
    if (!objectMode) {
        emitLabel("_start");
        emit("movw", cat("sp,", static_cast<int32_t>(kInitialSP & 0xFFFFu)));
        emit("movt", cat("sp,", static_cast<int32_t>((kInitialSP >> 16) & 0xFFFFu)));
        emit("bl", label(mainFn));
        // The result sits in r0 the moment execution reaches `halt`.
        emitLabel("halt");
        emit("b", "halt");
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

    // Running byte offset within the data segment (4-byte aligned from
    // the start), used to insert alignment padding ahead of a global
    // wider than 1 byte: an unaligned LDRH/STRH/LDR/STR access is
    // UNPREDICTABLE on this core and would silently round down to the
    // previous aligned address, clobbering whatever global sits there. A
    // `long long` only needs word alignment - it's always accessed as two
    // separate word-sized ldind/stind.
    size_t dataOffset = 0;
    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        // An external global isn't defined in this file - it must never
        // get a local storage word, only be referenced by name, letting
        // the assembler's undefined-symbol handling defer it to a relocation.
        if (slot->isExternal) continue;
        if (debugMapEnabled_) debugSymbols_.push_back({label(slot.get()), /*isFunc=*/false});
        size_t gTrailerStart = trailer_.size();
        int align = std::min(widthOfType(slot->type), 4);
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
    for (const auto &line : trailer_) out << line << "\n";
    return out.str();
}

std::map<std::string, std::string> ArmCodegen::perFileAsm() const { return perFileAsm_; }

// Every function saves/restores `lr` around its body, whether or not it
// calls anything else: ARM has exactly one link register, and a non-leaf
// function's own `BL` overwrites it, so without this save/restore the
// eventual `bx lr` would return to the wrong place. Always saving/
// restoring is simpler than tracking which functions are leaf.
//
// Every function also gets a per-call stack frame: after saving `lr`, it
// saves the caller's `fp`, points `fp` at that saved pair, and reserves
// `frameSize` bytes below it for its Locals (see loadFrameAddr() for the
// resulting address layout). This makes direct or indirect recursion
// safe: every activation gets its own `fp` and reserved Local words.
void ArmCodegen::genFunction(const ir::FunctionBody &fn) {
    lastDebugLine_ = 0;
    if (debugMapEnabled_) debugSymbols_.push_back({label(fn.func), /*isFunc=*/true});
    emitLabel(label(fn.func));
    emit("sub", "sp,sp,4");
    emit("stind", "sp,lr");
    emit("sub", "sp,sp,4");
    emit("stind", "sp,fp");
    emit("mov", "fp,sp");
    if (fn.func->frameSize > 0) emitFrameOffset("sp", fn.func->frameSize, /*isAdd=*/false);

    genStmt(*fn.body);

    emit("mov", "sp,fp");
    emit("ldind", "sp,fp");
    emit("add", "sp,sp,4");
    emit("ldind", "sp,lr");
    emit("add", "sp,sp,4");
    emit("bx", "lr");
    emitRaw("");
}

void ArmCodegen::genStmt(const ir::Stmt &s) {
    emitSourceLine(s.line);
    switch (s.kind) {
        case ir::StmtKind::Block: {
            for (const auto &child : s.body) genStmt(*child);
            return;
        }
        case ir::StmtKind::If: {
            genExpr(*s.cond);
            if (isWide(s.cond->type)) emit("orr", "r0,r0,r1");
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            emit("cmp", "r0,0");
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
            if (isWide(s.cond->type)) emit("orr", "r0,r0,r1");
            emit("cmp", "r0,0");
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
            // An early return bypasses genFunction()'s fall-through
            // epilogue, so it needs this same frame teardown inline.
            emit("mov", "sp,fp");
            emit("ldind", "sp,fp");
            emit("add", "sp,sp,4");
            emit("ldind", "sp,lr");
            emit("add", "sp,sp,4");
            emit("bx", "lr");
            return;
        }
        case ir::StmtKind::LocalDecl: {
            if (s.declSlot->arraySize > 0) {
                genLocalArrayInit(s.declSlot);
            } else if (s.initOrReturnOrExpr) {
                genExpr(*s.initOrReturnOrExpr);
                loadSlotAddr("r2", s.declSlot);
                emit("stind", "r2,r0");
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
            emit("b", irLabel(s.labelId));
            return;
        }
    }
}

void ArmCodegen::genExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit: {
            if (isWide(e.type)) {
                auto v = static_cast<uint64_t>(e.intValue);
                uint32_t lo = static_cast<uint32_t>(v), hi = static_cast<uint32_t>(v >> 32);
                emit("movw", cat("r0,", static_cast<int32_t>(lo & 0xFFFFu)));
                if ((lo >> 16) != 0) emit("movt", cat("r0,", static_cast<int32_t>((lo >> 16) & 0xFFFFu)));
                emit("movw", cat("r1,", static_cast<int32_t>(hi & 0xFFFFu)));
                if ((hi >> 16) != 0) emit("movt", cat("r1,", static_cast<int32_t>((hi >> 16) & 0xFFFFu)));
                return;
            }
            uint32_t uv = static_cast<uint32_t>(e.intValue);
            emit("movw", cat("r0,", static_cast<int32_t>(uv & 0xFFFFu)));
            if ((uv >> 16) != 0) {
                emit("movt", cat("r0,", static_cast<int32_t>((uv >> 16) & 0xFFFFu)));
            }
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
                emit("sub", "sp,sp,4");
                emit("stind", "sp,r2");
                genExpr(*e.rhs);
                emit("ldind", "sp,r2");
                emit("add", "sp,sp,4");
                storeOperand("r2", e.slot->type, /*mayBeUnaligned=*/e.slot->arraySize == 0);
                return;
            }
            genExpr(*e.rhs);
            loadSlotAddr("r2", e.slot);
            storeOperand("r2", e.slot->type);
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            if (e.lhs) {  // slot[idx] op= rhs (including slot[idx]++/--)
                genIndexAddr(e.slot, *e.lhs);  // r2 = target address
                genCompoundThroughR2(e, /*mayBeUnaligned=*/e.slot->arraySize == 0);
                return;
            }
            // A plain scalar target still needs its address computed
            // (ARM has no absolute-addressing memory operand), so it
            // goes through the same held-address tail as the indexed form.
            loadSlotAddr("r2", e.slot);
            genCompoundThroughR2(e);
            return;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            genExpr(*e.lhs);  // r0 = pointer value
            emit("mov", "r2,r0");  // r2 = address
            genCompoundThroughR2(e, /*mayBeUnaligned=*/true);
            return;
        }
        case ir::ExprKind::Ternary: {
            genExpr(*e.lhs);
            // The condition's type can still be wide, so this reuses
            // If/While's wide-OR-then-compare idiom, not a bare r0 test.
            if (isWide(e.lhs->type)) emit("orr", "r0,r0,r1");
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            emit("cmp", "r0,0");
            emit("beq", Lelse);
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
            genExpr(*e.lhs);  // r0 = pointer value
            emit("sub", "sp,sp,4");
            emit("stind", "sp,r0");
            genExpr(*e.rhs);  // r0, or r0:r1 if wide
            emit("ldind", "sp,r2");
            emit("add", "sp,sp,4");
            storeOperand("r2", e.type, /*mayBeUnaligned=*/true);
            return;
        }
        case ir::ExprKind::Unary: {
            if (e.op == "-") {
                genExpr(*e.rhs);
                if (isWide(e.rhs->type)) {
                    genNegate64();
                } else {
                    emit("rsb", "r0,r0,0");
                }
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                // Loads at e.type's width/signedness - see DerefAssign above.
                genExpr(*e.rhs);  // r0 = pointer value
                emit("mov", "r2,r0");
                loadOperand("r2", e.type, /*mayBeUnaligned=*/true);
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
            // Arguments are pushed right-to-left on the hardware stack,
            // so arg 0 ends up closest to the saved fp/lr pair, at `fp+8`
            // inside the callee. The callee never cleans these up itself,
            // so the caller restores `sp` here once the call returns.
            // Every argument reserves at least one native word, and a wide
            // (`long long`) argument pushes its high word first so its low
            // word ends up closer to `fp`, matching a wide Param's
            // frameOffset/frameOffset+4 layout.
            int totalBytes = 0;
            for (size_t i = e.args.size(); i-- > 0;) {
                genExpr(*e.args[i]);
                if (isWide(e.args[i]->type)) {
                    emit("sub", "sp,sp,4");
                    emit("stind", "sp,r1");
                    emit("sub", "sp,sp,4");
                    emit("stind", "sp,r0");
                    totalBytes += 8;
                } else {
                    emit("sub", "sp,sp,4");
                    emit("stind", "sp,r0");
                    totalBytes += 4;
                }
            }
            emit("bl", label(e.func));
            if (totalBytes > 0) emitFrameOffset("sp", totalBytes, /*isAdd=*/true);
            return;
        }
        case ir::ExprKind::CallPtr: {
            // e.args is always empty here - IRBuilder.cpp rejects any
            // argument on this kind, since the callee isn't known at
            // compile time. `blx r2` is a near call through whatever
            // address r2 holds, setting `lr` like `bl` does.
            genExpr(*e.lhs);  // r0 = the callee's entry address
            emit("mov", "r2,r0");
            emit("blx", "r2");
            return;
        }
        case ir::ExprKind::Binary: {
            genBinary(e);
            return;
        }
        case ir::ExprKind::Intrinsic: {
            // No portable interrupt enable/disable on this bare-metal
            // core with no exception/GIC setup - treated as a no-op.
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

// `r2` holds the computed address. `arraySize > 0` means the base is the
// array's own address (loadSlotAddr() dispatches Global vs. Local);
// otherwise `slot` is a plain pointer variable, so the base comes from
// reading that variable's stored value first. Either array's element
// addresses increase with `idx`.
void ArmCodegen::genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
    genExpr(idxExpr);  // r0 = index
    int shift = elemShift(slot->type);
    if (shift > 0) {
        emit("lsl", cat("r0,r0,", shift));  // r0 *= the element type's own width
    } else if (shift < 0) {
        // Non-power-of-two element width (a struct's): `mul` is register-only (no
        // immediate operand), so the width has to be loaded into a register first.
        emit("mov", cat("r3,", widthOfType(slot->type)));
        emit("mul", "r0,r0,r3");
    }
    if (slot->arraySize > 0) {
        loadSlotAddr("r2", slot);
        emit("add", "r2,r2,r0");
    } else {
        loadSlotAddr("r1", slot);
        emit("ldind", "r1,r1");  // r1 = the pointer variable's own stored value
        emit("add", "r2,r1,r0");
    }
}

void ArmCodegen::genIndex(const ir::Expr &e) {
    genIndexAddr(e.slot, *e.lhs);
    loadOperand("r2", e.slot->type, /*mayBeUnaligned=*/e.slot->arraySize == 0);
}

void ArmCodegen::genAddrOf(const ir::Expr &operand) {
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
        loadAddress("r2", label(operand.func));
        emit("mov", "r0,r2");
        return;
    }
    err(operand.line, "internal: unhandled addressable-operand kind");
}

// Entry: r0 = old, r1 = rhs (both already at `type`'s width/signedness).
void ArmCodegen::genCompoundOp(const std::string &op, Type type) {
    bool isUnsigned = type.isUnsigned;
    if (op == "+") { emit("add", "r0,r0,r1"); return; }
    if (op == "-") { emit("sub", "r0,r0,r1"); return; }
    if (op == "*") { emit("mul", "r0,r0,r1"); return; }
    if (op == "&") { emit("and", "r0,r0,r1"); return; }
    if (op == "|") { emit("orr", "r0,r0,r1"); return; }
    if (op == "^") { emit("eor", "r0,r0,r1"); return; }
    if (op == "<<" || op == ">>") {
        const char *insn = op == "<<" ? "lsl" : (isUnsigned ? "lsr" : "asr");
        emit(insn, "r0,r0,r1");
        return;
    }
    // "/" or "%" - r3 (genDivMod()'s own quotient-holding scratch) is
    // free to reuse here: by this point e.rhs has already fully
    // evaluated, so nothing still needs whatever r3 held during that
    // evaluation.
    emit((isUnsigned ? "udiv" : "sdiv"), "r3,r0,r1");
    if (op == "%") {
        emit("mls", "r0,r3,r1,r0");  // r0 = r0 - r3*r1 = old - quotient*rhs
    } else {
        emit("mov", "r0,r3");
    }
}

// r2 must already hold the resolved address on entry. The address is
// stashed on the stack across evaluating e.rhs (which can reach
// arbitrary nested codegen using r0 through r5) and never recomputed, so
// an indexed/pointer target's address sub-expression is evaluated exactly once.
void ArmCodegen::genCompoundThroughR2(const ir::Expr &e, bool mayBeUnaligned) {
    emit("sub", "sp,sp,4");
    emit("stind", "sp,r2");  // stash address
    genExpr(*e.rhs);                                  // r0 = rhs
    emit("mov", "r1,r0");                        // r1 = rhs
    emit("ldind", "sp,r2");
    emit("add", "sp,sp,4");      // r2 = address (restored)
    loadOperand("r2", e.type, mayBeUnaligned);            // r0 = old
    if (e.yieldOld) {
        emit("sub", "sp,sp,4");
        emit("stind", "sp,r0");
    }  // stash old
    genCompoundOp(e.op, e.type);                           // r0 = new (raw)
    narrowR0To(e.type);
    if (e.yieldOld) {
        emit("mov", "r3,r0");                          // r3 = new
        emit("ldind", "sp,r0");
        emit("add", "sp,sp,4");         // r0 = old
        emit("mov", "r1,r0");                             // r1 = old, preserved across the store below
        emit("mov", "r0,r3");                              // r0 = new - storeOperand always stores from r0
        storeOperand("r2", e.type, mayBeUnaligned);
        emit("mov", "r0,r1");                               // r0 = old (final result)
    } else {
        storeOperand("r2", e.type, mayBeUnaligned);  // r0 already = new
    }
}

void ArmCodegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    // Every arithmetic/bitwise/comparison operand here shares one common
    // type (IRBuilder's usual-arithmetic-conversion pass), except shift
    // (only the LHS's type matters) and &&/|| (width-independent).
    Type operandType = e.lhs->type;
    bool wide = isWide(operandType);
    bool isUnsigned = operandType.isUnsigned;
    if (op == "+" || op == "-") {
        if (wide) {
            genWideAdd(*e.lhs, *e.rhs, op == "+");
            return;
        }
        genExpr(*e.lhs);
        emit("sub", "sp,sp,4");
        emit("stind", "sp,r0");
        genExpr(*e.rhs);
        emit("ldind", "sp,r1");
        emit("add", "sp,sp,4");  // r1 = lhs, r0 = rhs
        if (op == "+") {
            emit("add", "r0,r1,r0");  // r0 = lhs + rhs
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
            emit("sub", "sp,sp,4");
            emit("stind", "sp,r1");
            emit("sub", "sp,sp,4");
            emit("stind", "sp,r0");
            genExpr(*e.rhs);
            emit("mov", "r4,r0");
            emit("mov", "r5,r1");
            emit("ldind", "sp,r0");
            emit("add", "sp,sp,4");
            emit("ldind", "sp,r1");
            emit("add", "sp,sp,4");
            emit(insn, "r0,r0,r4");
            emit(insn, "r1,r1,r5");
            return;
        }
        genExpr(*e.lhs);
        emit("sub", "sp,sp,4");
        emit("stind", "sp,r0");
        genExpr(*e.rhs);
        emit("ldind", "sp,r1");
        emit("add", "sp,sp,4");  // r1 = lhs, r0 = rhs
        emit(insn, "r0,r1,r0");
        return;
    }
    if (op == "<<" || op == ">>") {
        if (wide) {
            genWideShift(*e.lhs, *e.rhs, op == "<<", isUnsigned);
            return;
        }
        genExpr(*e.lhs);
        emit("sub", "sp,sp,4");
        emit("stind", "sp,r0");
        genExpr(*e.rhs);                                  // r0 = shift count
        emit("mov", "r1,r0");                        // r1 = shift count
        emit("ldind", "sp,r0");
        emit("add", "sp,sp,4");     // r0 = value to shift
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

// Real hardware multiply (`MUL`) - a full 32x32->32 product in one
// instruction, no HI/LO-register-pair unpacking the way MIPS's own
// `MULT`+`MFLO` needs.
void ArmCodegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    emit("sub", "sp,sp,4");
    emit("stind", "sp,r0");
    genExpr(rhs);
    emit("ldind", "sp,r1");
    emit("add", "sp,sp,4");  // r1 = lhs, r0 = rhs
    emit("mul", "r0,r1,r0");
}

// Real hardware divide (`SDIV`/`UDIV`) - a full 32-bit quotient directly.
// The remainder (`%`) then uses `MLS` to compute `dividend -
// quotient*divisor`; `r3` holds a copy of the divisor so `MLS`'s `Rd` and
// `Rm` are never the same register, which several ARM cores treat as
// UNPREDICTABLE for multiply-family instructions.
void ArmCodegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    emit("sub", "sp,sp,4");
    emit("stind", "sp,r0");
    genExpr(rhs);
    emit("ldind", "sp,r1");
    emit("add", "sp,sp,4");  // r1 = lhs (dividend), r0 = rhs (divisor)
    emit((isUnsigned ? "udiv" : "sdiv"), "r3,r1,r0");  // r3 = quotient
    if (!wantRemainder) {
        emit("mov", "r0,r3");
        return;
    }
    emit("mov", "r2,r0");          // r2 = divisor copy
    emit("mls", "r0,r3,r2,r1");    // r0 = r1 - r3*r2 = remainder
}

// Every ARM data-processing instruction is conditionally executed per its
// 4-bit condition field, so materializing a 0/1 boolean needs no branch:
// `MOV r0,#0` followed by one conditionally executed `MOV r0,#1`.
// `isUnsigned` picks the HS/LO/HI/LS condition family instead of
// LT/GE/LE/GT for the ordered operators.
void ArmCodegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs) {
    if (isWide(lhs.type)) {
        genWideCompare(op, lhs, rhs, lhs.type.isUnsigned);
        return;
    }
    bool isUnsigned = lhs.type.isUnsigned;
    genExpr(lhs);
    emit("sub", "sp,sp,4");
    emit("stind", "sp,r0");
    genExpr(rhs);
    emit("ldind", "sp,r1");
    emit("add", "sp,sp,4");  // r1 = lhs, r0 = rhs
    emit("cmp", "r1,r0");                      // flags = lhs - rhs
    emit("mov", "r0,0");
    if (op == "<") {
        emit(cat("mov", (isUnsigned ? "lo" : "lt")), "r0,1");
    } else if (op == ">") {
        emit(cat("mov", (isUnsigned ? "hi" : "gt")), "r0,1");
    } else if (op == "<=") {
        emit(cat("mov", (isUnsigned ? "ls" : "le")), "r0,1");
    } else if (op == ">=") {
        emit(cat("mov", (isUnsigned ? "hs" : "ge")), "r0,1");
    } else if (op == "==") {
        emit("moveq", "r0,1");
    } else if (op == "!=") {
        emit("movne", "r0,1");
    } else {
        err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
    }
}

// A wide (`long long`) operand's truthiness needs both words - `orr
// r0,r0,r1` folds them together before the usual "cmp r0,0".
void ArmCodegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    if (isWide(lhs.type)) emit("orr", "r0,r0,r1");
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    emit("cmp", "r0,0");
    emit("beq", Lfalse);
    genExpr(rhs);
    if (isWide(rhs.type)) emit("orr", "r0,r0,r1");
    emit("cmp", "r0,0");
    emit("beq", Lfalse);
    emit("b", Ltrue);
    emitLabel(Lfalse);
    emit("mov", "r0,0");
    emit("b", Ldone);
    emitLabel(Ltrue);
    emit("mov", "r0,1");
    emitLabel(Ldone);
}

void ArmCodegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    if (isWide(lhs.type)) emit("orr", "r0,r0,r1");
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    emit("cmp", "r0,0");
    emit("bne", Ltrue);
    genExpr(rhs);
    if (isWide(rhs.type)) emit("orr", "r0,r0,r1");
    emit("cmp", "r0,0");
    emit("bne", Ltrue);
    emit("b", Lfalse);
    emitLabel(Ltrue);
    emit("mov", "r0,1");
    emit("b", Ldone);
    emitLabel(Lfalse);
    emit("mov", "r0,0");
    emitLabel(Ldone);
}

// `!x` is exactly "x == 0" - one `CMP`+conditional-`MOV` pair.
void ArmCodegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    if (isWide(rhs.type)) emit("orr", "r0,r0,r1");
    emit("cmp", "r0,0");
    emit("mov", "r0,0");
    emit("moveq", "r0,1");
}

std::optional<std::string> ArmCodegen::assembleInProcess(const std::string &asmText,
                                                           const std::string &format) const {
    if (format == "raw") return assembleArm(asmText);
    // `coff`/`elf`/`macho`/`aout`/`objraw`: assembleArmObject() builds one
    // format-neutral ObjectFile, and the requested format just picks that
    // format's own FileFormatWriter to serialize it.
    if (auto kind = fileFormatKindFromName(format)) {
        ObjectFile obj = assembleArmObject(asmText);
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

std::optional<std::string> ArmCodegen::debugMap(const std::string &asmText, const std::string &format,
        const std::string &sourcePath) const {
    (void)format;
    return assembleArmDebugMap(asmText, sourcePath, debugSymbols_);
}

std::optional<std::string> ArmCodegen::debugMapPreLink(const std::string &asmText,
                                                    const std::string &sourcePath) const {
    return assembleArmDebugMap(asmText, sourcePath, debugSymbols_, /*allowExternal=*/true);
}

// Only emits a marker when `debugMapEnabled_` (set via setDebugMapEnabled(),
// before generate() runs) and `line` actually differs from the last one
// emitted - see Pdp7Codegen.cpp's identical emitSourceLine(), which this
// mirrors exactly.
void ArmCodegen::emitSourceLine(int line) {
    if (!debugMapEnabled_ || line == 0 || line == lastDebugLine_) return;
    emitRaw(cat("__line ", line));
    lastDebugLine_ = line;
}

}  // namespace rcc
