#include "codegen/m32c/M32cAssembler.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

std::string bytesOf(const std::string &asmText) { return rcc::assembleM32c(asmText); }

// `mov.w 1234,r0` - word immediate-to-register move: opcode 0x01, src tag
// 1 (immediate) + 16-bit value, dst tag 0 (register) + register code 0.
void testMovWordImmediateToReg() {
    std::string bytes = bytesOf("mov 1234,r0\n");
    std::string expected;
    expected.push_back(0x01);
    expected.push_back(0x01);
    expected.push_back(static_cast<char>(1234 & 0xFF));
    expected.push_back(static_cast<char>((1234 >> 8) & 0xFF));
    expected.push_back(0x00);
    expected.push_back(0x00);
    require(bytes == expected, "mov 1234,r0 didn't produce the expected byte sequence");
}

// `add r1,r0` - word register-to-register add: opcode 0x02, two register
// operands.
void testAddWordRegToReg() {
    std::string bytes = bytesOf("add r1,r0\n");
    std::string expected;
    expected.push_back(0x02);
    expected.push_back(0x00);
    expected.push_back(0x01);
    expected.push_back(0x00);
    expected.push_back(0x00);
    require(bytes == expected, "add r1,r0 didn't produce the expected byte sequence");
}

// `movb 5,r0` / `addb r1,r0` - the byte-sized family: same opcode shapes as
// the word family, distinct opcode bytes (0x1E/0x30), and a one-byte
// immediate field instead of two.
void testByteFamily() {
    std::string bytes = bytesOf("movb 5,r0\naddb r1,r0\n");
    std::string expected;
    expected.push_back(0x1E);
    expected.push_back(0x01);
    expected.push_back(0x05);
    expected.push_back(0x00);
    expected.push_back(0x00);
    expected.push_back(0x30);
    expected.push_back(0x00);
    expected.push_back(0x01);
    expected.push_back(0x00);
    expected.push_back(0x00);
    require(bytes == expected, "movb/addb didn't produce the expected byte sequence");
}

// `movl 1,r2r0` - dword move: opcode 0x50, a 4-byte immediate field (not
// 2), and the pair-register code (8) as the destination.
void testMovDwordImmediateToPairReg() {
    std::string bytes = bytesOf("movl 1,r2r0\n");
    std::string expected;
    expected.push_back(0x50);
    expected.push_back(0x01);
    expected.push_back(0x01);
    expected.push_back(0x00);
    expected.push_back(0x00);
    expected.push_back(0x00);
    expected.push_back(0x00);
    expected.push_back(0x08);
    require(bytes == expected, "movl 1,r2r0 didn't produce the expected byte sequence");
}

// `addl r3r1,r2r0` - dword register-pair add: opcode 0x51, register codes
// 9 (r3r1) and 8 (r2r0).
void testAddDwordPairRegs() {
    std::string bytes = bytesOf("addl r3r1,r2r0\n");
    std::string expected;
    expected.push_back(0x51);
    expected.push_back(0x00);
    expected.push_back(0x09);
    expected.push_back(0x00);
    expected.push_back(0x08);
    require(bytes == expected, "addl r3r1,r2r0 didn't produce the expected byte sequence");
}

// A plain register (not a pair) rejected as a dword operand, and a pair
// register rejected as a word operand - M32C has no way to address either
// mismatch.
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
    expected.push_back(0x5B);
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
    expected.push_back(0x5C);
    expected.push_back(0x00);
    expected.push_back(0x09);
    expected.push_back(0x5D);
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
    expected.push_back(0x59);
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
    expected.push_back(0x09);
    expected.push_back(0x00);
    expected.push_back(0x00);
    expected.push_back(0x58);
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
    expected.push_back(0x5E);
    expected.push_back(0x08);
    expected.push_back(0x5F);
    expected.push_back(0x08);
    require(bytes == expected, "pushl/popl didn't produce the expected byte sequence");
}

// `ldl a0,r2r0` / `stl r2r0,a0` - dword register-indirect load/store
// through an address register.
void testLdlStl() {
    std::string bytes = bytesOf("ldl a0,r2r0\nstl r2r0,a0\n");
    std::string expected;
    expected.push_back(0x60);
    expected.push_back(0x04);
    expected.push_back(0x08);
    expected.push_back(0x61);
    expected.push_back(0x08);
    expected.push_back(0x04);
    require(bytes == expected, "ldl/stl didn't produce the expected byte sequence");
}

