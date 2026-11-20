#pragma once

#include "frontend/Frontend.h"

namespace rcc {

// The Pascal frontend - see docs/pascal-language.md for the v1 subset
// this accepts: PascalLexer.g4/PascalParser.g4 (ANTLR) -> rcc::AST.
// Every top-level `procedure`/`function` becomes its own FunctionDecl
// (Pascal has no nested routines in this subset); the program's own
// statement part becomes a synthesized `main`. Records lower onto
// StructDecl, sets onto a single machine-word bit-vector, `var`
// parameters onto an implicit pointer parameter auto-dereferenced at
// each use - see PascalFrontend.cpp's own header comment for exactly
// what's reused as-is versus synthesized here.
class PascalFrontend : public Frontend {
   public:
    void parseAndMergeFile(const std::string &srcPath, const std::map<std::string, std::string> &predefinedMacros,
                            Program &combined) const override;
};

}  // namespace rcc
