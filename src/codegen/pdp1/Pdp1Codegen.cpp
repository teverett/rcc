#include "Pdp1Codegen.h"

#include <stdexcept>
#include <unordered_set>
#include <utility>

#include "Pdp1Assembler.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

// This target's native word is one's-complement: a negative value's bit
// pattern is the bitwise complement of its magnitude, not magnitude-plus-
// one the way two's complement is. Every literal this backend ever embeds
// in generated assembly - not just a user-written `-5` but every compile-
// time constant (frame offsets, array scaling, shift counts) - goes
// through this, never a bare two's-complement truncation, so hardware
// `add`/`sub` (which operate on one's-complement bit patterns) see values
// consistent with their own arithmetic.
long onesComplementWord(long v) {
    if (v >= 0) return v & 0777777L;
    long mag = (-v) & 0777777L;
    return (~mag) & 0777777L;
}

bool exprMakesCall(const ir::Expr *e) {
    if (!e) return false;
    if (e->kind == ir::ExprKind::Call || e->kind == ir::ExprKind::CallPtr) return true;
    if (exprMakesCall(e->lhs.get())) return true;
    if (exprMakesCall(e->rhs.get())) return true;
    for (const auto &a : e->args) {
        if (exprMakesCall(a.get())) return true;
    }
    return false;
}

bool slotIsWide(const ir::Slot *s) {
    return !isPointer(s->type) && (s->type.base == BaseType::Long || s->type.base == BaseType::LongLong);
}

bool stmtRulesOutLeaf(const ir::Stmt *s) {
    if (!s) return false;
    // A struct Local also disqualifies the leaf path: its member
    // accesses lower to real address arithmetic (&local + byte
    // offset), which needs a frame-pointer-relative address like any
    // other addressed Local/Param, not the leaf scheme's single
    // static cell per slot (and a struct's size is usually more than
    // one word anyway).
    if (s->kind == ir::StmtKind::LocalDecl && s->declSlot &&
        (s->declSlot->arraySize > 0 || slotIsWide(s->declSlot) ||
         s->declSlot->type.base == BaseType::Struct)) {
        return true;
    }
    if (exprMakesCall(s->cond.get())) return true;
    if (exprMakesCall(s->initOrReturnOrExpr.get())) return true;
    for (const auto &c : s->body) {
        if (stmtRulesOutLeaf(c.get())) return true;
    }
    if (stmtRulesOutLeaf(s->thenStmt.get())) return true;
    if (stmtRulesOutLeaf(s->elseStmt.get())) return true;
    if (stmtRulesOutLeaf(s->loopBody.get())) return true;
    if (stmtRulesOutLeaf(s->postStmt.get())) return true;
    return false;
}
}  // namespace

bool Pdp1Codegen::computeIsLeaf(const ir::Function *fn, const ir::Stmt *body) {
    for (const ir::Slot *p : fn->params) {
        if (p->arraySize > 0 || slotIsWide(p) || p->type.base == BaseType::Struct) return false;
    }
    return !stmtRulesOutLeaf(body);
}

std::string Pdp1Codegen::allocTemp() { return "T" + std::to_string(tempCounter_++); }

void Pdp1Codegen::declareTemp(const std::string &label) { trailer_.push_back(label + ": 0"); }

std::string Pdp1Codegen::arrayElemText(const ir::ArrayInitElem &e) {
    if (!e.isSymbol) return std::to_string(onesComplementWord(e.value));
    return e.slot ? label(e.slot) : label(e.func);
}

void Pdp1Codegen::declareArray(const std::string &label, const std::vector<ir::ArrayInitElem> &values) {
    if (values.empty()) return;
    trailer_.push_back(label + ": " + arrayElemText(values[0]));
    for (size_t i = 1; i < values.size(); i++) {
        trailer_.push_back(arrayElemText(values[i]));
    }
}

std::string Pdp1Codegen::literalLabel(long value) {
    auto it = literalCache_.find(value);
    if (it != literalCache_.end()) return it->second;
    std::string lbl = "N" + std::to_string(litCounter_++);
    literalCache_[value] = lbl;
    trailer_.push_back(lbl + ": " + std::to_string(onesComplementWord(value)));
    return lbl;
}

std::string Pdp1Codegen::newLabel() { return "J" + std::to_string(labelCounter_++); }

// AC += v (v>=0: `add`) or AC -= -v (v<0: `sub`) - see the class comment:
// this backend never needs a negative-valued literal cell for compile-time
// offset arithmetic, since hardware `sub` does the negating for free.
void Pdp1Codegen::addConst(long v) {
    if (v >= 0) {
        emitRaw(cat("   add ", literalLabel(v)));
    } else {
        emitRaw(cat("   sub ", literalLabel(-v)));
        normalizeNegZero();
    }
}

void Pdp1Codegen::negateAC() {
    emitRaw("   cma");
    normalizeNegZero();
}

// The all-ones bit pattern is this target's "negative zero" - mathematically
// 0, but a different bit pattern from all-zeros, which a raw `sad`/`sza`
// zero/equality test can't see through. Hardware `add` already normalizes
// this away on its own (verified against SIMH's decode logic - see
// Pdp1Codegen.h's class comment); `sub` and `cma` do not, so every one of
// this backend's own `sub`/`cma` emissions runs through here immediately
// after, keeping every live value (AC, a slot, a temp) in normal form -
// nothing downstream (sad/sza/sma/sza on a stored value) ever has to think
// about which zero it's looking at.
void Pdp1Codegen::normalizeNegZero() {
    std::string Lclean = newLabel(), Lskip = newLabel();
    emitRaw("   sad __mone");
    emitRaw(cat("   jmp ", Lclean));
    emitRaw(cat("   jmp ", Lskip));
    emitLabel(Lclean);
    emitRaw("   lac __zero");
    emitLabel(Lskip);
}

void Pdp1Codegen::genWideUnsupported(int line, const std::string &what) {
    err(line, "'long'/'long long' " + what +
                  " isn't supported on the pdp1 backend: a correct multi-word carry chain on this "
                  "one's-complement machine (which has no link/carry bit at all, unlike pdp7's "
                  "two's-complement-with-link architecture) would need its own from-scratch, independently "
                  "verified algorithm - see Pdp1Codegen.h's class comment");
}

