// Byte-exact coverage of the Z8000 instruction encodings this backend's own dialect uses -
// cross-checked against MAME's own z8000 CPU core decode tables (src/devices/cpu/z8000/
// z8000tbl.hxx and 8000dasm.cpp) - see src/codegen/z8000/Z8000Assembler.h's own header comment
// for the addressing-mode syntax every operand below uses. The Z8000 is big-endian, so every
// expected word below is high byte first.
#include "codegen/z8000/Z8000Assembler.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expectBytes(const std::string &asmText, const std::vector<uint8_t> &want, const char *why) {
    std::string got;
    try {
        got = rcc::assembleZ8000(asmText);
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << why << ": threw " << e.what() << '\n';
        failures++;
        return;
    }
    std::vector<uint8_t> gotBytes(got.begin(), got.end());
    if (gotBytes != want) {
        std::cerr << "FAIL " << why << ": got";
        for (uint8_t b : gotBytes) std::cerr << ' ' << std::hex << static_cast<int>(b);
        std::cerr << ", want";
        for (uint8_t b : want) std::cerr << ' ' << std::hex << static_cast<int>(b);
        std::cerr << '\n';
        failures++;
        return;
    }
    std::cout << "PASS " << why << '\n';
}

void expectThrows(const std::string &asmText, const char *why) {
    try {
        rcc::assembleZ8000(asmText);
    } catch (const std::runtime_error &) {
        std::cout << "PASS " << why << '\n';
        return;
    }
    std::cerr << "FAIL " << why << ": expected a throw\n";
    failures++;
}

