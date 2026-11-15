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

// A 4-byte big-endian value, as every `LIT` immediate and `.word` cell is encoded.
std::string be32(uint32_t v) {
    std::string s(4, '\0');
    for (int i = 0; i < 4; i++) s[i] = static_cast<char>((v >> (24 - 8 * i)) & 0xFF);
    return s;
}

// Every one of the 22 real opcodes (docs/SPEC.md section 2's own table), bare, in numeric order -
// `lit` alone needs an operand, so it gets its own immediate right after its opcode byte.
void testEveryOpcodeBareEncoding() {
    std::string asmText =
        "ret\n"
        "lit 1\n"
        "ld\n"
        "st\n"
        "dei\n"
        "deo\n"
        "jmp\n"
        "jcn\n"
        "cal\n"
        "add\n"
        "sub\n"
        "mul\n"
        "div\n"
        "and\n"
        "ora\n"
        "xor\n"
        "equ\n"
        "lth\n"
        "push\n"
        "pop\n"
        "lds\n"
        "sts\n";
    std::string bytes = rcc::assembleVex(asmText);
    std::string expected = bytesOf({0x00, 0x01}) + be32(1) +
                            bytesOf({0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
                                     0x0F, 0x10, 0x11, 0x13, 0x14, 0x15, 0x16});
    require(bytes == expected, "the 22 bare opcodes didn't encode to their own spec-table values in order");
}

// The `.r` suffix sets bit 5 (0x20) on every mnemonic that has one - including the ones the
// machine itself ignores it on (docs/SPEC.md section 2: "All other instructions ignore it"),
// since encoding it is this assembler's job, not deciding whether it matters.
void testRelFlagSetsBit5OnEveryMnemonic() {
    std::string bytes = rcc::assembleVex(
        "ret.r\n"
        "ld.r\n"
        "st.r\n"
        "jmp.r\n"
        "jcn.r\n"
        "cal.r\n"
        "add.r\n"
        "lds.r\n"
        "sts.r\n");
    std::string expected = bytesOf({0x20, 0x22, 0x23, 0x26, 0x27, 0x28, 0x29, 0x35, 0x36});
    require(bytes == expected, "'.r' should OR 0x20 into every one of these mnemonics' opcode byte");
}

void testUpperCaseMnemonicsAccepted() {
    std::string bytes = rcc::assembleVex("RET\nLIT 5\nADD\n");
    std::string expected = bytesOf({0x00, 0x01}) + be32(5) + bytesOf({0x09});
    require(bytes == expected, "mnemonics should be case-insensitive");
}

void testLitTakesExactlyOneOperand() {
    bool threw = false;
    try {
        rcc::assembleVex("lit\n");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "'lit' with no operand should be rejected");

    threw = false;
    try {
        rcc::assembleVex("lit 1, 2\n");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "'lit' with two operands should be rejected");
}

void testBareMnemonicRejectsAnOperand() {
    bool threw = false;
    try {
        rcc::assembleVex("add 5\n");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "'add' takes no operand - every operand of a non-'lit' instruction lives on the stack");
}

void testUnassignedOpcodeRejected() {
    // 0x12 (the old GTH slot) and 0x17-0x1F are unassigned per docs/SPEC.md section 2 - this
    // dialect has no mnemonic for them at all, so they're rejected at parse time, not encoded.
    bool threw = false;
    try {
        rcc::assembleVex("gth\n");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "'gth' isn't a real vex mnemonic and should be rejected");
}

// `.org` pads forward with zero bytes and only ever moves forward (docs/ASSEMBLER.md). A
// whole-program compile's offsets are counted from kVexBaseAddress (0x0100) - see
// VexAssembler.h's own comment - so `.org` targets an absolute address here, not a file-relative
// one: `add` (one byte) at 0x0100 leaves the next offset at 0x0101, and `.org 0x0105` pads
// exactly 4 bytes to reach it.
void testOrgPadsForward() {
    std::string bytes = rcc::assembleVex("add\n.org 0x0105\nsub\n");
    std::string expected = bytesOf({0x09, 0x00, 0x00, 0x00, 0x00, 0x0A});
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

// Label arithmetic (`label+4`, `label-1`) and forward references, resolved in a second pass.
void testLabelArithmeticAndForwardReference() {
    std::string bytes = rcc::assembleVex(
        "lit target+4\n"
        "jmp\n"
        "target: .zero 4\n"
        "ret\n");
    // lit(1)+imm(4)+jmp(1) = 6 bytes after kVexBaseAddress; target sits at base+6, so
    // target+4 = base+10.
    std::string expected = bytesOf({0x01}) + be32(rcc::kVexBaseAddress + 10) + bytesOf({0x06}) +
                            bytesOf({0x00, 0x00, 0x00, 0x00}) + bytesOf({0x00});
    require(bytes == expected, "label arithmetic/forward reference didn't resolve to the expected absolute address");
}

void testUndefinedSymbolThrowsInPlainMode() {
    bool threw = false;
    try {
        rcc::assembleVex("lit FN_other\ncal\n");
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
        "here: lit here\n"
        "lit FN_other\n"
        "cal\n");
    require(obj.relocations.size() == 2, "expected exactly two relocations: '..' for 'here', and 'FN_other' itself");
    bool sawDotDot = false, sawOther = false;
    for (const auto &r : obj.relocations) {
        require(r.type == rcc::kVexRelocAbs32, "every vex relocation should be kVexRelocAbs32");
        if (r.symbolName == "..") sawDotDot = true;
        if (r.symbolName == "FN_other") sawOther = true;
    }
    require(sawDotDot, "a same-object label used in a 'lit' should get a '..' base-shift relocation");
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
        rcc::assembleVex("lit [5]\n");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "this dialect has no bracketed/memory operand form at all");
}

}  // namespace

int main() {
    try {
        testEveryOpcodeBareEncoding();
        testRelFlagSetsBit5OnEveryMnemonic();
        testUpperCaseMnemonicsAccepted();
        testLitTakesExactlyOneOperand();
        testBareMnemonicRejectsAnOperand();
        testUnassignedOpcodeRejected();
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
