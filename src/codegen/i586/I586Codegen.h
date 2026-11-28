#pragma once

#include "codegen/i386/I386Codegen.h"

namespace rcc {

// i586 (Pentium) variant of I386Codegen. Every instruction I386Codegen.cpp's generate() emits
// is already valid i586 machine code (the Pentium is instruction-set compatible with the 386/486
// plus a handful of new instructions this backend never generates on its own), so generate()
// itself is inherited unchanged. The only difference is which assembler an asm{} block's text
// goes through: I386Assembler.cpp's assembleI586()/assembleI586Object()/assembleI586DebugMap()
// additionally accept cpuid/rdtsc/rdmsr/wrmsr/cmpxchg8b (and every i486-only instruction too),
// which assembleI386() rejects.
class I586Codegen : public I386Codegen {
   public:
    std::string name() const override { return "i586"; }
    std::string description() const override { return "Intel Pentium (i586)"; }
    std::optional<std::string> assembleInProcess(const std::string &asmText,
                                                  const std::string &format) const override;
    std::optional<std::string> debugMap(const std::string &asmText, const std::string &format,
                                         const std::string &sourcePath) const override;
    std::optional<std::string> debugMapPreLink(const std::string &asmText,
                                                const std::string &sourcePath) const override;
};

}  // namespace rcc
