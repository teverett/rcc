#include "codegen/riscv64/Riscv64Assembler.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}

std::string word(uint32_t w) {
    std::string s;
    s.push_back(static_cast<char>(w & 0xFF));
    s.push_back(static_cast<char>((w >> 8) & 0xFF));
    s.push_back(static_cast<char>((w >> 16) & 0xFF));
    s.push_back(static_cast<char>((w >> 24) & 0xFF));
    return s;
}

std::string qword(uint64_t v) { return word(static_cast<uint32_t>(v & 0xFFFFFFFFu)) + word(static_cast<uint32_t>(v >> 32)); }

void expectWords(const std::string &asmText, const std::vector<uint32_t> &words, const std::string &label) {
    std::string bytes = rcc::assembleRiscv64(asmText);
    std::string expected;
    for (uint32_t w : words) expected += word(w);
    require(bytes == expected, label + ": encoded bytes didn't match expectation");
}

void expectOne(const std::string &asmLine, uint32_t expectedWord, const std::string &label) {
    expectWords(asmLine + "\n", {expectedWord}, label);
}

// --- R-type: every RV64I ALU op (identical encoding to RV32I's) plus the full 64-bit M
// extension -------------------------------------------------------------------------------
void testRTypeAluOps() {
    expectOne("add t0,t1,t2", 0x007302b3u, "add");
    expectOne("sub t0,t1,t2", 0x407302b3u, "sub");
    expectOne("sll t0,t1,t2", 0x007312b3u, "sll");
    expectOne("slt t0,t1,t2", 0x007322b3u, "slt");
    expectOne("sltu t0,t1,t2", 0x007332b3u, "sltu");
    expectOne("xor t0,t1,t2", 0x007342b3u, "xor");
    expectOne("srl t0,t1,t2", 0x007352b3u, "srl");
    expectOne("sra t0,t1,t2", 0x407352b3u, "sra");
    expectOne("or t0,t1,t2", 0x007362b3u, "or");
    expectOne("and t0,t1,t2", 0x007372b3u, "and");
}

void testMExtension64() {
    expectOne("mul t0,t1,t2", 0x027302b3u, "mul");
    expectOne("mulh t0,t1,t2", 0x027312b3u, "mulh");
    expectOne("mulhsu t0,t1,t2", 0x027322b3u, "mulhsu");
    expectOne("mulhu t0,t1,t2", 0x027332b3u, "mulhu");
    expectOne("div t0,t1,t2", 0x027342b3u, "div");
    expectOne("divu t0,t1,t2", 0x027352b3u, "divu");
    expectOne("rem t0,t1,t2", 0x027362b3u, "rem");
    expectOne("remu t0,t1,t2", 0x027372b3u, "remu");
}

// --- RV64-only word-size R-type ops (OP-32, opcode 0x3B) ---------------------------------
void testWordSizeRType() {
    expectOne("addw t0,t1,t2", 0x007302bbu, "addw");
    expectOne("subw t0,t1,t2", 0x407302bbu, "subw");
    expectOne("sllw t0,t1,t2", 0x007312bbu, "sllw");
    expectOne("srlw t0,t1,t2", 0x007352bbu, "srlw");
    expectOne("sraw t0,t1,t2", 0x407352bbu, "sraw");
}

void testWordSizeMExtension() {
    expectOne("mulw t0,t1,t2", 0x027302bbu, "mulw");
    expectOne("divw t0,t1,t2", 0x027342bbu, "divw");
    expectOne("divuw t0,t1,t2", 0x027352bbu, "divuw");
    expectOne("remw t0,t1,t2", 0x027362bbu, "remw");
    expectOne("remuw t0,t1,t2", 0x027372bbu, "remuw");
}