std::string Pdp1Codegen::generate(const ir::Program &prog, bool objectMode) {
    (void)objectMode;  // this backend has no separate-compilation/object mode - always whole-program
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }

    std::vector<std::tuple<std::string, size_t, size_t>> codeRanges;
    std::vector<std::tuple<std::string, size_t, size_t>> trailerRanges;

    std::string homeFile = mainFn ? mainFn->sourceFile
                            : !prog.functionBodies.empty() ? prog.functionBodies.front().func->sourceFile
                            : !prog.slots.empty() ? prog.slots.front()->sourceFile
                                                   : std::string();

    if (mainFn) {
        size_t rangeStart = static_cast<size_t>(code_.tellp());
        emitRaw(cat("   jsp ", label(mainFn), "  ; call main"));
        emitRaw("   hlt");
        codeRanges.push_back({mainFn->sourceFile, rangeStart, static_cast<size_t>(code_.tellp())});
    }

    for (const auto &fb : prog.functionBodies) {
        size_t fCodeStart = static_cast<size_t>(code_.tellp());
        size_t fTrailerStart = trailer_.size();
        if (fb.func->hasAddress) {
            emitRaw("__saveloc = .");
            emitRaw(cat(". = ..+", fb.func->address));
        }
        genFunction(fb);
        if (fb.func->hasAddress) emitRaw(". = __saveloc");
        codeRanges.push_back({fb.func->sourceFile, fCodeStart, static_cast<size_t>(code_.tellp())});
        trailerRanges.push_back({fb.func->sourceFile, fTrailerStart, trailer_.size()});
    }

    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        if (slot->isExternal) continue;
        size_t gTrailerStart = trailer_.size();
        if (slot->hasAddress) {
            trailer_.push_back("__saveloc = .");
            trailer_.push_back(". = ..+" + std::to_string(slot->address));
        }
        if (slot->arraySize > 0) {
            int elemW = widthOfType(slot->type);
            bool first = true;
            for (size_t i = 0; i < slot->arrayInit.size(); i++) {
                const ir::ArrayInitElem &elem = slot->arrayInit[i];
                for (int j = 0; j < elemW; j++) {
                    std::string text =
                        (!elem.isSymbol) ? std::to_string(onesComplementWord(elem.value >> (18 * j)))
                                          : (j == 0 ? arrayElemText(elem) : "0");
                    if (first) {
                        trailer_.push_back(label(slot.get()) + ": " + text + "  ; " + slot->sourceName);
                        first = false;
                    } else {
                        trailer_.push_back(text);
                    }
                }
            }
        } else {
            int w = widthOfType(slot->type);
            long v = slot->hasInit ? slot->initValue : 0;
            for (int j = 0; j < w; j++) {
                std::string text = std::to_string(onesComplementWord(v >> (18 * j)));
                trailer_.push_back((j == 0 ? label(slot.get()) + ": " : "") + text +
                                    (j == 0 ? "  ; " + slot->sourceName : ""));
            }
        }
        if (slot->hasAddress) trailer_.push_back(". = __saveloc");
        trailerRanges.push_back({slot->sourceFile, gTrailerStart, trailer_.size()});
    }

    size_t sharedStart = trailer_.size();
    ensureRuntimeRoutines();
    trailerRanges.push_back({homeFile, sharedStart, trailer_.size()});

    std::string codeStr = code_.str();
    perFileAsm_.clear();
    for (const auto &[file, s, e] : codeRanges) perFileAsm_[file] += codeStr.substr(s, e - s);
    for (const auto &[file, s, e] : trailerRanges) {
        for (size_t i = s; i < e; i++) perFileAsm_[file] += trailer_[i] + "\n";
    }

    std::ostringstream out;
    out << codeStr;
    for (const auto &line : trailer_) out << line << "\n";
    return out.str();
}

std::map<std::string, std::string> Pdp1Codegen::perFileAsm() const { return perFileAsm_; }

void Pdp1Codegen::genFunction(const ir::FunctionBody &fn) {
    curFunctionLabel_ = label(fn.func);
    emitRaw(cat(curFunctionLabel_, ":  ; fn ", fn.func->sourceName));
    // `jsp` just set AC to the return address and jumped here - it must be
    // tucked away before anything else touches AC (see the class comment).
    emitRaw(cat("   dac ", retAddrCell()));
    trailer_.push_back(retAddrCell() + ": 0");

    currentFunctionIsLeaf_ = computeIsLeaf(fn.func, fn.body.get());
    if (currentFunctionIsLeaf_) {
        emitLeafParamBinding(fn.func);
        genStmt(*fn.body);
        emitLeafReturn(/*hasValue=*/false);
    } else {
        emitPrologue(fn.func);
        genStmt(*fn.body);
        emitEpilogueAndReturn(/*hasValue=*/false);
    }
}

void Pdp1Codegen::pushAC() {
    emitRaw("   dac __stmp");
    emitRaw("   lac __sp");
    emitRaw("   sub __one");
    normalizeNegZero();
    emitRaw("   dac __sp");
    emitRaw("   lac __stmp");
    emitRaw("   dac __sp i");
}

void Pdp1Codegen::popAC() {
    emitRaw("   lac __sp i");
    emitRaw("   dac __stmp");
    emitRaw("   lac __sp");
    emitRaw("   add __one");
    emitRaw("   dac __sp");
    emitRaw("   lac __stmp");
}

// Entry's "dac retAddrCell()" (genFunction()) already captured the return
// address; from here this is the same shape as pdp7's own prologue.
void Pdp1Codegen::emitPrologue(const ir::Function *fn) {
    emitRaw(cat("   lac ", retAddrCell()));
    pushAC();
    emitRaw("   lac __fp");
    pushAC();
    emitRaw("   lac __sp");
    emitRaw("   dac __fp");
    if (fn->frameSize > 0) {
        emitRaw("   lac __fp");
        addConst(-static_cast<long>(fn->frameSize));
        emitRaw("   dac __sp");
    }
}

void Pdp1Codegen::emitEpilogueAndReturn(bool hasValue) {
    if (hasValue) emitRaw("   dac __retval");
    emitRaw("   lac __fp");
    emitRaw("   dac __sp");
    popAC();
    emitRaw("   dac __fp");
    popAC();
    emitRaw(cat("   dac ", retAddrCell()));
    if (hasValue) emitRaw("   lac __retval");
    emitRaw(cat("   jmp ", retAddrCell(), " i"));
}

void Pdp1Codegen::emitLeafParamBinding(const ir::Function *fn) {
    for (ir::Slot *p : fn->params) {
        emitRaw("   lac __sp");
        addConst(p->paramIndex);
        emitRaw("   dac __addr");
        emitRaw("   lac __addr i");
        trailer_.push_back(leafLabel(p) + ": 0");
        emitRaw(cat("   dac ", leafLabel(p)));
    }
}

void Pdp1Codegen::emitLeafReturn(bool hasValue) {
    (void)hasValue;
    emitRaw(cat("   jmp ", retAddrCell(), " i"));
}

void Pdp1Codegen::emitLoadSlotAddress(const ir::Slot *slot) {
    if (slot->kind == ir::SlotKind::Global) {
        emitRaw(cat("   law ", label(slot)));
        return;
    }
    if (currentFunctionIsLeaf_) {
        emitRaw(cat("   law ", leafLabel(slot)));
        return;
    }
    long offset = (slot->kind == ir::SlotKind::Param) ? static_cast<long>(slot->frameOffset)
                                                       : -localBaseOffset(slot);
    emitRaw("   lac __fp");
    addConst(offset);
}

void Pdp1Codegen::emitLoadSlot(const ir::Slot *slot) {
    if (slot->kind == ir::SlotKind::Global) {
        emitRaw(cat("   lac ", label(slot)));
        narrowAC(slot->type);
        return;
    }
    if (currentFunctionIsLeaf_) {
        emitRaw(cat("   lac ", leafLabel(slot)));
        narrowAC(slot->type);
        return;
    }
    emitLoadSlotAddress(slot);
    emitRaw("   dac __addr");
    emitRaw("   lac __addr i");
    narrowAC(slot->type);
}

void Pdp1Codegen::emitStoreSlot(const ir::Slot *slot) {
    if (slot->kind == ir::SlotKind::Global) {
        emitRaw(cat("   dac ", label(slot)));
        return;
    }
    if (currentFunctionIsLeaf_) {
        emitRaw(cat("   dac ", leafLabel(slot)));
        return;
    }
    emitRaw("   dac __stmp");
    emitLoadSlotAddress(slot);
    emitRaw("   dac __addr");
    emitRaw("   lac __stmp");
    emitRaw("   dac __addr i");
}

void Pdp1Codegen::emitSlotBaseAddress(const ir::Slot *slot) {
    if (slot->arraySize > 0) {
        emitLoadSlotAddress(slot);
    } else {
        emitLoadSlot(slot);
    }
}

