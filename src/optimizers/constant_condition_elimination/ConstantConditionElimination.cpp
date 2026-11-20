#include "optimizers/constant_condition_elimination/ConstantConditionElimination.h"

#include <iostream>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ir/CFG.h"

namespace rcc {

namespace {

void collectLabelIds(const ir::Stmt *s, std::vector<int> &ids) {
    if (!s) return;
    switch (s->kind) {
        case ir::StmtKind::Block:
            for (const auto &child : s->body) collectLabelIds(child.get(), ids);
            break;
        case ir::StmtKind::If:
            collectLabelIds(s->thenStmt.get(), ids);
            collectLabelIds(s->elseStmt.get(), ids);
            break;
        case ir::StmtKind::While:
            collectLabelIds(s->loopBody.get(), ids);
            collectLabelIds(s->postStmt.get(), ids);
            break;
        case ir::StmtKind::Label:
            ids.push_back(s->labelId);
            break;
        default:
            break;
    }
}

// Every block reachable from `entry` as if the one structural edge
// `skipFrom -> skipTo` didn't exist - the graph this pass would actually
// leave behind once that edge's branch is dropped.
std::unordered_set<const ir::BasicBlock *> reachableWithoutEdge(const ir::BasicBlock *entry,
                                                                 const ir::BasicBlock *skipFrom,
                                                                 const ir::BasicBlock *skipTo) {
    std::unordered_set<const ir::BasicBlock *> seen;
    std::queue<const ir::BasicBlock *> worklist;
    worklist.push(entry);
    seen.insert(entry);
    while (!worklist.empty()) {
        const ir::BasicBlock *b = worklist.front();
        worklist.pop();
        for (const ir::BasicBlock *succ : {b->next, b->branchTarget}) {
            if (!succ) continue;
            if (b == skipFrom && succ == skipTo) continue;
            if (seen.insert(succ).second) worklist.push(succ);
        }
    }
    return seen;
}

// True if dropping `deadBranch` - by removing the `skipFrom -> skipTo`
// structural edge leading into it - leaves nothing else able to reach any
// Label inside it via some other, still-live edge (a switch's own
// desugared `goto`; see IR.h's StmtKind comment). A null `deadBranch` (an
// absent `else`) is trivially safe.
bool safeToDrop(const ir::Stmt *deadBranch, const ir::CFG &cfg, const ir::BasicBlock *skipFrom,
                 const ir::BasicBlock *skipTo) {
    if (!deadBranch) return true;

    std::vector<int> labelIds;
    collectLabelIds(deadBranch, labelIds);
    if (labelIds.empty()) return true;

    std::unordered_set<const ir::BasicBlock *> stillReachable = reachableWithoutEdge(cfg.entry, skipFrom, skipTo);
    for (int id : labelIds) {
        auto it = cfg.labelBlocks.find(id);
        if (it != cfg.labelBlocks.end() && stillReachable.count(it->second)) return false;
    }
    return true;
}

// The Branch-terminated block testing `cond`, found by pointer identity -
// buildCFG never shares one Expr node between two If/While nodes, so this
// is unambiguous.
ir::BasicBlock *findBranchBlock(const ir::CFG &cfg, const ir::Expr *cond) {
    for (auto &b : cfg.blocks) {
        if (b->term == ir::BasicBlock::Term::Branch && b->cond == cond) return b.get();
    }
    return nullptr;
}

enum class Decision { KeepThen, KeepElse, MakeEmpty };

void recordIfDecision(const ir::Stmt *s, const ir::CFG &cfg,
                       std::unordered_map<const ir::Stmt *, Decision> &decisions) {
    if (!s->cond || s->cond->kind != ir::ExprKind::IntLit) return;
    bool takeThen = s->cond->intValue != 0;
    const ir::Stmt *deadBranch = takeThen ? s->elseStmt.get() : s->thenStmt.get();
    const ir::Stmt *liveBranch = takeThen ? s->thenStmt.get() : s->elseStmt.get();

    ir::BasicBlock *branchBlock = findBranchBlock(cfg, s->cond.get());
    if (!branchBlock) return;
    ir::BasicBlock *deadEntry = takeThen ? branchBlock->next : branchBlock->branchTarget;

    if (!safeToDrop(deadBranch, cfg, branchBlock, deadEntry)) return;

    decisions[s] = liveBranch ? (takeThen ? Decision::KeepThen : Decision::KeepElse) : Decision::MakeEmpty;
}

void recordWhileDecision(const ir::Stmt *s, const ir::CFG &cfg,
                          std::unordered_map<const ir::Stmt *, Decision> &decisions) {
    if (!s->cond || s->cond->kind != ir::ExprKind::IntLit || s->cond->intValue != 0) return;

    ir::BasicBlock *header = findBranchBlock(cfg, s->cond.get());
    if (!header) return;
    ir::BasicBlock *bodyEntry = header->branchTarget;  // never taken: the condition is always false

    // The loop body and its for-loop post-expression (if any) never run
    // even once - the header's false edge is what reaches `after` instead
    // - so only the never-taken true edge needs checking for either one.
    if (!safeToDrop(s->loopBody.get(), cfg, header, bodyEntry) ||
        !safeToDrop(s->postStmt.get(), cfg, header, bodyEntry)) {
        return;
    }

    decisions[s] = Decision::MakeEmpty;
}

void collectDecisions(const ir::Stmt *s, const ir::CFG &cfg,
                       std::unordered_map<const ir::Stmt *, Decision> &decisions) {
    if (!s) return;
    switch (s->kind) {
        case ir::StmtKind::Block:
            for (const auto &child : s->body) collectDecisions(child.get(), cfg, decisions);
            break;
        case ir::StmtKind::If:
            recordIfDecision(s, cfg, decisions);
            collectDecisions(s->thenStmt.get(), cfg, decisions);
            collectDecisions(s->elseStmt.get(), cfg, decisions);
            break;
        case ir::StmtKind::While:
            recordWhileDecision(s, cfg, decisions);
            collectDecisions(s->loopBody.get(), cfg, decisions);
            collectDecisions(s->postStmt.get(), cfg, decisions);
            break;
        default:
            break;
    }
}

ir::StmtPtr makeEmptyStmt(int line) {
    auto empty = std::make_unique<ir::Stmt>();
    empty->kind = ir::StmtKind::Empty;
    empty->line = line;
    return empty;
}

// Applies every recorded decision, recursing into whichever content
// survives at each node - a decision's own target address stays valid
// across an ancestor's move (moving a unique_ptr relocates ownership, not
// the pointee), so decisions recorded against the original, pre-mutation
// tree in collectDecisions() are still found correctly here.
void applyDecisions(ir::StmtPtr &s, const std::unordered_map<const ir::Stmt *, Decision> &decisions, bool &changed,
                     const std::string &sourceFile) {
    if (!s) return;

    auto it = decisions.find(s.get());
    if (it != decisions.end()) {
        int line = s->line;
        switch (it->second) {
            case Decision::KeepThen:
                std::cerr << sourceFile << ": constant-condition-elimination: line " << line
                          << ": condition is always true, dropped the `else`\n";
                s = std::move(s->thenStmt);
                break;
            case Decision::KeepElse:
                std::cerr << sourceFile << ": constant-condition-elimination: line " << line
                          << ": condition is always false, dropped the `then`\n";
                s = std::move(s->elseStmt);
                break;
            case Decision::MakeEmpty:
                std::cerr << sourceFile << ": constant-condition-elimination: line " << line
                          << ": condition is always false, removed the statement\n";
                s = makeEmptyStmt(line);
                break;
        }
        changed = true;
    }

    switch (s->kind) {
        case ir::StmtKind::Block:
            for (auto &child : s->body) applyDecisions(child, decisions, changed, sourceFile);
            break;
        case ir::StmtKind::If:
            applyDecisions(s->thenStmt, decisions, changed, sourceFile);
            applyDecisions(s->elseStmt, decisions, changed, sourceFile);
            break;
        case ir::StmtKind::While:
            applyDecisions(s->loopBody, decisions, changed, sourceFile);
            applyDecisions(s->postStmt, decisions, changed, sourceFile);
            break;
        default:
            break;
    }
}

}  // namespace

bool ConstantConditionElimination::run(ir::Program &prog, const std::string &sourceFile) const {
    bool changed = false;
    for (auto &fb : prog.functionBodies) {
        ir::CFG cfg = ir::buildCFG(fb);

        std::unordered_map<const ir::Stmt *, Decision> decisions;
        collectDecisions(fb.body.get(), cfg, decisions);
        if (decisions.empty()) continue;

        applyDecisions(fb.body, decisions, changed, sourceFile);
    }
    return changed;
}

}  // namespace rcc
