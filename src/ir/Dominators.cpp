#include "Dominators.h"

#include <unordered_set>
#include <vector>

namespace rcc::ir {

namespace {

void postorder(BasicBlock *b, std::unordered_set<const BasicBlock *> &visited, std::vector<BasicBlock *> &order) {
    if (!b || !visited.insert(b).second) return;
    for (BasicBlock *succ : {b->next, b->branchTarget}) {
        postorder(succ, visited, order);
    }
    order.push_back(b);
}

}  // namespace

Dominators computeDominators(const CFG &cfg) {
    Dominators doms;
    doms.cfg = &cfg;
    if (!cfg.entry) return doms;

    std::unordered_set<const BasicBlock *> visited;
    std::vector<BasicBlock *> postorderBlocks;
    postorder(cfg.entry, visited, postorderBlocks);

    // Reverse postorder: a block's predecessors (other than a loop's own
    // back edge) always precede it here, so one pass already resolves most
    // blocks' idom and the fixpoint loop below converges fast.
    std::vector<BasicBlock *> rpo(postorderBlocks.rbegin(), postorderBlocks.rend());
    std::unordered_map<const BasicBlock *, int> rpoIndex;
    for (std::size_t i = 0; i < rpo.size(); ++i) rpoIndex[rpo[i]] = static_cast<int>(i);

    auto intersect = [&](BasicBlock *a, BasicBlock *b) -> BasicBlock * {
        while (a != b) {
            while (rpoIndex.at(a) > rpoIndex.at(b)) a = doms.idom.at(a);
            while (rpoIndex.at(b) > rpoIndex.at(a)) b = doms.idom.at(b);
        }
        return a;
    };

    doms.idom[cfg.entry] = cfg.entry;
    bool changed = true;
    while (changed) {
        changed = false;
        for (BasicBlock *b : rpo) {
            if (b == cfg.entry) continue;

            BasicBlock *newIdom = nullptr;
            for (BasicBlock *pred : b->preds) {
                if (doms.idom.find(pred) == doms.idom.end()) continue;  // not processed yet this pass
                newIdom = newIdom ? intersect(newIdom, pred) : pred;
            }
            if (!newIdom) continue;  // no processed predecessor yet - revisit on a later pass

            auto it = doms.idom.find(b);
            if (it == doms.idom.end() || it->second != newIdom) {
                doms.idom[b] = newIdom;
                changed = true;
            }
        }
    }

    return doms;
}

bool Dominators::dominates(const BasicBlock *a, const BasicBlock *b) const {
    if (idom.find(a) == idom.end() || idom.find(b) == idom.end()) return false;
    const BasicBlock *cur = b;
    while (true) {
        if (cur == a) return true;
        BasicBlock *next = idom.at(cur);
        if (next == cur) return false;  // reached entry without matching `a`
        cur = next;
    }
}

}  // namespace rcc::ir
