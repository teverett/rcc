// Checks I8080Assembler.cpp's IN n/OUT n encodings against the real Intel 8080 opcode table -
// added for src/rt/altair/'s 88-2SIO serial-board access, which needs port I/O the rest of this
// backend's instruction vocabulary can't express.
#include "codegen/i8080/I8080Assembler.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

void testInEncoding() {
    // IN n: DB nn - a plain immediate port number, always into the accumulator.
    for (int port : {0x00, 0x10, 0x11, 0xFF}) {
        std::string bytes = rcc::assembleI8080("in " + std::to_string(port) + "\n");
        require(bytes.size() == 2, "IN n should assemble to exactly 2 bytes");
        require(static_cast<uint8_t>(bytes[0]) == 0xDB, "IN n should start with opcode 0xDB");
        require(static_cast<uint8_t>(bytes[1]) == static_cast<uint8_t>(port), "IN n encoded the wrong port byte");
    }
}

void testOutEncoding() {
    // OUT n: D3 nn - same immediate-port shape as IN n above, writing the accumulator.
    for (int port : {0x00, 0x10, 0x11, 0xFF}) {
        std::string bytes = rcc::assembleI8080("out " + std::to_string(port) + "\n");
        require(bytes.size() == 2, "OUT n should assemble to exactly 2 bytes");
        require(static_cast<uint8_t>(bytes[0]) == 0xD3, "OUT n should start with opcode 0xD3");
        require(static_cast<uint8_t>(bytes[1]) == static_cast<uint8_t>(port), "OUT n encoded the wrong port byte");
    }
}

void rejects(const std::string &asmText, const char *why) {
    try {
        rcc::assembleI8080(asmText);
    } catch (const std::runtime_error &) {
        return;
    }
    throw std::runtime_error(std::string("should have rejected: ") + why);
}

void testRejections() {
    rejects("in\n", "IN with no port operand");
    rejects("in 1, 2\n", "IN with two operands - the 8080 has no register form");
    rejects("out\n", "OUT with no port operand");
    rejects("out 1, 2\n", "OUT with two operands - the 8080 has no register form");
}

}  // namespace

int main() {
    try {
        testInEncoding();
        testOutEncoding();
        testRejections();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
