#pragma once

#include <cstdint>
#include <string>

#include "fileformat/ObjectFile.h"

namespace rcc {

// This dialect's own machine ID; not a registered COFF/ELF value. ASCII "R6".
constexpr uint16_t kRiscv64CoffMachine = 0x5236;

// Must match Riscv64Codegen.cpp's own kBaseAddress: the RISC-V `virt` QEMU
// machine's RAM starts here for the 64-bit CPU too, so a linked or
// single-file program loads at the same address the test harness points
// the simulated CPU's PC at. The value itself fits in 32 bits (as it does
// for riscv32); see the header comment below on why that matters for
// absolute addressing at this width.
constexpr uint64_t kRiscv64BaseAddress = 0x80000000ull;

// Local relocation kinds, not GenericLinker.h's RelocKinds - the same four
// Riscv32Assembler.h defines, plus a 64-bit-wide data-word kind:
//   Hi/Lo: upper 20 / lower 12 bits of an address, split across an adjacent
//   `lui`/`addi` pair always emitted 4 bytes apart so the linker's Hi patch
//   can find the paired Lo site at +4 - identical bit-level convention to
//   riscv32's own (see Riscv32Assembler.h), since `lui`+`addi` only ever
//   build the low 32 bits of the address either way (see loadAddress()'s
//   own header comment in Riscv64Codegen.h for how the full 64-bit value is
//   then recovered).
constexpr uint16_t kRiscv64RelocHi = 1;
constexpr uint16_t kRiscv64RelocLo = 2;
// `jal`'s 20-bit signed word-pair-aligned displacement field, same as
// riscv32's.
constexpr uint16_t kRiscv64RelocJal = 3;
// A plain 32-bit data word (`dw`); ordinary additive patch.
constexpr uint16_t kRiscv64RelocAbs32 = 4;
// A plain 64-bit data word (`dq`); ordinary additive patch over 8 bytes.
// Used for a pointer-width global/array element holding another symbol's
// address - every address in this target's universe fits in the low 32
// bits, so only the low word's relocation ever carries a nonzero patch in
// practice, but the field itself is a full 64-bit quantity.
constexpr uint16_t kRiscv64RelocAbs64 = 5;

// Two-pass assembler for exactly the narrow RISC-V dialect Riscv64Codegen.cpp
// emits, not a general-purpose RISC-V assembler - the RV64 sibling of
// Riscv32Assembler.h, sharing its same narrowed operand shapes (no
// `offset(reg)` syntax; a load/store's base register and displacement are
// two plain, comma-separated operands) and the same `lui`/`addi` label-
// resolving convention. Implements the full RV64I base instruction set:
// every RV32I mnemonic (operating on the full 64-bit register file) plus
// the RV64-only word-size instructions `lwu`/`ld`/`sd`, the W-suffixed
// 32-bit-result arithmetic/shift ops (`addiw`/`slliw`/`srliw`/`sraiw`/
// `addw`/`subw`/`sllw`/`srlw`/`sraw`), and the 64-bit M extension
// (`mul`/`mulh`/`mulhsu`/`mulhu`/`div`/`divu`/`rem`/`remu` plus the
// W-suffixed word variants `mulw`/`divw`/`divuw`/`remw`/`remuw`). Also
// adds a `dq` data directive (a plain 64-bit word, literal or symbol) to
// the `db`/`dh`/`dw` riscv32 already has, needed for this target's own
// pointer-width (8-byte) storage. `slli`/`srli`/`srai` use a 6-bit shamt
// field (registers are 64 bits wide); the W-suffixed shift-immediate
// variants keep the 5-bit shamt their 32-bit result needs, exactly like
// real RV64I. Throws std::runtime_error on any assembly error.
std::string assembleRiscv64(const std::string &asmText);

// Separate-compilation entry point: assembles `asmText` (emitted with
// `org 0`) like assembleRiscv64(), but defers undefined symbols as
// relocations instead of failing, and additionally relocates every local
// absolute-address reference against the reserved pseudo-symbol ".." (see
// GenericLinker.h) since the object has no fixed base address yet.
ObjectFile assembleRiscv64Object(const std::string &asmText);

}  // namespace rcc
