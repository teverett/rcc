// Byte-exact coverage of the CP1610 instruction encodings this backend's own dialect uses -
// cross-checked against MAME's own cp1610.cpp/1610dasm.cpp decode tables (fetched live from
// GitHub while developing this backend) - see src/codegen/cp1610/CP1610Assembler.h's own header
// comment for the addressing-mode syntax every operand below uses. This CPU is word-addressed
// (see that same header comment): every word here is a plain 16-bit little-endian value, this
// dialect's own byte-packing choice for the `raw` format - not a real Intellivision ROM "decle".
#include "codegen/cp1610/CP1610Assembler.h"

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
        got = rcc::assembleCP1610(asmText);
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
        rcc::assembleCP1610(asmText);
    } catch (const std::runtime_error &) {
        std::cout << "PASS " << why << '\n';
        return;
    }
    std::cerr << "FAIL " << why << ": expected a throw\n";
    failures++;
}

// A little-endian 16-bit word.
std::vector<uint8_t> w(uint16_t v) { return {static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>((v >> 8) & 0xFF)}; }
std::vector<uint8_t> concat(std::vector<uint8_t> a, const std::vector<uint8_t> &b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

void testRegReg() {
    expectBytes("movr r1,r2\n", w(0x08A), "movr_reg_reg");
    expectBytes("tstr r3\n", w(0x09B), "tstr");
    expectBytes("addr r1,r2\n", w(0x0CA), "addr_reg_reg");
    expectBytes("subr r2,r1\n", w(0x111), "subr_reg_reg");
    expectBytes("cmpr r1,r2\n", w(0x14A), "cmpr_reg_reg");
    expectBytes("andr r1,r2\n", w(0x18A), "andr_reg_reg");
    expectBytes("xorr r1,r2\n", w(0x1CA), "xorr_reg_reg");
    expectBytes("clrr r4\n", w(0x1E4), "clrr");
}

void testOneReg() {
    expectBytes("incr r3\n", w(0x00B), "incr");
    expectBytes("decr r3\n", w(0x013), "decr");
    expectBytes("comr r3\n", w(0x01B), "comr");
    expectBytes("negr r3\n", w(0x023), "negr");
    expectBytes("adcr r3\n", w(0x02B), "adcr");
    expectBytes("gswd r2\n", w(0x032), "gswd");
    expectBytes("rswd r5\n", w(0x03D), "rswd");
    expectBytes("hlt\n", w(0x000), "hlt");
    expectBytes("eis\n", w(0x002), "eis");
    expectBytes("dis\n", w(0x003), "dis");
    expectBytes("tci\n", w(0x005), "tci");
    expectBytes("clrc\n", w(0x006), "clrc");
    expectBytes("setc\n", w(0x007), "setc");
    expectBytes("nop\n", w(0x034), "nop");
    expectBytes("sin\n", w(0x036), "sin");
}

void testShift() {
    expectBytes("swap r2,1\n", w(0x042), "swap_1");
    expectBytes("dswap r2\n", w(0x046), "dswap");
    expectBytes("sll r1,1\n", w(0x049), "sll_1");
    expectBytes("sll r1,2\n", w(0x04D), "sll_2");
    expectBytes("rlc r1,1\n", w(0x051), "rlc_1");
    expectBytes("rlc r1,2\n", w(0x055), "rlc_2");
    expectBytes("sllc r1,1\n", w(0x059), "sllc_1");
    expectBytes("slr r1,1\n", w(0x061), "slr_1");
    expectBytes("sar r1,1\n", w(0x069), "sar_1");
    expectBytes("rrc r1,1\n", w(0x071), "rrc_1");
    expectBytes("sarc r1,1\n", w(0x079), "sarc_1");
    expectThrows("sll r5,1\n", "shift_rejects_r4_and_up");
}

void testAddressed() {
    expectBytes("org 0\nfoo = 4096\nmvi foo,r2\n", concat(w(0x282), w(4096)), "mvi_direct");
    expectBytes("mvi [r1],r2\n", w(0x28A), "mvi_indirect_r1");
    expectBytes("mvi [r4],r2\n", w(0x2A2), "mvi_indirect_r4_autoinc");
    expectBytes("mvi [r6],r2\n", w(0x2B2), "mvi_indirect_r6_autodec");
    expectBytes("mvii 1234,r2\n", concat(w(0x2BA), w(1234)), "mvii");
    expectBytes("org 0\nfoo = 300\nmvo r2,foo\n", concat(w(0x242), w(300)), "mvo_direct");
    expectBytes("mvo r2,[r3]\n", w(0x25A), "mvo_indirect_r3");
    expectBytes("pshr r3\n", w(0x273), "pshr");
    expectBytes("pulr r3\n", w(0x2B3), "pulr");
    expectBytes("add 5,r2\n", concat(w(0x2FA), w(5)), "add_imm");
}

void testAddImmVsIndirect() {
    expectBytes("add [r1],r2\n", w(0x2C2 | (1 << 3)), "add_indirect_r1");
    expectBytes("sub [r2],r3\n", w(0x303 | (2 << 3)), "sub_indirect_r2");
    expectBytes("cmp 7,r1\n", concat(w(0x341 | (7 << 3)), w(7)), "cmp_imm");
    expectBytes("and [r3],r1\n", w(0x381 | (3 << 3)), "and_indirect_r3");
    expectBytes("xor [r4],r1\n", w(0x3C1 | (4 << 3)), "xor_indirect_r4");
}

void testBranchesAndJumps() {
    expectBytes("org 0\nfoo = 10\nb foo\n", concat(w(0x200), w((10u - 2u) & 0xFFFF)), "b_forward");
    expectBytes("org 10\nfoo = 0\nb foo\n", concat(w(0x200), w(static_cast<uint16_t>(0 - 12))), "b_backward_wraps");
    expectBytes("org 0\nfoo = 4\nbc foo\n", concat(w(0x201), w(2)), "bc");
    expectBytes("org 0\nfoo = 4\nbov foo\n", concat(w(0x202), w(2)), "bov");
    expectBytes("org 0\nfoo = 4\nbpl foo\n", concat(w(0x203), w(2)), "bpl");
    expectBytes("org 0\nfoo = 4\nbze foo\n", concat(w(0x204), w(2)), "bze");
    expectBytes("org 0\nfoo = 4\nblt foo\n", concat(w(0x205), w(2)), "blt");
    expectBytes("org 0\nfoo = 4\nble foo\n", concat(w(0x206), w(2)), "ble");
    expectBytes("org 0\nfoo = 4\nbnc foo\n", concat(w(0x209), w(2)), "bnc");
    expectBytes("org 0\nfoo = 4\nbmi foo\n", concat(w(0x20B), w(2)), "bmi");
    expectBytes("org 0\nfoo = 4\nbnze foo\n", concat(w(0x20C), w(2)), "bnze");
    expectBytes("org 0\nfoo = 4\nbge foo\n", concat(w(0x20D), w(2)), "bge");
    expectBytes("org 0\nfoo = 4\nbgt foo\n", concat(w(0x20E), w(2)), "bgt");

    expectBytes("org 0\nfoo = 4142\nj foo\n",
                concat(w(0x004), concat(w(0x300 | (((4142u >> 10) & 0x3F) << 2)), w(4142u & 0x3FF))), "plain_j");
    expectBytes(
        "org 0\nfoo = 4142\njsr r5,foo\n",
        concat(w(0x004), concat(w((1u << 8) | (((4142u >> 10) & 0x3F) << 2)), w(4142u & 0x3FF))), "jsr_r5");
    expectThrows("jsr r3,foo\n", "jsr_rejects_bad_link_register");
}

void testErrors() {
    expectThrows("org 0\nfoo = 1\nmovr r1,foo\n", "movr_needs_registers");
    expectThrows("frobnicate r1\n", "unsupported_mnemonic");
    expectThrows("mvi [r1+4],r2\n", "no_displacement_addressing");
}

}  // namespace

int main() {
    testRegReg();
    testOneReg();
    testShift();
    testAddressed();
    testAddImmVsIndirect();
    testBranchesAndJumps();
    testErrors();

    std::cout << "---counts---\n";
    std::cout << failures << '\n';
    return failures == 0 ? 0 : 1;
}
