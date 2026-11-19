#pragma once

#include <cstdint>
#include <string>

#include "fileformat/ObjectFile.h"

namespace rcc {

// This dialect's own machine ID; not a registered COFF/ELF value. ASCII "RV".
constexpr uint16_t kRiscv32CoffMachine = 0x5256;

// Must match Riscv32Codegen.cpp's own kBaseAddress: the RISC-V `virt` QEMU
// machine's RAM starts here, so a linked or single-file program loads at
// the same address the test harness points the simulated CPU's PC at.
constexpr uint32_t kRiscv32BaseAddress = 0x80000000u;

// Local relocation kinds, not GenericLinker.h's RelocKinds: a `lui`/`addi`
// absolute-address pair splits its target into an upper/lower half the way
// real RISC-V `%hi`/`%lo` relocations do (with the lower half's own sign
// rounded into the upper half - see Riscv32LinkerFactory.cpp's patchers),
// and `jal`'s 20-bit field is a word-pair-aligned PC-relative displacement.
// Hi/Lo: upper 20 / lower 12 bits of an address, split across an adjacent
// `lui`/`addi` pair always emitted 4 bytes apart so the linker's Hi patch
// can find the paired Lo site at +4.
constexpr uint16_t kRiscv32RelocHi = 1;
constexpr uint16_t kRiscv32RelocLo = 2;
// `jal`'s 20-bit signed word-pair-aligned displacement field (bits
// [20|10:1|11|19:12], see the real RISC-V J-type encoding); used only for
// a call to a symbol outside this object - an intra-object `jal` resolves
// at assembly time and needs no relocation at all.
constexpr uint16_t kRiscv32RelocJal = 3;
// A plain 32-bit data word (`dw`); ordinary additive patch.
constexpr uint16_t kRiscv32RelocAbs32 = 4;

// Two-pass assembler for exactly the narrow RISC-V dialect Riscv32Codegen.cpp
// emits, not a general-purpose RISC-V assembler, though every mnemonic below
// still encodes the real RV32I+M bit pattern the ISA manual specifies -
// only which operand shapes exist is narrowed (no `offset(reg)` syntax; a
// load/store's base register and displacement are instead two plain,
// comma-separated operands, and `lui`/`addi` resolve a label operand to its
// own upper-20/lower-12 bits automatically instead of needing `%hi()`/
// `%lo()`). Implements the full RV32I base instruction set (all six
// encoding formats) plus the M extension (mul/mulh/mulhsu/mulhu/div/divu/
// rem/remu), along with the standard `nop`/`li`/`mv`/`j`/`call`/`ret`/`jr`
// pseudo-instructions. Every instruction is a fixed 4 bytes; RISC-V has no
// branch-delay slot, unlike this project's MIPS/SPARC backends.
// Throws std::runtime_error on any assembly error.
std::string assembleRiscv32(const std::string &asmText);

// Separate-compilation entry point: assembles `asmText` (emitted with
// `org 0`) like assembleRiscv32(), but defers undefined symbols as
// relocations instead of failing, and additionally relocates every local
// absolute-address reference against the reserved pseudo-symbol ".." (see
// GenericLinker.h) since the object has no fixed base address yet.
ObjectFile assembleRiscv32Object(const std::string &asmText);

}  // namespace rcc
