#include "Riscv64LinkerFactory.h"

#include "codegen/riscv64/Riscv64Assembler.h"
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

// Identical bit-level convention to Riscv32LinkerFactory.cpp's own `patchRiscv32Lo` - `lui`/
// `addi` only ever build the low 32 bits of a 64-bit address (see Riscv64Codegen.h's own header
// comment on how the full 64-bit value is then recovered), so the Hi/Lo patch math itself is
// exactly the 32-bit scheme riscv32 already uses.
void patchRiscv64Lo(std::vector<uint8_t> &image, uint32_t siteOffset, uint32_t value, uint32_t) {
    uint32_t word = get32LE(image, siteOffset);
    uint32_t lo12 = (word >> 20) & 0xFFFu;
    lo12 = (lo12 + (value & 0xFFFu)) & 0xFFFu;
    put32LE(image, siteOffset, (word & 0x000FFFFFu) | (lo12 << 20));
}

void patchRiscv64Hi(std::vector<uint8_t> &image, uint32_t siteOffset, uint32_t value, uint32_t) {
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
void patchRiscv64Jal(std::vector<uint8_t> &image, uint32_t siteOffset, uint32_t value, uint32_t siteFinalAddr) {
    int32_t rel = static_cast<int32_t>(value) - static_cast<int32_t>(siteFinalAddr);
    uint32_t imm = static_cast<uint32_t>(rel);
    uint32_t bit20 = (imm >> 20) & 1u, bits10_1 = (imm >> 1) & 0x3FFu, bit11 = (imm >> 11) & 1u,
             bits19_12 = (imm >> 12) & 0xFFu;
    uint32_t word = get32LE(image, siteOffset);
    word = (word & 0x00000FFFu) | (bit20 << 31) | (bits10_1 << 21) | (bit11 << 20) | (bits19_12 << 12);
    put32LE(image, siteOffset, word);
}

// Plain contiguous 32-bit field; value adds straight onto the pre-patch word.
void patchRiscv64Abs32(std::vector<uint8_t> &image, uint32_t siteOffset, uint32_t value, uint32_t) {
    put32LE(image, siteOffset, get32LE(image, siteOffset) + value);
}

// `dq`'s own plain 64-bit field: only the low 32 bits ever carry a nonzero patch in practice
// (every address in this target's universe fits there - see Riscv64Codegen.h), but the field
// itself spans 8 bytes, so both words are read/written; the high word's own addition can still
// carry out of the low word's in the fully general case (a local-address "..` base shift plus
// a large positive linked base), so it's patched as one genuine 64-bit addition.
void patchRiscv64Abs64(std::vector<uint8_t> &image, uint32_t siteOffset, uint32_t value, uint32_t) {
    uint64_t old = static_cast<uint64_t>(get32LE(image, siteOffset)) |
                   (static_cast<uint64_t>(get32LE(image, siteOffset + 4)) << 32);
    uint64_t patched = old + static_cast<uint64_t>(value);
    put32LE(image, siteOffset, static_cast<uint32_t>(patched & 0xFFFFFFFFu));
    put32LE(image, siteOffset + 4, static_cast<uint32_t>((patched >> 32) & 0xFFFFFFFFu));
}

// Comfortably above the loaded program and its data, matching
// Riscv64Codegen.cpp's own kInitialSP. Built via `lui`+the same
// slli/srli-32 zero-extend Riscv64Codegen.h's own `loadAddress()` uses
// (see its header comment) rather than `li`: 0x80100000's bit 31 is set,
// so a plain `li` (which sign-extends like real `lui`/`addi`) would leave
// `sp` holding 0xFFFFFFFF80100000, not the intended, zero-extended
// 0x0000000080100000 - fatal for every later `sp`-relative memory access.
// `0x80100000`'s own low 12 bits are already zero, so no `addi` is needed.
constexpr const char *kEntryAsm =
    "org 0\n"
    "_start:\n"
    "    lui sp,0x80100\n"
    "    slli sp,sp,32\n"
    "    srli sp,sp,32\n"
    "    call fn_main\n"
    "halt:\n"
    "    j halt\n";

}  // namespace

std::unique_ptr<TargetLinker> makeRiscv64Linker() {
    GenericLinkerConfig config;
    config.baseAddress = static_cast<uint32_t>(kRiscv64BaseAddress);
    config.machineType = kRiscv64CoffMachine;
    config.entryObject = assembleRiscv64Object(kEntryAsm);
    config.relocPatchers[kRiscv64RelocHi] = patchRiscv64Hi;
    config.relocPatchers[kRiscv64RelocLo] = patchRiscv64Lo;
    config.relocPatchers[kRiscv64RelocJal] = patchRiscv64Jal;
    config.relocPatchers[kRiscv64RelocAbs32] = patchRiscv64Abs32;
    config.relocPatchers[kRiscv64RelocAbs64] = patchRiscv64Abs64;
    config.outputFormats = {"raw", "coff", "elf", "aout"};
    return std::make_unique<GenericLinker>(std::move(config));
}

}  // namespace rcc
