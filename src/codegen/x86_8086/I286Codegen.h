#pragma once

#include "codegen/x86_8086/I8086Codegen.h"

namespace rcc {

// 80286 variant of I8086Codegen, the same relationship I486Codegen has to I386Codegen:
// every instruction generate() emits is already valid 80286 real-mode machine code (the
// 80286 is instruction-set compatible with the 8086 plus a large handful of new general
// and protected-mode/descriptor-table instructions this backend never generates on its
// own), so generate() itself - and the dosAbi_ entry/exit convention it drives - is
// inherited unchanged. The only difference is which assembler an i286 source's text goes
// through: I8086Assembler.cpp's assembleI286()/assembleI286Object()/
// assembleI286DebugMap() additionally accept pusha/popa/push-of-an-immediate/2-and-3-
// operand imul/shift-by-immediate-count/bound/enter/leave/insb/insw/outsb/outsw and the
// full protected-mode group (lgdt/sgdt/lidt/sidt/lldt/sldt/ltr/str/lmsw/smsw/clts/arpl/
// verr/verw), every one of which assembleI8086() still rejects.
class I286Codegen : public I8086Codegen {
   public:
    explicit I286Codegen(bool dosAbi = false) : I8086Codegen(dosAbi) {}
    std::string name() const override { return dosAbi_ ? "i286-dos" : "i286"; }
    std::string description() const override {
        return dosAbi_ ? "Intel 80286 (MS-DOS .com)" : "Intel 80286 (real mode)";
    }
    std::optional<std::string> assembleInProcess(const std::string &asmText,
                                                  const std::string &format) const override;
    std::optional<std::string> debugMap(const std::string &asmText, const std::string &format,
                                         const std::string &sourcePath) const override;
    std::optional<std::string> debugMapPreLink(const std::string &asmText,
                                                const std::string &sourcePath) const override;
};

}  // namespace rcc
