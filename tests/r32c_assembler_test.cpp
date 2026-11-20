#include "codegen/r32c/R32cAssembler.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

std::string bytesOf(const std::string &asmText) { return rcc::assembleR32c(asmText); }

void pushAddr32(std::string &s, uint32_t v) {
    s.push_back(static_cast<char>(v & 0xFF));
    s.push_back(static_cast<char>((v >> 8) & 0xFF));
    s.push_back(static_cast<char>((v >> 16) & 0xFF));
    s.push_back(static_cast<char>((v >> 24) & 0xFF));
}

// `mov 1234,r0` - word immediate-to-register move: opcode 0x10, src tag 1
// (immediate) + 16-bit value, dst tag 0 (register) + register code 0.
void testMovWordImmediateToReg() {
    std::string bytes = bytesOf("mov 1234,r0\n");
    std::string expected;
    expected.push_back(0x10);
    expected.push_back(0x01);
    expected.push_back(static_cast<char>(1234 & 0xFF));
    expected.push_back(static_cast<char>((1234 >> 8) & 0xFF));
    expected.push_back(0x00);
    expected.push_back(0x00);
    require(bytes == expected, "mov 1234,r0 didn't produce the expected byte sequence");
}

// `add r1,r0` - word register-to-register add: opcode 0x11, two register
// operands.
void testAddWordRegToReg() {
    std::string bytes = bytesOf("add r1,r0\n");
    std::string expected;
    expected.push_back(0x11);
    expected.push_back(0x00);
    expected.push_back(0x01);
    expected.push_back(0x00);
    expected.push_back(0x00);
    require(bytes == expected, "add r1,r0 didn't produce the expected byte sequence");
}

// `movb 5,r0` / `addb r1,r0` - the byte-sized family: same opcode shapes as
// the word family, distinct opcode bytes (0x40/0x41), and a one-byte
// immediate field instead of two.
void testByteFamily() {
    std::string bytes = bytesOf("movb 5,r0\naddb r1,r0\n");
    std::string expected;
    expected.push_back(0x40);
    expected.push_back(0x01);
    expected.push_back(0x05);
    expected.push_back(0x00);
    expected.push_back(0x00);
    expected.push_back(0x41);
    expected.push_back(0x00);
    expected.push_back(0x01);
    expected.push_back(0x00);
    expected.push_back(0x00);
    require(bytes == expected, "movb/addb didn't produce the expected byte sequence");
}

// `movl 1,r2r0` - dword move: opcode 0x60, a 4-byte immediate field (not
// 2), and the pair-register code (8) as the destination.
void testMovDwordImmediateToPairReg() {
    std::string bytes = bytesOf("movl 1,r2r0\n");
    std::string expected;
    expected.push_back(0x60);
    expected.push_back(0x01);
    expected.push_back(0x01);
    expected.push_back(0x00);
    expected.push_back(0x00);
    expected.push_back(0x00);
    expected.push_back(0x00);
    expected.push_back(0x08);
    require(bytes == expected, "movl 1,r2r0 didn't produce the expected byte sequence");
}

// `addl r3r1,r2r0` - dword register-pair add: opcode 0x61, register codes
// 9 (r3r1) and 8 (r2r0).
void testAddDwordPairRegs() {
    std::string bytes = bytesOf("addl r3r1,r2r0\n");
    std::string expected;
    expected.push_back(0x61);
    expected.push_back(0x00);
    expected.push_back(0x09);
    expected.push_back(0x00);
    expected.push_back(0x08);
    require(bytes == expected, "addl r3r1,r2r0 didn't produce the expected byte sequence");
}

// A plain register (not a pair) rejected as a dword operand, and a pair
// register rejected as a word operand - R32C has no way to address either
// mismatch (same restriction as its M32C ancestor).
void testDwordRejectsPlainRegister() {
    bool threw = false;
    try {
        bytesOf("addl r1,r2r0\n");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "'addl r1,r2r0' should be rejected - r1 isn't a register pair");
}

void testWordRejectsPairRegister() {
    bool threw = false;
    try {
        bytesOf("add r2r0,r0\n");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "'add r2r0,r0' should be rejected - r2r0 isn't a plain word register");
}