// --- I-type arithmetic, including negative-immediate and 12-bit-range boundary cases ----
void testITypeArith() {
    expectOne("addi t0,t1,-1", 0xfff30293u, "addi -1");
    expectOne("addi t0,t1,2047", 0x7ff30293u, "addi max (2047)");
    expectOne("addi t0,t1,-2048", 0x80030293u, "addi min (-2048)");
    expectOne("slti t0,t1,5", 0x00532293u, "slti");
    expectOne("sltiu t0,t1,5", 0x00533293u, "sltiu");
    expectOne("xori t0,t1,-1", 0xfff34293u, "xori -1");
    expectOne("ori t0,t1,5", 0x00536293u, "ori");
    expectOne("andi t0,t1,5", 0x00537293u, "andi");
}

void testAddiw() {
    expectOne("addiw t0,t1,-1", 0xfff3029bu, "addiw -1");
    expectOne("addiw t0,t1,100", 0x0643029bu, "addiw 100");
}

void testOutOfRangeImmediateRejected() {
    bool threw = false;
    try {
        rcc::assembleRiscv64("addi t0,t1,2048\n");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "addi with a 2048 immediate (one past the signed 12-bit max) should be rejected");

    threw = false;
    try {
        rcc::assembleRiscv64("addi t0,t1,-2049\n");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "addi with a -2049 immediate (one past the signed 12-bit min) should be rejected");
}

// --- slli/srli/srai: 6-bit shamt (0-63), since registers are 64 bits wide ---------------
void testShiftImmediates64() {
    expectOne("slli t0,t1,63", 0x03f31293u, "slli (shamt=63, max)");
    expectOne("srli t0,t1,63", 0x03f35293u, "srli (shamt=63, max)");
    expectOne("srai t0,t1,63", 0x43f35293u, "srai (shamt=63, max)");
    expectOne("slli t0,t1,0", 0x00031293u, "slli (shamt=0)");
    expectOne("slli t0,t1,31", 0x01f31293u, "slli (shamt=31, still within a 5-bit range too)");
}

// slliw/srliw/sraiw: still a 5-bit shamt (0-31) - the result is only ever 32 bits wide.
void testShiftImmediateWord() {
    expectOne("slliw t0,t1,31", 0x01f3129bu, "slliw (shamt=31, max)");
    expectOne("srliw t0,t1,31", 0x01f3529bu, "srliw (shamt=31, max)");
    expectOne("sraiw t0,t1,31", 0x41f3529bu, "sraiw (shamt=31, max)");
    expectOne("slliw t0,t1,0", 0x0003129bu, "slliw (shamt=0)");
}

// --- Loads/stores, every width including the RV64-only lwu/ld/sd ------------------------
void testLoadsAndStores() {
    expectOne("lb t0,t1,-1", 0xfff30283u, "lb");
    expectOne("lh t0,t1,4", 0x00431283u, "lh");
    expectOne("lw t0,t1,4", 0x00432283u, "lw");
    expectOne("ld t0,t1,8", 0x00833283u, "ld");
    expectOne("lbu t0,t1,4", 0x00434283u, "lbu");
    expectOne("lhu t0,t1,4", 0x00435283u, "lhu");
    expectOne("lwu t0,t1,4", 0x00436283u, "lwu");
    expectOne("sb t0,t1,-1", 0xfe530fa3u, "sb");
    expectOne("sh t0,t1,4", 0x00531223u, "sh");
    expectOne("sw t0,t1,4", 0x00532223u, "sw");
    expectOne("sd t0,t1,8", 0x00533423u, "sd");
    // Displacement omitted defaults to 0.
    expectOne("ld t0,t1", 0x00033283u, "ld (implicit 0 displacement)");
    expectOne("sd t0,t1", 0x00533023u, "sd (implicit 0 displacement)");
}

