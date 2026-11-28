#include "codegen/i386/I386Assembler.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

std::string bytesOf(const std::string &asmText) { return rcc::assembleI586("org 0\n" + asmText); }

// `cpuid`/`rdtsc`/`rdmsr`/`wrmsr`: 0F A2/0F 31/0F 32/0F 30, no operand, no ModRM.
void testSimpleNoOperand() {
    require(bytesOf("cpuid\n") == std::string("\x0F\xA2", 2), "cpuid");
    require(bytesOf("rdtsc\n") == std::string("\x0F\x31", 2), "rdtsc");
    require(bytesOf("rdmsr\n") == std::string("\x0F\x32", 2), "rdmsr");
    require(bytesOf("wrmsr\n") == std::string("\x0F\x30", 2), "wrmsr");
}

// `cmpxchg8b [g]`: 0F C7 /1 against a direct [label] memory operand (mod=00, rm=101,
// disp32 = the label's own address, which is 0 here since it's also the first byte emitted).
void testCmpxchg8bMem() {
    std::string bytes = bytesOf("g: cmpxchg8b [g]\n");
    std::string expected("\x0F\xC7\x0D", 3);
    expected += '\0';
    expected += '\0';
    expected += '\0';
    expected += '\0';
    require(bytes == expected, "cmpxchg8b [g]");
}

// cmpxchg8b has no register-only form - a real 8-byte compare-exchange always needs a
// memory operand.
void testCmpxchg8bRejectsRegister() {
    bool threw = false;
    try {
        rcc::assembleI586("org 0\ncmpxchg8b eax\n");
    } catch (const std::exception &) {
        threw = true;
    }
    require(threw, "cmpxchg8b eax should be rejected (no register form)");
}

void testI386AndI486RejectI586Instructions() {
    static const char *mnemonics[] = {"cpuid\n", "rdtsc\n", "rdmsr\n", "wrmsr\n", "cmpxchg8b [g]\n"};
    for (const char *src : mnemonics) {
        bool threwOnI386 = false;
        try {
            rcc::assembleI386(std::string("org 0\ng: ") + src);
        } catch (const std::exception &) {
            threwOnI386 = true;
        }
        require(threwOnI386, (std::string("--target=i386 should reject: ") + src).c_str());

        bool threwOnI486 = false;
        try {
            rcc::assembleI486(std::string("org 0\ng: ") + src);
        } catch (const std::exception &) {
            threwOnI486 = true;
        }
        require(threwOnI486, (std::string("--target=i486 should reject: ") + src).c_str());
    }
}

// i586 is a superset of i486: every i486-only instruction (see i486_assembler_test.cpp) must
// still assemble under --target=i586.
void testI586AcceptsI486Instructions() {
    require(bytesOf("bswap eax\n") == std::string("\x0F\xC8", 2), "bswap eax under i586");
    require(bytesOf("invd\n") == std::string("\x0F\x08", 2), "invd under i586");
}

}  // namespace

int main() {
    try {
        testSimpleNoOperand();
        testCmpxchg8bMem();
        testCmpxchg8bRejectsRegister();
        testI386AndI486RejectI586Instructions();
        testI586AcceptsI486Instructions();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