long Pdp1Codegen::localBaseOffset(const ir::Slot *slot) { return ir::localLowOffset(*slot, typeWidths()); }

void Pdp1Codegen::emitLocalArrayInit(const ir::Slot *slot) {
    int elemW = widthOfType(slot->type);
    for (size_t i = 0; i < slot->arrayInit.size(); i++) {
        const ir::ArrayInitElem &elem = slot->arrayInit[i];
        for (int j = 0; j < elemW; j++) {
            if (!elem.isSymbol) {
                // literalLabel() itself applies onesComplementWord() - do
                // not pre-mask `elem.value` with a plain two's-complement
                // `&0777777` here, which would bake in the wrong bit
                // pattern for a negative value before that conversion runs.
                emitRaw(cat("   lac ", literalLabel(elem.value >> (18 * j))));
            } else if (j > 0) {
                emitRaw("   lac __zero");
            } else if (elem.slot) {
                emitLoadSlotAddress(elem.slot);
            } else {
                emitRaw(cat("   law ", label(elem.func)));
            }
            emitRaw("   dac __stmp");
            emitRaw("   lac __fp");
            long offset = -localBaseOffset(slot) + static_cast<long>(i) * elemW + j;
            addConst(offset);
            emitRaw("   dac __addr");
            emitRaw("   lac __stmp");
            emitRaw("   dac __addr i");
        }
    }
}

void Pdp1Codegen::storeThroughStackedAddress() {
    emitRaw("   dac __stmp2");
    popAC();
    emitRaw("   dac __addr");
    emitRaw("   lac __stmp2");
    emitRaw("   dac __addr i");
}

std::string Pdp1Codegen::popAddressIntoScratch() {
    popAC();
    emitRaw("   dac __addr");
    return "__addr";
}

void Pdp1Codegen::genStmt(const ir::Stmt &s) {
    switch (s.kind) {
        case ir::StmtKind::Block: {
            for (const auto &child : s.body) genStmt(*child);
            return;
        }
        case ir::StmtKind::If: {
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            genCondBranch(*s.cond, s.elseStmt ? Lelse : Ldone);
            genStmt(*s.thenStmt);
            if (s.elseStmt) {
                emitRaw(cat("   jmp ", Ldone));
                emitLabel(Lelse);
                genStmt(*s.elseStmt);
            }
            emitLabel(Ldone);
            return;
        }
        case ir::StmtKind::While: {
            std::string Lstart = newLabel();
            std::string Lend = newLabel();
            std::string Lcont = s.postStmt ? newLabel() : Lstart;
            emitLabel(Lstart);
            genCondBranch(*s.cond, Lend);
            loopLabels_.push_back({Lcont, Lend});
            genStmt(*s.loopBody);
            loopLabels_.pop_back();
            if (s.postStmt) {
                emitLabel(Lcont);
                genStmt(*s.postStmt);
            }
            emitRaw(cat("   jmp ", Lstart));
            emitLabel(Lend);
            return;
        }
        case ir::StmtKind::Break: {
            emitRaw(cat("   jmp ", loopLabels_.back().second));
            return;
        }
        case ir::StmtKind::Continue: {
            emitRaw(cat("   jmp ", loopLabels_.back().first));
            return;
        }
        case ir::StmtKind::Return: {
            bool hasValue = s.initOrReturnOrExpr != nullptr;
            if (hasValue) genExpr(*s.initOrReturnOrExpr);
            if (currentFunctionIsLeaf_) {
                emitLeafReturn(hasValue);
            } else {
                emitEpilogueAndReturn(hasValue);
            }
            return;
        }
        case ir::StmtKind::LocalDecl: {
            if (s.declSlot->arraySize > 0) {
                emitLocalArrayInit(s.declSlot);
            } else if (isWide(s.declSlot->type)) {
                genWideUnsupported(s.line, "local variable");
            } else {
                if (currentFunctionIsLeaf_) {
                    trailer_.push_back(leafLabel(s.declSlot) + ": 0");
                }
                if (s.initOrReturnOrExpr) {
                    genExpr(*s.initOrReturnOrExpr);
                } else {
                    emitRaw("   lac __zero");
                }
                emitStoreSlot(s.declSlot);
            }
            return;
        }
        case ir::StmtKind::ExprStmt: {
            genExpr(*s.initOrReturnOrExpr);
            return;
        }
        case ir::StmtKind::Empty:
            return;
        case ir::StmtKind::Asm: {
            emitVerbatim(s.asmText);
            return;
        }
        case ir::StmtKind::Label: {
            emitLabel(irLabel(s.labelId));
            return;
        }
        case ir::StmtKind::Goto: {
            emitRaw(cat("   jmp ", irLabel(s.labelId)));
            return;
        }
    }
}

// `to.base` names a pointer's pointee type, not its own, so narrowing AC as
// a `char` here would corrupt a real address.
//
// AC holds a full-word one's-complement VALUE that may be negative - unlike
// a two's-complement word, this target's negative bit pattern's own low 8
// bits are NOT that value's mod-256 truncation (two's-complement masking is
// only exact because of two's complement's clean mod-2^n structure; one's
// complement doesn't have it), so a negative value's byte is computed from
// the VALUE (magnitude, then `256 - magnitude mod 256`) rather than by
// masking AC's raw bits directly. A non-negative value's bit pattern
// equals its value in both schemes, so masking it is still exact - see the
// `Ldone` branch below, shared by both paths once each has produced the
// right pre-mask byte.
void Pdp1Codegen::narrowAC(Type to) {
    if (isPointer(to)) return;
    if (to.base != BaseType::Char) return;
    std::string Lneg = newLabel(), Ldone = newLabel();
    emitRaw("   sma");
    emitRaw(cat("   jmp ", Ldone));
    emitRaw(cat("   jmp ", Lneg));
    emitLabel(Lneg);
    emitRaw("   cma");                                // AC = |V|
    emitRaw(cat("   and ", literalLabel(0377)));  // AC = |V| mod 256
    emitRaw("   dac __stmp");
    emitRaw(cat("   lac ", literalLabel(256)));
    emitRaw("   sub __stmp");  // 256 - (|V| mod 256); 256 itself (when the
                                 // mod result was 0) masks to 0 below, still
                                 // correct
    normalizeNegZero();
    emitLabel(Ldone);
    emitRaw(cat("   and ", literalLabel(0377)));
    if (to.isUnsigned) return;
    emitRaw("   dac __stmp");
    emitRaw(cat("   and ", literalLabel(0200)));
    std::string Lhineg = newLabel(), Lhipos = newLabel(), Lhidone = newLabel();
    emitRaw("   sza");
    emitRaw(cat("   jmp ", Lhineg));
    emitRaw(cat("   jmp ", Lhipos));
    emitLabel(Lhineg);
    emitRaw("   lac __stmp");
    addConst(-256);
    emitRaw(cat("   jmp ", Lhidone));
    emitLabel(Lhipos);
    emitRaw("   lac __stmp");
    emitLabel(Lhidone);
}

