#include "ir/Dominators.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "frontend/c/CFrontend.h"
#include "ir/CFG.h"
#include "ir/IRBuilder.h"

namespace {

rcc::TypeWidths testWidths() {
    rcc::TypeWidths w;
    w.charUnits = 1;
    w.shortUnits = 2;
    w.intUnits = 4;
    w.longUnits = 4;
    w.longLongUnits = 8;
    return w;
}

rcc::ir::Program buildIRFromC(const std::string &source) {
    auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("rcc-dominators-test-" + std::to_string(suffix) + ".c");
    {
        std::ofstream file(path);
        if (!file) throw std::runtime_error("could not create C test source");
        file << source;
    }
    rcc::CFrontend frontend;
    rcc::Program prog;
    try {
        frontend.parseAndMergeFile(path.string(), {}, prog);
        std::filesystem::remove(path);
    } catch (...) {
        std::filesystem::remove(path);
        throw;
    }
    return rcc::buildIR(prog, testWidths(), /*allowExternal=*/true);
}

const rcc::ir::FunctionBody &findFunctionBody(const rcc::ir::Program &prog, const std::string &name) {
    for (const auto &fb : prog.functionBodies) {
        if (fb.func->sourceName == name) return fb;
    }
    throw std::runtime_error("no function body named: " + name);
}

const rcc::ir::Stmt *findFirstWhile(const rcc::ir::Stmt *s) {
    if (!s) return nullptr;
    if (s->kind == rcc::ir::StmtKind::While) return s;
    if (s->kind == rcc::ir::StmtKind::Block) {
        for (const auto &child : s->body) {
            if (const rcc::ir::Stmt *found = findFirstWhile(child.get())) return found;
        }
    }
    return nullptr;
}

void requireTrue(bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}

// A diamond if/else: entry dominates every block, but neither branch
// dominates the other's sibling or the merge point they both reach -
// idom(merge) must be entry itself, not either branch.
void testDiamondMergeDominatedOnlyByEntry() {
    rcc::ir::Program prog = buildIRFromC(
        "int classify(int x) {\n"
        "    int r;\n"
        "    if (x) {\n"
        "        r = 1;\n"
        "    } else {\n"
        "        r = 2;\n"
        "    }\n"
        "    return r;\n"
        "}\n");
    const rcc::ir::FunctionBody &fb = findFunctionBody(prog, "classify");
    rcc::ir::CFG cfg = rcc::ir::buildCFG(fb);
    rcc::ir::Dominators doms = rcc::ir::computeDominators(cfg);

    rcc::ir::BasicBlock *thenBlock = cfg.entry->branchTarget;
    rcc::ir::BasicBlock *elseBlock = cfg.entry->next;
    rcc::ir::BasicBlock *merge = thenBlock->next;

    requireTrue(doms.dominates(cfg.entry, thenBlock), "entry must dominate the then-block");
    requireTrue(doms.dominates(cfg.entry, elseBlock), "entry must dominate the else-block");
    requireTrue(doms.dominates(cfg.entry, merge), "entry must dominate the merge block");

    requireTrue(!doms.dominates(thenBlock, merge), "then-block alone must not dominate the merge block");
    requireTrue(!doms.dominates(elseBlock, merge), "else-block alone must not dominate the merge block");
    requireTrue(!doms.dominates(thenBlock, elseBlock), "then-block must not dominate its sibling else-block");

    requireTrue(doms.idom.at(merge) == cfg.entry, "merge's immediate dominator must be entry, not either branch");
    requireTrue(doms.idom.at(thenBlock) == cfg.entry, "then-block's immediate dominator must be entry");
    requireTrue(doms.idom.at(elseBlock) == cfg.entry, "else-block's immediate dominator must be entry");
}

// A for-loop with no break: the header gates both the body and the
// after-loop block, so it immediately dominates each, but the body
// itself must not dominate the after-loop block - a path that skips
// the body (the header's false edge) reaches it directly.
void testLoopHeaderDominatesBodyAndAfterNotEachOther() {
    rcc::ir::Program prog = buildIRFromC(
        "int sumLoop() {\n"
        "    int i;\n"
        "    int sum;\n"
        "    sum = 0;\n"
        "    for (i = 0; i < 10; i = i + 1) {\n"
        "        sum = sum + i;\n"
        "    }\n"
        "    return sum;\n"
        "}\n");
    const rcc::ir::FunctionBody &fb = findFunctionBody(prog, "sumLoop");
    const rcc::ir::Stmt *whileStmt = findFirstWhile(fb.body.get());
    requireTrue(whileStmt != nullptr, "expected a desugared While for the for-loop");

    rcc::ir::CFG cfg = rcc::ir::buildCFG(fb);
    rcc::ir::Dominators doms = rcc::ir::computeDominators(cfg);

    rcc::ir::BasicBlock *header = nullptr;
    for (auto &b : cfg.blocks) {
        if (b->cond == whileStmt->cond.get()) header = b.get();
    }
    requireTrue(header != nullptr, "expected a block testing the loop's own condition");

    rcc::ir::BasicBlock *body = header->branchTarget;
    rcc::ir::BasicBlock *after = header->next;

    requireTrue(doms.dominates(cfg.entry, header), "entry must dominate the loop header");
    requireTrue(doms.dominates(header, body), "header must dominate the loop body");
    requireTrue(doms.dominates(header, after), "header must dominate the after-loop block");
    requireTrue(!doms.dominates(body, after), "loop body must not dominate the after-loop block");

    requireTrue(doms.idom.at(body) == header, "body's immediate dominator must be the header");
    requireTrue(doms.idom.at(after) == header, "after-loop block's immediate dominator must be the header");
}

}  // namespace

int main() {
    try {
        testDiamondMergeDominatedOnlyByEntry();
        testLoopHeaderDominatesBodyAndAfterNotEachOther();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
