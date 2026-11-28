#include "I586Codegen.h"

#include "codegen/i386/I386Assembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

std::optional<std::string> I586Codegen::assembleInProcess(const std::string &asmText,
                                                            const std::string &format) const {
    if (format == "raw") return assembleI586(asmText);
    if (auto kind = fileFormatKindFromName(format)) {
        ObjectFile obj = assembleI586Object(asmText);
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

std::optional<std::string> I586Codegen::debugMap(const std::string &asmText, const std::string &format,
                                                  const std::string &sourcePath) const {
    (void)format;
    return assembleI586DebugMap(asmText, sourcePath, debugSymbols_);
}

std::optional<std::string> I586Codegen::debugMapPreLink(const std::string &asmText,
                                                          const std::string &sourcePath) const {
    return assembleI586DebugMap(asmText, sourcePath, debugSymbols_, /*allowExternal=*/true);
}

}  // namespace rcc
