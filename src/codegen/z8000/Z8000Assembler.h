#pragma once

#include <cstdint>
#include <string>

#include "fileformat/ObjectFile.h"

namespace rcc {

// This target's own custom COFF/object-format machine ID (no real one exists for this dialect).
constexpr uint16_t kZ8000CoffMachine = 0x8000;

// Load/entry address for a linked whole program; matches Z8000Codegen.cpp's raw-format
// kBaseAddress.
constexpr uint32_t kZ8000BaseAddress = 0x1000;

// The Z8000 is a big-endian machine (unlike most of this project's other 16-bit targets), so
// every multi-byte field below - instruction words, extension words, and this one relocation
// kind - is big-endian. One relocation kind suffices: every label reference this backend emits
// (a `ld`/`add`/... immediate operand, a `[Rbase+N]` indexed displacement, or a direct-address
// extension word for `ld`/`lda`/`call`/`jp`) is a plain, non-PC-relative absolute 16-bit value -
// unlike the PDP-11 backend, this one never emits PC-relative addressing, so there's no second
// relocation kind to go with it.
constexpr uint16_t kZ8000RelocAbs16BE = 1;

// A from-scratch, in-process two-pass assembler for exactly the Z8000 dialect Z8000Codegen.cpp
// emits (not a general-purpose Z8000 assembler, though the opcode table itself - cross-checked
// against MAME's own z8000 CPU core decode tables - covers the real Z8000's full register-to-
// register/register-to-immediate/indexed/direct-address instruction set, not just the forms the
// codegen happens to use). Parses via the shared grammar/rccasm/RccAsm.g4 grammar.
//
// Addressing modes, spelled using that shared grammar's existing syntax (see
// Pdp11Assembler.h's own header comment for the general approach this follows):
//   r0..r15         register direct (word)
//   rl0..rl7/rh0..rh7  register direct (byte - low/high half of r0..r7; r8..r15 have no byte half)
//   label           direct address (absolute, never PC-relative) - reads/writes the value
//                   stored there, or (as `lda`'s source) means "that label's own address"
//   [Rn+N]/[Rn-N]   indexed (base register Rn, literal displacement N) - frame-pointer-relative
//                   Local/Param addressing (base `r13`), or a pointer dereference at a
//                   compile-time-constant offset (base `r2`)
//   a bare number   immediate
//
// Every conditional-branch/jump/call mnemonic (`beq`/`bne`/`blt`/.../`br`/`call`) targets a
// direct address (2 words: opcode + absolute target), not the Z8000's own short PC-relative
// `jr`/`calr` - so none of them has a limited range the way the PDP-11 backend's branches do.
//
// Throws std::runtime_error on any assembly error.
std::string assembleZ8000(const std::string &asmText);

// Separate-compilation entry point: assembles with `org 0`, defers undefined symbols as
// relocations. Returns a format-neutral ObjectFile; never writes real object bytes.
ObjectFile assembleZ8000Object(const std::string &asmText);

}  // namespace rcc
