#include "frontend/pascal/PascalFrontend.h"

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

// Parses `source` as one Pascal file and returns the resulting AST
// Program - throws on a syntax or lowering error, same as the real
// driver's own frontend.parseAndMergeFile() call would.
rcc::Program parsePascal(const std::string &source) {
    auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("rcc-pascal-test-" + std::to_string(suffix) + ".pas");
    {
        std::ofstream file(path);
        if (!file) throw std::runtime_error("could not create Pascal test source");
        file << source;
    }
    rcc::PascalFrontend frontend;
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
// any semantic error (undefined name, redeclaration, ...).
void compilePascal(const std::string &source) {
    rcc::Program prog = parsePascal(source);
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

// var/const/assignment/if/while - the straight-line part of the
// frontend, every statement mapping onto an existing AST node with no
// extra machinery.
void testVarConstAssignIfWhile() {
    compilePascal(
        "program P;\n"
        "const Limit = 5;\n"
        "var i, total: integer;\n"
        "begin\n"
        "  total := 0;\n"
        "  i := 1;\n"
        "  while i <= Limit do begin\n"
        "    if i = 3 then total := total + 10\n"
        "    else total := total + i;\n"
        "    i := i + 1;\n"
        "  end;\n"
        "  writeln(total);\n"
        "end.\n");
}

// FOR (both directions) and REPEAT/UNTIL - FOR desugars onto an
// existing While+postStmt (see lowerFor's own comment); REPEAT onto
// While(1)+If+Break (see lowerRepeat's own comment) - neither is a
// real AST primitive of its own.
void testForBothDirectionsAndRepeat() {
    compilePascal(
        "program P;\n"
        "var i, j, n: integer;\n"
        "begin\n"
        "  n := 0;\n"
        "  for i := 1 to 5 do n := n + i;\n"
        "  for j := 5 downto 1 do n := n - j;\n"
        "  i := 0;\n"
        "  repeat i := i + 1 until i >= 3;\n"
        "  writeln(n);\n"
        "end.\n");
}

// Records (plain field access and pointer-to-record field access via
// '@'/'^') and a multi-dimensional array - buildMemberExpr's two most
// common shapes, plus fillArrayIndices' multi-subscript path.
void testRecordsPointersAndArrays() {
    compilePascal(
        "program P;\n"
        "type\n"
        "  Point = record x, y: integer; end;\n"
        "  PPoint = ^Point;\n"
        "var\n"
        "  p: Point;\n"
        "  pp: PPoint;\n"
        "  grid: array[0..2, 0..2] of integer;\n"
        "begin\n"
        "  p.x := 1;\n"
        "  p.y := 2;\n"
        "  pp := @p;\n"
        "  grid[1, 1] := pp^.x + pp^.y;\n"
        "  writeln(grid[1, 1]);\n"
        "end.\n");
}

// A set literal assigned to a declared set variable (using that
// variable's own low bound - see lowerAssign's special case) plus
// 'in' against both a set variable and an inline set literal.
void testSetsAndInOperator() {
    compilePascal(
        "program P;\n"
        "type Digits = set of 0..9;\n"
        "var s: Digits;\n"
        "begin\n"
        "  s := [1, 3, 5..7];\n"
        "  if 5 in s then writeln('yes');\n"
        "  if 2 in [1, 2, 3] then writeln('also yes');\n"
        "end.\n");
}

// A 'var' parameter (implicit pointer + auto-deref at every use - see
// readPlainName/assignPlainName's isVarParamName branches) and a
// function result assigned through the synthesized "__result" local.
void testVarParamAndFunctionResult() {
    compilePascal(
        "program P;\n"
        "var n: integer;\n"
        "procedure AddOne(var x: integer);\n"
        "begin\n"
        "  x := x + 1;\n"
        "end;\n"
        "function Square(k: integer): integer;\n"
        "begin\n"
        "  Square := k * k;\n"
        "end;\n"
        "begin\n"
        "  n := 10;\n"
        "  AddOne(n);\n"
        "  n := Square(n);\n"
        "  writeln(n);\n"
        "end.\n");
}

// A function calling another function/procedure declared later in the
// same file, and a function calling itself - both resolve because
// every top-level signature is registered before any body is lowered
// (see lowerProgram's own two-pass comment), not because of textual
// order.
void testForwardReferenceAndRecursion() {
    compilePascal(
        "program P;\n"
        "function Fib(k: integer): integer;\n"
        "begin\n"
        "  if k <= 1 then Fib := k\n"
        "  else Fib := Fib(k - 1) + Fib(k - 2);\n"
        "end;\n"
        "function Caller(k: integer): integer;\n"
        "begin\n"
        "  Caller := LaterFn(k) + 1;\n"
        "end;\n"
        "function LaterFn(k: integer): integer;\n"
        "begin\n"
        "  LaterFn := k * 2;\n"
        "end;\n"
        "begin\n"
        "  writeln(Fib(6));\n"
        "  writeln(Caller(3));\n"
        "end.\n");
}

// CASE with a multi-constant branch (1, 2: ...) must not re-lower the
// shared body twice - it lowers onto one real case plus empty,
// fallthrough-only cases for every earlier constant (see lowerCase's
// own comment) - and an ELSE branch.
void testCaseWithMultiConstantBranchAndElse() {
    compilePascal(
        "program P;\n"
        "var n: integer;\n"
        "begin\n"
        "  n := 2;\n"
        "  case n of\n"
        "    1, 2: writeln('one or two');\n"
        "    3: writeln('three');\n"
        "  else\n"
        "    writeln('other');\n"
        "  end;\n"
        "end.\n");
}

// Array dimension-count mismatch, an unknown identifier, and a nested
// procedure declaration are all frontend-level checks (fillArrayIndices/
// lookupVarType/processDeclPart), not something IRBuilder itself would
// catch.
void testFrontendLevelErrors() {
    requireThrows(
        [] {
            compilePascal(
                "program P;\n"
                "var grid: array[0..2, 0..2] of integer;\n"
                "begin\n"
                "  grid[1] := 1;\n"
                "end.\n");
        },
        "dimension(s) but was indexed with");

    requireThrows(
        [] {
            compilePascal(
                "program P;\n"
                "begin\n"
                "  writeln(NoSuchVariable);\n"
                "end.\n");
        },
        "unknown identifier");

    requireThrows(
        [] {
            compilePascal(
                "program P;\n"
                "procedure Outer;\n"
                "  procedure Inner;\n"
                "  begin\n"
                "  end;\n"
                "begin\n"
                "end;\n"
                "begin\n"
                "end.\n");
        },
        "nested procedure declarations aren't supported");
}

}  // namespace

int main() {
    try {
        testVarConstAssignIfWhile();
        testForBothDirectionsAndRepeat();
        testRecordsPointersAndArrays();
        testSetsAndInOperator();
        testVarParamAndFunctionResult();
        testForwardReferenceAndRecursion();
        testCaseWithMultiConstantBranchAndElse();
        testFrontendLevelErrors();
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
    std::cout << "pascal_frontend_test: all tests passed\n";
    return 0;
}
