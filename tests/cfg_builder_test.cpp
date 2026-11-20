#include "ir/CFG.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_set>

#include "frontend/c/CFrontend.h"
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
        std::filesystem::temp_directory_path() / ("rcc-cfg-test-" + std::to_string(suffix) + ".c");
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
    if (s->kind == rcc::ir::StmtKind::If) {
        if (const rcc::ir::Stmt *found = findFirstWhile(s->thenStmt.get())) return found;
        if (const rcc::ir::Stmt *found = findFirstWhile(s->elseStmt.get())) return found;
    }
    return nullptr;
}

void collectLabelIds(const rcc::ir::Stmt *s, std::vector<int> &ids) {
    if (!s) return;
    switch (s->kind) {
        case rcc::ir::StmtKind::Block:
            for (const auto &child : s->body) collectLabelIds(child.get(), ids);
            break;
        case rcc::ir::StmtKind::If:
            collectLabelIds(s->thenStmt.get(), ids);
            collectLabelIds(s->elseStmt.get(), ids);
            break;
        case rcc::ir::StmtKind::While:
            collectLabelIds(s->loopBody.get(), ids);
            collectLabelIds(s->postStmt.get(), ids);
            break;
        case rcc::ir::StmtKind::Label:
            ids.push_back(s->labelId);
            break;
        default:
            break;
    }
}

void requireTrue(bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}

// Blocks reachable from `entry` via `next`/`branchTarget`, including `entry`
// itself - what any forward dataflow or dominator pass would walk.
std::unordered_set<const rcc::ir::BasicBlock *> reachableFrom(const rcc::ir::BasicBlock *entry) {
    std::unordered_set<const rcc::ir::BasicBlock *> seen;
    std::queue<const rcc::ir::BasicBlock *> worklist;
    worklist.push(entry);
    seen.insert(entry);
    while (!worklist.empty()) {
        const rcc::ir::BasicBlock *b = worklist.front();
        worklist.pop();
        for (const rcc::ir::BasicBlock *succ : {b->next, b->branchTarget}) {
            if (succ && seen.insert(succ).second) worklist.push(succ);
        }
    }
    return seen;
}

// An if/else where both branches are plain straight-line code: the
// branch block's two successors must be distinct blocks that both
// fall through into one common merge block carrying the join's single
// Return.
void testIfElseMergesToCommonBlock() {
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

    requireTrue(cfg.entry->term == rcc::ir::BasicBlock::Term::Branch, "entry should end in a Branch");
    rcc::ir::BasicBlock *thenBlock = cfg.entry->branchTarget;
    rcc::ir::BasicBlock *elseBlock = cfg.entry->next;
    requireTrue(thenBlock != nullptr && elseBlock != nullptr && thenBlock != elseBlock,
                "if/else must produce two distinct successor blocks");
    requireTrue(thenBlock->term == rcc::ir::BasicBlock::Term::Fallthrough &&
                    elseBlock->term == rcc::ir::BasicBlock::Term::Fallthrough,
                "both branches should fall through to a merge block");
    requireTrue(thenBlock->next == elseBlock->next, "both branches must join the same merge block");

    rcc::ir::BasicBlock *merge = thenBlock->next;
    requireTrue(merge->preds.size() == 2, "merge block should have exactly 2 predecessors");
    requireTrue(merge->term == rcc::ir::BasicBlock::Term::Return, "merge block holds the function's return");
    requireTrue(merge->next == cfg.exit, "a Return must edge to the CFG's exit block");
}