void Pdp1Codegen::genExpr(const ir::Expr &e) {
    if (isWide(e.type)) genWideUnsupported(e.line, "expression");
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit: {
            emitRaw(cat("   lac ", literalLabel(e.intValue)));
            return;
        }
        case ir::ExprKind::Var: {
            emitLoadSlot(e.slot);
            return;
        }
        case ir::ExprKind::Index: {
            genIndex(e);
            return;
        }
        case ir::ExprKind::Assign: {
            if (e.lhs) {
                if (isCallFree(*e.rhs)) {
                    computeElementAddressAC(e.slot, *e.lhs);
                    std::string T = allocTemp();
                    declareTemp(T);
                    emitRaw(cat("   dac ", T));
                    genExpr(*e.rhs);
                    emitRaw(cat("   dac ", T, " i"));
                    return;
                }
                computeElementAddress(e.slot, *e.lhs);
                genExpr(*e.rhs);
                storeThroughStackedAddress();
                return;
            }
            genExpr(*e.rhs);
            emitStoreSlot(e.slot);
            return;
        }
        case ir::ExprKind::Unary: {
            if (e.op == "-") {
                genExpr(*e.rhs);
                negateAC();
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                genExpr(*e.rhs);
                emitRaw("   dac __addr");
                emitRaw("   lac __addr i");
            } else if (e.op == "~") {
                // C's `~x` is the VALUE -x-1, same on every target
                // regardless of the hardware's own complement scheme -
                // `cma` alone only gives -x here (one's-complement
                // negate), so the extra `sub __one` finishes it: -x-1.
                genExpr(*e.rhs);
                emitRaw("   cma");
                normalizeNegZero();
                emitRaw("   sub __one");
                normalizeNegZero();
            } else {
                genAddrOf(*e.rhs);
            }
            return;
        }
        case ir::ExprKind::DerefAssign: {
            genExpr(*e.lhs);
            if (isCallFree(*e.rhs)) {
                std::string T = allocTemp();
                declareTemp(T);
                emitRaw(cat("   dac ", T));
                genExpr(*e.rhs);
                emitRaw(cat("   dac ", T, " i"));
                return;
            }
            pushAC();
            genExpr(*e.rhs);
            storeThroughStackedAddress();
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            if (e.lhs) {
                if (isCallFree(*e.rhs)) {
                    computeElementAddressAC(e.slot, *e.lhs);
                    genCompoundThroughCell(e);
                    return;
                }
                computeElementAddress(e.slot, *e.lhs);
                genCompoundThroughStackedAddress(e);
                return;
            }
            genExpr(*e.rhs);
            std::string rhsCell = prepareCompoundRhs(e);
            emitLoadSlot(e.slot);
            if (e.yieldOld) pushAC();
            finishCompoundOp(e, rhsCell);
            narrowAC(e.type);
            emitStoreSlot(e.slot);
            if (e.yieldOld) popAC();
            return;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            genExpr(*e.lhs);
            if (isCallFree(*e.rhs)) {
                genCompoundThroughCell(e);
                return;
            }
            pushAC();
            genCompoundThroughStackedAddress(e);
            return;
        }
        case ir::ExprKind::Ternary: {
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            genCondBranch(*e.lhs, Lelse);
            genExpr(*e.rhs);
            emitRaw(cat("   jmp ", Ldone));
            emitLabel(Lelse);
            genExpr(*e.elseExpr);
            emitLabel(Ldone);
            return;
        }
        case ir::ExprKind::Call: {
            int totalWords = 0;
            for (size_t i = e.args.size(); i-- > 0;) {
                genExpr(*e.args[i]);
                pushAC();
                totalWords += 1;
            }
            emitRaw(cat("   jsp ", label(e.func)));
            if (totalWords > 0) {
                emitRaw("   dac __retval");
                emitRaw("   lac __sp");
                addConst(static_cast<long>(totalWords));
                emitRaw("   dac __sp");
                emitRaw("   lac __retval");
            }
            return;
        }
        case ir::ExprKind::CallPtr: {
            genExpr(*e.lhs);
            emitRaw("   dac __addr");
            emitRaw("   jsp __addr i");
            return;
        }
        case ir::ExprKind::Binary: {
            genBinary(e);
            return;
        }
        case ir::ExprKind::Intrinsic: {
            // No interrupt-mask instruction exists in the subset of the
            // instruction set this backend uses (see Pdp1Codegen.h's class
            // comment - no I/O support in this pass) - both are plain
            // no-ops here, the same as on a target with no real
            // interrupt-mask instruction of its own (e.g. i8008).
            (void)e.op;
            return;
        }
        case ir::ExprKind::Convert: {
            genConvert(e);
            return;
        }
        case ir::ExprKind::Func:
            err(e.line, "internal: Func reached genExpr - should only ever appear as '&'s operand");
    }
}

void Pdp1Codegen::genConvert(const ir::Expr &e) {
    if (isWide(e.lhs->type) || isWide(e.type)) genWideUnsupported(e.line, "conversion");
    genExpr(*e.lhs);
    narrowAC(e.type);
}

std::optional<std::string> Pdp1Codegen::simpleCell(const ir::Expr &e) {
    if (e.kind == ir::ExprKind::IntLit) return literalLabel(e.intValue);
    if (e.kind != ir::ExprKind::Var || isWide(e.type)) return std::nullopt;
    const ir::Slot *slot = e.slot;
    if (!isPointer(slot->type) && slot->type.base == BaseType::Char) return std::nullopt;
    if (slot->kind == ir::SlotKind::Global) return label(slot);
    if (currentFunctionIsLeaf_) return leafLabel(slot);
    return std::nullopt;
}

bool Pdp1Codegen::isCallFree(const ir::Expr &e) {
    if (e.kind == ir::ExprKind::Call || e.kind == ir::ExprKind::CallPtr) return false;
    for (const ir::Expr *child : {e.lhs.get(), e.rhs.get(), e.elseExpr.get()}) {
        if (child && !isCallFree(*child)) return false;
    }
    for (const auto &arg : e.args) {
        if (!isCallFree(*arg)) return false;
    }
    return true;
}

// No RhsXform is needed on this target: `+`/`-` use native `add`/`sub`
// directly (no pre-negation), and `|` uses native `ior` directly (no
// De Morgan complement) - see genBinary()/genBitwise().
std::string Pdp1Codegen::genOperandPair(const ir::Expr &lhs, const ir::Expr &rhs, RhsXform xform,
                                         const std::string &dest) {
    (void)xform;
    if (dest.empty()) {
        if (rhs.kind == ir::ExprKind::IntLit) {
            genExpr(lhs);
            return literalLabel(rhs.intValue);
        }
        if (auto cell = simpleCell(rhs)) {
            genExpr(lhs);
            return *cell;
        }
    }
    std::string cell = dest.empty() ? "__stmp2" : dest;
    auto finishRhs = [&]() { emitRaw(cat("   dac ", cell)); };
    if (simpleCell(lhs)) {
        genExpr(rhs);
        finishRhs();
        genExpr(lhs);
        return cell;
    }
    genExpr(lhs);
    if (isCallFree(rhs)) {
        std::string T = allocTemp();
        declareTemp(T);
        emitRaw(cat("   dac ", T));
        genExpr(rhs);
        finishRhs();
        emitRaw(cat("   lac ", T));
        return cell;
    }
    pushAC();
    genExpr(rhs);
    finishRhs();
    popAC();
    return cell;
}

void Pdp1Codegen::computeElementAddress(const ir::Slot *slot, const ir::Expr &idxExpr) {
    computeElementAddressAC(slot, idxExpr);
    pushAC();
}

void Pdp1Codegen::computeElementAddressAC(const ir::Slot *slot, const ir::Expr &idxExpr) {
    int elemW = widthOfType(slot->type);
    if (idxExpr.kind == ir::ExprKind::IntLit) {
        long offset = idxExpr.intValue * elemW;
        emitSlotBaseAddress(slot);
        if (offset != 0) addConst(offset);
        return;
    }
    genExpr(idxExpr);
    if (elemW > 1) {
        std::string Tidx = allocTemp();
        declareTemp(Tidx);
        emitRaw(cat("   dac ", Tidx));
        for (int i = 1; i < elemW; i++) emitRaw(cat("   add ", Tidx));
    }
    emitRaw("   dac __stmp2");
    emitSlotBaseAddress(slot);
    emitRaw("   add __stmp2");
}

