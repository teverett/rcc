#include "codegen/i386/I386Assembler.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

std::string bytesOf(const std::string &asmText) { return rcc::assembleI486("org 0\n" + asmText); }

// `bswap eax` / `bswap ecx`: 0F C8+rd, register-only, no ModRM.
void testBswap() {
    require(bytesOf("bswap eax\n") == std::string("\x0F\xC8", 2), "bswap eax");
    require(bytesOf("bswap ecx\n") == std::string("\x0F\xC9", 2), "bswap ecx");
}

// `xadd eax,ecx`: 0F C1 /r, register-register form - reg field is the source (ecx=1), rm
// field is the destination (eax=0).
void testXaddRegReg() {
    require(bytesOf("xadd eax,ecx\n") == std::string("\x0F\xC1\xC8", 3), "xadd eax,ecx");
}

// `xadd [g],eax`: 0F C1 /r against a direct [label] memory destination (mod=00, rm=101,
// disp32 = the label's own address, which is 0 here since it's also the first byte emitted).
void testXaddMemReg() {
    std::string bytes = bytesOf("g: xadd [g],eax\n");
    std::string expected("\x0F\xC1\x05", 3);
    expected += '\0';
    expected += '\0';
    expected += '\0';
    expected += '\0';
    require(bytes == expected, "xadd [g],eax");
}

// `cmpxchg eax,ecx`: 0F B1 /r, same operand shape as xadd.
void testCmpxchgRegReg() {
    require(bytesOf("cmpxchg eax,ecx\n") == std::string("\x0F\xB1\xC8", 3), "cmpxchg eax,ecx");
}

// `cmpxchg [g],ecx`: 0F B1 /r against a direct [label] memory destination - the same shape
// testXaddMemReg() checks for xadd, since both share the same dst,src encoder.
void testCmpxchgMemReg() {
    std::string bytes = bytesOf("g: cmpxchg [g],ecx\n");
    std::string expected("\x0F\xB1\x0D", 3);
    expected += '\0';
    expected += '\0';
    expected += '\0';
    expected += '\0';
    require(bytes == expected, "cmpxchg [g],ecx");
}

// bswap/xadd/cmpxchg have no 8/16-bit register form on real hardware; `al`/`ax`/`cl` must be
// rejected rather than silently encoded as if they were eax/ecx (al/ax/cl alias a plain
// register's own index - see Operand's own is8/is16 comment).
void testRejectsNarrowRegisters() {
    static const char *mnemonics[] = {"bswap al\n", "bswap ax\n", "xadd eax,al\n", "xadd al,eax\n",
                                       "cmpxchg eax,ax\n", "cmpxchg ax,eax\n"};
    for (const char *src : mnemonics) {
        bool threw = false;
        try {
            bytesOf(src);
        } catch (const std::exception &) {
            threw = true;
        }
        require(threw, (std::string("should reject an 8/16-bit register operand: ") + src).c_str());
    }
}

void testInvdWbinvd() {
    require(bytesOf("invd\n") == std::string("\x0F\x08", 2), "invd");
    require(bytesOf("wbinvd\n") == std::string("\x0F\x09", 2), "wbinvd");
}

// `invlpg [g]`: 0F 01 /7 against a direct [label] memory operand.
void testInvlpg() {
    std::string bytes = bytesOf("g: invlpg [g]\n");
    std::string expected("\x0F\x01\x3D", 3);
    expected += '\0';
    expected += '\0';
    expected += '\0';
    expected += '\0';
    require(bytes == expected, "invlpg [g]");
}

void testI386RejectsI486Instructions() {
    static const char *mnemonics[] = {"bswap eax\n",   "xadd eax,ecx\n", "cmpxchg eax,ecx\n",
                                       "invd\n",        "wbinvd\n",       "invlpg [g]\n"};
    for (const char *src : mnemonics) {
        bool threw = false;
        try {
            rcc::assembleI386(std::string("org 0\ng: ") + src);
        } catch (const std::exception &) {
            threw = true;
        }
        require(threw, (std::string("--target=i386 should reject: ") + src).c_str());
    }
}

}  // namespace

int main() {
    try {
        testBswap();
        testXaddRegReg();
        testXaddMemReg();
        testCmpxchgRegReg();
        testCmpxchgMemReg();
        testRejectsNarrowRegisters();
        testInvdWbinvd();
        testInvlpg();
        testI386RejectsI486Instructions();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