// --- Branches: identical B-type encoding/range to riscv32's own ------------------------
void testBranches() {
    expectWords("beq t0,t1,L\nnop\nL:\n", {0x00628463u, 0x00000013u}, "beq +8 (forward)");
    expectWords("L:\nnop\nbne t0,t1,L\n", {0x00000013u, 0xfe629ee3u}, "bne -4 (backward)");
    expectOne("blt t0,t1,L\nL:", 0x0062c263u, "blt +4");
    expectOne("bge t0,t1,L\nL:", 0x0062d263u, "bge +4");
    expectOne("bltu t0,t1,L\nL:", 0x0062e263u, "bltu +4");
    expectOne("bgeu t0,t1,L\nL:", 0x0062f263u, "bgeu +4");
}

// --- U-type: lui/auipc ------------------------------------------------------------------
void testUType() {
    expectOne("lui t0,0xFFFFF", 0xfffff2b7u, "lui max imm20");
    expectOne("lui t0,1", 0x000012b7u, "lui imm20=1");
    expectOne("auipc t0,1", 0x00001297u, "auipc imm20=1");
    expectOne("lui t0,0", 0x000002b7u, "lui imm20=0");
}

// --- J-type: jal -------------------------------------------------------------------------
void testJal() {
    expectOne("jal ra,L\nL:", 0x004000efu, "jal ra,label (2-op, +4)");
    expectOne("jal L\nL:", 0x004000efu, "jal label (1-op pseudo, rd=ra implied, +4)");
    expectOne("j L\nL:", 0x0040006fu, "j label (pseudo, rd=zero, +4)");
}

// --- jalr/jr/ret/call/mv/li/nop pseudo-instructions -------------------------------------
void testJalrAndPseudoOps() {
    expectOne("jalr t0,t1,4", 0x004302e7u, "jalr rd,rs1,imm (3-op)");
    expectOne("jalr t0,t1", 0x000302e7u, "jalr rd,rs1 (2-op, imm=0)");
    expectOne("jalr t1", 0x000300e7u, "jalr rs1 (1-op pseudo, rd=ra)");
    expectOne("jr t1", 0x00030067u, "jr rs1 (pseudo, rd=zero)");
    expectOne("ret", 0x00008067u, "ret (pseudo, jalr zero,ra,0)");
    expectOne("nop", 0x00000013u, "nop (pseudo, addi zero,zero,0)");
    expectOne("mv t0,t1", 0x000302b3u, "mv (pseudo, add rd,rs,zero)");
    expectOne("li t0,100", 0x06400293u, "li (pseudo, fits in 12 bits -> addi)");
}

// `li` with a value outside the 12-bit range expands to a real `lui`+`addi` pair, same
// sign-rounding convention as riscv32's own `li`/absolute-address encoding.
void testLiWideExpansion() {
    std::string bytes = rcc::assembleRiscv64("li t0,2271560481\n");
    require(bytes.size() == 8, "li with a value outside [-2048,2047] should emit two instructions");
    uint32_t hiWord = static_cast<uint32_t>(static_cast<uint8_t>(bytes[0])) |
                       (static_cast<uint32_t>(static_cast<uint8_t>(bytes[1])) << 8) |
                       (static_cast<uint32_t>(static_cast<uint8_t>(bytes[2])) << 16) |
                       (static_cast<uint32_t>(static_cast<uint8_t>(bytes[3])) << 24);
    uint32_t loWord = static_cast<uint32_t>(static_cast<uint8_t>(bytes[4])) |
                       (static_cast<uint32_t>(static_cast<uint8_t>(bytes[5])) << 8) |
                       (static_cast<uint32_t>(static_cast<uint8_t>(bytes[6])) << 16) |
                       (static_cast<uint32_t>(static_cast<uint8_t>(bytes[7])) << 24);
    uint32_t hi20 = (hiWord >> 12) & 0xFFFFFu;
    int32_t lo12 = static_cast<int32_t>((loWord >> 20) & 0xFFFu);
    if (lo12 & 0x800) lo12 -= 0x1000;
    int32_t reconstructed = static_cast<int32_t>(hi20 << 12) + lo12;
    require(reconstructed == static_cast<int32_t>(2271560481u),
            "li's lui+addi pair should reconstruct the original 32-bit value exactly");
}

