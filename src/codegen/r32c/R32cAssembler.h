#pragma once

#include <cstdint>
#include <utility>
#include <string>
#include <vector>

#include "fileformat/ObjectFile.h"

namespace rcc {

// This project's own non-registered COFF machine ID for the R32C; checked by
// R32cLinkerFactory.cpp against `--target=r32c`.
constexpr uint16_t kR32cCoffMachine = 0x32C4;

// Load/start address for a whole program; must match R32cCodegen.cpp's
// raw-format `kBaseAddress` so a linked program behaves like a single-file one.
constexpr uint32_t kR32cBaseAddress = 0x1000;

// The one relocation kind this dialect needs: a little-endian 32-bit
// absolute patch - every jump/call/label reference here encodes a full
// 32-bit address field (see this file's own header comment on why that
// width, unlike M32cAssembler.h's invented 16-bit field).
constexpr uint16_t kR32cRelocAbs32 = 1;

// A from-scratch two-pass assembler for the subset of R32C assembly
// R32cCodegen.cpp emits plus the rest of this dialect's mnemonic vocabulary
// (see R32cAssembler.cpp's own top-of-file comment for exactly which
// mnemonic groups are covered and which real R32C instruction families are
// deliberately left out). Its byte encoding is this project's own invention
// (not real R32C object code - see M16cAssembler.cpp's header comment for
// why this project takes that approach for the whole M16C/M32C/R32C
// lineage), but every mnemonic is a real R32C instruction used for its real
// hardware effect.
//
// R32C is Renesas's own successor to the M32C, extending that family's
// addressing rather than its register file: this dialect keeps the exact
// same eight general/address/stack/frame registers M32cAssembler.h already
// defines (`R0`-`R3`, `A0`-`A1`, `SP`, `FB`, plus the 32-bit pairs `R2R0`/
// `R3R1`) - real R32C silicon is documented as mnemonic/register-file
// compatible with its M16C/M32C ancestors - but every absolute-address
// operand (a label reference, whether a jump/call target or a `Mem`
// operand naming a global) is a full 32-bit field here, not M32C's
// 16-bit one, reflecting the wider linear address space the "R32C" name
// itself denotes. `int` stays 16 bits, matching the M16C/M32C/R32C
// family's own shared C ABI; `long`/`unsigned long` use the 32-bit pair
// registers and the `*l` (`movl`/`addl`/`mull`/`divul`/...) instruction
// family exactly as M32cCodegen.h describes for M32C.
//
// Every jump/conditional jump takes a full 32-bit absolute address operand,
// so there is no branch-range limit to work around.
//
// Parses via the shared grammar ../../../grammar/rccasm/RccAsm.g4; a register name
// is syntactically just an identifier, so this file decides per operand
// whether an identifier names a register or a label (see classifyOperand()
// in R32cAssembler.cpp).
//
// An instruction's size depends on which addressing mode each operand
// resolves to (a register operand is smaller than an immediate/absolute
// one), so this needs two passes for labels to resolve correctly.
//
// Throws std::runtime_error on any assembly error.
std::string assembleR32c(const std::string &asmText);

// The separate-compilation entry point (`--format=coff`/`elf`/`macho`/
// `aout`): assembles `asmText` (generated with `org 0`, not `kR32cBaseAddress`)
// like assembleR32c(), except an undefined symbol becomes a relocation
// instead of a hard error, and every address reference also gets a
// relocation against the reserved pseudo-symbol ".." (this object's own
// eventual linked base, resolved only by a linker).
ObjectFile assembleR32cObject(const std::string &asmText);


std::string assembleR32cDebugMap(const std::string &asmText, const std::string &sourcePath,
                       const std::vector<std::pair<std::string, bool>> &symbols,
                                     bool allowExternal = false);

}  // namespace rcc