// `bset 3,r0` / `btst 0,[ac]` - bit manipulation: a byte-sized immediate
// bit number, then the destination operand.
void testBitOps() {
    std::string asmText =
        "org 0x100\n"
        "ac: dw 0\n"
        "bset 3,r0\n"
        "btst 0,ac\n";
    std::string bytes = bytesOf(asmText);
    std::string expected;
    expected.push_back(0x00);
    expected.push_back(0x00);  // ac's own `dw 0`
    expected.push_back(0x70);  // bset
    expected.push_back(0x01);
    expected.push_back(0x03);
    expected.push_back(0x00);
    expected.push_back(0x00);
    expected.push_back(0x72);  // btst
    expected.push_back(0x01);
    expected.push_back(0x00);
    expected.push_back(0x02);  // `ac`'s own absolute-address tag (a bare label is Mem, not Disp)
    expected.push_back(0x00);
    expected.push_back(0x01);  // ac = 0x0100, little-endian
    require(bytes == expected, "bset/btst didn't produce the expected byte sequence");
}

// `bclr 7,r3` / `bnot 2,r1` - the other two bit-manipulation mnemonics.
void testBclrBnot() {
    std::string bytes = bytesOf("bclr 7,r3\nbnot 2,r1\n");
    std::string expected;
    expected.push_back(0x71);
    expected.push_back(0x01);
    expected.push_back(0x07);
    expected.push_back(0x00);
    expected.push_back(0x03);
    expected.push_back(0x73);
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
    expected.push_back(0x74);
    expected.push_back(0x01);
    expected.push_back(0x0F);
    expected.push_back(0x75);
    expected.push_back(0x01);
    expected.push_back(0x0F);
    require(bytes == expected, "pushm/popm didn't produce the expected byte sequence");
}

// `enter 16` / `exitd` - the compound frame-setup/teardown pair.
void testEnterExitd() {
    std::string bytes = bytesOf("enter 16\nexitd\n");
    std::string expected;
    expected.push_back(0x76);
    expected.push_back(0x01);
    expected.push_back(0x10);
    expected.push_back(0x00);
    expected.push_back(0x77);
    require(bytes == expected, "enter/exitd didn't produce the expected byte sequence");
}

// `reit`/`nop`/`brk`/`und` - the remaining zero-operand instructions.
void testZeroOperandMisc() {
    std::string bytes = bytesOf("reit\nnop\nbrk\nund\n");
    std::string expected;
    expected.push_back(0x78);
    expected.push_back(0x79);
    expected.push_back(0x7A);
    expected.push_back(0x7B);
    require(bytes == expected, "reit/nop/brk/und didn't produce the expected byte sequence");
}

// `exts` / `extz` - sign/zero-extend r0 into r2r0, no operand.
void testExtsExtz() {
    std::string bytes = bytesOf("exts\nextz\n");
    std::string expected;
    expected.push_back(0x63);
    expected.push_back(0x64);
    require(bytes == expected, "exts/extz didn't produce the expected byte sequence");
}