void testLiOutOfRangeRejected() {
    bool threw = false;
    try {
        rcc::assembleRiscv64("li t0,4294967296\n");  // 2^32: one past the signed-32-bit-range li accepts
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "'li' with a value outside the signed 32-bit range should be rejected");
}

// --- fence/fence.i/ecall/ebreak ----------------------------------------------------------
void testSystemInstructions() {
    expectOne("fence", 0x0ff0000fu, "fence (pred=succ=iorw)");
    expectOne("fence.i", 0x0000100fu, "fence.i");
    expectOne("ecall", 0x00000073u, "ecall");
    expectOne("ebreak", 0x00100073u, "ebreak");
}

// --- every one of the 32 integer registers, reachable both by its bare number and its
// standard ABI name -----------------------------------------------------------------------
void testAllThirtyTwoRegisters() {
    static const std::pair<const char *, int> kRegs[] = {
        {"zero", 0}, {"ra", 1}, {"sp", 2}, {"gp", 3}, {"tp", 4}, {"t0", 5}, {"t1", 6}, {"t2", 7},
        {"fp", 8}, {"s1", 9}, {"a0", 10}, {"a1", 11}, {"a2", 12}, {"a3", 13}, {"a4", 14}, {"a5", 15},
        {"a6", 16}, {"a7", 17}, {"s2", 18}, {"s3", 19}, {"s4", 20}, {"s5", 21}, {"s6", 22}, {"s7", 23},
        {"s8", 24}, {"s9", 25}, {"s10", 26}, {"s11", 27}, {"t3", 28}, {"t4", 29}, {"t5", 30}, {"t6", 31},
    };
    for (const auto &[name, num] : kRegs) {
        std::string abiAsm = std::string("mv ") + name + ",zero\n";
        std::string numAsm = "mv x" + std::to_string(num) + ",x0\n";
        uint32_t expected = 0x00000033u | (static_cast<uint32_t>(num) << 7);
        expectOne("mv " + std::string(name) + ",zero", expected, std::string("ABI register name '") + name + "'");
        expectOne("mv x" + std::to_string(num) + ",x0", expected, "numeric register name x" + std::to_string(num));
    }
}

void testFpS0Alias() {
    expectOne("mv t0,fp", 0x000402b3u, "fp names x8");
    expectOne("mv t0,s0", 0x000402b3u, "s0 names the same register as fp");
}

// --- object-mode: an undefined symbol defers as a relocation; plain mode rejects it ------
void testObjectModeExternalCallBecomesRelocation() {
    rcc::ObjectFile obj = rcc::assembleRiscv64Object("call fn_other\n");
    bool foundRelocation = false;
    for (const auto &reloc : obj.relocations) {
        if (reloc.symbolName == "fn_other") {
            require(reloc.type == rcc::kRiscv64RelocJal, "a cross-object 'call' should use the Jal relocation kind");
            foundRelocation = true;
        }
    }
    require(foundRelocation, "'call fn_other' with fn_other undefined in this object should defer as a relocation");

    bool foundUndefinedSymbol = false;
    for (const auto &sym : obj.symbols) {
        if (sym.name == "fn_other") {
            foundUndefinedSymbol = true;
            require(!sym.isDefined, "the undefined symbol's own table entry should be marked isDefined=false");
        }
    }
    require(foundUndefinedSymbol, "an undefined relocation target should still get its own symbol-table entry");
}

void testObjectModeExternalAddressBecomesHiLoRelocations() {
    rcc::ObjectFile obj = rcc::assembleRiscv64Object("lui t0,g_other\naddi t0,t0,g_other\n");
    bool foundHi = false, foundLo = false;
    for (const auto &reloc : obj.relocations) {
        if (reloc.symbolName != "g_other") continue;
        if (reloc.type == rcc::kRiscv64RelocHi) {
            require(reloc.offset == 0, "the Hi relocation should sit at the 'lui' instruction's own offset");
            foundHi = true;
        } else if (reloc.type == rcc::kRiscv64RelocLo) {
            require(reloc.offset == 4, "the Lo relocation should sit at the paired 'addi', 4 bytes after 'lui'");
            foundLo = true;
        }
    }
    require(foundHi && foundLo, "an external label split across 'lui'/'addi' should produce both a Hi and a Lo relocation");
}