void Pdp1Codegen::genIndex(const ir::Expr &e) {
    computeElementAddressAC(e.slot, *e.lhs);
    emitRaw("   dac __addr");
    emitRaw("   lac __addr i");
}

void Pdp1Codegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        emitLoadSlotAddress(operand.slot);
        return;
    }
    if (operand.kind == ir::ExprKind::Index) {
        computeElementAddressAC(operand.slot, *operand.lhs);
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        emitRaw(cat("   law ", label(operand.func)));
        return;
    }
    err(operand.line, "internal: unhandled addressable-operand kind");
}

std::string Pdp1Codegen::prepareCompoundRhs(const ir::Expr &e) {
    const std::string &op = e.op;
    if (op == "+" || op == "-" || op == "&" || op == "^" || op == "|") {
        emitRaw("   dac __stmp2");
        return "__stmp2";
    }
    if (op == "*") {
        std::string Tr = allocTemp();
        declareTemp(Tr);
        emitRaw(cat("   dac ", Tr));
        return Tr;
    }
    if (op == "/" || op == "%") {
        emitRaw("   dac __dm_b");
        return "__dm_b";
    }
    if (op == "<<" || op == ">>") {
        std::string N = allocTemp();
        declareTemp(N);
        emitRaw(cat("   dac ", N));
        return N;
    }
    err(e.line, "internal: unhandled compound-assignment operator '" + op + "'");
}

void Pdp1Codegen::finishCompoundOp(const ir::Expr &e, const std::string &rhsCell) {
    const std::string &op = e.op;
    if (op == "+") {
        emitRaw(cat("   add ", rhsCell));
        return;
    }
    if (op == "-") {
        emitRaw(cat("   sub ", rhsCell));
        normalizeNegZero();
        return;
    }
    if (op == "&" || op == "^") {
        emitRaw(cat("   ", (op == "&" ? "and" : "xor"), " ", rhsCell));
        return;
    }
    if (op == "|") {
        emitRaw(cat("   ior ", rhsCell));
        return;
    }
    if (op == "*") {
        needMul_ = true;
        emitRaw("   dac __mul_a");
        emitRaw(cat("   lac ", rhsCell));
        emitRaw("   dac __mul_b");
        emitRaw("   jsp __mul");
        return;
    }
    if (op == "/" || op == "%") {
        emitRaw("   dac __dm_a");
        callDivMod(e.type.isUnsigned);
        if (op == "%") emitRaw("   lac __dm_a");
        return;
    }
    if (op == "<<" || op == ">>") {
        std::string V = allocTemp();
        declareTemp(V);
        emitRaw(cat("   dac ", V));
        std::string Lstart = newLabel(), Lbody = newLabel(), Lend = newLabel();
        emitLabel(Lstart);
        emitRaw(cat("   lac ", rhsCell));
        emitRaw("   sza");
        emitRaw(cat("   jmp ", Lbody));
        emitRaw(cat("   jmp ", Lend));
        emitLabel(Lbody);
        emitRaw(cat("   lac ", V));
        emitShiftOne(op, e.type.isUnsigned);
        emitRaw(cat("   dac ", V));
        emitRaw(cat("   lac ", rhsCell));
        emitRaw("   sub __one");
        normalizeNegZero();
        emitRaw(cat("   dac ", rhsCell));
        emitRaw(cat("   jmp ", Lstart));
        emitLabel(Lend);
        emitRaw(cat("   lac ", V));
        return;
    }
    err(e.line, "internal: unhandled compound-assignment operator '" + op + "'");
}

void Pdp1Codegen::genCompoundThroughCell(const ir::Expr &e) {
    std::string T = allocTemp();
    declareTemp(T);
    emitRaw(cat("   dac ", T));
    genExpr(*e.rhs);
    std::string rhsCell = prepareCompoundRhs(e);
    emitRaw(cat("   lac ", T, " i"));
    if (e.yieldOld) pushAC();
    finishCompoundOp(e, rhsCell);
    narrowAC(e.type);
    emitRaw(cat("   dac ", T, " i"));
    if (e.yieldOld) popAC();
}

void Pdp1Codegen::genCompoundThroughStackedAddress(const ir::Expr &e) {
    genExpr(*e.rhs);
    std::string rhsCell = prepareCompoundRhs(e);
    std::string addr = popAddressIntoScratch();
    emitRaw(cat("   lac ", addr, " i"));
    if (e.yieldOld) pushAC();
    finishCompoundOp(e, rhsCell);
    narrowAC(e.type);
    emitRaw(cat("   dac ", addr, " i"));
    if (e.yieldOld) popAC();
}

void Pdp1Codegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    if (op == "+") {
        std::string rhsCell = genOperandPair(*e.lhs, *e.rhs, RhsXform::None);
        emitRaw(cat("   add ", rhsCell));
        return;
    }
    if (op == "-") {
        std::string rhsCell = genOperandPair(*e.lhs, *e.rhs, RhsXform::None);
        emitRaw(cat("   sub ", rhsCell));
        normalizeNegZero();
        return;
    }
    if (op == "*") {
        genMul(*e.lhs, *e.rhs);
        return;
    }
    if (op == "/") {
        genDivMod(*e.lhs, *e.rhs, /*wantRemainder=*/false);
        return;
    }
    if (op == "%") {
        genDivMod(*e.lhs, *e.rhs, /*wantRemainder=*/true);
        return;
    }
    if (op == "&" || op == "^" || op == "|") {
        genBitwise(op, *e.lhs, *e.rhs);
        return;
    }
    if (op == "<<" || op == ">>") {
        genShift(op, *e.lhs, *e.rhs);
        return;
    }
    if (op == "&&") {
        genLogicalAnd(*e.lhs, *e.rhs);
        return;
    }
    if (op == "||") {
        genLogicalOr(*e.lhs, *e.rhs);
        return;
    }
    genCompare(op, *e.lhs, *e.rhs);
}