// `mull r3r1,r2r0` - dword multiply, fixed `r2r0` destination. The
// destination isn't itself re-encoded in the byte stream (it's always
// `r2r0`, so there's nothing to distinguish), matching `mul`/`divu`'s own
// existing fixed-`r0`-destination convention.
void testMullFixedAccumulator() {
    std::string bytes = bytesOf("mull r3r1,r2r0\n");
    std::string expected;
    expected.push_back(0x6D);
    expected.push_back(0x00);
    expected.push_back(0x09);
    require(bytes == expected, "mull r3r1,r2r0 didn't produce the expected byte sequence");
}

void testMullRejectsWrongAccumulator() {
    bool threw = false;
    try {
        bytesOf("mull r3r1,r0\n");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "'mull' should reject any destination other than r2r0");
}

// `divul`/`divxl` - unsigned/signed dword divide, same fixed-accumulator
// shape as `mull`.
void testDivulAndDivxl() {
    std::string bytes = bytesOf("divul r3r1,r2r0\ndivxl r3r1,r2r0\n");
    std::string expected;
    expected.push_back(0x6E);
    expected.push_back(0x00);
    expected.push_back(0x09);
    expected.push_back(0x6F);
    expected.push_back(0x00);
    expected.push_back(0x09);
    require(bytes == expected, "divul/divxl didn't produce the expected byte sequence");
}

// `shll r1,r2r0` / `shal r1,r2r0` - dword shift, whose count operand (`r1`)
// is a plain word register even though the destination is a pair - the
// one asymmetric-width instruction pair in this dialect.
void testShiftCountIsWordWidth() {
    std::string bytes = bytesOf("shll r1,r2r0\n");
    std::string expected;
    expected.push_back(0x70);
    expected.push_back(0x00);
    expected.push_back(0x01);
    expected.push_back(0x00);
    expected.push_back(0x08);
    require(bytes == expected, "shll r1,r2r0 didn't produce the expected byte sequence");
}

void testShiftRejectsPairCount() {
    bool threw = false;
    try {
        bytesOf("shll r2r0,r2r0\n");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "'shll' should reject a register pair as its shift-count operand");
}

// `not`/`neg`/`rol`/`ror` and their dword counterparts - the one-operand
// family.
void testOneOpWordAndDword() {
    std::string bytes = bytesOf("not r0\nnegl r2r0\n");
    std::string expected;
    expected.push_back(0x1B);
    expected.push_back(0x00);
    expected.push_back(0x00);
    expected.push_back(0x6A);
    expected.push_back(0x00);
    expected.push_back(0x08);
    require(bytes == expected, "not/negl didn't produce the expected byte sequence");
}

// `pushl r2r0` / `popl r2r0` - dword stack save/restore, one opcode byte
// plus a bare register code (no operand tag, unlike every other
// instruction here - matches `push`/`pop`'s own existing shape).
void testPushlPopl() {
    std::string bytes = bytesOf("pushl r2r0\npopl r2r0\n");
    std::string expected;
    expected.push_back(0x72);
    expected.push_back(0x08);
    expected.push_back(0x73);
    expected.push_back(0x08);
    require(bytes == expected, "pushl/popl didn't produce the expected byte sequence");
}

// `ldl a0,r2r0` / `stl r2r0,a0` - dword register-indirect load/store
// through an address register.
void testLdlStl() {
    std::string bytes = bytesOf("ldl a0,r2r0\nstl r2r0,a0\n");
    std::string expected;
    expected.push_back(0x74);
    expected.push_back(0x04);
    expected.push_back(0x08);
    expected.push_back(0x75);
    expected.push_back(0x08);
    expected.push_back(0x04);
    require(bytes == expected, "ldl/stl didn't produce the expected byte sequence");
}

// `bset 3,r0` / `btst 0,ac` - bit manipulation: a byte-sized immediate bit
// number, then the destination operand. `ac`'s own absolute-address field
// is 4 bytes here (unlike M32cAssembler's 2-byte field), the one visible
// encoding difference this dialect's wider address space produces.
void testBitOps() {
    std::string asmText =
        "org 0x1000\n"
        "ac: dw 0\n"
        "bset 3,r0\n"
        "btst 0,ac\n";
    std::string bytes = bytesOf(asmText);
    std::string expected;
    expected.push_back(0x00);
    expected.push_back(0x00);  // ac's own `dw 0`
    expected.push_back(0x80);  // bset
    expected.push_back(0x01);
    expected.push_back(0x03);
    expected.push_back(0x00);
    expected.push_back(0x00);
    expected.push_back(0x82);  // btst
    expected.push_back(0x01);
    expected.push_back(0x00);
    expected.push_back(0x02);  // `ac`'s own absolute-address tag (a bare label is Mem, not Disp)
    pushAddr32(expected, 0x1000);  // ac = 0x1000, little-endian, 4-byte field
    require(bytes == expected, "bset/btst didn't produce the expected byte sequence");
}

