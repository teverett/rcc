#include "CFrontend.h"

#include "ANTLRInputStream.h"
#include "BaseErrorListener.h"
#include "CLexer.h"
#include "CParser.h"
#include "CommonTokenStream.h"
#include "ast/BuildAst.h"
#include "preprocessor/Preprocessor.h"

namespace rcc {

namespace {

// ANTLR's default error listener only prints to stderr and keeps
// going; this one also lets the caller know a syntax error happened at
// all, so it can refuse to compile a tree built from broken input.
class CountingErrorListener : public antlr4::BaseErrorListener {
   public:
    void syntaxError(antlr4::Recognizer *, antlr4::Token *, size_t, size_t, const std::string &,
                      std::exception_ptr) override {
        count++;
    }
    int count = 0;
};

}  // namespace

void CFrontend::parseAndMergeFile(const std::string &srcPath,
                                   const std::map<std::string, std::string> &predefinedMacros,
                                   Program &combined) const {
    std::string source = preprocess(srcPath, predefinedMacros);

    CountingErrorListener errors;

    antlr4::ANTLRInputStream input(source);
    CLexer lexer(&input);
    lexer.removeErrorListeners();
    lexer.addErrorListener(&errors);

    antlr4::CommonTokenStream tokens(&lexer);
    CParser parser(&tokens);
    parser.removeErrorListeners();
    parser.addErrorListener(&errors);

    CParser::ProgramContext *tree = parser.program();
    if (errors.count > 0) throw std::runtime_error(srcPath + ": syntax errors, not compiling");

    Program fileProg = buildProgram(tree);
    for (auto &fn : fileProg.functions) {
        fn.sourceFile = srcPath;
        combined.functions.push_back(std::move(fn));
    }
    for (auto &g : fileProg.globals) {
        g.sourceFile = srcPath;
        combined.globals.push_back(std::move(g));
    }
    for (auto &s : fileProg.structs) {
        s.sourceFile = srcPath;
        combined.structs.push_back(std::move(s));
    }
}

}  // namespace rcc