void Pdp1Codegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs) {
    if (op == "==") return emitEq(lhs, rhs, /*negate=*/false);
    if (op == "!=") return emitEq(lhs, rhs, /*negate=*/true);
    if (op == "<") return emitLt(lhs, rhs, /*negate=*/false);
    if (op == ">=") return emitLt(lhs, rhs, /*negate=*/true);
    if (op == ">") return emitLt(rhs, lhs, /*negate=*/false);
    if (op == "<=") return emitLt(rhs, lhs, /*negate=*/true);
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

void Pdp1Codegen::emitEq(const ir::Expr &lhs, const ir::Expr &rhs, bool negate) {
    std::string rhsCell = genOperandPair(lhs, rhs, RhsXform::None);
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    emitRaw(cat("   sad ", rhsCell));
    emitRaw(cat("   jmp ", Ltrue));
    emitRaw(cat("   jmp ", Lfalse));
    emitLabel(Ltrue);
    emitRaw("   lac __one");
    emitRaw(cat("   jmp ", Ldone));
    emitLabel(Lfalse);
    emitRaw("   lac __zero");
    emitLabel(Ldone);
    if (negate) emitRaw("   xor __one");
}

// a<b via `sub`+sign-test when same-sign (safe: same-sign magnitudes can't
// overflow this target's representable range); decided directly when signs
// differ, avoiding the overflow a cross-sign subtract could hit. See
// Pdp1Codegen.h's class comment for this target's documented `unsigned`
// range ceiling (2^17-1) that this whole comparison family relies on.
void Pdp1Codegen::emitLt(const ir::Expr &lhs, const ir::Expr &rhs, bool negate) {
    bool unsignedCmp = lhs.type.isUnsigned;
    auto flip = [&]() {
        if (unsignedCmp) emitRaw("   xor __signbit");
    };
    // Whether `e`'s sign is knowable with no runtime `sma` test at all: a
    // compile-time literal, a boolean-valued expression (a comparison/
    // logical operator, or `!`, which always evaluates to 0 or 1
    // regardless of its own operands' values) - or, via the other
    // operand's own `knownNonNegative` check below, "the sibling operand
    // has already been proven non-negative". Only ever consulted as the
    // *other* operand in signAKnown/signBKnown below, never to make `e`
    // itself skip its own test: an IntLit/boolean-valued `e` still runs
    // through genExpr()+toTemp() like any other operand on this target (no
    // direct-literal-cell path the way Pdp7Codegen.cpp's own simpleCell()
    // has), so its *own* sign never needs skipping - only a sibling Var's
    // `knownNonNegative` check is ever actually skipped as a result.
    // Mirrors Pdp7Codegen.cpp's own genCompareCells()'s `hasStaticSign()`:
    // same two optimizer passes (InductionNonNegative/GuardNonNegative,
    // see IR.h's own `knownNonNegative`), same underlying reasoning on a
    // different one's-complement target that also has no condition-code
    // register and so also needs a runtime sign dispatch for every signed
    // `<`.
    static const std::unordered_set<std::string> kBoolOps = {"==", "!=", "<", "<=", ">", ">=", "&&", "||"};
    auto hasStaticSign = [&](const ir::Expr &e) {
        if (e.kind == ir::ExprKind::IntLit) return true;
        if (e.kind == ir::ExprKind::Var && e.slot && e.slot->knownNonNegative) return true;
        if (e.kind == ir::ExprKind::Unary && e.op == "!") return true;
        if (e.kind == ir::ExprKind::Binary && kBoolOps.count(e.op)) return true;
        return false;
    };
    // Only ever claims a *non-negative* sign (never a known-negative one -
    // `knownNonNegative` only ever proves the former), and only when the
    // other operand also has a statically known sign: with just one side
    // known, the dispatch below would still need a live test on the other
    // side in every arm anyway, so skipping just one side's own test saves
    // nothing. Never applied under `unsignedCmp`, since `knownNonNegative`
    // proves a fact about the original signed value, not about the
    // sign-bit-flipped representation this comparison actually runs on.
    bool signAKnown = !unsignedCmp && lhs.kind == ir::ExprKind::Var && lhs.slot && lhs.slot->knownNonNegative &&
                       hasStaticSign(rhs);
    bool signBKnown = !unsignedCmp && rhs.kind == ir::ExprKind::Var && rhs.slot && rhs.slot->knownNonNegative &&
                       hasStaticSign(lhs);
    std::string A, B;
    auto toTemp = [&]() {
        std::string T = allocTemp();
        declareTemp(T);
        emitRaw(cat("   dac ", T));
        return T;
    };
    if (isCallFree(rhs)) {
        genExpr(lhs);
        flip();
        A = toTemp();
        genExpr(rhs);
        flip();
        B = toTemp();
    } else {
        genExpr(lhs);
        flip();
        if (isCallFree(rhs)) {
            A = toTemp();
            genExpr(rhs);
            flip();
            B = toTemp();
        } else {
            pushAC();
            genExpr(rhs);
            flip();
            B = toTemp();
            popAC();
            A = toTemp();
        }
    }
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel(), Lsub = newLabel();
    // `sma` skips the immediately following instruction when AC<0: with
    // "sma\n jmp X\n jmp Y", X runs when AC>=0 and Y runs when AC<0 (the
    // skip jumps over X, landing on Y) - every branch below is ordered to
    // that rule, not to the "jmp true-case then jmp false-case" shape
    // used elsewhere in this file for an `sza`/`sad` test.
    if (signAKnown && signBKnown) {
        // Both operands provably non-negative: same sign is guaranteed, so
        // neither side needs a runtime test at all - fall straight through
        // into the subtract below.
    } else if (signAKnown) {
        // A>=0 known: B>=0 (same sign) needs Lsub; B<0 decides A<B false -
        // skips A's own dispatch entirely, testing only B.
        emitRaw(cat("   lac ", B));
        emitRaw("   sma");
        emitRaw(cat("   jmp ", Lsub));
        emitRaw(cat("   jmp ", Lfalse));
    } else if (signBKnown) {
        // B>=0 known: A<0<=B decides true directly; A>=0 (same sign) needs
        // Lsub - skips B's own dispatch entirely, testing only A.
        emitRaw(cat("   lac ", A));
        emitRaw("   sma");
        emitRaw(cat("   jmp ", Lsub));
        emitRaw(cat("   jmp ", Ltrue));
    } else {
        std::string LaNeg = newLabel(), LaPos = newLabel();
        emitRaw(cat("   lac ", A));
        emitRaw("   sma");
        emitRaw(cat("   jmp ", LaPos));
        emitRaw(cat("   jmp ", LaNeg));
        // A<0: B>=0 decides A<B directly (true); B<0 (same sign) needs Lsub.
        emitLabel(LaNeg);
        emitRaw(cat("   lac ", B));
        emitRaw("   sma");
        emitRaw(cat("   jmp ", Ltrue));
        emitRaw(cat("   jmp ", Lsub));
        // A>=0: B>=0 (same sign) needs Lsub; B<0 decides A<B directly (false).
        emitLabel(LaPos);
        emitRaw(cat("   lac ", B));
        emitRaw("   sma");
        emitRaw(cat("   jmp ", Lsub));
        emitRaw(cat("   jmp ", Lfalse));
    }
    emitLabel(Lsub);
    emitRaw(cat("   lac ", A));
    emitRaw(cat("   sub ", B));
    normalizeNegZero();
    emitRaw("   sma");
    emitRaw(cat("   jmp ", Lfalse));
    emitRaw(cat("   jmp ", Ltrue));
    emitLabel(Ltrue);
    emitRaw(cat("   lac ", (negate ? "__zero" : "__one")));
    emitRaw(cat("   jmp ", Ldone));
    emitLabel(Lfalse);
    emitRaw(cat("   lac ", (negate ? "__one" : "__zero")));
    emitLabel(Ldone);
}

void Pdp1Codegen::genCondBranch(const ir::Expr &cond, const std::string &Lfalse) {
    if (cond.kind == ir::ExprKind::Binary && cond.op == "&&") {
        genCondBranch(*cond.lhs, Lfalse);
        genCondBranch(*cond.rhs, Lfalse);
        return;
    }
    genExpr(cond);
    std::string Ltrue = newLabel();
    emitRaw("   sza");
    emitRaw(cat("   jmp ", Ltrue));
    emitRaw(cat("   jmp ", Lfalse));
    emitLabel(Ltrue);
}

void Pdp1Codegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    std::string Lcheckb = newLabel(), Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    emitRaw("   sza");
    emitRaw(cat("   jmp ", Lcheckb));
    emitRaw(cat("   jmp ", Lfalse));
    emitLabel(Lcheckb);
    genExpr(rhs);
    emitRaw("   sza");
    emitRaw(cat("   jmp ", Ltrue));
    emitRaw(cat("   jmp ", Lfalse));
    emitLabel(Ltrue);
    emitRaw("   lac __one");
    emitRaw(cat("   jmp ", Ldone));
    emitLabel(Lfalse);
    emitRaw("   lac __zero");
    emitLabel(Ldone);
}