// `bclr 7,r3` / `bnot 2,r1` - the other two bit-manipulation mnemonics.
void testBclrBnot() {
    std::string bytes = bytesOf("bclr 7,r3\nbnot 2,r1\n");
    std::string expected;
    expected.push_back(0x81);
    expected.push_back(0x01);
    expected.push_back(0x07);
    expected.push_back(0x00);
    expected.push_back(0x03);
    expected.push_back(0x83);
    expected.push_back(0x01);
    expected.push_back(0x02);
    expected.push_back(0x00);
    expected.push_back(0x01);
    require(bytes == expected, "bclr/bnot didn't produce the expected byte sequence");
}

// `pushm 0x0F` / `popm 0x0F` - multiple-register save/restore, an
// immediate register-bitmask operand.
void testPushmPopm() {
    std::string bytes = bytesOf("pushm 0x0F\npopm 0x0F\n");
    std::string expected;
    expected.push_back(0x84);
    expected.push_back(0x01);
    expected.push_back(0x0F);
    expected.push_back(0x85);
    expected.push_back(0x01);
    expected.push_back(0x0F);
    require(bytes == expected, "pushm/popm didn't produce the expected byte sequence");
}

// `enter 16` / `exitd` - the compound frame-setup/teardown pair.
void testEnterExitd() {
    std::string bytes = bytesOf("enter 16\nexitd\n");
    std::string expected;
    expected.push_back(0x86);
    expected.push_back(0x01);
    expected.push_back(0x10);
    expected.push_back(0x00);
    expected.push_back(0x87);
    require(bytes == expected, "enter/exitd didn't produce the expected byte sequence");
}

// `reit`/`nop`/`brk`/`und` - the remaining zero-operand instructions.
void testZeroOperandMisc() {
    std::string bytes = bytesOf("reit\nnop\nbrk\nund\n");
    std::string expected;
    expected.push_back(0x88);
    expected.push_back(0x89);
    expected.push_back(0x8A);
    expected.push_back(0x8B);
    require(bytes == expected, "reit/nop/brk/und didn't produce the expected byte sequence");
}

// `exts` / `extz` - sign/zero-extend r0 into r2r0, no operand.
void testExtsExtz() {
    std::string bytes = bytesOf("exts\nextz\n");
    std::string expected;
    expected.push_back(0x8C);
    expected.push_back(0x8D);
    require(bytes == expected, "exts/extz didn't produce the expected byte sequence");
}

// `cmp`/`cmpb`/`cmpl` - the read-only comparison family, one per operand
// size, none of which accepts an immediate destination (same restriction
// as every writing two-operand instruction).
void testCmpFamily() {
    std::string bytes = bytesOf("cmp r1,r0\ncmpb r1,r0\ncmpl r3r1,r2r0\n");
    std::string expected;
    expected.push_back(0x1A);
    expected.push_back(0x00);
    expected.push_back(0x01);
    expected.push_back(0x00);
    expected.push_back(0x00);
    expected.push_back(0x4A);
    expected.push_back(0x00);
    expected.push_back(0x01);
    expected.push_back(0x00);
    expected.push_back(0x00);
    expected.push_back(0x68);
    expected.push_back(0x00);
    expected.push_back(0x09);
    expected.push_back(0x00);
    expected.push_back(0x08);
    require(bytes == expected, "cmp/cmpb/cmpl didn't produce the expected byte sequence");
}