// `cmp`/`cmpb`/`cmpl` - the read-only comparison family, one per operand
// size, none of which accepts an immediate destination (same restriction
// as every writing two-operand instruction).
void testCmpFamily() {
    std::string bytes = bytesOf("cmp r1,r0\ncmpb r1,r0\ncmpl r3r1,r2r0\n");
    std::string expected;
    expected.push_back(0x08);
    expected.push_back(0x00);
    expected.push_back(0x01);
    expected.push_back(0x00);
    expected.push_back(0x00);
    expected.push_back(0x35);
    expected.push_back(0x00);
    expected.push_back(0x01);
    expected.push_back(0x00);
    expected.push_back(0x00);
    expected.push_back(0x56);
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
    expected.push_back(0x29);
    expected.push_back(0x00);
    expected.push_back(0x00);  // rol r0
    expected.push_back(0x2A);
    expected.push_back(0x00);
    expected.push_back(0x00);  // ror r0
    expected.push_back(0x07);
    expected.push_back(0x00);
    expected.push_back(0x01);
    expected.push_back(0x00);
    expected.push_back(0x00);  // shl r1,r0
    expected.push_back(0x25);
    expected.push_back(0x00);
    expected.push_back(0x01);
    expected.push_back(0x00);
    expected.push_back(0x00);  // sha r1,r0
    expected.push_back(0x3A);
    expected.push_back(0x00);
    expected.push_back(0x00);  // rolb r0
    expected.push_back(0x3B);
    expected.push_back(0x00);
    expected.push_back(0x00);  // rorb r0
    expected.push_back(0x38);
    expected.push_back(0x00);
    expected.push_back(0x01);
    expected.push_back(0x00);
    expected.push_back(0x00);  // shlb r1,r0
    expected.push_back(0x39);
    expected.push_back(0x00);
    expected.push_back(0x01);
    expected.push_back(0x00);
    expected.push_back(0x00);  // shab r1,r0
    expected.push_back(0x67);
    expected.push_back(0x00);
    expected.push_back(0x08);  // roll r2r0
    expected.push_back(0x68);
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
    expected.push_back(0x27);
    expected.push_back(0x00);
    expected.push_back(0x01);
    expected.push_back(0x00);
    expected.push_back(0x00);  // adc
    expected.push_back(0x28);
    expected.push_back(0x00);
    expected.push_back(0x01);
    expected.push_back(0x00);
    expected.push_back(0x00);  // sbb
    expected.push_back(0x3C);
    expected.push_back(0x00);
    expected.push_back(0x01);
    expected.push_back(0x00);
    expected.push_back(0x00);  // adcb
    expected.push_back(0x3D);
    expected.push_back(0x00);
    expected.push_back(0x01);
    expected.push_back(0x00);
    expected.push_back(0x00);  // sbbb
    expected.push_back(0x65);
    expected.push_back(0x00);
    expected.push_back(0x09);
    expected.push_back(0x00);
    expected.push_back(0x08);  // adcl
    expected.push_back(0x66);
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
    const uint8_t wordOps[] = {0x03, 0x04, 0x05, 0x06};
    for (uint8_t op : wordOps) {
        expected.push_back(static_cast<char>(op));
        expected.push_back(0x00);
        expected.push_back(0x01);
        expected.push_back(0x00);
        expected.push_back(0x00);
    }
    const uint8_t byteOps[] = {0x31, 0x32, 0x33, 0x34};
    for (uint8_t op : byteOps) {
        expected.push_back(static_cast<char>(op));
        expected.push_back(0x00);
        expected.push_back(0x01);
        expected.push_back(0x00);
        expected.push_back(0x00);
    }
    const uint8_t longOps[] = {0x52, 0x53, 0x54, 0x55};
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

// `jsr`/`jmp`/conditional-jump/`rts` - control flow, unchanged from
// M16cAssembler's own shape (always a full absolute-address operand).
void testJsrJmpRts() {
    std::string asmText =
        "jsr fn\n"
        "jmp fn\n"
        "rts\n"
        "fn:\n"
        "    nop\n";
    std::string bytes = bytesOf(asmText);
    std::string expected;
    expected.push_back(0x12);
    expected.push_back(0x07);
    expected.push_back(0x00);
    expected.push_back(0x14);
    expected.push_back(0x07);
    expected.push_back(0x00);
    expected.push_back(0x13);
    expected.push_back(0x79);
    require(bytes == expected, "jsr/jmp/rts didn't produce the expected byte sequence");
}

// An undefined symbol becomes a relocation in object mode, rather than a
// hard assembly error the way plain assembleM32c() treats it.
void testObjectModeUndefinedSymbolBecomesRelocation() {
    rcc::ObjectFile obj = rcc::assembleM32cObject("jsr fn_other\n");
    require(obj.machineType == rcc::kM32cCoffMachine, "object's machineType should be kM32cCoffMachine");
    bool foundReloc = false;
    for (const auto &reloc : obj.relocations) {
        if (reloc.symbolName == "fn_other") {
            foundReloc = true;
            require(reloc.type == rcc::kM32cRelocAbs16, "relocation type should be kM32cRelocAbs16");
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
    require(threw, "assembleM32c should reject an undefined symbol, unlike assembleM32cObject");
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
        testObjectModeUndefinedSymbolBecomesRelocation();
        testPlainModeUndefinedSymbolThrows();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