void Pdp1Codegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    std::string Ltrue = newLabel(), Lcheckb = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    emitRaw("   sza");
    emitRaw(cat("   jmp ", Ltrue));
    emitRaw(cat("   jmp ", Lcheckb));
    emitLabel(Lcheckb);
    genExpr(rhs);
    emitRaw("   sza");
    emitRaw(cat("   jmp ", Ltrue));
    emitRaw(cat("   jmp ", Lfalse));
    emitLabel(Ltrue);
    emitRaw("   lac __one");
    emitRaw(cat("   jmp ", Ldone));
    emitLabel(Lfalse);
    emitRaw("   lac __zero");
    emitLabel(Ldone);
}

void Pdp1Codegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    emitRaw("   sza");
    emitRaw(cat("   jmp ", Lfalse));
    emitRaw(cat("   jmp ", Ltrue));
    emitLabel(Ltrue);
    emitRaw("   lac __one");
    emitRaw(cat("   jmp ", Ldone));
    emitLabel(Lfalse);
    emitRaw("   lac __zero");
    emitLabel(Ldone);
}

// Software sign-magnitude multiply (see pdp1RuntimeAsm()'s `__mul`): this
// target has no hardware multiply, so `*` always calls the shared runtime
// routine rather than inlining.
void Pdp1Codegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    needMul_ = true;
    // genOperandPair() leaves `lhs`'s value in AC and stores `rhs`'s value
    // into __mul_b; AC only needs to be stashed into __mul_a to complete
    // the pair.
    genOperandPair(lhs, rhs, RhsXform::None, "__mul_b");
    emitRaw("   dac __mul_a");
    emitRaw("   jsp __mul");
}

void Pdp1Codegen::ensureRuntimeRoutines() {
    std::istringstream lines(pdp1RuntimeAsm(needDivMod_, needUDivMod_, needMul_));
    for (std::string line; std::getline(lines, line);) trailer_.push_back(line);
}

void Pdp1Codegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder) {
    genOperandPair(lhs, rhs, RhsXform::None, "__dm_b");
    emitRaw("   dac __dm_a");
    callDivMod(lhs.type.isUnsigned);
    if (wantRemainder) emitRaw("   lac __dm_a");
}

void Pdp1Codegen::callDivMod(bool isUnsigned) {
    if (isUnsigned) {
        needUDivMod_ = true;
        emitRaw("   jsp __udivmod");
    } else {
        needDivMod_ = true;
        emitRaw("   jsp __divmod");
    }
}

// "&"/"^" are real hardware instructions; "|" is also real hardware on this
// target (`ior`) - no De Morgan synthesis needed, unlike pdp7.
//
// Only verified for non-negative operands (same scoping as pdp7's own
// `mul`): these operate directly on AC's raw one's-complement bit
// pattern, which only equals a non-negative value's own two's-complement
// bit pattern (what C's `&`/`|`/`^` are specified against) when the sign
// bit is clear - unlike unary `~` (see genExpr()'s Unary case), there's
// no equivalent pure-arithmetic identity for a BINARY bitwise op that
// sidesteps this, so a negative operand here can give a value C wouldn't.
void Pdp1Codegen::genBitwise(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string rhsCell = genOperandPair(lhs, rhs, RhsXform::None);
    const char *instr = op == "&" ? "and" : (op == "^" ? "xor" : "ior");
    emitRaw(cat("   ", instr, " ", rhsCell));
}

// A single bit of shift/rotate, purely as bit manipulation (see
// Pdp1Codegen.h's method comment) - no dependence on this target's
// arithmetic (`add`/`sub`) semantics at all, so none of the one's-
// complement -0 concerns that apply elsewhere in this file apply here.
void Pdp1Codegen::emitShiftOne(const std::string &op, bool isUnsigned) {
    if (op == "<<") {
        emitRaw("   ral");
        emitRaw("   and __notbit0");
        return;
    }
    if (isUnsigned) {
        emitRaw("   rar");
        emitRaw("   and __notsignbit");
        return;
    }
    std::string Lneg = newLabel(), Lpos = newLabel(), Ldone = newLabel();
    emitRaw("   sma");
    emitRaw(cat("   jmp ", Lpos));
    emitRaw(cat("   jmp ", Lneg));
    emitLabel(Lneg);
    emitRaw("   rar");
    emitRaw("   ior __signbit");
    emitRaw(cat("   jmp ", Ldone));
    emitLabel(Lpos);
    emitRaw("   rar");
    emitRaw("   and __notsignbit");
    emitLabel(Ldone);
}

void Pdp1Codegen::emitConstantShift(const std::string &op, long count, bool isUnsigned) {
    for (long i = 0; i < count; i++) emitShiftOne(op, isUnsigned);
}

// No variable-count hardware shift is used on this target (see
// emitShiftOne()'s comment) - constant and variable counts both just
// repeat the one-bit primitive, the constant case merely unrolled instead
// of looped.
void Pdp1Codegen::genShift(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs) {
    if (rhs.kind == ir::ExprKind::IntLit && rhs.intValue >= 0 && rhs.intValue < 18) {
        genExpr(lhs);
        emitConstantShift(op, rhs.intValue, lhs.type.isUnsigned);
        return;
    }
    std::string N = allocTemp();
    declareTemp(N);
    genOperandPair(lhs, rhs, RhsXform::None, N);
    std::string V = allocTemp();
    declareTemp(V);
    emitRaw(cat("   dac ", V));
    std::string Lstart = newLabel(), Lbody = newLabel(), Lend = newLabel();
    emitLabel(Lstart);
    emitRaw(cat("   lac ", N));
    emitRaw("   sza");
    emitRaw(cat("   jmp ", Lbody));
    emitRaw(cat("   jmp ", Lend));
    emitLabel(Lbody);
    emitRaw(cat("   lac ", V));
    emitShiftOne(op, lhs.type.isUnsigned);
    emitRaw(cat("   dac ", V));
    emitRaw(cat("   lac ", N));
    emitRaw("   sub __one");
    normalizeNegZero();
    emitRaw(cat("   dac ", N));
    emitRaw(cat("   jmp ", Lstart));
    emitLabel(Lend);
    emitRaw(cat("   lac ", V));
}

std::optional<std::string> Pdp1Codegen::assembleInProcess(const std::string &asmText,
                                                            const std::string &format) const {
    if (format == "list") return assemblePdp1(asmText, Pdp1OutputFormat::List);
    if (format == "rim") return assemblePdp1(asmText, Pdp1OutputFormat::Rim);
    return std::nullopt;
}

