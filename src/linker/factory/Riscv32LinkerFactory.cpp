#include "Riscv32LinkerFactory.h"

#include "codegen/riscv32/Riscv32Assembler.h"
#include "linker/GenericLinker.h"

namespace rcc {

namespace {

// RISC-V instruction words are always little-endian.
uint32_t get32LE(const std::vector<uint8_t> &image, size_t idx) {
    return static_cast<uint32_t>(image[idx]) | (static_cast<uint32_t>(image[idx + 1]) << 8) |
           (static_cast<uint32_t>(image[idx + 2]) << 16) | (static_cast<uint32_t>(image[idx + 3]) << 24);
}
void put32LE(std::vector<uint8_t> &image, size_t idx, uint32_t v) {
    image[idx] = static_cast<uint8_t>(v & 0xFF);
    image[idx + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    image[idx + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    image[idx + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

// Patches `addi`'s 12-bit immediate (the low address half, bits[31:20] of
// the instruction word). The field holds the low 12 bits of the pre-patch
// partial address verbatim (not sign-extended), so simply adding `value`'s
// own low 12 bits and truncating mod 4096 is exact - `patchRiscv32Hi`
// reads this site's own pre-patch value (at -4) to work out the matching
// upper-20 adjustment, so this must patch *after* Hi (relocations are
// patched in code order, and `lui` is always recorded before its `addi`).
void patchRiscv32Lo(std::vector<uint8_t> &image, uint32_t siteOffset, uint32_t value, uint32_t) {
    uint32_t word = get32LE(image, siteOffset);
    uint32_t lo12 = (word >> 20) & 0xFFFu;
    lo12 = (lo12 + (value & 0xFFFu)) & 0xFFFu;
    put32LE(image, siteOffset, (word & 0x000FFFFFu) | (lo12 << 20));
}

// Patches `lui`'s 20-bit immediate (the high address half). Reconstructs the pre-patch 32-bit
// value from this site's own upper-20 field and the paired `addi` site's pre-patch lower-12
// field (at +4, still unpatched - see `patchRiscv32Lo`'s own comment), adds `value`, then
// re-splits the sum with the same sign-rounding `addi`'s own runtime sign-extension needs
// (mirrors the real R_RISCV_HI20/LO12_I relocation pair).
void patchRiscv32Hi(std::vector<uint8_t> &image, uint32_t siteOffset, uint32_t value, uint32_t) {
    uint32_t hiWord = get32LE(image, siteOffset);
    uint32_t loWordOrig = get32LE(image, siteOffset + 4);
    uint32_t hi20Old = (hiWord >> 12) & 0xFFFFFu;
    uint32_t lo12Old = (loWordOrig >> 20) & 0xFFFu;
    uint32_t borrowOld = (lo12Old & 0x800u) ? 1u : 0u;
    uint32_t oldFull = (hi20Old << 12) - (borrowOld << 12) + lo12Old;

    uint32_t newFull = oldFull + value;
    uint32_t lo12New = newFull & 0xFFFu;
    uint32_t borrowNew = (lo12New & 0x800u) ? 1u : 0u;
    uint32_t hi20New = ((newFull >> 12) + borrowNew) & 0xFFFFFu;
    put32LE(image, siteOffset, (hiWord & 0x00000FFFu) | (hi20New << 12));
}

// `jal`'s 20-bit PC-relative field; `value` is the target symbol's own final absolute address.
void patchRiscv32Jal(std::vector<uint8_t> &image, uint32_t siteOffset, uint32_t value, uint32_t siteFinalAddr) {
    int32_t rel = static_cast<int32_t>(value) - static_cast<int32_t>(siteFinalAddr);
    uint32_t imm = static_cast<uint32_t>(rel);
    uint32_t bit20 = (imm >> 20) & 1u, bits10_1 = (imm >> 1) & 0x3FFu, bit11 = (imm >> 11) & 1u,
             bits19_12 = (imm >> 12) & 0xFFu;
    uint32_t word = get32LE(image, siteOffset);
    word = (word & 0x00000FFFu) | (bit20 << 31) | (bits10_1 << 21) | (bit11 << 20) | (bits19_12 << 12);
    put32LE(image, siteOffset, word);
}

// Plain contiguous 32-bit field; value adds straight onto the pre-patch word.
void patchRiscv32Abs32(std::vector<uint8_t> &image, uint32_t siteOffset, uint32_t value, uint32_t) {
    put32LE(image, siteOffset, get32LE(image, siteOffset) + value);
}

// Comfortably above the loaded program and its data, matching
// Riscv32Codegen.cpp's own kInitialSP.
constexpr const char *kEntryAsm =
    "org 0\n"
    "_start:\n"
    "    li sp,0x80100000\n"
    "    call fn_main\n"
    "halt:\n"
    "    j halt\n";

}  // namespace

std::unique_ptr<TargetLinker> makeRiscv32Linker() {
    GenericLinkerConfig config;
    config.baseAddress = kRiscv32BaseAddress;
    config.machineType = kRiscv32CoffMachine;
    config.entryObject = assembleRiscv32Object(kEntryAsm);
    config.relocPatchers[kRiscv32RelocHi] = patchRiscv32Hi;
    config.relocPatchers[kRiscv32RelocLo] = patchRiscv32Lo;
    config.relocPatchers[kRiscv32RelocJal] = patchRiscv32Jal;
    config.relocPatchers[kRiscv32RelocAbs32] = patchRiscv32Abs32;
    config.outputFormats = {"raw", "coff", "elf", "aout"};
    return std::make_unique<GenericLinker>(std::move(config));
}

}  // namespace rcc
