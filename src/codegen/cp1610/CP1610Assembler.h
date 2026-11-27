#pragma once

#include <cstdint>
#include <utility>
#include <string>
#include <vector>

#include "fileformat/ObjectFile.h"

namespace rcc {

// This target's own custom COFF/object-format machine ID (no real one exists for this dialect).
constexpr uint16_t kCP1610CoffMachine = 0x1610;

// Load/entry word address for a linked whole program; matches CP1610Codegen.cpp's raw-format
// kBaseAddress. In WORD units (see this file's own header comment on why) - the word-addressed
// counterpart of every other backend's byte-based kXxxBaseAddress.
constexpr uint32_t kCP1610BaseAddress = 0x1000;

// `kCP1610RelocAbs16`: a plain, non-PC-relative word-address reference (this backend never emits
// PC-relative addressing - every branch target and direct-mode operand is a full absolute word
// address, same convention as the Z8000 backend's own single relocation kind).
constexpr uint16_t kCP1610RelocAbs16 = 1;

// A from-scratch, in-process two-pass assembler for exactly the CP1610 dialect
// CP1610Codegen.cpp emits (opcode table cross-checked against MAME's own cp1610.cpp/1610dasm.cpp
// decode tables). Parses via the shared grammar/rccasm/RccAsm.g4 grammar.
//
// Word-addressed, unlike every other backend here: `org`/a label's own value/`dw`'s own address
// are all in WORD units (one CP1610 memory location, holding one full 16-bit value), not bytes -
// this assembler's own internal `offset_`/`labels_` track word positions directly, with no ×2
// conversion needed anywhere in CP1610Codegen.cpp (its own frameOffset/array-index arithmetic is
// already in the same native-word units typeWidths() reports). The byte-packed `.textBytes`
// vector a real file format needs still exists underneath (2 bytes per word, this dialect's own
// choice of byte order - nothing in the real Intellivision's own byte-packed ROM "decle" format
// is reproduced here, since that's purely a cartridge-hardware detail this backend's flat `raw`
// image format has no reason to care about); `assembleCP1610Object()` is the one place that
// multiplies a resolved word address by 2 to express it in the byte-offset terms
// `GenericLinkerConfig::addressUnitBytes` (set to 2 by CP1610LinkerFactory.cpp) expects.
//
// Addressing modes, spelled using the shared grammar's existing syntax:
//   r0..r7          register direct
//   label           direct address (absolute word address, never PC-relative)
//   [r1]/[r2]/[r3]  indirect, no side effect - the only addressing mode with no side effect,
//                   since this real CPU's `@R4`/`@R5`/`@R6` are *always* auto-incrementing/
//                   decrementing (see below); never spelled with a displacement, since real
//                   CP1610 hardware has no indexed/displacement addressing mode at all
//   [r4]/[r5]       indirect, auto-increments r4/r5 by 1 *after* the access
//   [r6]            indirect, auto-decrements r6 by 1 *before* a store (`mvo`/`add`/`sub`/
//                   `cmp`/`and`/`xor`) or auto-increments it by 1 *after* a load (`mvi`) -
//                   real hardware's own PSHR/PULR convention (see `pshr`/`pulr` below)
//   a bare number   immediate
//
// Every two-operand instruction except `mvo` puts the addressable operand first and the
// register second (`add label,r0`/`add [r2],r0`/`add 5,r0` - real CP1610 syntax, "ADD src,Rd"
// meaning Rd += src); `mvo` reverses that (`mvo r0,label` - real syntax "MVO Rs,dst", since it's
// the one instruction that stores *to* memory rather than loading *from* it). `pshr Rs`/`pulr Rd`
// are this dialect's own names for the real CP1610's `MVO@ Rs,(R6)-`/`MVI@ (R6)+,Rd` idiom (a
// `pulr r7`/"PULR PC" is this CPU's real, documented return instruction - there's no dedicated
// `ret`).
//
// Deliberately not implemented, and not meaningfully reachable from this backend's own codegen:
// `SDBD` (the real CP1610's "next operand is two packed bytes, not one word" prefix - meaningless
// without a byte-addressable memory model, which this backend's own word-addressed design never
// has) and the real CP1610's alternate "reverse-direction" branch opcodes (functionally
// redundant with the ones this dialect does implement - see CP1610Codegen.h's own header
// comment). `MVOI` (storing to an immediate "address") is implemented for completeness even
// though real hardware's own behaviour there is undocumented/degenerate and this backend's
// codegen never emits it.
//
// Throws std::runtime_error on any assembly error.
std::string assembleCP1610(const std::string &asmText);

// Separate-compilation entry point: assembles with `org 0`, defers undefined symbols as
// relocations. Returns a format-neutral ObjectFile; never writes real object bytes.
ObjectFile assembleCP1610Object(const std::string &asmText);


std::string assembleCP1610DebugMap(const std::string &asmText, const std::string &sourcePath,
                       const std::vector<std::pair<std::string, bool>> &symbols,
                                     bool allowExternal = false);

}  // namespace rcc