std::string pdp1RuntimeAsm(bool divMod, bool uDivMod, bool mul) {
    std::ostringstream o;
    // `__sp` starts at the top of the 4096-word (12-bit address field)
    // address space, `kPdp1StackReserve` words clear of
    // `kPdp1StackSafeLimit` - see Pdp1Assembler.h. `__signbit`/
    // `__notsignbit`/`__notbit0`/`__mone` are the fixed bit masks
    // emitShiftOne()/normalizeNegZero() use.
    o << "__one: 1\n__zero: 0\n__mone: 0777777\n"
         "__signbit: 0400000\n__notsignbit: 0377777\n__notbit0: 0777776\n"
         "__sp: 4095\n__fp: 0\n__stmp: 0\n__stmp2: 0\n__addr: 0\n__retval: 0\n";
    // `__eighteen` is shared by `__mul`'s and `__udivmod`'s own 18-iteration
    // loops - declared here, once, rather than only alongside whichever of
    // the two happens to be needed, since either one alone must still see
    // it defined.
    if (divMod || uDivMod || mul) o << "__eighteen: 18\n";
    if (divMod || uDivMod) o << "__dm_a: 0\n__dm_b: 0\n";
    if (mul) o << "__mul_a: 0\n__mul_b: 0\n__mul_s: 0\n__mul_p: 0\n__mul_n: 0\n";
    if (mul) {
        // Sign-magnitude multiply: strip each operand's sign, shift-add the
        // magnitudes (`__mul_a` doubling left each step, `__mul_b` shifting
        // right to expose its next bit), then reapply the combined sign.
        // Every step works on non-negative magnitudes only, so none of
        // this needs the `sub`-after-`add` normalization dance above - see
        // Pdp1Codegen.h's class comment on this target's documented
        // `unsigned`/magnitude range ceiling (2^17-1), which this relies on.
        o << "__mul:\n"
             "   dac __mul_ra\n"
             "   lac __mul_a\n   xor __mul_b\n   and __signbit\n   dac __mul_s\n"
             "   lac __mul_a\n   sma\n   jmp M1P\n   jmp M1N\n"
             "M1N:\n   lac __mul_a\n   cma\n   dac __mul_a\n"
             "M1P:\n"
             "   lac __mul_b\n   sma\n   jmp M2P\n   jmp M2N\n"
             "M2N:\n   lac __mul_b\n   cma\n   dac __mul_b\n"
             "M2P:\n"
             "   lac __zero\n   dac __mul_p\n"
             "   lac __eighteen\n   dac __mul_n\n"
             "MLOOP:\n"
             "   lac __mul_b\n   and __one\n"
             "   sza\n   jmp MADD\n   jmp MSHIFT\n"
             "MADD:\n"
             "   lac __mul_p\n   add __mul_a\n   dac __mul_p\n"
             "MSHIFT:\n"
             "   lac __mul_a\n   ral\n   and __notbit0\n   dac __mul_a\n"
             "   lac __mul_b\n   rar\n   and __notsignbit\n   dac __mul_b\n"
             "   lac __mul_n\n   sub __one\n   dac __mul_n\n"
             "   sza\n   jmp MLOOP\n   jmp MDONE\n"
             "MDONE:\n"
             "   lac __mul_s\n"
             "   sza\n   jmp MSNEG\n   jmp MSPOS\n"
             // `cma` on an exact-zero product (e.g. 0 * -5) lands on the
             // all-ones "negative zero" pattern - same fixup as __divmod's
             // own (see that routine's comment).
             "MSNEG:\n   lac __mul_p\n   cma\n   dac __mul_p\n"
             "   sad __mone\n   jmp MSZFIX\n   jmp MSZSKIP\n"
             "MSZFIX:\n   lac __zero\n   dac __mul_p\n"
             "MSZSKIP:\n"
             "MSPOS:\n"
             "   lac __mul_p\n   jmp __mul_ra i\n"
             "__mul_ra: 0\n";
    }
    if (divMod || uDivMod) {
        o << "__dm_r: 0\n__dm_q: 0\n__dm_tmp: 0\n__dm_bit: 0\n__dm_qbit: 0\n__dm_n: 0\n";
    }
    if (divMod || uDivMod) {
        // Sign-magnitude restoring binary division of two magnitudes - see
        // the comment above `__mul` for the same sign-magnitude strategy
        // and its documented range ceiling. Returns the quotient in AC and
        // leaves the remainder in `__dm_a` (both magnitudes here; sign is
        // reapplied by `__divmod` below, never by this routine itself).
        o << "__udivmod:\n"
             "   dac __udivmod_ra\n"
             "   lac __zero\n   dac __dm_r\n   lac __zero\n   dac __dm_q\n"
             "   lac __eighteen\n   dac __dm_n\n"
             "UDMLOOP:\n"
             "   lac __dm_a\n   ral\n   dac __dm_tmp\n   and __one\n   dac __dm_bit\n"
             "   lac __dm_tmp\n   and __notbit0\n   dac __dm_a\n"
             "   lac __dm_r\n   ral\n   and __notbit0\n   ior __dm_bit\n   dac __dm_r\n"
             "   lac __dm_r\n   sub __dm_b\n"
             "   sma\n   jmp UDMSUB\n   jmp UDMNOSUB\n"
             "UDMSUB:\n"
             "   dac __dm_r\n   lac __one\n   dac __dm_qbit\n   jmp UDMMERGE\n"
             "UDMNOSUB:\n"
             "   lac __zero\n   dac __dm_qbit\n"
             "UDMMERGE:\n"
             "   lac __dm_q\n   ral\n   and __notbit0\n   ior __dm_qbit\n   dac __dm_q\n"
             "   lac __dm_n\n   sub __one\n   dac __dm_n\n"
             "   sza\n   jmp UDMLOOP\n   jmp UDMDONE\n"
             "UDMDONE:\n"
             "   lac __dm_r\n   dac __dm_a\n"
             "   lac __dm_q\n   jmp __udivmod_ra i\n"
             "__udivmod_ra: 0\n";
    }
    if (divMod) {
        o << "__dm_s: 0\n__dm_qs: 0\n"
             "__divmod:\n"
             "   dac __divmod_ra\n"
             "   lac __dm_a\n   sma\n   jmp DMAP\n   jmp DMAN\n"
             "DMAN:\n   lac __one\n   dac __dm_s\n   lac __dm_a\n   cma\n   dac __dm_a\n   jmp DMAD\n"
             "DMAP:\n   lac __zero\n   dac __dm_s\n"
             "DMAD:\n"
             "   lac __dm_b\n   sma\n   jmp DMBP\n   jmp DMBN\n"
             "DMBN:\n   lac __one\n   xor __dm_s\n   dac __dm_qs\n   lac __dm_b\n   cma\n   dac __dm_b\n   jmp DMDIV\n"
             "DMBP:\n   lac __zero\n   xor __dm_s\n   dac __dm_qs\n"
             "DMDIV:\n"
             "   jsp __udivmod\n   dac __dm_q\n"
             // Each `cma` below can land on the all-ones "negative zero"
             // pattern (an exact quotient/remainder negated - see
             // Pdp1Codegen.h's class comment on normalizeNegZero()), so
             // each is followed by the same inline fixup that method
             // emits: `sad __mone` skips the following `lac __zero`
             // unless AC is exactly that pattern.
             "   lac __dm_qs\n   sza\n   jmp DMQNEG\n   jmp DMQPOS\n"
             "DMQNEG:\n   lac __dm_q\n   cma\n   dac __dm_q\n"
             "   sad __mone\n   jmp DMQZFIX\n   jmp DMQZSKIP\n"
             "DMQZFIX:\n   lac __zero\n   dac __dm_q\n"
             "DMQZSKIP:\n"
             "DMQPOS:\n"
             "   lac __dm_s\n   sza\n   jmp DMRNEG\n   jmp DMRPOS\n"
             "DMRNEG:\n   lac __dm_a\n   cma\n   dac __dm_a\n"
             "   sad __mone\n   jmp DMRZFIX\n   jmp DMRZSKIP\n"
             "DMRZFIX:\n   lac __zero\n   dac __dm_a\n"
             "DMRZSKIP:\n"
             "DMRPOS:\n"
             "   lac __dm_q\n   jmp __divmod_ra i\n"
             "__divmod_ra: 0\n";
    }
    return o.str();
}

}  // namespace rcc
