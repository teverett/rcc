#pragma once

#include <cstdint>
#include <string>

#include "fileformat/ObjectFile.h"

namespace rcc {

// This project's own non-registered COFF machine ID for the M32C; checked by
// M32cLinkerFactory.cpp against `--target=m32c`.
constexpr uint16_t kM32cCoffMachine = 0x32C3;

// Load/start address for a whole program; must match M32cCodegen.cpp's
// raw-format `kBaseAddress` so a linked program behaves like a single-file one.
constexpr uint32_t kM32cBaseAddress = 0x0400;

// The one relocation kind this dialect needs: a little-endian 16-bit absolute
// patch, since every jump/call here always encodes a full absolute address
// (the invented address space this dialect uses stays 16-bit even though
// real M32C silicon has a wider one - see M32cAssembler.cpp's header
// comment).
constexpr uint16_t kM32cRelocAbs16 = 1;

// A from-scratch two-pass assembler for the subset of M32C assembly
// M32cCodegen.cpp emits plus the rest of this dialect's mnemonic vocabulary
// (see M32cAssembler.cpp's own top-of-file comment for exactly which
// mnemonic groups are covered and which real M32C instruction families are
// deliberately left out). Its byte encoding is this project's own invention
// (not real M32C object code, the same approach M16cAssembler.cpp already
// takes for the M16C), but every mnemonic is a real M32C instruction used
// for its real hardware effect.
//
// M32C is Renesas's 32-bit-register extension of the M16C/R8C family: it
// keeps the M16C's 16-bit general registers R0-R3/A0-A1/SP/FB, but R0/R2 and
// R1/R3 can also be read/written as the 32-bit pairs R2R0/R3R1 - this
// dialect's registers 8 and 9 (see regNumber() in M32cAssembler.cpp). `int`
// stays 16 bits (matching M32C's own C ABI, which keeps `int` compatible
// with M16C code); `long`/`unsigned long` use the 32-bit pair registers
// directly via the `*l` (`movl`/`addl`/`mull`/`divul`/...) instruction
// family instead of M16C's manual multi-word carry-chain loop - see
// M32cCodegen.h's header comment.
//
// Every jump/conditional jump always takes a full absolute address operand
// (unlike real M32C's short PC-relative conditional jumps), so there is no
// branch-range limit to work around.
//
// Parses via the shared grammar ../../../grammar/rccasm/RccAsm.g4; a register name
// is syntactically just an identifier, so this file decides per operand
// whether an identifier names a register or a label (see classifyOperand()
// in M32cAssembler.cpp).
//
// An instruction's size depends on which addressing mode each operand
// resolves to (a register operand is smaller than an immediate/absolute
// one), so this needs two passes for labels to resolve correctly.
//
// Throws std::runtime_error on any assembly error.
std::string assembleM32c(const std::string &asmText);

// The separate-compilation entry point (`--format=coff`/`elf`/`macho`/
// `aout`): assembles `asmText` (generated with `org 0`, not `kBaseAddress`)
// like assembleM32c(), except an undefined symbol becomes a relocation
// instead of a hard error, and every address reference also gets a
// relocation against the reserved pseudo-symbol ".." (this object's own
// eventual linked base, resolved only by a linker).
ObjectFile assembleM32cObject(const std::string &asmText);

}  // namespace rcc
