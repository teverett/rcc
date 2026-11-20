#pragma once

#include "frontend/Frontend.h"

namespace rcc {

// The BASIC frontend - see docs/basic-language.md for the dialect this
// accepts: BasicLexer.g4/BasicParser.g4 (ANTLR) -> a single synthesized
// `main` FunctionDecl in rcc::AST. Every line number becomes a Label
// (StmtKind::Label) GOTO/GOSUB can target; GOSUB/RETURN itself lowers onto
// a synthesized return-address stack (two GlobalVars) plus a single
// dispatch Switch at the end of `main` - see BasicFrontend.cpp's own
// header comment - so, like the rest of this frontend, it needs no new
// AST/IR node and no codegen changes.
class BasicFrontend : public Frontend {
   public:
    void parseAndMergeFile(const std::string &srcPath, const std::map<std::string, std::string> &predefinedMacros,
                            Program &combined) const override;
};

}  // namespace rcc
