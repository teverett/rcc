#include "I286Codegen.h"

#include "codegen/x86_8086/I8086Assembler.h"

namespace rcc {

std::optional<std::string> I286Codegen::assembleInProcess(const std::string &asmText,
                                                            const std::string &format) const {
    const uint16_t machineType = dosAbi_ ? kI8086DosCoffMachine : kI8086CoffMachine;
    if (format == "raw") return assembleI286(asmText, I8086OutputFormat::Bin);
    if (format == "coff") {
        return assembleI286(asmText, I8086OutputFormat::Coff, /*allowExternal=*/true, machineType);
    }
    if (format == "elf") return assembleI286(asmText, I8086OutputFormat::Elf, /*allowExternal=*/false, machineType);
    return std::nullopt;
}

std::optional<std::string> I286Codegen::debugMap(const std::string &asmText, const std::string &format,
                                                  const std::string &sourcePath) const {
    if (format == "coff") return std::nullopt;
    return assembleI286DebugMap(asmText, sourcePath, debugSymbols_);
}

std::optional<std::string> I286Codegen::debugMapPreLink(const std::string &asmText,
                                                          const std::string &sourcePath) const {
    return assembleI286DebugMap(asmText, sourcePath, debugSymbols_, /*allowExternal=*/true);
}

}  // namespace rcc