std::vector<uint8_t> w(uint16_t hi) { return {static_cast<uint8_t>(hi >> 8), static_cast<uint8_t>(hi & 0xFF)}; }
std::vector<uint8_t> concat(std::vector<uint8_t> a, const std::vector<uint8_t> &b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

// --- ld/ldb: register, immediate, direct-address (global), and indexed (local/pointer) -----
void testLoadStore() {
    expectBytes("ld r0,r1\n", w(0xA110), "ld_reg_reg");
    expectBytes("ldb rl0,rl1\n", w(0xA098), "ldb_reg_reg");
    expectBytes("ld r0,1234\n", concat(w(0x2100), w(1234)), "ld_reg_imm16");
    expectBytes("ldb rl0,127\n", concat(w(0x2008), w(127)), "ldb_reg_imm8");
    expectBytes("org 0\nfoo = 4096\nld r0,foo\n", concat(w(0x6100), w(4096)), "ld_reg_direct_global");
    expectBytes("org 0\nfoo = 4096\nld foo,r0\n", concat(w(0x6F00), w(4096)), "ld_direct_global_reg");
    expectBytes("org 0\nfoo = 300\nldb rl0,foo\n", concat(w(0x6008), w(300)), "ldb_reg_direct_global");
    expectBytes("org 0\nfoo = 300\nldb foo,rl0\n", concat(w(0x6E08), w(300)), "ldb_direct_global_reg");
    expectBytes("ld r0,[r13+4]\n", concat(w(0x31D0), w(4)), "ld_reg_indexed");
    expectBytes("ld [r13+4],r0\n", concat(w(0x33D0), w(4)), "ld_indexed_reg");
    expectBytes("ldb rl0,[r2+8]\n", concat(w(0x3028), w(8)), "ldb_reg_indexed");
    expectBytes("ldb [r2+8],rl0\n", concat(w(0x3228), w(8)), "ldb_indexed_reg");
}

void testLda() {
    expectBytes("org 0\nfoo = 4096\nlda r0,foo\n", concat(w(0x7600), w(4096)), "lda_direct");
    expectBytes("lda r0,[r13+6]\n", concat(w(0x34D0), w(6)), "lda_indexed");
}

// --- Word arithmetic/logical: register-register and register-immediate --------------------
void testWordAlu() {
    expectBytes("add r0,r1\n", w(0x8110), "add_reg_reg");
    expectBytes("add r0,5\n", concat(w(0x0100), w(5)), "add_reg_imm");
    expectBytes("sub r0,r1\n", w(0x8310), "sub_reg_reg");
    expectBytes("sub r0,5\n", concat(w(0x0300), w(5)), "sub_reg_imm");
    expectBytes("and r0,r1\n", w(0x8710), "and_reg_reg");
    expectBytes("and r0,255\n", concat(w(0x0700), w(255)), "and_reg_imm");
    expectBytes("or r0,r1\n", w(0x8510), "or_reg_reg");
    expectBytes("or r0,255\n", concat(w(0x0500), w(255)), "or_reg_imm");
    expectBytes("xor r0,r1\n", w(0x8910), "xor_reg_reg");
    expectBytes("xor r0,255\n", concat(w(0x0900), w(255)), "xor_reg_imm");
    expectBytes("cp r0,r1\n", w(0x8B10), "cp_reg_reg");
    expectBytes("cp r0,255\n", concat(w(0x0B00), w(255)), "cp_reg_imm");
    expectBytes("adc r0,r1\n", w(0xB510), "adc_reg_reg");
    expectBytes("sbc r0,r1\n", w(0xB710), "sbc_reg_reg");
}

// --- Byte arithmetic/logical: register-register and register-immediate -------------------
void testByteAlu() {
    expectBytes("addb rl0,rl1\n", w(0x8098), "addb_reg_reg");
    expectBytes("addb rl0,16\n", concat(w(0x0008), w(16)), "addb_reg_imm");
    expectBytes("subb rl0,rl1\n", w(0x8298), "subb_reg_reg");
    expectBytes("andb rl0,rl1\n", w(0x8698), "andb_reg_reg");
    expectBytes("orb rl0,rl1\n", w(0x8498), "orb_reg_reg");
    expectBytes("xorb rl0,rl1\n", w(0x8898), "xorb_reg_reg");
    expectBytes("cpb rl0,rl1\n", w(0x8A98), "cpb_reg_reg");
}

void testMulDiv() {
    expectBytes("mult r0,r2\n", w(0x9920), "mult_reg_reg");
    expectBytes("div r0,r2\n", w(0x9B20), "div_reg_reg");
}

// --- Single-register word family: com/neg/test/tset/clr -----------------------------------
void testWordSingleReg() {
    expectBytes("com r0\n", w(0x8D00), "com_reg");
    expectBytes("neg r0\n", w(0x8D02), "neg_reg");
    expectBytes("test r0\n", w(0x8D04), "test_reg");
    expectBytes("tset r0\n", w(0x8D06), "tset_reg");
    expectBytes("clr r0\n", w(0x8D08), "clr_reg");
}

void testByteSingleReg() {
    expectBytes("comb rl0\n", w(0x8C80), "comb_reg");
    expectBytes("negb rl0\n", w(0x8C82), "negb_reg");
    expectBytes("testb rl0\n", w(0x8C84), "testb_reg");
    expectBytes("tsetb rl0\n", w(0x8C86), "tsetb_reg");
    expectBytes("clrb rl0\n", w(0x8C88), "clrb_reg");
}

void testIncDec() {
    expectBytes("inc r0\n", w(0xA900), "inc_reg");
    expectBytes("dec r0\n", w(0xAB00), "dec_reg");
    expectBytes("incb rl0\n", w(0xA880), "incb_reg");
    expectBytes("decb rl0\n", w(0xAA80), "decb_reg");
}

void testExtsAndRotate() {
    expectBytes("exts r0\n", w(0xB10A), "exts_reg");
    expectBytes("extsb r0\n", w(0xB100), "extsb_reg");
    expectBytes("rlc r0\n", w(0xB308), "rlc_reg");
    expectBytes("rrc r0\n", w(0xB30C), "rrc_reg");
    expectBytes("rlcb rl0\n", w(0xB288), "rlcb_reg");
    expectBytes("rrcb rl0\n", w(0xB28C), "rrcb_reg");
}

void testFlagsAndSystem() {
    expectBytes("clc\n", w(0x8D81), "clc");
    expectBytes("nop\n", w(0x8D07), "nop");
    expectBytes("halt\n", w(0x7A00), "halt");
    expectBytes("ret\n", w(0x9E08), "ret");
}

void testStack() {
    expectBytes("push r3\n", w(0x93F3), "push_reg");
    expectBytes("pop r3\n", w(0x97F3), "pop_reg");
}

void testCallsAndBranches() {
    expectBytes("org 0\nfoo = 4096\ncall foo\n", concat(w(0x5F00), w(4096)), "call_direct");
    expectBytes("callind r2\n", w(0x1F20), "callind_reg");
    expectBytes("org 0\nfoo = 4096\nbeq foo\n", concat(w(0x5E06), w(4096)), "beq");
    expectBytes("org 0\nfoo = 4096\nbne foo\n", concat(w(0x5E0E), w(4096)), "bne");
    expectBytes("org 0\nfoo = 4096\nblt foo\n", concat(w(0x5E01), w(4096)), "blt");
    expectBytes("org 0\nfoo = 4096\nble foo\n", concat(w(0x5E02), w(4096)), "ble");
    expectBytes("org 0\nfoo = 4096\nbgt foo\n", concat(w(0x5E0A), w(4096)), "bgt");
    expectBytes("org 0\nfoo = 4096\nbge foo\n", concat(w(0x5E09), w(4096)), "bge");
    expectBytes("org 0\nfoo = 4096\nblo foo\n", concat(w(0x5E07), w(4096)), "blo");
    expectBytes("org 0\nfoo = 4096\nblos foo\n", concat(w(0x5E03), w(4096)), "blos");
    expectBytes("org 0\nfoo = 4096\nbhi foo\n", concat(w(0x5E0B), w(4096)), "bhi");
    expectBytes("org 0\nfoo = 4096\nbhis foo\n", concat(w(0x5E0F), w(4096)), "bhis");
    expectBytes("org 0\nfoo = 4096\nbr foo\n", concat(w(0x5E08), w(4096)), "br");
}

void testErrors() {
    expectThrows("ld r0,nosuchlabel\n", "undefined_symbol");
    expectThrows("ld r0,[r16+1]\n", "bad_base_register_out_of_range");
    expectThrows("ld r0,[r0+4]\n", "reserved_base_register_r0");
    expectThrows("callind r0\n", "callind_reserved_register_r0");
    expectThrows("frobnicate r0\n", "unsupported_mnemonic");
    expectThrows("org 0\nfoo = 1\npush foo\n", "push_needs_a_register_not_a_label");
}

}  // namespace

int main() {
    testLoadStore();
    testLda();
    testWordAlu();
    testByteAlu();
    testMulDiv();
    testWordSingleReg();
    testByteSingleReg();
    testIncDec();
    testExtsAndRotate();
    testFlagsAndSystem();
    testStack();
    testCallsAndBranches();
    testErrors();

    std::cout << "---counts---\n";
    std::cout << failures << '\n';
    return failures == 0 ? 0 : 1;
}
