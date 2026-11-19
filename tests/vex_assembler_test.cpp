#include "codegen/vex/VexAssembler.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

std::string bytesOf(std::initializer_list<unsigned char> bs) {
    return std::string(bs.begin(), bs.end());
}

// A 4-byte big-endian value, as every `LDI` immediate and `.word` cell is encoded.
std::string be32(uint32_t v) {
    std::string s(4, '\0');
    for (int i = 0; i < 4; i++) s[i] = static_cast<char>((v >> (24 - 8 * i)) & 0xFF);
    return s;
}

// Every one of the 26 real opcodes (docs/SPEC.md section 2's own table), with the exact
// register-operand count (and, for `ldi`, the trailing immediate) its own shape needs, in
// numeric order.
void testEveryOpcodeEncoding() {
    std::string asmText =
        "ret\n"
        "ldi r0, 1\n"
        "mov r1, r2\n"
        "ld r0, r1\n"
        "st r0, r1\n"
        "dei r0, r1\n"
        "deo r0, r1\n"
        "jmp r0\n"
        "jcn r0, r1\n"
        "cal r0\n"
        "add r0, r1, r2\n"
        "sub r0, r1, r2\n"
        "mul r0, r1, r2\n"
        "div r0, r1, r2\n"
        "and r0, r1, r2\n"
        "ora r0, r1, r2\n"
        "xor r0, r1, r2\n"
        "ceq r0, r1, r2\n"
        "clt r0, r1, r2\n"
        "nop\n"
        "psh r0\n"
        "pop r1\n"
        "iret\n"
        "sei\n"
        "cli\n"
        "hlt\n";
    std::string bytes = rcc::assembleVex(asmText);
    std::string expected = bytesOf({0x00}) + bytesOf({0x01, 0x00}) + be32(1) + bytesOf({0x02, 0x01, 0x02}) +
                            bytesOf({0x03, 0x00, 0x01}) + bytesOf({0x04, 0x00, 0x01}) + bytesOf({0x05, 0x00, 0x01}) +
                            bytesOf({0x06, 0x00, 0x01}) + bytesOf({0x07, 0x00}) + bytesOf({0x08, 0x00, 0x01}) +
                            bytesOf({0x09, 0x00}) + bytesOf({0x0A, 0x00, 0x01, 0x02}) +
                            bytesOf({0x0B, 0x00, 0x01, 0x02}) + bytesOf({0x0C, 0x00, 0x01, 0x02}) +
                            bytesOf({0x0D, 0x00, 0x01, 0x02}) + bytesOf({0x0E, 0x00, 0x01, 0x02}) +
                            bytesOf({0x0F, 0x00, 0x01, 0x02}) + bytesOf({0x10, 0x00, 0x01, 0x02}) +
                            bytesOf({0x11, 0x00, 0x01, 0x02}) + bytesOf({0x12, 0x00, 0x01, 0x02}) +
                            bytesOf({0x13}) + bytesOf({0x14, 0x00}) + bytesOf({0x15, 0x01}) + bytesOf({0x16}) +
                            bytesOf({0x17}) + bytesOf({0x18}) + bytesOf({0x19});
    require(bytes == expected, "the 26 opcodes didn't encode to their own spec-table shape/values in order");
}

void testNopTakesNoOperand() {
    bool threw = false;
    try {
        rcc::assembleVex("nop r0\n");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "'nop' takes no operand at all");
}

void testUpperCaseMnemonicsAndRegistersAccepted() {
    std::string bytes = rcc::assembleVex("RET\nLDI R0, 5\nADD R0, R1, R2\n");
    std::string expected = bytesOf({0x00}) + bytesOf({0x01, 0x00}) + be32(5) + bytesOf({0x0A, 0x00, 0x01, 0x02});
    require(bytes == expected, "mnemonics and register names should both be case-insensitive");
}

void testRegisterOperandOutOfRangeRejected() {
    bool threw = false;
    try {
        rcc::assembleVex("jmp r8\n");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "'r8' isn't a valid register (only r0-r7 exist) and should be rejected");
}

