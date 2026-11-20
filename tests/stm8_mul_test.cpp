// Coverage for STM8Codegen.cpp's hardware-multiply path: a plain (non-"long") `*` now composes
// three `MUL X,A` instructions (unsigned 8x8->16, the lower byte of `x` times `a`, full 16-bit
// result left in `x`) instead of looping 16 times through a shift-and-add, mirroring
// AVRCodegen.cpp's genNarrowMul(). Two things are checked: that the assembler dialect recognizes
// the new `mul` mnemonic and rejects any operand shape other than `x, a`, and that the exact
// instruction sequence STM8Codegen::genNarrowMul() emits, run through a tiny interpreter for just
// the handful of opcodes that sequence uses, produces the correct 16-bit truncated product for a
// range of operand pairs (including the ones a plain shift-and-add loop could get wrong: the two
// cross terms' overflow, the dropped hi*hi term, and two's-complement negative operands).
#include "codegen/stm8/STM8Assembler.h"

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
        got = rcc::assembleSTM8(asmText);
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
        std::cerr << std::dec << '\n';
        failures++;
        return;
    }
    std::cout << "PASS " << why << '\n';
}

void expectThrows(const std::string &asmText, const char *why) {
    try {
        rcc::assembleSTM8(asmText);
    } catch (const std::runtime_error &) {
        std::cout << "PASS " << why << '\n';
        return;
    }
    std::cerr << "FAIL " << why << ": expected a throw\n";
    failures++;
}

void testMulEncoding() {
    expectBytes("mul x, a\n", {0x24}, "mul_x_a_encodes_to_single_byte");
    expectThrows("mul y, a\n", "mul_rejects_y_as_first_operand");
    expectThrows("mul x, y\n", "mul_rejects_y_as_second_operand");
    expectThrows("mul a, x\n", "mul_rejects_swapped_operands");
}

// --- A tiny interpreter for just the opcodes genNarrowMul()'s own output uses --------------

struct MiniInterp {
    std::vector<uint8_t> mem;
    uint16_t x = 0;
    uint8_t a = 0;

    explicit MiniInterp(const std::string &bytes) : mem(bytes.begin(), bytes.end()) {}

    uint16_t rd16(size_t addr) const {
        return static_cast<uint16_t>((static_cast<uint16_t>(mem.at(addr)) << 8) | mem.at(addr + 1));
    }
    void wr16(size_t addr, uint16_t v) {
        mem.at(addr) = static_cast<uint8_t>(v >> 8);
        mem.at(addr + 1) = static_cast<uint8_t>(v & 0xFF);
    }

    // Runs straight-line code (no jumps) for exactly `codeLen` bytes.
    void run(size_t codeLen) {
        size_t pc = 0;
        while (pc < codeLen) {
            uint8_t op = mem.at(pc++);
            switch (op) {
                case 0x24:  // mul x, a: x = (x's low byte) * a, full 16-bit result
                    x = static_cast<uint16_t>(static_cast<uint16_t>(x & 0xFF) * a);
                    break;
                case 0x40: {  // ldw x, mem
                    uint16_t addr = rd16(pc);
                    pc += 2;
                    x = rd16(addr);
                    break;
                }
                case 0x42: {  // ldw mem, x
                    uint16_t addr = rd16(pc);
                    pc += 2;
                    wr16(addr, x);
                    break;
                }
                case 0x44: {  // ld a, mem
                    uint16_t addr = rd16(pc);
                    pc += 2;
                    a = mem.at(addr);
                    break;
                }
                case 0x45: {  // ld mem, a
                    uint16_t addr = rd16(pc);
                    pc += 2;
                    mem.at(addr) = a;
                    break;
                }
                case 0x74: {  // add a, mem (no carry)
                    uint16_t addr = rd16(pc);
                    pc += 2;
                    a = static_cast<uint8_t>(a + mem.at(addr));
                    break;
                }
                default:
                    throw std::runtime_error("unexpected opcode 0x" + std::to_string(op) +
                                              " in narrow-mul snippet");
            }
        }
    }
};

// Mirrors STM8Codegen::genNarrowMul()'s own emitted instruction sequence exactly (`bc` =
// multiplicand, `dx` = multiplier, `ac` = product; `mc`/`lc` scratch) - see that function's
// header comment in STM8Codegen.cpp for why only these three MULs and this one cross-term
// summation are needed for a correct 16-bit result.
const char *kNarrowMulAsm =
    "    ldw x, bc\n    ld a, dx+1\n    mul x, a\n"
    "    ldw ac, x\n"
    "    ld a, bc\n    ld mc+1, a\n    ldw x, mc\n    ld a, dx+1\n    mul x, a\n"
    "    ldw mc, x\n    ld a, mc+1\n    ld lc, a\n"
    "    ldw x, bc\n    ld a, dx\n    mul x, a\n"
    "    ldw mc, x\n    ld a, mc+1\n    add a, lc\n    ld lc, a\n"
    "    ld a, ac\n    add a, lc\n    ld ac, a\n"
    "bc: db 0, 0\n"
    "dx: db 0, 0\n"
    "ac: db 0, 0\n"
    "mc: db 0, 0\n"
    "lc: db 0\n";

void checkProduct(uint16_t bc, uint16_t dx, uint16_t wantProduct, const char *why) {
    std::string bytes = rcc::assembleSTM8(kNarrowMulAsm);
    const size_t dataBytes = 2 + 2 + 2 + 2 + 1;  // bc, dx, ac, cx, lc
    size_t codeLen = bytes.size() - dataBytes;
    size_t addrBc = codeLen, addrDx = codeLen + 2, addrAc = codeLen + 4;

    MiniInterp interp(bytes);
    interp.wr16(addrBc, bc);
    interp.wr16(addrDx, dx);
    interp.run(codeLen);
    uint16_t got = interp.rd16(addrAc);

    if (got != wantProduct) {
        std::cerr << "FAIL " << why << ": " << bc << " * " << dx << " -> got " << got << ", want "
                  << wantProduct << '\n';
        failures++;
        return;
    }
    std::cout << "PASS " << why << '\n';
}

void testNarrowMulArithmetic() {
    checkProduct(200, 200, 40000u, "small_operands_no_truncation");
    checkProduct(300, 300, 24464u, "cross_term_wraps_90000_mod_65536");
    checkProduct(256, 256, 0u, "hi_times_hi_term_fully_discarded");
    checkProduct(255, 255, 65025u, "max_byte_operands");
    checkProduct(0, 12345, 0u, "zero_operand");
    checkProduct(static_cast<uint16_t>(-5), 7, static_cast<uint16_t>(-35), "negative_times_positive");
    checkProduct(static_cast<uint16_t>(-5), static_cast<uint16_t>(-7), 35u, "negative_times_negative");
    checkProduct(1, static_cast<uint16_t>(-1), static_cast<uint16_t>(-1), "identity_with_negative");
}

}  // namespace

int main() {
    testMulEncoding();
    testNarrowMulArithmetic();
    if (failures > 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "All tests passed\n";
    return 0;
}