// `rol`/`ror`/`shl`/`sha` (word), `rolb`/`rorb`/`shlb`/`shab` (byte),
// `roll`/`rorl` (dword) - the rotate/shift-by-register families not yet
// exercised above (`shll`/`shal`'s asymmetric-width case already is).
void testRotateAndShiftFamily() {
    std::string bytes = bytesOf("rol r0\nror r0\nshl r1,r0\nsha r1,r0\nrolb r0\nrorb r0\nshlb r1,r0\nshab r1,r0\n"
                                 "roll r2r0\nrorl r2r0\n");
    std::string expected;
    expected.push_back(0x1D);
    expected.push_back(0x00);
    expected.push_back(0x00);  // rol r0
    expected.push_back(0x1E);
    expected.push_back(0x00);
    expected.push_back(0x00);  // ror r0
    expected.push_back(0x16);
    expected.push_back(0x00);
    expected.push_back(0x01);
    expected.push_back(0x00);
    expected.push_back(0x00);  // shl r1,r0
    expected.push_back(0x17);
    expected.push_back(0x00);
    expected.push_back(0x01);
    expected.push_back(0x00);
    expected.push_back(0x00);  // sha r1,r0
    expected.push_back(0x4D);
    expected.push_back(0x00);
    expected.push_back(0x00);  // rolb r0
    expected.push_back(0x4E);
    expected.push_back(0x00);
    expected.push_back(0x00);  // rorb r0
    expected.push_back(0x46);
    expected.push_back(0x00);
    expected.push_back(0x01);
    expected.push_back(0x00);
    expected.push_back(0x00);  // shlb r1,r0
    expected.push_back(0x47);
    expected.push_back(0x00);
    expected.push_back(0x01);
    expected.push_back(0x00);
    expected.push_back(0x00);  // shab r1,r0
    expected.push_back(0x6B);
    expected.push_back(0x00);
    expected.push_back(0x08);  // roll r2r0
    expected.push_back(0x6C);
    expected.push_back(0x00);
    expected.push_back(0x08);  // rorl r2r0
    require(bytes == expected, "rotate/shift family didn't produce the expected byte sequence");
}

// `adc`/`sbb` (word), `adcb`/`sbbb` (byte), `adcl`/`sbbl` (dword) - the
// carry/borrow-chain family used to ripple a multi-word `long long`
// add/subtract.
void testAdcSbbFamily() {
    std::string bytes = bytesOf("adc r1,r0\nsbb r1,r0\nadcb r1,r0\nsbbb r1,r0\nadcl r3r1,r2r0\nsbbl r3r1,r2r0\n");
    std::string expected;
    expected.push_back(0x18);
    expected.push_back(0x00);
    expected.push_back(0x01);
    expected.push_back(0x00);
    expected.push_back(0x00);  // adc
    expected.push_back(0x19);
    expected.push_back(0x00);
    expected.push_back(0x01);
    expected.push_back(0x00);
    expected.push_back(0x00);  // sbb
    expected.push_back(0x48);
    expected.push_back(0x00);
    expected.push_back(0x01);
    expected.push_back(0x00);
    expected.push_back(0x00);  // adcb
    expected.push_back(0x49);
    expected.push_back(0x00);
    expected.push_back(0x01);
    expected.push_back(0x00);
    expected.push_back(0x00);  // sbbb
    expected.push_back(0x66);
    expected.push_back(0x00);
    expected.push_back(0x09);
    expected.push_back(0x00);
    expected.push_back(0x08);  // adcl
    expected.push_back(0x67);
    expected.push_back(0x00);
    expected.push_back(0x09);
    expected.push_back(0x00);
    expected.push_back(0x08);  // sbbl
    require(bytes == expected, "adc/sbb family didn't produce the expected byte sequence");
}

// `and`/`or`/`xor` (word), `andb`/`orb`/`xorb` (byte), `andl`/`orl`/`xorl`
// (dword), `sub`/`subb`/`subl` - the remaining arithmetic/logic mnemonics
// not yet individually exercised above.
void testRemainingArithmeticAndLogic() {
    std::string bytes = bytesOf("sub r1,r0\nand r1,r0\nor r1,r0\nxor r1,r0\n"
                                 "subb r1,r0\nandb r1,r0\norb r1,r0\nxorb r1,r0\n"
                                 "subl r3r1,r2r0\nandl r3r1,r2r0\norl r3r1,r2r0\nxorl r3r1,r2r0\n");
    std::string expected;
    const uint8_t wordOps[] = {0x12, 0x13, 0x14, 0x15};
    for (uint8_t op : wordOps) {
        expected.push_back(static_cast<char>(op));
        expected.push_back(0x00);
        expected.push_back(0x01);
        expected.push_back(0x00);
        expected.push_back(0x00);
    }
    const uint8_t byteOps[] = {0x42, 0x43, 0x44, 0x45};
    for (uint8_t op : byteOps) {
        expected.push_back(static_cast<char>(op));
        expected.push_back(0x00);
        expected.push_back(0x01);
        expected.push_back(0x00);
        expected.push_back(0x00);
    }
    const uint8_t longOps[] = {0x62, 0x63, 0x64, 0x65};
    for (uint8_t op : longOps) {
        expected.push_back(static_cast<char>(op));
        expected.push_back(0x00);
        expected.push_back(0x09);
        expected.push_back(0x00);
        expected.push_back(0x08);
    }
    require(bytes == expected, "sub/and/or/xor family didn't produce the expected byte sequence");
}

