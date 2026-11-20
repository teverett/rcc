#include "CFG.h"

#include <unordered_map>

namespace rcc::ir {

namespace {

struct LoopTargets {
    BasicBlock *breakTarget;
    BasicBlock *continueTarget;
};

// Pre-creates one BasicBlock per Label in `s`'s subtree, so a Goto reached
// before its target Label during the linear walk can still link to it.
// Only Block/If/While nest further Stmts; every other kind is a leaf here.
void collectLabels(Stmt *s, std::unordered_map<int, BasicBlock *> &labelBlocks, CFG &cfg) {
    if (!s) return;
    switch (s->kind) {
        case StmtKind::Block:
            for (auto &child : s->body) collectLabels(child.get(), labelBlocks, cfg);
            break;
        case StmtKind::If:
            collectLabels(s->thenStmt.get(), labelBlocks, cfg);
            collectLabels(s->elseStmt.get(), labelBlocks, cfg);
            break;
        case StmtKind::While:
            collectLabels(s->loopBody.get(), labelBlocks, cfg);
            collectLabels(s->postStmt.get(), labelBlocks, cfg);
            break;
        case StmtKind::Label: {
            cfg.blocks.push_back(std::make_unique<BasicBlock>());
            BasicBlock *block = cfg.blocks.back().get();
            block->id = static_cast<int>(cfg.blocks.size()) - 1;
            labelBlocks[s->labelId] = block;
            break;
        }
        default:
            break;
    }
}

class Builder {
   public:
    Builder(CFG &cfg, const std::unordered_map<int, BasicBlock *> &labelBlocks)
        : cfg_(cfg), labelBlocks_(labelBlocks) {}

    void run(Stmt *body) {
        cur_ = cfg_.entry;
        walk(body);
        closeFallthroughTo(cur_, cfg_.exit);
    }

   private:
    BasicBlock *newBlock() {
        cfg_.blocks.push_back(std::make_unique<BasicBlock>());
        BasicBlock *block = cfg_.blocks.back().get();
        block->id = static_cast<int>(cfg_.blocks.size()) - 1;
        return block;
    }

    // Links `from` to `to` as an ordinary fallthrough edge, unless `from`
    // was already given a terminator earlier in this walk (a nested
    // Return/Break/Continue/Goto already closed it, in which case `from` is
    // the fresh, still-open orphan block created right after - this call
    // always targets that one, never the already-closed block itself).
    void closeFallthroughTo(BasicBlock *from, BasicBlock *to) {
        if (from->term != BasicBlock::Term::Unreachable) return;
        from->term = BasicBlock::Term::Fallthrough;
        from->next = to;
        to->preds.push_back(from);
    }

    void walk(Stmt *s) {
        switch (s->kind) {
            case StmtKind::Block:
                for (auto &child : s->body) walk(child.get());
                break;

            case StmtKind::LocalDecl:
            case StmtKind::ExprStmt:
            case StmtKind::Asm:
            case StmtKind::Empty:
                cur_->stmts.push_back(s);
                break;

            case StmtKind::If: {
                BasicBlock *branch = cur_;
                branch->term = BasicBlock::Term::Branch;
                branch->cond = s->cond.get();

                BasicBlock *thenBlock = newBlock();
                branch->branchTarget = thenBlock;
                thenBlock->preds.push_back(branch);

                BasicBlock *elseBlock = s->elseStmt ? newBlock() : nullptr;
                BasicBlock *merge = newBlock();

                if (elseBlock) {
                    branch->next = elseBlock;
                    elseBlock->preds.push_back(branch);
                } else {
                    branch->next = merge;
                    merge->preds.push_back(branch);
                }

                cur_ = thenBlock;
                walk(s->thenStmt.get());
                closeFallthroughTo(cur_, merge);

                if (elseBlock) {
                    cur_ = elseBlock;
                    walk(s->elseStmt.get());
                    closeFallthroughTo(cur_, merge);
                }

                cur_ = merge;
                break;
            }

            case StmtKind::While: {
                BasicBlock *header = newBlock();
                closeFallthroughTo(cur_, header);

                header->term = BasicBlock::Term::Branch;
                header->cond = s->cond.get();

                BasicBlock *body = newBlock();
                header->branchTarget = body;
                body->preds.push_back(header);

                BasicBlock *after = newBlock();
                header->next = after;
                after->preds.push_back(header);

                // A for-loop's post-statement runs on `continue`, before
                // re-testing the header's condition; a real `while` has no
                // postStmt, so continue re-tests the header directly.
                BasicBlock *continueTarget = s->postStmt ? newBlock() : header;

                loopStack_.push_back({after, continueTarget});
                cur_ = body;
                walk(s->loopBody.get());
                closeFallthroughTo(cur_, continueTarget);

                if (s->postStmt) {
                    cur_ = continueTarget;
                    walk(s->postStmt.get());
                    closeFallthroughTo(cur_, header);
                }

                loopStack_.pop_back();
                cur_ = after;
                break;
            }

            case StmtKind::Break:
                cur_->term = BasicBlock::Term::Jump;
                cur_->next = loopStack_.back().breakTarget;
                loopStack_.back().breakTarget->preds.push_back(cur_);
                cur_ = newBlock();
                break;

            case StmtKind::Continue:
                cur_->term = BasicBlock::Term::Jump;
                cur_->next = loopStack_.back().continueTarget;
                loopStack_.back().continueTarget->preds.push_back(cur_);
                cur_ = newBlock();
                break;

            case StmtKind::Return:
                cur_->term = BasicBlock::Term::Return;
                cur_->next = cfg_.exit;
                cfg_.exit->preds.push_back(cur_);
                cur_ = newBlock();
                break;

            case StmtKind::Goto: {
                BasicBlock *target = labelBlocks_.at(s->labelId);
                cur_->term = BasicBlock::Term::Jump;
                cur_->next = target;
                target->preds.push_back(cur_);
                cur_ = newBlock();
                break;
            }

            case StmtKind::Label: {
                BasicBlock *target = labelBlocks_.at(s->labelId);
                closeFallthroughTo(cur_, target);
                cur_ = target;
                break;
            }
        }
    }

    CFG &cfg_;
    const std::unordered_map<int, BasicBlock *> &labelBlocks_;
    BasicBlock *cur_ = nullptr;
    std::vector<LoopTargets> loopStack_;
};

}  // namespace

CFG buildCFG(const FunctionBody &fb) {
    CFG cfg;
    cfg.func = fb.func;

    cfg.blocks.push_back(std::make_unique<BasicBlock>());
    cfg.exit = cfg.blocks.back().get();
    cfg.exit->id = 0;

    cfg.blocks.push_back(std::make_unique<BasicBlock>());
    cfg.entry = cfg.blocks.back().get();
    cfg.entry->id = 1;

    std::unordered_map<int, BasicBlock *> labelBlocks;
    collectLabels(fb.body.get(), labelBlocks, cfg);

    Builder builder(cfg, labelBlocks);
    builder.run(fb.body.get());

    return cfg;
}

}  // namespace rcc::ir
