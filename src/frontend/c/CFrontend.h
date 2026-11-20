#pragma once

#include "frontend/Frontend.h"

namespace rcc {

// The C-subset frontend: Preprocessor -> CLexer/CParser (ANTLR) ->
// BuildAst.cpp's walk into rcc::AST.
class CFrontend : public Frontend {
   public:
    void parseAndMergeFile(const std::string &srcPath, const std::map<std::string, std::string> &predefinedMacros,
                            Program &combined) const override;
};

}  // namespace rcc