void testImmediateDestinationRejected() {
    bool threw = false;
    try {
        bytesOf("mov r0,5\n");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "'mov r0,5' should be rejected - an immediate can't be a destination");
}

// `jsr`/`jmp`/conditional-jump/`rts` - control flow, with a full 32-bit
// absolute-address operand (unlike M32cAssembler's 16-bit field - see this
// file's header comment on R32C's wider address space).
void testJsrJmpRts() {
    std::string asmText =
        "org 0x1000\n"
        "jsr fn\n"
        "jmp fn\n"
        "rts\n"
        "fn:\n"
        "    nop\n";
    std::string bytes = bytesOf(asmText);
    std::string expected;
    expected.push_back(0x26);
    pushAddr32(expected, 0x100B);  // fn's address: 0x1000 + 11 bytes of jsr(5)/jmp(5)/rts(1) ahead of it
    expected.push_back(0x29);
    pushAddr32(expected, 0x100B);
    expected.push_back(0x28);
    expected.push_back(0x89);
    require(bytes == expected, "jsr/jmp/rts didn't produce the expected byte sequence");
}

// `jeq`/`jne`/`jlt`/`jle`/`jgt`/`jge`/`jltu`/`jleu`/`jgtu`/`jgeu` - the full
// signed/unsigned conditional-jump family, each with its own opcode and a
// 32-bit absolute target.
void testConditionalJumpFamily() {
    std::string asmText =
        "org 0\n"
        "jeq t\njne t\njlt t\njle t\njgt t\njge t\njltu t\njleu t\njgtu t\njgeu t\n"
        "t:\n"
        "    nop\n";
    std::string bytes = bytesOf(asmText);
    std::string expected;
    const uint8_t ops[] = {0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33};
    for (uint8_t op : ops) {
        expected.push_back(static_cast<char>(op));
        pushAddr32(expected, 50);  // t's own address: 10 instructions * 5 bytes each
    }
    expected.push_back(0x89);  // t's own `nop`
    require(bytes == expected, "conditional jump family didn't produce the expected byte sequence");
}

// `clc`/`stc` - carry-flag clear/set; `fset i`/`fclr i` - interrupt-enable
// flag set/clear, the one flag this dialect's own `fset`/`fclr` supports.
void testFlagOps() {
    std::string bytes = bytesOf("clc\nstc\nfset i\nfclr i\n");
    std::string expected;
    expected.push_back(0x34);
    expected.push_back(0x35);
    expected.push_back(0x36);
    expected.push_back(0x37);
    require(bytes == expected, "clc/stc/fset/fclr didn't produce the expected byte sequence");
}

// `mul`/`divu` - word multiply/unsigned-divide, fixed `r0` accumulator.
void testMulDivuFixedAccumulator() {
    std::string bytes = bytesOf("mul r1,r0\ndivu r1,r0\n");
    std::string expected;
    expected.push_back(0x1F);
    expected.push_back(0x00);
    expected.push_back(0x01);
    expected.push_back(0x20);
    expected.push_back(0x00);
    expected.push_back(0x01);
    require(bytes == expected, "mul/divu didn't produce the expected byte sequence");
}

void testMulRejectsWrongAccumulator() {
    bool threw = false;
    try {
        bytesOf("mul r1,r2\n");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "'mul' should reject any destination other than r0");
}