void testRegisterOperandMustBePlain() {
    // A register operand is never resolved against the label table or label arithmetic the
    // way `ldi`'s immediate is - a label name, a number out of r0-r7 shape, or `reg+1` must
    // all be rejected at parse time, not treated as a (wrong) register.
    bool threw = false;
    try {
        rcc::assembleVex("jmp some_label\n");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "a label name isn't a valid register operand");

    threw = false;
    try {
        rcc::assembleVex("jmp r0+1\n");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "'r0+1' (label-arithmetic shape) isn't a valid plain register operand");
}

void testWrongOperandCountRejected() {
    bool threw = false;
    try {
        rcc::assembleVex("ldi r0\n");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "'ldi' with no immediate should be rejected");

    threw = false;
    try {
        rcc::assembleVex("add r0, r1\n");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "'add' with only two operands (needs three) should be rejected");

    threw = false;
    try {
        rcc::assembleVex("ret r0\n");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "'ret' takes no operand at all");
}

void testUnassignedOpcodeRejected() {
    // 0x1A-0xFF are unassigned per docs/SPEC.md section 2 - this dialect has no mnemonic for
    // them at all, so they're rejected at parse time, not encoded.
    bool threw = false;
    try {
        rcc::assembleVex("cgt r0, r1, r2\n");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "'cgt' isn't a real vex mnemonic (CLT with swapped operands covers it) and should be rejected");
}

// `PSH`/`POP` each take exactly one register operand (`Ra`/`Rd` respectively) - docs/SPEC.md
// section 2's own length table, added in v1.2.
void testPshPopTakeExactlyOneOperand() {
    bool threw = false;
    try {
        rcc::assembleVex("psh r0, r1\n");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "'psh' with two operands (takes exactly one) should be rejected");

    threw = false;
    try {
        rcc::assembleVex("pop\n");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "'pop' with no operand (takes exactly one) should be rejected");
}

// `IRET`/`SEI`/`CLI`/`HLT` all take no operands at all - same shape as `RET`/`NOP`.
void testIretSeiCliTakeNoOperand() {
    for (const char *mnemonic : {"iret", "sei", "cli", "hlt"}) {
        bool threw = false;
        try {
            rcc::assembleVex(std::string(mnemonic) + " r0\n");
        } catch (const std::runtime_error &) {
            threw = true;
        }
        require(threw, "an operand on a zero-operand mnemonic should be rejected");
    }
}

