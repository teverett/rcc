#pragma once

#include <map>
#include <string>

#include "ast/AST.h"

namespace rcc {

// A source language's path from a file on disk into the shared rcc::AST
// (src/ast/AST.h). Everything past this point - IRBuilder, the
// optimizers, and every target's Codegen - is language-agnostic and
// shared by every Frontend implementation.
class Frontend {
   public:
    virtual ~Frontend() = default;

    // Parses one source file and appends its functions/globals/structs
    // onto `combined`, tagging each with `srcPath` (see FunctionDecl/
    // GlobalVar/StructDecl's own sourceFile field in AST.h).
    // `predefinedMacros` is only meaningful to a Frontend that runs a
    // C-style preprocessor; one that doesn't can ignore it. Throws
    // std::runtime_error on a syntax error.
    virtual void parseAndMergeFile(const std::string &srcPath,
                                    const std::map<std::string, std::string> &predefinedMacros,
                                    Program &combined) const = 0;
};

}  // namespace rcc
