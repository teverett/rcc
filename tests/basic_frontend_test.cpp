#include "frontend/basic/BasicFrontend.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

#include "ir/IRBuilder.h"
#include "ir/IRValidator.h"

namespace {

// Byte-addressable, 4-byte-int widths - enough for buildIR's own
// sizeof/frame-layout math; no backend/codegen is exercised here.
rcc::TypeWidths testWidths() {
    rcc::TypeWidths w;
    w.charUnits = 1;
    w.shortUnits = 2;
    w.intUnits = 4;
    w.longUnits = 4;
    w.longLongUnits = 8;
    return w;
}

// Parses `source` as one BASIC file and returns the resulting AST
// Program - throws on a syntax or lowering error, same as the real
// driver's own frontend.parseAndMergeFile() call would.
rcc::Program parseBasic(const std::string &source) {
    auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("rcc-basic-test-" + std::to_string(suffix) + ".bas");
    {
        std::ofstream file(path);
        if (!file) throw std::runtime_error("could not create BASIC test source");
        file << source;
    }
    rcc::BasicFrontend frontend;
    rcc::Program prog;
    try {
        frontend.parseAndMergeFile(path.string(), {}, prog);
        std::filesystem::remove(path);
    } catch (...) {
        std::filesystem::remove(path);
        throw;
    }
    return prog;
}

// Lowers `source` all the way through buildIR + validateIRSyntax, same
// pipeline stage every other frontend's output goes through - throws on
// any semantic error (undefined label, redeclaration, ...).
void compileBasic(const std::string &source) {
    rcc::Program prog = parseBasic(source);
    rcc::ir::Program ir = rcc::buildIR(prog, testWidths());
    rcc::validateIRSyntax(ir);
}

void requireThrows(const std::function<void()> &action, const std::string &expectedSubstring) {
    try {
        action();
    } catch (const std::exception &e) {
        std::string what = e.what();
        if (what.find(expectedSubstring) == std::string::npos) {
            throw std::runtime_error("expected an error containing '" + expectedSubstring + "', got: " + what);
        }
        return;
    }
    throw std::runtime_error("expected an exception containing: " + expectedSubstring);
}

// LET/IF/GOTO/PRINT: the straight-line, non-control-flow-synthesizing
// part of the frontend - every statement maps onto an existing AST node
// with no extra machinery.
void testLetIfGotoPrint() {
    compileBasic(
        "10 LET A = 1\n"
        "20 IF A < 10 THEN GOTO 40\n"
        "30 PRINT \"UNREACHABLE\"\n"
        "40 PRINT A\n"
        "50 END\n");
}

// FOR/NEXT needs its own matching pass (see BasicFrontend.cpp's own
// header comment) since the grammar doesn't nest it - this exercises
// both a simple loop and one nested inside another.
void testNestedForNext() {
    compileBasic(
        "10 LET TOTAL = 0\n"
        "20 FOR I = 1 TO 3\n"
        "30 FOR J = 1 TO 3 STEP 2\n"
        "40 LET TOTAL = TOTAL + J\n"
        "50 NEXT J\n"
        "60 NEXT I\n"
        "70 END\n");
}

// GOSUB/RETURN has no existing AST primitive - this is the one
// construct synthesized from scratch (return-id stack + dispatch
// switch). Two call sites, so the dispatch switch gets more than one
// case.
void testGosubReturnDispatch() {
    compileBasic(
        "10 GOSUB 100\n"
        "20 GOSUB 100\n"
        "30 END\n"
        "100 PRINT \"IN SUBROUTINE\"\n"
        "110 RETURN\n");
}

// DIM must precede use, and a stray NEXT is rejected - both are
// frontend-level checks (ensureArray/findMatchingNext), not something
// IRBuilder itself would catch.
void testArrayAndControlFlowErrors() {
    compileBasic(
        "10 DIM A(10)\n"
        "20 LET A(5) = 42\n"
        "30 PRINT A(5)\n"
        "40 END\n");

    requireThrows([] { compileBasic("10 LET B(0) = 1\n20 END\n"); }, "used before DIM");
    requireThrows([] { compileBasic("10 NEXT I\n20 END\n"); }, "NEXT without a matching FOR");
    requireThrows([] { compileBasic("10 FOR I = 1 TO 3\n20 END\n"); }, "FOR with no matching NEXT");
}

}  // namespace

int main() {
    try {
        testLetIfGotoPrint();
        testNestedForNext();
        testGosubReturnDispatch();
        testArrayAndControlFlowErrors();
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
    std::cout << "basic_frontend_test: all tests passed\n";
    return 0;
}
