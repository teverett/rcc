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

// Every `LIT` immediate that names a symbol (a global/function label) is the only relocatable
// reference this dialect ever has - a full 4-byte, big-endian field - so one relocation kind
// covers it: RelocKinds::patchAbs32BE.
constexpr uint16_t kVexRelocAbs32 = 1;

// A two-pass, in-process assembler for the dialect VexCodegen.cpp emits, parsed via the shared
// grammar/RccAsm.g4 (see M6809Assembler.h's own comment for the shared design this follows).
//
// Every vex instruction is a bare one-byte opcode (`RET LIT LD ST DEI DEO JMP JCN CAL ADD SUB MUL
// DIV AND ORA XOR EQU LTH PUSH POP LDS STS`), optionally suffixed `.r` (sets the R bit, bit 5 -
// per docs/SPEC.md section 2 this is accepted, and encoded, on every mnemonic even where the
// machine itself ignores R, since the suffix is purely an encoding detail of the opcode byte, not
// a semantic choice this assembler makes). Only `LIT` takes an operand: one plain (never
// bracketed) expression - a number, a label, or a label +/- a constant - emitted as a 4-byte
// big-endian immediate. Every other mnemonic takes none; its operands live on the machine's own
// stacks, never in the instruction stream.
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
// `~label`-style relative-offset literals (docs/ASSEMBLER.md) are likewise not part of this
// dialect: VexCodegen.cpp only ever emits the absolute (non-`.r`) form of `LD`/`ST`/`JMP`/`JCN`/
// `CAL` (see VexCodegen.h's class comment), and ordinary label arithmetic (`label-otherLabel`,
// already native to the shared grammar's `expr`) covers any relative displacement a hand-written
// assembler test needs, computed the same way as every other value here (two-pass, resolved
// labels). This narrows `.r` to its other meaning - selecting the return stack for `LDS`/`STS` -
// which this assembler does encode (every mnemonic's `.r` suffix just sets the same bit).
//
// Throws std::runtime_error on any assembly error.
std::string assembleVex(const std::string &asmText);

// Entry point for separate compilation (the runtime library, linked against a user program via
// `--link`). Assembles like assembleVex(), except an undefined symbol becomes a relocation
// instead of a hard error, and every `LIT` of a same-file label also gets a relocation against the
// reserved pseudo-symbol ".." (this object's eventual linked base), since labels here resolve
// relative to offset 0 until GenericLinker adds that base in - same convention as
// M6809Assembler.h's own assembleM6809Object().
ObjectFile assembleVexObject(const std::string &asmText);

}  // namespace rcc
