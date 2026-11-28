#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "fileformat/ObjectFile.h"

namespace rcc {

// A from-scratch, in-process two-pass assembler for the narrow 16-bit real-mode x86
// dialect I8086Codegen.cpp emits - not general-purpose (no 32-bit operands, only
// `[label]`/`[bx]`/`[label+bx]` addressing). A mnemonic selects a real x86 instruction
// and each operand is a distinct register/memory/immediate argument.
//
// Every instruction's size is fixed by its syntactic form alone (e.g. `jmp` is always
// near/rel16, `jCC` always short/rel8), never by an operand's resolved value: pass 1
// walks every line to record label addresses, pass 2 re-walks and emits real bytes. A
// jump target out of short-jump range is a pass-2 error, not silent miscompilation.
//
// Throws std::runtime_error on any assembly error.
enum class I8086OutputFormat { Bin, Coff, Elf };

// Custom, non-registered COFF machine ID (real 8086/DOS tooling used OMF, not COFF).
constexpr uint16_t kI8086CoffMachine = 0x0086;

// `--target=8086-dos`'s own machine ID: same instruction encoding as plain `8086`, but
// a genuinely different entry/exit convention (see I8086LinkerFactory.cpp's
// `makeI8086DosLinker()`), so it gets its own reserved ID rather than sharing
// `kI8086CoffMachine` - the two conventions are link-incompatible, and GenericLinker's
// own machine-type check (GenericLinker.cpp) is what catches an attempt to mix them.
constexpr uint16_t kI8086DosCoffMachine = 0x0D86;

// `allowExternal` is separate compilation's switch (--format=coff): when true, a
// label this text doesn't define becomes a COFF relocation instead of a hard error,
// and ORG is whatever the text says (an object's `ORG 0`, not the whole-program
// `kI8086BaseAddress`/`kI8086DosBaseAddress` - placement is the linker's job).
// `machineType` is stamped into the resulting object/ELF unchanged (see
// `ObjectFile::machineType`); it only matters for `Coff`/`Elf`, never `Bin`.
std::string assembleI8086(const std::string &asmText, I8086OutputFormat format, bool allowExternal = false,
                           uint16_t machineType = kI8086CoffMachine);

// The fixed load address every whole-program 8086 compile starts from - a bare flat
// binary with no boot-sector/BIOS convention of any kind (that's out of scope for this
// project; see I8086LinkerFactory.cpp's own entry-stub comment).
constexpr uint32_t kI8086BaseAddress = 0x1000;

// `--target=8086-dos`'s own load address: DOS loads a `.com` program at offset `0x100`
// within the one segment it hands the process (CS=DS=ES=SS, all already set up by DOS
// itself) - see `makeI8086DosLinker()`'s own entry-stub comment.
constexpr uint32_t kI8086DosBaseAddress = 0x100;

// Relocation `type` values an 8086 object's ObjRelocation entries use: both are plain
// additive/relative 16-bit patches (GenericLinker's patchAbs16/patchRel16).
constexpr uint16_t kI8086RelocAbs16 = 1;
constexpr uint16_t kI8086RelocRel16 = 2;

// Like assembleI8086(asmText, Coff, true, machineType), but returns the format-neutral
// ObjectFile directly instead of serialized COFF bytes - avoids a serialize-then-
// deserialize round trip when the linker assembles its own synthesized entry object.
ObjectFile assembleI8086Object(const std::string &asmText, uint16_t machineType = kI8086CoffMachine);

// Assembles like assembleI8086(asmText, Bin), then formats a plain-text FILE/SYM/LINE
// debug map against the result (see Backend::debugMap()'s own comment in
// src/codegen/Codegen.h) - `symbols` is the exact (label, isFunc) list I8086Codegen's own
// debugSymbols_ collects.
std::string assembleI8086DebugMap(const std::string &asmText, const std::string &sourcePath,
                                   const std::vector<std::pair<std::string, bool>> &symbols,
                                     bool allowExternal = false);

// Same narrow dialect as assembleI8086()/assembleI8086Object()/assembleI8086DebugMap(), plus
// the instructions the 80286 adds over the plain 8086: `pusha`/`popa`, `push`/`pushb` of an
// immediate, 2- and 3-operand `imul` with an immediate, a shift/rotate (`shl`/`shr`/`sar`/
// `rcl`/`rcr`) by an immediate count other than 1, `bound`, `enter`/`leave`, `insb`/`insw`/
// `outsb`/`outsw`, and the protected-mode/descriptor-table group (`lgdt`/`sgdt`/`lidt`/`sidt`/
// `lldt`/`sldt`/`ltr`/`str`/`lmsw`/`smsw`/`clts`/`arpl`/`verr`/`verw`). assembleI8086()'s own
// three entry points still reject every one of those with a clear "needs --target=i286" error,
// so a plain 8086/8088/8086-dos program can never silently depend on one.
std::string assembleI286(const std::string &asmText, I8086OutputFormat format = I8086OutputFormat::Bin,
                          bool allowExternal = false, uint16_t machineType = kI8086CoffMachine);
ObjectFile assembleI286Object(const std::string &asmText, uint16_t machineType = kI8086CoffMachine);
std::string assembleI286DebugMap(const std::string &asmText, const std::string &sourcePath,
                                  const std::vector<std::pair<std::string, bool>> &symbols,
                                  bool allowExternal = false);

}  // namespace rcc
