#pragma once

#include "codegen/i386/I386Codegen.h"

namespace rcc {

// i486 variant of I386Codegen. Every instruction I386Codegen.cpp's generate() emits is already
// valid i486 machine code (the 486 is instruction-set compatible with the 386 plus a handful of
// new instructions this backend never generates on its own), so generate() itself is inherited
// unchanged. The only difference is which assembler an asm{} block's text goes through:
// I386Assembler.cpp's assembleI486()/assembleI486Object()/assembleI486DebugMap() additionally
// accept bswap/xadd/cmpxchg/invd/wbinvd/invlpg, which assembleI386() rejects.
class I486Codegen : public I386Codegen {
   public:
    std::string name() const override { return "i486"; }
    std::string description() const override { return "Intel 486"; }
    std::optional<std::string> assembleInProcess(const std::string &asmText,
                                                  const std::string &format) const override;
    std::optional<std::string> debugMap(const std::string &asmText, const std::string &format,
                                         const std::string &sourcePath) const override;
    std::optional<std::string> debugMapPreLink(const std::string &asmText,
                                                const std::string &sourcePath) const override;
};

}  // namespace rcc