// `push`/`pop` - word stack save/restore.
void testPushPop() {
    std::string bytes = bytesOf("push r0\npop r0\n");
    std::string expected;
    expected.push_back(0x21);
    expected.push_back(0x00);
    expected.push_back(0x22);
    expected.push_back(0x00);
    require(bytes == expected, "push/pop didn't produce the expected byte sequence");
}

// `ld`/`st` - word register-indirect load/store; `ldb`/`stb` - the byte
// variant.
void testLdStFamily() {
    std::string bytes = bytesOf("ld a0,r0\nst r0,a0\nldb a1,r1\nstb r1,a1\n");
    std::string expected;
    expected.push_back(0x23);
    expected.push_back(0x04);
    expected.push_back(0x00);
    expected.push_back(0x24);
    expected.push_back(0x00);
    expected.push_back(0x04);
    expected.push_back(0x4F);
    expected.push_back(0x05);
    expected.push_back(0x01);
    expected.push_back(0x50);
    expected.push_back(0x01);
    expected.push_back(0x05);
    require(bytes == expected, "ld/st/ldb/stb didn't produce the expected byte sequence");
}

// `lea`/`leal` - address load from a frame-relative displacement, whose own
// `Disp` encoding stays a 16-bit offset (not 32-bit) even on this wider
// dialect - see R32cAssembler.cpp's own `encodeOperand()` comment.
void testLeaFromDisp() {
    std::string bytes = bytesOf("lea [fb-4],r0\nleal [fb-8],r2r0\n");
    std::string expected;
    expected.push_back(0x25);
    expected.push_back(0x03);
    expected.push_back(0x07);  // fb
    expected.push_back(static_cast<char>((-4) & 0xFF));
    expected.push_back(static_cast<char>(((-4) >> 8) & 0xFF));
    expected.push_back(0x00);  // dst r0
    expected.push_back(0x76);
    expected.push_back(0x03);
    expected.push_back(0x07);
    expected.push_back(static_cast<char>((-8) & 0xFF));
    expected.push_back(static_cast<char>(((-8) >> 8) & 0xFF));
    expected.push_back(0x08);  // dst r2r0
    require(bytes == expected, "lea/leal didn't produce the expected byte sequence");
}

// An undefined symbol becomes a relocation in object mode, rather than a
// hard assembly error the way plain assembleR32c() treats it.
void testObjectModeUndefinedSymbolBecomesRelocation() {
    rcc::ObjectFile obj = rcc::assembleR32cObject("jsr fn_other\n");
    require(obj.machineType == rcc::kR32cCoffMachine, "object's machineType should be kR32cCoffMachine");
    bool foundReloc = false;
    for (const auto &reloc : obj.relocations) {
        if (reloc.symbolName == "fn_other") {
            foundReloc = true;
            require(reloc.type == rcc::kR32cRelocAbs32, "relocation type should be kR32cRelocAbs32");
        }
    }
    require(foundReloc, "referencing an undefined symbol in object mode should produce a relocation for it");
}

void testPlainModeUndefinedSymbolThrows() {
    bool threw = false;
    try {
        bytesOf("jsr fn_other\n");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "assembleR32c should reject an undefined symbol, unlike assembleR32cObject");
}

}  // namespace

int main() {
    try {
        testMovWordImmediateToReg();
        testAddWordRegToReg();
        testByteFamily();
        testMovDwordImmediateToPairReg();
        testAddDwordPairRegs();
        testDwordRejectsPlainRegister();
        testWordRejectsPairRegister();
        testMullFixedAccumulator();
        testMullRejectsWrongAccumulator();
        testDivulAndDivxl();
        testShiftCountIsWordWidth();
        testShiftRejectsPairCount();
        testOneOpWordAndDword();
        testPushlPopl();
        testLdlStl();
        testBitOps();
        testBclrBnot();
        testPushmPopm();
        testEnterExitd();
        testZeroOperandMisc();
        testExtsExtz();
        testCmpFamily();
        testRotateAndShiftFamily();
        testAdcSbbFamily();
        testRemainingArithmeticAndLogic();
        testImmediateDestinationRejected();
        testJsrJmpRts();
        testConditionalJumpFamily();
        testFlagOps();
        testMulDivuFixedAccumulator();
        testMulRejectsWrongAccumulator();
        testPushPop();
        testLdStFamily();
        testLeaFromDisp();
        testObjectModeUndefinedSymbolBecomesRelocation();
        testPlainModeUndefinedSymbolThrows();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
