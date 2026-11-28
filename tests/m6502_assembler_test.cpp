#include "codegen/m6502/M6502Assembler.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

// `ldxi_hi`/`ldyi_hi` are this dialect's `#>label` immediate-high-byte
// loads into X/Y (see M6502Assembler.cpp's own `encodeInstr()` comment),
// added for src/rt/bbc/'s MOS wrappers, which need X/Y loaded with the
// low/high halves of a parameter block's address. `lbl` sits at $0100
// (`org 0x100`), so its high byte is $01.
void testLdxiHiLdyiHiAgainstLabel() {
    std::string asmText =
        "org 0x100\n"
        "lbl: db 0\n"
        "ldxi_hi lbl\n"
        "ldyi_hi lbl\n";
    std::string bytes = rcc::assembleM6502(asmText);

    std::string expected;
    expected.push_back(0x00);                      // lbl's own `db 0` cell
    expected.push_back(static_cast<char>(0xA2));    // LDX #immediate
    expected.push_back(0x01);                       // high byte of $0100
    expected.push_back(static_cast<char>(0xA0));    // LDY #immediate
    expected.push_back(0x01);                       // high byte of $0100
    require(bytes == expected, "ldxi_hi/ldyi_hi against a label didn't encode the expected opcode+high-byte bytes");
}

// Same two mnemonics against a label whose address needs both bytes of
// its high byte nonzero-distinguishable from its low byte, confirming
// the two loads are independent of each other and of `ldai_hi`/`ldai`.
void testLdxiHiLdyiHiDistinctFromLow() {
    std::string asmText =
        "org 0x1234\n"
        "lbl: db 0\n"
        "ldxi lbl\n"
        "ldxi_hi lbl\n"
        "ldyi lbl\n"
        "ldyi_hi lbl\n";
    std::string bytes = rcc::assembleM6502(asmText);

    std::string expected;
    expected.push_back(0x00);                      // lbl's own `db 0` cell
    expected.push_back(static_cast<char>(0xA2));
    expected.push_back(0x34);                       // low byte of $1234
    expected.push_back(static_cast<char>(0xA2));
    expected.push_back(0x12);                       // high byte of $1234
    expected.push_back(static_cast<char>(0xA0));
    expected.push_back(0x34);                       // low byte of $1234
    expected.push_back(static_cast<char>(0xA0));
    expected.push_back(0x12);                       // high byte of $1234
    require(bytes == expected, "ldxi/ldxi_hi/ldyi/ldyi_hi against a label didn't encode the low/high bytes correctly");
}

// Absolute-addressing `ldx`/`ldy` (real opcodes $AE/$AC) - added for src/rt/bbc/'s own
// OSBGET/OSBPUT/OSFIND wrappers, which need a dynamic (not compile-time-known) byte loaded into
// X/Y from a global cell, the one case `ldxi`/`ldyi`'s immediate addressing can't reach. `cell`
// sits at $0300.
void testLdxLdyAbsolute() {
    std::string asmText =
        "org 0x300\n"
        "cell: db 0\n"
        "ldx cell\n"
        "ldy cell\n";
    std::string bytes = rcc::assembleM6502(asmText);

    std::string expected;
    expected.push_back(0x00);                      // cell's own `db 0`
    expected.push_back(static_cast<char>(0xAE));    // LDX absolute
    expected.push_back(0x00);                       // low byte of $0300
    expected.push_back(0x03);                       // high byte of $0300
    expected.push_back(static_cast<char>(0xAC));    // LDY absolute
    expected.push_back(0x00);
    expected.push_back(0x03);
    require(bytes == expected, "ldx/ldy against a label didn't encode the expected opcode+16-bit-address bytes");
}

// Absolute-addressing `sty` (real opcode $8C) - added for src/rt/apple2/'s own a2_pread(), which
// needs to store the result PREAD leaves in Y back into a global cell (`sta`/absolute `ldy` alone
// can't move a value out of Y). `cell` sits at $0300.
void testStyAbsolute() {
    std::string asmText =
        "org 0x300\n"
        "cell: db 0\n"
        "sty cell\n";
    std::string bytes = rcc::assembleM6502(asmText);

    std::string expected;
    expected.push_back(0x00);                      // cell's own `db 0`
    expected.push_back(static_cast<char>(0x8C));    // STY absolute
    expected.push_back(0x00);                       // low byte of $0300
    expected.push_back(0x03);                       // high byte of $0300
    require(bytes == expected, "sty against a label didn't encode the expected opcode+16-bit-address bytes");
}

}  // namespace

int main() {
    try {
        testLdxiHiLdyiHiAgainstLabel();
        testLdxiHiLdyiHiDistinctFromLow();
        testLdxLdyAbsolute();
        testStyAbsolute();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
