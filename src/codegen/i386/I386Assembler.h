#pragma once

#include <cstdint>
#include <utility>
#include <string>
#include <vector>

#include "fileformat/ObjectFile.h"

namespace rcc {

// Custom (unregistered) COFF machine ID for this project's i386 dialect.
constexpr uint16_t kI386CoffMachine = 0x3386;

// Multiboot load address; shared with the raw-format whole-program entry sequence
// (I386Codegen.cpp's kLoadAddr) and the linker's synthesized entry object so a linked
// program loads identically to a single-file one.
constexpr uint32_t kI386LoadAddr = 0x100000;

// Plain contiguous 32-bit relocation fields (no hi/lo split), patched via
// GenericLinker.h's RelocKinds::patchAbs32/patchRel32.
constexpr uint16_t kI386RelocAbs32 = 1;  // [label] direct memory operand
constexpr uint16_t kI386RelocRel32 = 2;  // call/jmp label (rel32)

// From-scratch two-pass assembler for the narrow i386 dialect I386Codegen.cpp emits, not a
// general-purpose x86 assembler. Only addressing modes: [label], [ebx], [label+ebx]. One
// flat image, `org` called exactly once. Throws std::runtime_error on assembly error.
std::string assembleI386(const std::string &asmText);

// Separate-compilation entry point: like assembleI386(), but asmText is generated with
// `org 0`, an undefined symbol becomes a relocation instead of an error, and every
// absolute-address reference also gets a relocation against the reserved pseudo-symbol
// ".." (labels resolve relative to this object's own org 0 until the linker adds its base).
ObjectFile assembleI386Object(const std::string &asmText);


std::string assembleI386DebugMap(const std::string &asmText, const std::string &sourcePath,
                       const std::vector<std::pair<std::string, bool>> &symbols,
                                     bool allowExternal = false);

// Same narrow dialect as assembleI386()/assembleI386Object()/assembleI386DebugMap(), plus the
// six i486-only instructions I386Codegen.cpp never emits itself but an asm{} block may use:
// bswap, xadd, cmpxchg, invd, wbinvd, invlpg. assembleI386()'s own three entry points still
// reject those mnemonics, so a program built with --target=i386 that uses one fails to
// assemble exactly as a real i386 chip would reject the opcode.
std::string assembleI486(const std::string &asmText);
ObjectFile assembleI486Object(const std::string &asmText);
std::string assembleI486DebugMap(const std::string &asmText, const std::string &sourcePath,
                                  const std::vector<std::pair<std::string, bool>> &symbols,
                                  bool allowExternal = false);

// Same dialect as assembleI486()/assembleI486Object()/assembleI486DebugMap() (so every
// i486-only instruction still assembles), plus the five i586-only instructions I386Codegen.cpp
// never emits itself but an asm{} block may use: cpuid, rdtsc, rdmsr, wrmsr, cmpxchg8b.
// assembleI386()/assembleI486()'s own entry points still reject those mnemonics, so a program
// built with --target=i386 or --target=i486 that uses one fails to assemble exactly as a real
// 386/486 chip would reject the opcode.
std::string assembleI586(const std::string &asmText);
ObjectFile assembleI586Object(const std::string &asmText);
std::string assembleI586DebugMap(const std::string &asmText, const std::string &sourcePath,
                                  const std::vector<std::pair<std::string, bool>> &symbols,
                                  bool allowExternal = false);

}  // namespace rcc