// for(;;) with both `break` and `continue`, where `continue` must still
// run the for-loop's post-expression (see tests/break_continue.c) - the
// CFG shape that guarantees that: continue jumps to the post-expression's
// own block, not straight back to the header.
void testForLoopBreakContinueEdges() {
    rcc::ir::Program prog = buildIRFromC(
        "int sumLoop() {\n"
        "    int i;\n"
        "    int sum;\n"
        "    sum = 0;\n"
        "    for (i = 0; i < 10; i = i + 1) {\n"
        "        if (i == 5) continue;\n"
        "        if (i == 8) break;\n"
        "        sum = sum + i;\n"
        "    }\n"
        "    return sum;\n"
        "}\n");
    const rcc::ir::FunctionBody &fb = findFunctionBody(prog, "sumLoop");
    const rcc::ir::Stmt *whileStmt = findFirstWhile(fb.body.get());
    requireTrue(whileStmt != nullptr, "expected a desugared While for the for-loop");
    requireTrue(whileStmt->postStmt != nullptr, "a for-loop's postStmt must survive desugaring");

    rcc::ir::CFG cfg = rcc::ir::buildCFG(fb);

    rcc::ir::BasicBlock *header = nullptr;
    for (auto &b : cfg.blocks) {
        if (b->cond == whileStmt->cond.get()) header = b.get();
    }
    requireTrue(header != nullptr, "expected a block testing the loop's own condition");
    requireTrue(header->preds.size() == 2, "header reached from loop entry and from the post-expression block");

    rcc::ir::BasicBlock *body = header->branchTarget;
    rcc::ir::BasicBlock *after = header->next;
    requireTrue(body->preds.size() == 1 && body->preds[0] == header, "loop body is entered only from the header");
    requireTrue(after->preds.size() == 2, "after-loop block reached from header's false edge and from break");

    rcc::ir::BasicBlock *post = header->preds[0] == cfg.entry ? header->preds[1] : header->preds[0];
    requireTrue(post != nullptr && post != header, "expected a distinct post-expression block preceding the header");
    requireTrue(post->next == header, "post-expression block must fall back into the header");
    requireTrue(post->preds.size() == 2,
                "post-expression block reached from the body's normal fallthrough and from continue");

    auto reachable = reachableFrom(cfg.entry);
    requireTrue(reachable.count(cfg.exit) == 1, "the function's exit must stay reachable from entry");
}

// switch's own desugaring (IRBuilder.cpp's buildSwitch) turns every case
// into a Goto-reached Label, including one after an earlier case's
// `return` - this is the scenario tests/switch_return_first_case.c
// guards at the backend level; here it's the CFG-construction
// equivalent: every such Label must still come out as a block with a
// real predecessor, not an orphaned, pred-less block.
void testSwitchDesugaredLabelsStayLinked() {
    rcc::ir::Program prog = buildIRFromC(
        "int classify(int x) {\n"
        "    switch (x) {\n"
        "        case 0: return 1;\n"
        "        case 1: return 42;\n"
        "        case 2: return 3;\n"
        "        default: return -1;\n"
        "    }\n"
        "}\n");
    const rcc::ir::FunctionBody &fb = findFunctionBody(prog, "classify");

    std::vector<int> labelIds;
    collectLabelIds(fb.body.get(), labelIds);
    requireTrue(labelIds.size() >= 4, "expected at least one Label per case");

    rcc::ir::CFG cfg = rcc::ir::buildCFG(fb);
    auto reachable = reachableFrom(cfg.entry);

    std::set<int> seenIds(labelIds.begin(), labelIds.end());
    requireTrue(seenIds.size() == labelIds.size(), "each case must desugar to its own distinct label id");

    for (auto &b : cfg.blocks) {
        if (b->term == rcc::ir::BasicBlock::Term::Return && reachable.count(b.get()) == 0 &&
            !b->preds.empty()) {
            throw std::runtime_error("a case's block is linked but not reachable from entry");
        }
    }
    requireTrue(reachable.count(cfg.exit) == 1, "at least one case's return must reach the CFG's exit");
}

}  // namespace

int main() {
    try {
        testIfElseMergesToCommonBlock();
        testForLoopBreakContinueEdges();
        testSwitchDesugaredLabelsStayLinked();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
