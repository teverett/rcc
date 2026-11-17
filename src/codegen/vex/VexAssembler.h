#pragma once

#include <cstdint>
#include <string>

#include "fileformat/ObjectFile.h"

namespace rcc {

// No official machine ID for this architecture exists; chosen arbitrarily, distinct from every
// other target's own kXxxCoffMachine constant.
constexpr uint16_t kVexCoffMachine = 0x5658;  // "VX"

// Where the interpreter (interpreter/vex.c in the sibling vex project) loads a ROM: the file's
// own byte 0 lands at this VM address, so every label must resolve to a value that already
// includes it for LD/ST/JMP/CAL to reach the right byte at runtime. assembleVex() (a whole-program
// compile) counts every label from here directly. assembleVexObject() instead counts from 0 ("org
// 0", exactly like the vex project's own `vasm` numbers a single file) and leaves adding this in
// to GenericLinker, via GenericLinkerConfig::baseAddress and the ".." pseudo-symbol's base-shift
// relocation - the same split M6809Assembler.h's own kM6809BaseAddress documents.
constexpr uint32_t kVexBaseAddress = 0x0100;

// Every `LDI` immediate that names a symbol (a global/function label) is the only relocatable
// reference this dialect ever has - a full 4-byte, big-endian field - so one relocation kind
// covers it: RelocKinds::patchAbs32BE.
constexpr uint16_t kVexRelocAbs32 = 1;

// A two-pass, in-process assembler for the dialect VexCodegen.cpp emits, parsed via the shared
// grammar/RccAsm.g4 (see M6809Assembler.h's own comment for the shared design this follows).
//
// Vex v1.2 is an 8-register (R0-R7), 32-bit register machine - no dedicated data stack, no
// relative-addressing flag, no stack-relative access at all (see docs/SPEC.md). `PSH`/`POP`
// share the one return stack with `CAL`/`RET` and interrupt entry/`IRET` rather than being a
// separate stack - see docs/SPEC.md section 3's own "Stack" and "Interrupts" for the balance
// risk that sharing brings, and VexCodegen.h's class comment for why this backend's own
// codegen never emits `PSH`/`POP` itself (they're assembler/asm-block-only, for hand-written
// interrupt handlers and the like). Every instruction is a one-byte opcode followed by zero or
// more one-byte register operands (`r0`-`r7`, case-insensitive, like every other identifier
// here), plus `LDI`'s own trailing 4-byte immediate:
//   RET                  (no operands)     NOP   (no operands)
//   LDI  Rd, imm32        MOV  Rd, Ra
//   LD   Rd, Ra           ST   Ra, Rb
//   DEI  Rd, Ra           DEO  Ra, Rb
//   JMP  Ra               JCN  Ra, Rb       CAL  Ra
//   ADD/SUB/MUL/DIV/AND/ORA/XOR/CEQ/CLT  Rd, Ra, Rb
//   PSH  Ra               POP  Rd
//   IRET (no operands)    SEI  (no operands)   CLI  (no operands)
// Each mnemonic takes exactly the operand count docs/SPEC.md section 2's own length table
// gives it - no more, no fewer. A register operand must spell exactly `r0`..`r7` (one bare
// identifier syllable, no sign, no label arithmetic); anything else (an out-of-range number,
// a label name, `(...)` grouping) is a parse-time error, since a register operand is never
// resolved against the label table the way `LDI`'s immediate is.
//
// Four directives, each exactly the generic `mnemonic (operand (, operand)*)?` shape the shared
// grammar already parses, so none of them needed a grammar change:
//   .org ADDR          - pad with zero bytes until the current (file-relative) offset is ADDR;
//                        offsets only move forward, matching docs/ASSEMBLER.md.
//   .byte/.short/.word v...  - emit each value big-endian, 1/2/4 bytes.
//   .zero N            - emit N zero bytes.
// `.ascii`/`.asciz`/`.include` are NOT part of this dialect: VexCodegen.cpp never emits a packed
// byte string (every char-typed Slot/array element is its own full 4-byte cell - see
// VexCodegen.h's class comment on why), and a whole-program compile never spans more than one
// generated text blob, so there is nothing for either directive to do here. A hand-written test
// needing string data spells it out with `.word` (one cell per character code), same as VexCodegen
// itself does for a string literal.
//
// Throws std::runtime_error on any assembly error.
std::string assembleVex(const std::string &asmText);

// Entry point for separate compilation (the runtime library, linked against a user program via
// `--link`). Assembles like assembleVex(), except an undefined symbol becomes a relocation
// instead of a hard error, and every `LDI` of a same-file label also gets a relocation against the
// reserved pseudo-symbol ".." (this object's eventual linked base), since labels here resolve
// relative to offset 0 until GenericLinker adds that base in - same convention as
// M6809Assembler.h's own assembleM6809Object().
ObjectFile assembleVexObject(const std::string &asmText);

}  // namespace rcc