void testObjectModeExternalDqBecomesAbs64Relocation() {
    rcc::ObjectFile obj = rcc::assembleRiscv64Object("dq g_other\n");
    bool found = false;
    for (const auto &reloc : obj.relocations) {
        if (reloc.symbolName == "g_other") {
            require(reloc.type == rcc::kRiscv64RelocAbs64, "a symbol 'dq' should use the Abs64 relocation kind");
            require(reloc.offset == 0, "the Abs64 relocation should sit at the 'dq' word's own offset");
            found = true;
        }
    }
    require(found, "'dq g_other' with g_other undefined in this object should defer as a relocation");
}

void testPlainModeUndefinedSymbolThrows() {
    bool threw = false;
    try {
        rcc::assembleRiscv64("call fn_other\n");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "assembleRiscv64 should reject an undefined symbol, unlike assembleRiscv64Object");
}

void testBracketedOperandRejected() {
    bool threw = false;
    try {
        rcc::assembleRiscv64("lw t0,[t1]\n");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "a bracketed memory operand isn't part of this dialect and should be rejected");
}

// --- db/dh/dw/dq data directives, plus an org-based label address check -----------------
void testDataDirectivesAndOrg() {
    std::string bytes = rcc::assembleRiscv64("org 0x1000\nL: dw 0x12345678\n");
    require(bytes.size() == 4, "a single 'dw' should emit exactly 4 bytes");
    require(bytes == word(0x12345678u), "'dw' should emit its word little-endian");

    bytes = rcc::assembleRiscv64("db 1,2,3\n");
    require(bytes.size() == 3 && bytes[0] == 1 && bytes[1] == 2 && bytes[2] == 3, "'db' byte sequence");

    bytes = rcc::assembleRiscv64("dh 0x1234\n");
    require(bytes.size() == 2 && static_cast<uint8_t>(bytes[0]) == 0x34 && static_cast<uint8_t>(bytes[1]) == 0x12,
            "'dh' should emit its halfword little-endian");

    bytes = rcc::assembleRiscv64("dq 0x123456789ABCDEF0\n");
    require(bytes.size() == 8, "a single 'dq' should emit exactly 8 bytes");
    require(bytes == qword(0x123456789ABCDEF0ull), "'dq' should emit its quadword little-endian");

    // A negative literal, to confirm 'dq' sign-extends/wraps the same way the other
    // directives do rather than truncating to 32 bits first.
    bytes = rcc::assembleRiscv64("dq -1\n");
    require(bytes.size() == 8, "'dq -1' should still emit exactly 8 bytes");
    require(bytes == qword(0xFFFFFFFFFFFFFFFFull), "'dq -1' should emit all-ones across the full 8 bytes");
}

}  // namespace

int main() {
    try {
        testRTypeAluOps();
        testMExtension64();
        testWordSizeRType();
        testWordSizeMExtension();
        testITypeArith();
        testAddiw();
        testOutOfRangeImmediateRejected();
        testShiftImmediates64();
        testShiftImmediateWord();
        testLoadsAndStores();
        testBranches();
        testUType();
        testJal();
        testJalrAndPseudoOps();
        testLiWideExpansion();
        testLiOutOfRangeRejected();
        testSystemInstructions();
        testAllThirtyTwoRegisters();
        testFpS0Alias();
        testObjectModeExternalCallBecomesRelocation();
        testObjectModeExternalAddressBecomesHiLoRelocations();
        testObjectModeExternalDqBecomesAbs64Relocation();
        testPlainModeUndefinedSymbolThrows();
        testBracketedOperandRejected();
        testDataDirectivesAndOrg();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
