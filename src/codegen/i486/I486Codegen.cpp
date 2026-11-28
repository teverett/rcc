#include "I486Codegen.h"

#include "codegen/i386/I386Assembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

std::optional<std::string> I486Codegen::assembleInProcess(const std::string &asmText,
                                                            const std::string &format) const {
    if (format == "raw") return assembleI486(asmText);
    if (auto kind = fileFormatKindFromName(format)) {
        ObjectFile obj = assembleI486Object(asmText);
        if (windowsAbi()) obj.machineType = 0x014C;
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

std::optional<std::string> I486Codegen::debugMap(const std::string &asmText, const std::string &format,
                                                  const std::string &sourcePath) const {
    (void)format;
    return assembleI486DebugMap(asmText, sourcePath, debugSymbols_);
}

std::optional<std::string> I486Codegen::debugMapPreLink(const std::string &asmText,
                                                          const std::string &sourcePath) const {
    return assembleI486DebugMap(asmText, sourcePath, debugSymbols_, /*allowExternal=*/true);
}

}  // namespace rcc