// `.org` pads forward with zero bytes and only ever moves forward (docs/ASSEMBLER.md). A
// whole-program compile's offsets are counted from kVexBaseAddress (0x0100) - see
// VexAssembler.h's own comment - so `.org` targets an absolute address here, not a file-relative
// one: `ret` (one byte) at 0x0100 leaves the next offset at 0x0101, and `.org 0x0105` pads
// exactly 4 bytes to reach it.
void testOrgPadsForward() {
    std::string bytes = rcc::assembleVex("ret\n.org 0x0105\nret\n");
    std::string expected = bytesOf({0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    require(bytes == expected, "'.org' should pad with exactly the right number of zero bytes");
}

void testOrgCannotMoveBackward() {
    bool threw = false;
    try {
        rcc::assembleVex(".org 0x0100\n.org 0x00FF\n");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "'.org' moving backward should be rejected");
}

// `.byte`/`.short`/`.word` emit 1/2/4 bytes big-endian - independent of instruction width.
void testByteShortWordDirectives() {
    std::string bytes = rcc::assembleVex(".byte 0x12, 0x34\n.short 0x1234\n.word 0x12345678\n");
    std::string expected = bytesOf({0x12, 0x34, 0x12, 0x34, 0x12, 0x34, 0x56, 0x78});
    require(bytes == expected, "'.byte'/'.short'/'.word' should each emit their own width, big-endian");
}

void testByteRejectsOutOfRangeValue() {
    bool threw = false;
    try {
        rcc::assembleVex(".byte 256\n");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "'.byte 256' doesn't fit in a byte and should be rejected");
}

void testZeroDirective() {
    std::string bytes = rcc::assembleVex(".zero 3\nret\n");
    std::string expected = bytesOf({0x00, 0x00, 0x00, 0x00});
    require(bytes == expected, "'.zero 3' should emit exactly 3 zero bytes before 'ret'");
}

// Label arithmetic (`label+4`, `label-1`) and forward references, resolved in a second pass -
// only ever inside `ldi`'s own immediate, never a register operand (see
// testRegisterOperandMustBePlain()).
void testLabelArithmeticAndForwardReference() {
    std::string bytes = rcc::assembleVex(
        "ldi r0, target+4\n"
        "jmp r0\n"
        "target: .zero 4\n"
        "ret\n");
    // ldi(2 bytes)+imm(4)+jmp(2) = 8 bytes after kVexBaseAddress; target sits at base+8, so
    // target+4 = base+12.
    std::string expected = bytesOf({0x01, 0x00}) + be32(rcc::kVexBaseAddress + 12) + bytesOf({0x07, 0x00}) +
                            bytesOf({0x00, 0x00, 0x00, 0x00}) + bytesOf({0x00});
    require(bytes == expected, "label arithmetic/forward reference didn't resolve to the expected absolute address");
}

void testUndefinedSymbolThrowsInPlainMode() {
    bool threw = false;
    try {
        rcc::assembleVex("ldi r2, FN_other\ncal r2\n");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "assembleVex should reject an undefined symbol, unlike assembleVexObject");
}

// Object mode: an undefined symbol becomes a relocation (against kVexRelocAbs32) instead of a
// hard error, and a same-object label reference also gets a ".." base-shift relocation, since
// labels here resolve relative to offset 0 until GenericLinker adds the real base in.
void testObjectModeRelocations() {
    rcc::ObjectFile obj = rcc::assembleVexObject(
        "here: ldi r0, here\n"
        "ldi r2, FN_other\n"
        "cal r2\n");
    require(obj.relocations.size() == 2, "expected exactly two relocations: '..' for 'here', and 'FN_other' itself");
    bool sawDotDot = false, sawOther = false;
    for (const auto &r : obj.relocations) {
        require(r.type == rcc::kVexRelocAbs32, "every vex relocation should be kVexRelocAbs32");
        if (r.symbolName == "..") sawDotDot = true;
        if (r.symbolName == "FN_other") sawOther = true;
    }
    require(sawDotDot, "a same-object label used in an 'ldi' should get a '..' base-shift relocation");
    require(sawOther, "an undefined symbol should become a relocation, not a hard error, in object mode");

    bool foundOther = false;
    for (const auto &s : obj.symbols) {
        if (s.name == "FN_other") {
            foundOther = true;
            require(!s.isDefined, "the undefined relocation target should still get its own symbol-table entry");
        }
    }
    require(foundOther, "FN_other should appear in the object's own symbol table");
}

void testNoBracketedOperand() {
    bool threw = false;
    try {
        rcc::assembleVex("ldi r0, [5]\n");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "this dialect has no bracketed/memory operand form at all");
}

}  // namespace

int main() {
    try {
        testEveryOpcodeEncoding();
        testUpperCaseMnemonicsAndRegistersAccepted();
        testRegisterOperandOutOfRangeRejected();
        testRegisterOperandMustBePlain();
        testWrongOperandCountRejected();
        testUnassignedOpcodeRejected();
        testNopTakesNoOperand();
        testPshPopTakeExactlyOneOperand();
        testIretSeiCliTakeNoOperand();
        testOrgPadsForward();
        testOrgCannotMoveBackward();
        testByteShortWordDirectives();
        testByteRejectsOutOfRangeValue();
        testZeroDirective();
        testLabelArithmeticAndForwardReference();
        testUndefinedSymbolThrowsInPlainMode();
        testObjectModeRelocations();
        testNoBracketedOperand();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
