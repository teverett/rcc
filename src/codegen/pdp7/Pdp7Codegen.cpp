#include "Pdp7Codegen.h"

#include <stdexcept>
#include <unordered_set>
#include <utility>

#include "Pdp7Assembler.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

// A raw data word (grammar/rccasm/RccAsm.g4's bare NUMBER syntax has no
// unary minus) must be written as its two's-complement 18-bit pattern
// rather than a signed decimal string.
long pdp7Word(long v) { return v & 0777777; }

// Decimal 15 (octal 17), the highest-numbered PDP-7 hardware autoindex
// register: any *indirect* reference to a word address in [8,15]
// (octal [10,17]) auto-increments it as a CPU side effect
// (grammar/rccasm/RccAsm.g4's NUMBER token is plain decimal, so this
// must stay written in decimal everywhere it's emitted, not as "17").
// `emitLeafParamBinding()` is the only place this backend ever
// deliberately uses it - see docs/known-bugs.md's own guard entry on
// why that's safe only under UnixV0-C's current CAL-trap/interrupt
// setup, not in general.
const char *kParamCursorReg = "15";

// See Pdp7Codegen.h's "Leaf fast path" comment. `exprMakesCall` walks
// every sub-expression a Call/CallPtr could be hiding in;
// `stmtRulesOutLeaf` adds the other disqualifier (a Local/Param
// array).
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

// A wide Local/Param disqualifies the leaf path: the leaf scheme's
// static cells and `__sp`-relative param reads are hardcoded to one
// word per slot. A pointer is always one word regardless of pointee,
// checked first since `.base` names a pointer's pointee type, not its
// own.
bool slotIsWide(const ir::Slot *s) {
    return !isPointer(s->type) && (s->type.base == BaseType::Long || s->type.base == BaseType::LongLong);
}

// `allowCalls` is true only for a function declared `@nonrecursive`
// (ir::Function::isNonRecursive) - the programmer's own assertion that
// no activation of it is ever concurrent with another, which is exactly
// what the leaf scheme's single static cell per slot otherwise requires
// a real call-free leaf to guarantee on its own. The storage-shape
// disqualifiers below apply regardless: they're about what a slot's
// own static cell can represent, not about reentrancy.
bool stmtRulesOutLeaf(const ir::Stmt *s, bool allowCalls) {
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
    // An `asm{...}` block references Param/Local only via ordinary
    // bare-expression statements, which already go through
    // emitLoadSlot()/emitStoreSlot(); it never names `__fp`/`__sp`
    // directly, so it needs no special exclusion here.
    if (!allowCalls) {
        if (exprMakesCall(s->cond.get())) return true;
        if (exprMakesCall(s->initOrReturnOrExpr.get())) return true;
    }
    for (const auto &c : s->body) {
        if (stmtRulesOutLeaf(c.get(), allowCalls)) return true;
    }
    if (stmtRulesOutLeaf(s->thenStmt.get(), allowCalls)) return true;
    if (stmtRulesOutLeaf(s->elseStmt.get(), allowCalls)) return true;
    if (stmtRulesOutLeaf(s->loopBody.get(), allowCalls)) return true;
    if (stmtRulesOutLeaf(s->postStmt.get(), allowCalls)) return true;
    return false;
}

// Whether `s` (or anything it contains) makes a call anywhere, with none
// of stmtRulesOutLeaf()'s other disqualifiers - used to decide
// `currentFunctionCallsSafe_` (see Pdp7Codegen.h's per-slot 'register'
// extension of the leaf fast path), which needs this reentrancy check on
// its own, independent of whether the function also happens to have a
// wide/array/struct slot that rules out the full leaf path.
bool stmtHasCall(const ir::Stmt *s) {
    if (!s) return false;
    if (exprMakesCall(s->cond.get())) return true;
    if (exprMakesCall(s->initOrReturnOrExpr.get())) return true;
    for (const auto &c : s->body) {
        if (stmtHasCall(c.get())) return true;
    }
    if (stmtHasCall(s->thenStmt.get())) return true;
    if (stmtHasCall(s->elseStmt.get())) return true;
    if (stmtHasCall(s->loopBody.get())) return true;
    if (stmtHasCall(s->postStmt.get())) return true;
    return false;
}

// Conservative "every path through this statement already returns"
// check: a Block falls through iff its own last statement does (an
// earlier Return makes everything after it dead code, a separate
// concern this doesn't need to catch), an If needs BOTH arms to
// return (no `else` always falls through), and a loop/anything else
// is never assumed to - this only needs to catch the common,
// provably-safe case (a function whose last top-level statement is a
// Return, directly or through an if/else), not every real path
// control flow could take.
bool stmtAlwaysReturns(const ir::Stmt *s) {
    if (!s) return false;
    switch (s->kind) {
        case ir::StmtKind::Return:
            return true;
        case ir::StmtKind::Block:
            return !s->body.empty() && stmtAlwaysReturns(s->body.back().get());
        case ir::StmtKind::If:
            return s->elseStmt && stmtAlwaysReturns(s->thenStmt.get()) && stmtAlwaysReturns(s->elseStmt.get());
        default:
            return false;
    }
}
}  // namespace

bool Pdp7Codegen::computeIsLeaf(const ir::Function *fn, const ir::Stmt *body) {
    for (const ir::Slot *p : fn->params) {
        if (p->arraySize > 0 || slotIsWide(p) || p->type.base == BaseType::Struct) return false;
    }
    return !stmtRulesOutLeaf(body, fn->isNonRecursive);
}

// See Pdp7Codegen.h's per-slot 'register'/'const' extension of the leaf
// fast path. Never true when `currentFunctionIsLeaf_` already is - the
// two share the same cell/label, just reached via different conditions.
bool Pdp7Codegen::usesStaticCell(const ir::Slot *slot) const {
    return !currentFunctionIsLeaf_ && currentFunctionCallsSafe_ && (slot->isRegister || slot->isConst) &&
           (slot->kind == ir::SlotKind::Local || slot->kind == ir::SlotKind::Param) &&
           slot->arraySize == 0 && !slotIsWide(slot) && slot->type.base != BaseType::Struct;
}

bool Pdp7Codegen::isFastCallEligible(const ir::Function *fn) const {
    if (!fn || !fn->isNonRecursive) return false;
    // A wide-returning call always goes through genWideExpr()'s own
    // Call case, never genExpr()'s - which is the only one this fast
    // path's caller side is wired into - so a fast entry here could
    // never actually be reached from any call site. Excluding it here
    // too just avoids emitting that dead entry point's own 4 words.
    if (!fn->isVoid && isWide(fn->returnType)) return false;
    if (fn->isExternal) {
        if (!fn->hasPrototype) return false;
        for (const Type &t : fn->prototypeParams) {
            if (isWide(t)) return false;
        }
        return true;
    }
    auto it = leafEligibleByFunc_.find(fn);
    return it != leafEligibleByFunc_.end() && it->second;
}

std::string Pdp7Codegen::allocTemp() { return "T" + std::to_string(tempCounter_++); }

void Pdp7Codegen::declareTemp(const std::string &label) { trailer_.push_back(label + ": 0"); }

// A literal element is the two's-complement word text (see
// pdp7Word()); a symbol element is the referenced Slot's or
// Function's label, written bare - the assembler treats a bare
// identifier with no operands as a one-syllable expression evaluating
// to that symbol's resolved address.
std::string Pdp7Codegen::arrayElemText(const ir::ArrayInitElem &e) {
    if (!e.isSymbol) return std::to_string(pdp7Word(e.value));
    return e.slot ? label(e.slot) : label(e.func);
}

void Pdp7Codegen::declareArray(const std::string &label, const std::vector<ir::ArrayInitElem> &values) {
    // One value per line, label on the first - the shared grammar
    // uses ';' for comments, not as a multi-statement-per-line
    // separator.
    if (values.empty()) return;
    trailer_.push_back(label + ": " + arrayElemText(values[0]));
    for (size_t i = 1; i < values.size(); i++) {
        trailer_.push_back(arrayElemText(values[i]));
    }
}

std::string Pdp7Codegen::literalLabel(long value) {
    auto it = literalCache_.find(value);
    if (it != literalCache_.end()) return it->second;
    std::string lbl = "N" + std::to_string(litCounter_++);
    literalCache_[value] = lbl;
    trailer_.push_back(lbl + ": " + std::to_string(pdp7Word(value)));
    return lbl;
}

std::string Pdp7Codegen::newLabel() { return "J" + std::to_string(labelCounter_++); }

// `char`/`short`/`int` are all one pdp7 word; a slot always occupies
// a full word regardless of declared type, so a `char`'s value (not
// its storage) is masked, and re-sign-extended if signed, at every
// point it's read or converted - never at store time, since a stray
// untruncated upper bit is invisible to any later read.
void Pdp7Codegen::narrowAC(Type to) {
    // `to.base` names a pointer's pointee type, not its own, so
    // narrowing AC as a `char` here would corrupt a real address.
    if (isPointer(to)) return;
    if (to.base != BaseType::Char) return;
    emit("and", literalLabel(0377));
    if (to.isUnsigned) return;
    emit("dac", "__stmp");
    emit("and", literalLabel(0200));
    std::string Lneg = newLabel(), Lpos = newLabel(), Ldone = newLabel();
    emit("sza");
    emit("jmp", Lneg);
    emit("jmp", Lpos);
    emitLabel(Lneg);
    emit("lac", "__stmp");
    emit("tad", literalLabel(-256));
    emit("jmp", Ldone);
    emitLabel(Lpos);
    emit("lac", "__stmp");
    emitLabel(Ldone);
}

// Placement (`@addr`): a placed function/global is emitted in its
// normal declaration-order position but wrapped in a save/jump/
// restore of the location-counter symbol ("."): `__saveloc = .`
// stashes it, `. = ..+addr` jumps to the placed address, the item is
// emitted there, then `. = __saveloc` resumes normal placement.
//
// `addr` is an offset from `..` (the relocation base), not a true
// absolute address: the `rim`/`ptr` punch formats only emit memory
// from `..` upward, so an address below it would be silently absent
// from the binary even though it shows in an `a7out` listing.
// `__saveloc` is reused across every placement (never nested).
std::string Pdp7Codegen::generate(const ir::Program &prog, bool objectMode) {
    // Every Slot/Function name is already resolved by IRBuilder - no
    // symbol table of our own, just emit code and declare storage.
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }

    // --dump-asm wants one file per input, not one combined dump.
    // Tracked as (sourceFile, start, end) ranges into code_ (by
    // tellp position) and trailer_ (by index range), built into
    // perFileAsm_ at the end.
    std::vector<std::tuple<std::string, size_t, size_t>> codeRanges;
    std::vector<std::tuple<std::string, size_t, size_t>> trailerRanges;

    // Real separate compilation (`--format=coff`) means `prog` may
    // not define `main` at all; `objectMode` means no object emits
    // the entry sequence itself - the linker synthesizes exactly one
    // once every object is merged. `homeFile` is just --dump-asm's
    // per-file attribution for the handful of things with no single
    // owning Slot/Function.
    std::string homeFile = mainFn ? mainFn->sourceFile
                            : !prog.functionBodies.empty() ? prog.functionBodies.front().func->sourceFile
                            : !prog.slots.empty() ? prog.slots.front()->sourceFile
                                                   : std::string();

    if (mainFn && !objectMode) {
        size_t rangeStart = static_cast<size_t>(code_.tellp());
        emit("jms", cat(label(mainFn), "  ; call main"));
        emit("hlt");
        codeRanges.push_back({mainFn->sourceFile, rangeStart, static_cast<size_t>(code_.tellp())});
    }

    // Computed up front, over every function's own real body, before
    // any of them are generated: isFastCallEligible()'s own same-file
    // case trusts this exact verdict for a callee, rather than
    // re-deriving its own approximation from just `fn->params` (see
    // Pdp7Codegen.h's own comment on `leafEligibleByFunc_`) - and a
    // function defined LATER in this file than one of its own callers
    // still needs an answer available when that EARLIER caller's own
    // code is generated.
    for (const auto &fb : prog.functionBodies) {
        leafEligibleByFunc_[fb.func] = computeIsLeaf(fb.func, fb.body.get());
    }

    for (const auto &fb : prog.functionBodies) {
        size_t fCodeStart = static_cast<size_t>(code_.tellp());
        size_t fTrailerStart = trailer_.size();
        // See the Placement comment above generate().
        if (fb.func->hasAddress) {
            emitRaw("__saveloc = .");
            emitRaw(cat(". = ..+", fb.func->address));
        }
        genFunction(fb);
        if (fb.func->hasAddress) {
            emitRaw(". = __saveloc");
            emitRaw(cat("__root ", label(fb.func)));
        }
        codeRanges.push_back({fb.func->sourceFile, fCodeStart, static_cast<size_t>(code_.tellp())});
        trailerRanges.push_back({fb.func->sourceFile, fTrailerStart, trailer_.size()});
    }

    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        // An external global isn't defined in this file - it must
        // never get a storage word here, only be referenced by name,
        // letting the assembler defer it to a relocation.
        if (slot->isExternal) continue;
        size_t gTrailerStart = trailer_.size();
        if (slot->hasAddress) {
            trailer_.push_back("__saveloc = .");
            trailer_.push_back(". = ..+" + std::to_string(slot->address));
        }
        if (slot->arraySize > 0) {
            // One value per line (';' is a comment in the shared
            // grammar, not a statement separator). Each element
            // reserves `elemW` consecutive words (word 0/low first);
            // a symbol element only fills word 0, zero-padded above.
            int elemW = widthOfType(slot->type);
            bool first = true;
            for (size_t i = 0; i < slot->arrayInit.size(); i++) {
                const ir::ArrayInitElem &elem = slot->arrayInit[i];
                for (int j = 0; j < elemW; j++) {
                    std::string text =
                        (!elem.isSymbol) ? std::to_string(pdp7Word(elem.value >> (18 * j)))
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
                std::string text = std::to_string(pdp7Word(v >> (18 * j)));
                trailer_.push_back((j == 0 ? label(slot.get()) + ": " : "") + text +
                                    (j == 0 ? "  ; " + slot->sourceName : ""));
            }
        }
        if (slot->hasAddress) {
            trailer_.push_back(". = __saveloc");
            trailer_.push_back("__root " + label(slot.get()));
        }
        trailerRanges.push_back({slot->sourceFile, gTrailerStart, trailer_.size()});
    }

    // Shared backend-internal cells with no single owning Slot/
    // Function, attributed to main's own file. Only emitted in
    // whole-program mode: under real separate compilation each `.c`
    // file assembles as its own independent object, and the
    // assembler resolves an in-file-defined label like `__sp` to a
    // local address, so defining these per-file would give each
    // object its own private `__sp`/`__fp`/`__retaddr`, corrupting
    // any call crossing an object-file boundary. Under `objectMode`
    // every file instead leaves these as external references, and
    // `Pdp7LinkerFactory::makePdp7Linker()`'s entry object supplies
    // the one shared definition.
    if (!objectMode) {
        size_t sharedStart = trailer_.size();
        ensureRuntimeRoutines();
        trailerRanges.push_back({homeFile, sharedStart, trailer_.size()});
    }

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

std::map<std::string, std::string> Pdp7Codegen::perFileAsm() const { return perFileAsm_; }

void Pdp7Codegen::genFunction(const ir::FunctionBody &fn) {
    curFunctionLabel_ = label(fn.func);
    emitRaw(cat(curFunctionLabel_, ": 0  ; fn ", fn.func->sourceName));

    // See Pdp7Codegen.h's "Leaf fast path" comment - every other
    // method that treats a Local/Param differently reads this same
    // flag, set once here for whichever function is being generated.
    currentFunctionIsLeaf_ = computeIsLeaf(fn.func, fn.body.get());
    // See Pdp7Codegen.h's per-slot 'register' extension: the same
    // reentrancy guarantee the leaf path itself relies on, computed
    // independently of whether some other slot also rules out the full
    // leaf path.
    currentFunctionCallsSafe_ =
        currentFunctionIsLeaf_ || !stmtHasCall(fn.body.get()) || fn.func->isNonRecursive;
    if (currentFunctionIsLeaf_) {
        emitLeafParamBinding(fn.func);
        // See Pdp7Codegen.h's own fast-call eligibility comment: a
        // second entry point, right where the stack-based entry's own
        // param-binding code (just above) already finishes - a fast
        // caller has already written every argument directly into the
        // exact cells that code would otherwise have copied them INTO
        // from the stack, so jumping in here skips it entirely. Only
        // emitted when a fast caller could actually exist for this
        // function (isFastCallEligible() - same check a call site
        // itself applies), never as dead weight otherwise.
        //
        // The stack-based entry's own fall-through can never be
        // allowed to just run into this new label's own reserved
        // word next (`jms` to it would need a real instruction
        // there, same as any other callee label, and 0 happens to
        // decode as a CAL trap on this target) - true even when
        // emitLeafParamBinding() above emitted no instructions at
        // all (an eligible function with no params): an explicit
        // `jmp` over it is required unconditionally, not just when
        // it happens to fall through into something harmless.
        if (isFastCallEligible(fn.func)) {
            std::string bodyStart = newLabel();
            emit("jmp", bodyStart);
            emitRaw(cat(label(fn.func), "_fast: 0"));
            // `jms <fast label>` wrote ITS caller's return address
            // into the fast label's own cell, not the primary entry's
            // - but the body's own eventual `jmp i <primary label>`
            // (emitLeafReturn()) only ever reads the PRIMARY cell,
            // regardless of which entry got used. Copying it over
            // here, once, lets that one shared return instruction
            // work correctly no matter which entry was actually
            // called through.
            emit("lac", cat(label(fn.func), "_fast"));
            emit("dac", label(fn.func));
            emitLabel(bodyStart);
        }
        genStmt(*fn.body);
        // Default return if the body doesn't return on every path;
        // skipped as dead code when stmtAlwaysReturns() can already
        // prove every path through the body returns on its own.
        if (!stmtAlwaysReturns(fn.body.get())) emitLeafReturn(/*hasValue=*/false);
    } else {
        emitPrologue(fn.func);
        // A qualifying 'register'/'const' Param's cell starts empty -
        // copy its real value in once, from its actual frame address
        // (emitFrameSlotAddress(), bypassing usesStaticCell()'s own
        // redirect), before any other code (which now reads/writes the
        // cell exclusively) runs.
        for (ir::Slot *p : fn.func->params) {
            if (!usesStaticCell(p)) continue;
            trailer_.push_back(leafLabel(p) + ": 0");
            emitFrameSlotAddress(p);
            emit("dac", "__addr");
            emit("lac", "__addr i");
            narrowAC(p->type);
            emit("dac", leafLabel(p));
        }
        genStmt(*fn.body);
        // Same dead-code skip as the leaf path above, for the full
        // frame-based epilogue (7 words here, not 1 - every function
        // with an `asm{}` block anywhere in it, a recursive call, or a
        // wide/array/struct local takes this path, so this matters
        // more here than in the leaf case).
        if (!stmtAlwaysReturns(fn.body.get())) emitEpilogueAndReturn(/*hasValue=*/false, Type{});
    }
}

void Pdp7Codegen::pushAC() {
    needPushPop_ = true;
    emit("jms", "__push");
}

void Pdp7Codegen::popAC() {
    needPushPop_ = true;
    emit("jms", "__pop");
}

void Pdp7Codegen::emitPrologue(const ir::Function *fn) {
    emit("lac", curFunctionLabel_);  // AC = return address jms just wrote here
    pushAC();
    emit("lac", "__fp");
    pushAC();
    emit("lac", "__sp");
    emit("dac", "__fp");  // new frame's own __fp - points at the just-pushed oldFP word
    if (fn->frameSize > 0) {
        emit("tad", literalLabel(-static_cast<long>(fn->frameSize)));
        emit("dac", "__sp");  // reserve this function's own Locals, in one bulk step
    }
}

void Pdp7Codegen::emitEpilogueAndReturn(bool hasValue, Type retType) {
    bool wide = hasValue && isWide(retType);
    int w = wide ? widthOfType(retType) : 0;
    if (hasValue) {
        if (wide) {
            // The caller (genStmt's Return case) already pushed `w`
            // words via genWideExpr (word 0/low on top) - stash them in
            // `__retvalW<i>` before the teardown below discards
            // anything still on the real stack below `__fp`.
            for (int i = 0; i < w; i++) {
                popAC();
                emit("dac", cat("__retvalW", i));
            }
        } else {
            emit("dac", "__retval");
        }
    }
    emit("lac", "__fp");
    emit("dac", "__sp");  // discard this call's own Locals (and anything else below __fp)
    popAC();                    // AC = oldFP
    emit("dac", "__fp");
    popAC();                    // AC = this call's own return address
    emit("dac", "__retaddr");
    if (hasValue) {
        if (wide) {
            // Push the saved words back onto the now-restored stack
            // (word 0/low on top, same convention as any other wide
            // value) - see the `Call` case in genExpr for why this
            // lands exactly where that call's own post-call cleanup
            // expects a wide result to be.
            for (int i = w - 1; i >= 0; i--) {
                emit("lac", cat("__retvalW", i));
                pushAC();
            }
        } else {
            emit("lac", "__retval");
        }
    }
    emit("jmp", "__retaddr i");
}

void Pdp7Codegen::emitLeafParamBinding(const ir::Function *fn) {
    if (fn->params.empty()) return;
    // Params sit on the stack at entry, closest-first (Param 0 pushed
    // last by the caller), so Param k is at `__sp+k` - read without
    // advancing `__sp`, since the caller's own post-call cleanup
    // unconditionally reclaims every argument it pushed regardless of
    // whether the callee was a leaf.
    //
    // Rather than recomputing `__sp+k` from scratch for every k (`lac
    // __sp; tad k; dac __addr; lac __addr i`, 4 words each - the same
    // cost for every k regardless of how many params came before),
    // stage `__sp` itself into the one PDP-7 hardware autoindex
    // register this backend ever uses for this (kParamCursorReg - see
    // its own comment) and read every param through it in increasing
    // index order: `fn->params` is already built index-ascending with
    // no gaps (IRBuilder.cpp sets `p->paramIndex` from the same loop
    // index it pushes params in), so each indirect read both fetches
    // Param k and advances the register to Param (k+1)'s own address
    // for free, as a CPU side effect of referencing it indirectly.
    // After the one-time staging, each param costs only `lac <reg>
    // i; dac <local>` - 2 words instead of 5.
    //
    // This hardware autoindex register increments BEFORE the fetch it
    // gates (confirmed directly against the real simulator, not
    // assumed from the PDP-8-family lineage docs: staging `__sp`
    // itself and reading made every param come out as its own
    // successor's value), so it's staged one below `__sp` - the first
    // pre-increment then lands exactly on Param 0's own address.
    emit("lac", "__sp");
    emit("tad", "__mone");
    emit("dac", kParamCursorReg);
    for (ir::Slot *p : fn->params) {
        emit("lac", std::string(kParamCursorReg) + " i");
        trailer_.push_back(leafLabel(p) + ": 0");
        emit("dac", leafLabel(p));
    }
}

void Pdp7Codegen::emitLeafReturn(bool hasValue) {
    (void)hasValue;  // AC already holds the return value, if any
    emit("jmp", cat("i ", curFunctionLabel_));
}

void Pdp7Codegen::emitFrameSlotAddress(const ir::Slot *slot) {
    long offset = (slot->kind == ir::SlotKind::Param) ? static_cast<long>(slot->frameOffset)
                                                       : -localBaseOffset(slot);
    emit("lac", "__fp");
    emit("tad", literalLabel(offset));
}

void Pdp7Codegen::emitLoadSlotAddress(const ir::Slot *slot) {
    // `law <symbol>` loads AC with its own raw encoded instruction word
    // (0760000 | address), not a decoded address - masking to the low
    // 13-bit address field (every valid address is already < that limit,
    // enforced by Pdp7Assembler.cpp's checkAddressSafe) strips the opcode
    // bits back off.
    if (slot->kind == ir::SlotKind::Global) {
        emit("law", label(slot));
        emit("and", literalLabel(kAddressFieldLimit - 1));
        return;
    }
    if (currentFunctionIsLeaf_ || usesStaticCell(slot)) {
        emit("law", leafLabel(slot));
        emit("and", literalLabel(kAddressFieldLimit - 1));
        return;
    }
    emitFrameSlotAddress(slot);
}

void Pdp7Codegen::emitLoadSlot(const ir::Slot *slot) {
    if (slot->kind == ir::SlotKind::Global) {
        emit("lac", label(slot));
        narrowAC(slot->type);
        return;
    }
    if (currentFunctionIsLeaf_ || usesStaticCell(slot)) {
        emit("lac", leafLabel(slot));
        narrowAC(slot->type);
        return;
    }
    emitLoadSlotAddress(slot);
    emit("dac", "__addr");
    emit("lac", "__addr i");
    narrowAC(slot->type);
}

void Pdp7Codegen::emitStoreSlot(const ir::Slot *slot) {
    if (slot->kind == ir::SlotKind::Global) {
        emit("dac", label(slot));
        return;
    }
    if (currentFunctionIsLeaf_ || usesStaticCell(slot)) {
        emit("dac", leafLabel(slot));
        return;
    }
    emit("dac", "__stmp");
    emitLoadSlotAddress(slot);
    emit("dac", "__addr");
    emit("lac", "__stmp");
    emit("dac", "__addr i");
}

void Pdp7Codegen::emitSlotBaseAddress(const ir::Slot *slot) {
    if (slot->arraySize > 0) {
        emitLoadSlotAddress(slot);
    } else {
        emitLoadSlot(slot);
    }
}

// A Local's address is its lowest word (see ir::localLowOffset()), and
// its words (an array's elements, a wide value's low word first) run
// upward from there, the same direction as a Global's.
long Pdp7Codegen::localBaseOffset(const ir::Slot *slot) { return ir::localLowOffset(*slot, typeWidths()); }

void Pdp7Codegen::emitLocalArrayInit(const ir::Slot *slot) {
    // Each element occupies `elemW` consecutive words, word 0 at the
    // lowest address: element i's word j sits at
    // `__fp-localBaseOffset(slot)+i*elemW+j`.
    int elemW = widthOfType(slot->type);
    for (size_t i = 0; i < slot->arrayInit.size(); i++) {
        const ir::ArrayInitElem &elem = slot->arrayInit[i];
        for (int j = 0; j < elemW; j++) {
            if (!elem.isSymbol) {
                long word = (elem.value >> (18 * j)) & 0777777L;
                emit("lac", literalLabel(word));
            } else if (j > 0) {
                // A symbol element is a plain native-word address;
                // only word 0 holds it, every higher word is zero.
                emit("lac", "__zero");
            } else if (elem.slot) {
                emitLoadSlotAddress(elem.slot);
            } else {
                // See emitLoadSlotAddress's comment: law bakes its own
                // opcode bits into the result.
                emit("law", label(elem.func));
                emit("and", literalLabel(kAddressFieldLimit - 1));
            }
            emit("dac", "__stmp");
            emit("lac", "__fp");
            long offset = -localBaseOffset(slot) + static_cast<long>(i) * elemW + j;
            emit("tad", literalLabel(offset));
            emit("dac", "__addr");
            emit("lac", "__stmp");
            emit("dac", "__addr i");
        }
    }
}

void Pdp7Codegen::storeThroughStackedAddress() {
    // __stmp2, not __stmp: popAC() below already uses __stmp
    // internally, which would otherwise silently clobber the value
    // stashed here before it's read back.
    emit("dac", "__stmp2");
    popAC();
    emit("dac", "__addr");
    emit("lac", "__stmp2");
    emit("dac", "__addr i");
}

std::string Pdp7Codegen::popAddressIntoScratch() {
    popAC();
    emit("dac", "__addr");
    return "__addr";
}

// --- `long`/`long long` (wide) support -------------------------------

void Pdp7Codegen::pushWideFromAddress(Type type) {
    emit("dac", "__addr");
    int w = widthOfType(type);
    for (int i = w - 1; i >= 0; i--) {
        emit("lac", "__addr");
        if (i > 0) emit("tad", literalLabel(i));
        emit("dac", "__stmp");
        emit("lac", "__stmp i");
        pushAC();
    }
}

void Pdp7Codegen::popWideToAddress(Type type) {
    emit("dac", "__addr");
    int w = widthOfType(type);
    for (int i = 0; i < w; i++) {
        popAC();
        emit("dac", "__stmp2");  // __stmp2, not __stmp - popAC() above already uses __stmp
        emit("lac", "__addr");
        if (i > 0) emit("tad", literalLabel(i));
        emit("dac", "__stmp");
        emit("lac", "__stmp2");
        emit("dac", "__stmp i");
    }
}

void Pdp7Codegen::pushWideLiteral(Type type, long value) {
    int w = widthOfType(type);
    // Each word is an 18-bit slice of `value`'s own two's-complement
    // pattern - a plain signed arithmetic right shift (relied on
    // elsewhere in this codebase too) sign-extends correctly even
    // past the host `long`'s own 64 bits (the vacated high bits of
    // an arithmetic shift are indistinguishable from "the number's
    // own sign, repeated forever"), so this is correct for every
    // word even on `long long` (4 words = 72 bits, wider than the
    // host type holding `value`).
    for (int i = w - 1; i >= 0; i--) {
        long wordVal = (value >> (18 * i)) & 0777777L;
        emit("lac", literalLabel(wordVal));
        pushAC();
    }
}

// dest = a + b + carryCell, updating carryCell to this word's carry-
// out. `tad` complements the link on 18-bit overflow (each `cll`
// starts from a known-zero link, so complement and set coincide). No
// single `tad` captures "carry out of a+b+carryIn" in one step, so
// this computes it as two half-adds - `a+b` (carry1), then
// conditionally `+1` (carry2, only when `a+b` wrapped to exactly 0
// with carryIn 1) - ORed together.
void Pdp7Codegen::emitWideAddWithCarry(const std::string &dest, const std::string &a, const std::string &b,
                                         const std::string &carryCell, const std::string &carry1Cell,
                                         const std::string &carry2Cell) {
    std::string Lc1set = newLabel(), Lc1clear = newLabel(), Lc1done = newLabel();
    emit("cll");
    emit("lac", a);
    emit("tad", b);
    emit("dac", dest);
    emit("snl");
    emit("jmp", Lc1clear);
    emit("jmp", Lc1set);
    emitLabel(Lc1set);
    emit("lac", "__one");
    emit("dac", carry1Cell);
    emit("jmp", Lc1done);
    emitLabel(Lc1clear);
    emit("lac", "__zero");
    emit("dac", carry1Cell);
    emitLabel(Lc1done);

    emit("cll");
    emit("lac", dest);
    emit("tad", carryCell);
    emit("dac", dest);
    std::string Lc2set = newLabel(), Lc2clear = newLabel(), Lc2done = newLabel();
    emit("snl");
    emit("jmp", Lc2clear);
    emit("jmp", Lc2set);
    emitLabel(Lc2set);
    emit("lac", "__one");
    emit("dac", carry2Cell);
    emit("jmp", Lc2done);
    emitLabel(Lc2clear);
    emit("lac", "__zero");
    emit("dac", carry2Cell);
    emitLabel(Lc2done);

    std::string Lorset = newLabel(), Lorclear = newLabel(), Lordone = newLabel();
    emit("lac", carry1Cell);
    emit("tad", carry2Cell);
    emit("sza");
    emit("jmp", Lorset);
    emit("jmp", Lorclear);
    emitLabel(Lorset);
    emit("lac", "__one");
    emit("dac", carryCell);
    emit("jmp", Lordone);
    emitLabel(Lorclear);
    emit("lac", "__zero");
    emit("dac", carryCell);
    emitLabel(Lordone);
}

void Pdp7Codegen::genWideAddSub(bool isAdd, int wordCount) {
    std::vector<std::string> rhsT(wordCount), lhsT(wordCount), resT(wordCount);
    for (auto &t : rhsT) {
        t = allocTemp();
        declareTemp(t);
    }
    for (auto &t : lhsT) {
        t = allocTemp();
        declareTemp(t);
    }
    for (auto &t : resT) {
        t = allocTemp();
        declareTemp(t);
    }
    for (int i = 0; i < wordCount; i++) {
        popAC();
        emit("dac", rhsT[i]);
    }
    for (int i = 0; i < wordCount; i++) {
        popAC();
        emit("dac", lhsT[i]);
    }

    std::string carry = allocTemp(), carry1 = allocTemp(), carry2 = allocTemp();
    declareTemp(carry);
    declareTemp(carry1);
    declareTemp(carry2);

    if (!isAdd) {
        // a - b = a + (~b + 1) - negate rhsT in place first, via the
        // same ripple-carry machinery below (starting carry=1, the
        // "+1" of 2's complement negation; `b` is `__zero` here
        // since there's nothing else to add).
        emit("lac", "__one");
        emit("dac", carry);
        for (int i = 0; i < wordCount; i++) {
            emit("lac", rhsT[i]);
            emit("cma");
            emit("dac", rhsT[i]);
            emitWideAddWithCarry(rhsT[i], rhsT[i], "__zero", carry, carry1, carry2);
        }
    }

    emit("lac", "__zero");
    emit("dac", carry);
    for (int i = 0; i < wordCount; i++) {
        emitWideAddWithCarry(resT[i], lhsT[i], rhsT[i], carry, carry1, carry2);
    }
    for (int i = wordCount - 1; i >= 0; i--) {
        emit("lac", resT[i]);
        pushAC();
    }
}

void Pdp7Codegen::genWideNegate(int wordCount) {
    std::vector<std::string> t(wordCount);
    for (auto &x : t) {
        x = allocTemp();
        declareTemp(x);
    }
    for (int i = 0; i < wordCount; i++) {
        popAC();
        emit("cma");
        emit("dac", t[i]);
    }
    std::string carry = allocTemp(), carry1 = allocTemp(), carry2 = allocTemp();
    declareTemp(carry);
    declareTemp(carry1);
    declareTemp(carry2);
    emit("lac", "__one");
    emit("dac", carry);
    for (int i = 0; i < wordCount; i++) {
        emitWideAddWithCarry(t[i], t[i], "__zero", carry, carry1, carry2);
    }
    for (int i = wordCount - 1; i >= 0; i--) {
        emit("lac", t[i]);
        pushAC();
    }
}

// Each word is independent - no carry chain needed. `|` is synthesized
// via De Morgan, word-by-word (see genBitwise()).
void Pdp7Codegen::genWideBitwise(const std::string &op, int wordCount) {
    std::vector<std::string> rhsT(wordCount), lhsT(wordCount);
    for (auto &t : rhsT) {
        t = allocTemp();
        declareTemp(t);
    }
    for (auto &t : lhsT) {
        t = allocTemp();
        declareTemp(t);
    }
    for (int i = 0; i < wordCount; i++) {
        popAC();
        emit("dac", rhsT[i]);
    }
    for (int i = 0; i < wordCount; i++) {
        popAC();
        emit("dac", lhsT[i]);
    }
    for (int i = wordCount - 1; i >= 0; i--) {
        if (op == "|") {
            emit("lac", lhsT[i]);
            emit("cma");
            emit("dac", "__stmp2");
            emit("lac", rhsT[i]);
            emit("cma");
            emit("and", "__stmp2");
            emit("cma");
        } else {
            emit("lac", lhsT[i]);
            emit((op == "&" ? "and" : "xor"), rhsT[i]);
        }
        pushAC();
    }
}

void Pdp7Codegen::genWideCompare(const std::string &op, int wordCount, bool isUnsigned) {
    std::vector<std::string> rhsT(wordCount), lhsT(wordCount);
    for (auto &t : rhsT) {
        t = allocTemp();
        declareTemp(t);
    }
    for (auto &t : lhsT) {
        t = allocTemp();
        declareTemp(t);
    }
    for (int i = 0; i < wordCount; i++) {
        popAC();
        emit("dac", rhsT[i]);
    }
    for (int i = 0; i < wordCount; i++) {
        popAC();
        emit("dac", lhsT[i]);
    }
    if (op == "==" || op == "!=") {
        std::string Ldiffer = newLabel(), Ldone = newLabel();
        for (int i = wordCount - 1; i >= 0; i--) {
            std::string Lcontinue = newLabel();
            emit("lac", lhsT[i]);
            emit("sad", rhsT[i]);
            emit("jmp", Lcontinue);
            emit("jmp", Ldiffer);
            emitLabel(Lcontinue);
        }
        emit("lac", (op == "==" ? "__one" : "__zero"));
        emit("jmp", Ldone);
        emitLabel(Ldiffer);
        emit("lac", (op == "==" ? "__zero" : "__one"));
        emitLabel(Ldone);
        return;
    }
    // `<`/`>=` (negate of `<`) - `>`/`<=` reduce to these by swapping
    // operands.
    bool negate = (op == ">=" || op == "<=");
    bool swap = (op == ">" || op == "<=");
    if (swap) std::swap(lhsT, rhsT);
    // Compare from the most significant word down, short-circuiting
    // once a word differs; only the top word's compare flips sign
    // (via the sign-bit-flip trick) when `isUnsigned`, since the sign
    // only lives in the top word and lower words are always unsigned
    // magnitude. Each word's compare goes through emitSignedLtCells()
    // rather than a plain subtract-and-test-sign, since the flip
    // trick routinely produces operands at opposite extremes.
    std::string Llt = newLabel(), Lge = newLabel(), Ldone = newLabel();
    for (int i = wordCount - 1; i >= 0; i--) {
        bool flip = (i == wordCount - 1) ? isUnsigned : true;
        std::string A = allocTemp(), B = allocTemp();
        declareTemp(A);
        declareTemp(B);
        emit("lac", lhsT[i]);
        if (flip) emit("xor", literalLabel(0400000L));
        emit("dac", A);
        emit("lac", rhsT[i]);
        if (flip) emit("xor", literalLabel(0400000L));
        emit("dac", B);
        if (i > 0) {
            std::string Lcontinue = newLabel();
            emit("lac", A);
            emit("sad", B);
            emit("jmp", Lcontinue);
            emitSignedLtCells(A, B, Llt, Lge);
            emitLabel(Lcontinue);
        } else {
            emitSignedLtCells(A, B, Llt, Lge);
        }
    }
    emitLabel(Llt);
    emit("lac", (negate ? "__zero" : "__one"));
    emit("jmp", Ldone);
    emitLabel(Lge);
    emit("lac", (negate ? "__one" : "__zero"));
    emitLabel(Ldone);
}

// Multiply and divide/modulo are not implemented for a wide operand:
// a genuine multi-word multiply/divide needs the ripple-carry
// machinery nested inside its own bit-serial or word-serial loop
// (shift-and-add / compare-shift-subtract), and this backend has no
// general-purpose register to hold the several words such a routine
// needs live at once.
void Pdp7Codegen::genWideMulDivErr(int line, const std::string &opName) {
    err(line, "'" + opName + "' on a 'long'/'long long' operand isn't supported on this backend (pdp7 has no "
                               "general-purpose register and no verified multi-word multiply/divide routine - "
                               "see Pdp7Codegen.cpp's own genWideMulDivErr() comment)");
}

// Only called when `e.type` is narrow (genExpr()'s Convert case
// dispatches to genWideExpr() when wide). A wide source contributes
// only its low word, discarding the rest; a narrow source just goes
// through narrowAC().
void Pdp7Codegen::genConvert(const ir::Expr &e) {
    if (isWide(e.lhs->type)) {
        genWideExpr(*e.lhs);
        popAC();  // AC = the low word - this is the value being kept
        std::string Tlow = allocTemp();
        declareTemp(Tlow);
        emit("dac", Tlow);
        int w = widthOfType(e.lhs->type);
        for (int i = 1; i < w; i++) popAC();  // discard every higher word
        emit("lac", Tlow);
        narrowAC(e.type);
        return;
    }
    genExpr(*e.lhs);
    narrowAC(e.type);
}

// Generalizes genExpr() for a wide-typed expression: leaves its
// `widthOfType(e.type)` words pushed on the stack, word 0 (low) on
// top, instead of in AC.
void Pdp7Codegen::genWideExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::IntLit: {
            pushWideLiteral(e.type, e.intValue);
            return;
        }
        case ir::ExprKind::Var: {
            emitLoadSlotAddress(e.slot);
            pushWideFromAddress(e.slot->type);
            return;
        }
        case ir::ExprKind::Index: {
            // computeElementAddress() scales by the element width and
            // leaves that element's word-0 address pushed.
            computeElementAddress(e.slot, *e.lhs);
            popAC();
            pushWideFromAddress(e.type);
            return;
        }
        case ir::ExprKind::DerefAssign: {
            // Same shape as a wide Assign's non-indexed branch, except
            // the store address comes from evaluating e.lhs (the
            // pointer, always narrow) instead of emitLoadSlotAddress().
            int w = widthOfType(e.type);
            genExpr(*e.lhs);  // AC = pointer value
            pushAC();          // protect it while evaluating rhs (which may itself call)
            genWideExpr(*e.rhs);
            std::vector<std::string> temps(w);
            for (auto &t : temps) {
                t = allocTemp();
                declareTemp(t);
            }
            for (auto &t : temps) {
                popAC();
                emit("dac", t);
            }
            popAC();  // AC = the pointer's own address
            emit("dac", "__addr");
            for (int i = 0; i < w; i++) {
                emit("lac", "__addr");
                if (i > 0) emit("tad", literalLabel(i));
                emit("dac", "__stmp2");
                emit("lac", temps[i]);
                emit("dac", "__stmp2 i");
            }
            for (int i = w - 1; i >= 0; i--) {
                emit("lac", temps[i]);
                pushAC();
            }
            return;
        }
        case ir::ExprKind::Assign: {
            int w = widthOfType(e.slot->type);
            std::vector<std::string> temps(w);
            for (auto &t : temps) {
                t = allocTemp();
                declareTemp(t);
            }
            if (e.lhs) {  // slot[idx] = rhs, a wide array element
                computeElementAddress(e.slot, *e.lhs);  // pushes the element's own address (1 word)
                genWideExpr(*e.rhs);                       // pushes w words, above that address
                for (auto &t : temps) {
                    popAC();
                    emit("dac", t);
                }
                popAC();  // AC = the element's own address
                emit("dac", "__addr");
            } else {
                genWideExpr(*e.rhs);
                for (auto &t : temps) {
                    popAC();
                    emit("dac", t);
                }
                emitLoadSlotAddress(e.slot);
                emit("dac", "__addr");
            }
            for (int i = 0; i < w; i++) {
                emit("lac", "__addr");
                if (i > 0) emit("tad", literalLabel(i));
                emit("dac", "__stmp2");
                emit("lac", temps[i]);
                emit("dac", "__stmp2 i");
            }
            for (int i = w - 1; i >= 0; i--) {
                emit("lac", temps[i]);
                pushAC();
            }
            return;
        }
        case ir::ExprKind::Unary: {
            if (e.op == "-") {
                genWideExpr(*e.rhs);
                genWideNegate(widthOfType(e.type));
                return;
            }
            if (e.op == "*") {
                // A wide pointee - the pointer itself is always a
                // narrow, single-word address.
                genExpr(*e.rhs);  // AC = pointer value
                pushWideFromAddress(e.type);
                return;
            }
            if (e.op == "~") {
                int w = widthOfType(e.type);
                genWideExpr(*e.rhs);
                std::vector<std::string> t(w);
                for (auto &x : t) {
                    x = allocTemp();
                    declareTemp(x);
                }
                for (int i = 0; i < w; i++) {
                    popAC();
                    emit("cma");
                    emit("dac", t[i]);
                }
                for (int i = w - 1; i >= 0; i--) {
                    emit("lac", t[i]);
                    pushAC();
                }
                return;
            }
            err(e.line, "internal: unary '" + e.op + "' on a wide ('long'/'long long') value isn't supported "
                                                        "on this backend");
        }
        case ir::ExprKind::Convert: {
            Type from = e.lhs->type;
            int toW = widthOfType(e.type);
            if (!isWide(from)) {
                genExpr(*e.lhs);
                std::string Tlo = allocTemp();
                declareTemp(Tlo);
                emit("dac", Tlo);
                std::string Text = allocTemp();
                declareTemp(Text);
                if (from.isUnsigned) {
                    emit("lac", "__zero");
                    emit("dac", Text);
                } else {
                    std::string Lneg = newLabel(), Lpos = newLabel(), Ldone = newLabel();
                    emit("lac", Tlo);
                    emit("sma");
                    emit("jmp", Lpos);
                    emit("jmp", Lneg);
                    emitLabel(Lneg);
                    emit("lac", literalLabel(0777777L));
                    emit("dac", Text);
                    emit("jmp", Ldone);
                    emitLabel(Lpos);
                    emit("lac", "__zero");
                    emit("dac", Text);
                    emitLabel(Ldone);
                }
                for (int i = 0; i < toW - 1; i++) {
                    emit("lac", Text);
                    pushAC();
                }
                emit("lac", Tlo);
                pushAC();
                return;
            }
            int fromW = widthOfType(from);
            genWideExpr(*e.lhs);
            std::vector<std::string> temps(fromW);
            for (auto &t : temps) {
                t = allocTemp();
                declareTemp(t);
            }
            for (auto &t : temps) {
                popAC();
                emit("dac", t);
            }
            if (toW <= fromW) {
                for (int i = toW - 1; i >= 0; i--) {
                    emit("lac", temps[i]);
                    pushAC();
                }
                return;
            }
            std::string Text = allocTemp();
            declareTemp(Text);
            if (from.isUnsigned) {
                emit("lac", "__zero");
                emit("dac", Text);
            } else {
                std::string Lneg = newLabel(), Lpos = newLabel(), Ldone = newLabel();
                emit("lac", temps[fromW - 1]);
                emit("sma");
                emit("jmp", Lpos);
                emit("jmp", Lneg);
                emitLabel(Lneg);
                emit("lac", literalLabel(0777777L));
                emit("dac", Text);
                emit("jmp", Ldone);
                emitLabel(Lpos);
                emit("lac", "__zero");
                emit("dac", Text);
                emitLabel(Ldone);
            }
            for (int i = 0; i < toW - fromW; i++) {
                emit("lac", Text);
                pushAC();
            }
            for (int i = fromW - 1; i >= 0; i--) {
                emit("lac", temps[i]);
                pushAC();
            }
            return;
        }
        case ir::ExprKind::Call: {
            // Same argument-passing convention as genExpr()'s Call
            // case. A wide callee's emitEpilogueAndReturn() leaves its
            // return words pushed at `__sp` as it stands right after
            // return - `totalWords` (the argument words) further down
            // than where argument-pushing left it, not where
            // `__sp += totalWords` alone would put it. So: pop the
            // return words from where they actually are first, undo
            // the argument push, then push the saved words back to
            // where the rest of this file expects a wide stack value.
            int totalWords = 0;
            for (size_t i = e.args.size(); i-- > 0;) {
                if (isWide(e.args[i]->type)) {
                    genWideExpr(*e.args[i]);
                    totalWords += widthOfType(e.args[i]->type);
                } else {
                    genExpr(*e.args[i]);
                    pushAC();
                    totalWords += 1;
                }
            }
            emit("jms", label(e.func));
            int returnWords = widthOfType(e.type);
            std::vector<std::string> retTemps(returnWords);
            for (auto &t : retTemps) {
                t = allocTemp();
                declareTemp(t);
            }
            for (auto &t : retTemps) {
                popAC();
                emit("dac", t);
            }
            if (totalWords > 0) {
                emit("lac", "__sp");
                emit("tad", literalLabel(static_cast<long>(totalWords)));
                emit("dac", "__sp");
            }
            for (int i = returnWords - 1; i >= 0; i--) {
                emit("lac", retTemps[i]);
                pushAC();
            }
            return;
        }
        case ir::ExprKind::Binary: {
            const std::string &op = e.op;
            if (op == "+" || op == "-") {
                genWideExpr(*e.lhs);
                genWideExpr(*e.rhs);
                genWideAddSub(op == "+", widthOfType(e.type));
                return;
            }
            if (op == "&" || op == "|" || op == "^") {
                genWideExpr(*e.lhs);
                genWideExpr(*e.rhs);
                genWideBitwise(op, widthOfType(e.type));
                return;
            }
            if (op == "*") genWideMulDivErr(e.line, "*");
            if (op == "/") genWideMulDivErr(e.line, "/");
            if (op == "%") genWideMulDivErr(e.line, "%");
            err(e.line, "internal: binary '" + op + "' on a wide ('long'/'long long') value isn't supported "
                                                       "on this backend");
        }
        default:
            err(e.line, "internal: this expression kind can't produce a wide ('long'/'long long') value on this "
                          "backend");
    }
}

void Pdp7Codegen::genStmt(const ir::Stmt &s) {
    switch (s.kind) {
        case ir::StmtKind::Block: {
            for (const auto &child : s.body) genStmt(*child);
            return;
        }
        case ir::StmtKind::If: {
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            // genCondBranch() fuses a plain comparison (or `&&` of
            // them) directly into the branch; anything else falls
            // back to evaluate-then-`sna` inside genCondBranch()
            // itself.
            genCondBranch(*s.cond, s.elseStmt ? Lelse : Ldone);
            genStmt(*s.thenStmt);
            if (s.elseStmt) {
                emit("jmp", Ldone);
                emitLabel(Lelse);
                genStmt(*s.elseStmt);
            }
            emitLabel(Ldone);
            return;
        }
        case ir::StmtKind::While: {
            std::string Lstart = newLabel();
            std::string Lend = newLabel();
            // `continue` jumps here if this loop has a post-statement
            // (a desugared for-loop's), so it still runs before the
            // condition is retested; otherwise it's just Lstart.
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
            emit("jmp", Lstart);
            emitLabel(Lend);
            return;
        }
        case ir::StmtKind::Break: {
            emit("jmp", loopLabels_.back().second);
            return;
        }
        case ir::StmtKind::Continue: {
            emit("jmp", loopLabels_.back().first);
            return;
        }
        case ir::StmtKind::Return: {
            bool hasValue = s.initOrReturnOrExpr != nullptr;
            if (hasValue) {
                if (isWide(s.initOrReturnOrExpr->type)) {
                    genWideExpr(*s.initOrReturnOrExpr);
                } else {
                    genExpr(*s.initOrReturnOrExpr);
                }
            }
            if (currentFunctionIsLeaf_) {
                // No frame teardown on this path - a wide value's
                // words stay exactly where genWideExpr left them.
                emitLeafReturn(hasValue);
            } else {
                emitEpilogueAndReturn(hasValue, hasValue ? s.initOrReturnOrExpr->type : Type{});
            }
            return;
        }
        case ir::StmtKind::LocalDecl: {
            if (s.declSlot->arraySize > 0) {
                emitLocalArrayInit(s.declSlot);
            } else if (isWide(s.declSlot->type)) {
                // A wide Local always takes the non-leaf, frame-based
                // path, addressed via emitLoadSlotAddress(); genExpr()'s
                // wide-collapsing-to-AC shortcut would drop all but
                // the low word, so this uses genWideExpr()/
                // popWideToAddress() directly.
                if (s.initOrReturnOrExpr) {
                    genWideExpr(*s.initOrReturnOrExpr);
                } else {
                    pushWideLiteral(s.declSlot->type, 0);
                }
                emitLoadSlotAddress(s.declSlot);
                popWideToAddress(s.declSlot->type);
            } else {
                // A leaf function's Local gets a static cell the one
                // time its declaration is generated; the assembler
                // already zero-initializes it (": 0"), so an
                // uninitialized scalar's cell needs no store here -
                // same as a non-leaf's frame slot, which simply isn't
                // written until something actually assigns it.
                if (currentFunctionIsLeaf_ || usesStaticCell(s.declSlot)) {
                    trailer_.push_back(leafLabel(s.declSlot) + ": 0");
                }
                if (s.initOrReturnOrExpr) {
                    genExpr(*s.initOrReturnOrExpr);
                    emitStoreSlot(s.declSlot);
                }
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
            // Verbatim passthrough - the text between the braces goes
            // straight into the generated assembly, unmodified.
            emitVerbatim(s.asmText);
            return;
        }
        case ir::StmtKind::Label: {
            emitLabel(irLabel(s.labelId));
            return;
        }
        case ir::StmtKind::Goto: {
            emit("jmp", irLabel(s.labelId));
            return;
        }
    }
}

void Pdp7Codegen::genExpr(const ir::Expr &e) {
    // genExpr()'s contract is "leaves the result in AC" - a wide
    // result can't fit there, so this computes it via genWideExpr()
    // and collapses to just the low word in AC, discarding the rest.
    // Callers that need the full wide value (LocalDecl's initializer,
    // Return) call genWideExpr() directly instead.
    if (isWide(e.type)) {
        genWideExpr(e);
        int w = widthOfType(e.type);
        std::string Tlow = allocTemp();
        declareTemp(Tlow);
        popAC();
        emit("dac", Tlow);
        for (int i = 1; i < w; i++) popAC();
        emit("lac", Tlow);
        return;
    }
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit: {
            emit("lac", literalLabel(e.intValue));
            return;
        }
        case ir::ExprKind::Var: {
            // IRBuilder rejects a bare array Var outside of `&`, so
            // e.slot is always a scalar here.
            emitLoadSlot(e.slot);
            return;
        }
        case ir::ExprKind::Index: {
            genIndex(e);
            return;
        }
        case ir::ExprKind::Assign: {
            if (e.lhs) {  // slot[idx] = rhs
                if (auto direct = directElementOperand(e.slot, *e.lhs)) {
                    // The target address is a compile-time constant
                    // baked into the store instruction's own operand -
                    // nothing rhs's own evaluation could do (including
                    // a nested call) changes it, so there's no address
                    // to protect across that evaluation the way the
                    // two branches below both need to.
                    genExpr(*e.rhs);
                    emit("dac", *direct);
                    return;
                }
                if (isCallFree(*e.rhs)) {
                    computeElementAddressAC(e.slot, *e.lhs);
                    std::string T = allocTemp();
                    declareTemp(T);
                    emit("dac", T);
                    genExpr(*e.rhs);
                    emit("dac", cat(T, " i"));
                    return;
                }
                computeElementAddress(e.slot, *e.lhs);  // pushes the element's own address
                genExpr(*e.rhs);                          // may itself call - the address is safe on the stack
                storeThroughStackedAddress();              // dac leaves AC unchanged
                return;
            }
            genExpr(*e.rhs);
            emitStoreSlot(e.slot);  // leaves AC unchanged
            return;
        }
        case ir::ExprKind::Unary: {
            if (e.op == "-") {
                genExpr(*e.rhs);
                emit("cma");
                emit("tad", "__one");
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                genExpr(*e.rhs);  // AC = pointer value
                emit("dac", "__addr");
                emit("lac", "__addr i");
            } else if (e.op == "~") {
                genExpr(*e.rhs);
                emit("cma");
            } else {  // "&"
                genAddrOf(*e.rhs);
            }
            return;
        }
        case ir::ExprKind::DerefAssign: {
            genExpr(*e.lhs);  // AC = pointer value
            if (isCallFree(*e.rhs)) {
                std::string T = allocTemp();
                declareTemp(T);
                emit("dac", T);
                genExpr(*e.rhs);
                emit("dac", cat(T, " i"));
                return;
            }
            pushAC();          // protect it - evaluating rhs may itself call
            genExpr(*e.rhs);  // AC = value to store
            storeThroughStackedAddress();
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            if (e.lhs) {  // slot[idx] <op>= rhs (including slot[idx]++/--)
                if (auto direct = directElementOperand(e.slot, *e.lhs)) {
                    genCompoundThroughDirectOperand(e, *direct);
                    return;
                }
                if (isCallFree(*e.rhs)) {
                    computeElementAddressAC(e.slot, *e.lhs);
                    genCompoundThroughCell(e);
                    return;
                }
                computeElementAddress(e.slot, *e.lhs);  // pushes the element's own address
                genCompoundThroughStackedAddress(e);
                return;
            }
            // Plain scalar target - its own address (a Global's label,
            // or a Local/Param's `__fp`-relative offset) is cheap
            // enough to recompute from scratch after rhs's own
            // evaluation, the same way plain Assign's own non-indexed
            // branch above already does, so there's nothing here that
            // needs to survive across a nested call the way an
            // indexed/deref target's own sub-expression does.
            genExpr(*e.rhs);  // AC = rhs
            std::string rhsCell = prepareCompoundRhs(e);
            emitLoadSlot(e.slot);  // AC = old
            if (e.yieldOld) pushAC();
            finishCompoundOp(e, rhsCell);
            narrowAC(e.type);
            emitStoreSlot(e.slot);  // leaves AC (the new value) unchanged
            if (e.yieldOld) popAC();
            return;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            genExpr(*e.lhs);  // AC = pointer value
            if (isCallFree(*e.rhs)) {
                genCompoundThroughCell(e);
                return;
            }
            pushAC();          // protect it - evaluating rhs may itself call
            genCompoundThroughStackedAddress(e);
            return;
        }
        case ir::ExprKind::Ternary: {
            // Mirrors StmtKind::If's own label/branch shape exactly,
            // using genExpr (not genStmt) for both branches, since a
            // ternary produces a value rather than running a
            // statement.
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            genCondBranch(*e.lhs, Lelse);
            genExpr(*e.rhs);
            emit("jmp", Ldone);
            emitLabel(Lelse);
            genExpr(*e.elseExpr);
            emitLabel(Ldone);
            return;
        }
        case ir::ExprKind::Call: {
            // See Pdp7Codegen.h's own fast-call eligibility comment.
            // Every argument must itself be call-free: evaluating one
            // argument can't be allowed to reenter this same callee
            // and overwrite an EARLIER argument's cell before the
            // call below ever runs (the real stack-based convention
            // below doesn't have this hazard - each pending call gets
            // its own stack region - which is exactly what this fast
            // path trades away for skipping it). Narrow-only
            // (!isWide) on top of isFastCallEligible()'s own
            // parameter-shape check: a wide ARGUMENT can't fit through
            // one scalar cell either, even passed to an otherwise-
            // eligible callee.
            if (isFastCallEligible(e.func) &&
                std::all_of(e.args.begin(), e.args.end(),
                            [this](const ir::ExprPtr &a) { return isCallFree(*a) && !isWide(a->type); })) {
                for (size_t i = 0; i < e.args.size(); i++) {
                    genExpr(*e.args[i]);
                    emit("dac", cat(label(e.func), "_P", std::to_string(i)));
                }
                emit("jms", cat(label(e.func), "_fast"));
                return;
            }
            // Arguments are pushed in reverse order so param 0 ends up
            // closest to the callee's `__fp`; the callee reads every
            // Param off this stack, never a fixed per-function cell,
            // so recursive calls never collide. Each argument may
            // itself contain a call safely, since every earlier
            // argument is already on the stack by the time a later
            // one evaluates.
            int totalWords = 0;
            for (size_t i = e.args.size(); i-- > 0;) {
                if (isWide(e.args[i]->type)) {
                    genWideExpr(*e.args[i]);
                    totalWords += widthOfType(e.args[i]->type);
                } else {
                    genExpr(*e.args[i]);
                    pushAC();
                    totalWords += 1;
                }
            }
            emit("jms", label(e.func));
            // The callee's epilogue leaves AC holding the return value
            // and `__sp` right where it was after the arguments were
            // pushed - caller-cleans-up convention, so `__retval`
            // protects the return value in AC while `__sp` is adjusted
            // back past `totalWords` (the real word count pushed,
            // since a wide argument pushes several words).
            if (totalWords > 0) {
                emit("dac", "__retval");
                emit("lac", "__sp");
                emit("tad", literalLabel(static_cast<long>(totalWords)));
                emit("dac", "__sp");
                emit("lac", "__retval");
            }
            return;
        }
        case ir::ExprKind::CallPtr: {
            // e.args is always empty - IRBuilder.cpp rejects any
            // argument on this kind, since there's no ABI slot for one
            // until the callee is known, which it never is here.
            genExpr(*e.lhs);  // AC = the callee's entry address
            emit("dac", "__addr");
            emit("jms", "__addr i");
            return;
        }
        case ir::ExprKind::Binary: {
            genBinary(e);
            return;
        }
        case ir::ExprKind::Intrinsic: {
            // ion/iof aren't in pdp7as's builtin opcode table; emitting
            // the literal instruction word directly avoids needing to
            // link against the kernel's own definition of them.
            emit((e.op == "ion" ? "0700042" : "0700002"), cat(" ; ", e.op));
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

std::optional<std::string> Pdp7Codegen::simpleCell(const ir::Expr &e) {
    if (e.kind == ir::ExprKind::IntLit) return literalLabel(e.intValue);
    if (e.kind != ir::ExprKind::Var || isWide(e.type)) return std::nullopt;
    const ir::Slot *slot = e.slot;
    if (!isPointer(slot->type) && slot->type.base == BaseType::Char) return std::nullopt;  // needs narrowAC()
    if (slot->kind == ir::SlotKind::Global) return label(slot);
    if (currentFunctionIsLeaf_ || usesStaticCell(slot)) return leafLabel(slot);
    return std::nullopt;
}

bool Pdp7Codegen::isCallFree(const ir::Expr &e) {
    if (e.kind == ir::ExprKind::Call || e.kind == ir::ExprKind::CallPtr) return false;
    for (const ir::Expr *child : {e.lhs.get(), e.rhs.get(), e.elseExpr.get()}) {
        if (child && !isCallFree(*child)) return false;
    }
    for (const auto &arg : e.args) {
        if (!isCallFree(*arg)) return false;
    }
    return true;
}

std::string Pdp7Codegen::genOperandPair(const ir::Expr &lhs, const ir::Expr &rhs, RhsXform xform,
                                        const std::string &dest) {
    if (dest.empty()) {
        if (rhs.kind == ir::ExprKind::IntLit) {
            long v = rhs.intValue;
            if (xform == RhsXform::Negate) v = -v;
            if (xform == RhsXform::Complement) v = ~v;
            genExpr(lhs);
            return literalLabel(v);
        }
        if (xform == RhsXform::None) {
            if (auto cell = simpleCell(rhs)) {
                genExpr(lhs);
                return *cell;
            }
        }
    }
    std::string cell = dest.empty() ? "__stmp2" : dest;
    auto finishRhs = [&]() {
        if (xform == RhsXform::Negate) {
            emit("cma");
            emit("tad", "__one");
        }
        if (xform == RhsXform::Complement) emit("cma");
        emit("dac", cell);
    };
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
        emit("dac", T);
        genExpr(rhs);
        finishRhs();
        emit("lac", T);
        return cell;
    }
    pushAC();
    genExpr(rhs);
    finishRhs();
    popAC();
    return cell;
}

// A Global array's own element address, for a compile-time-constant
// index, is itself a compile-time constant (the array's link-time
// address plus a literal offset) - `label+offset` lets the assembler
// resolve it the same way it resolves any other symbol (confirmed
// directly: `RccAsm.g4`'s grammar and `Pdp7Assembler.cpp`'s own
// `evalExpr()` already combine a signed syllable via real addition,
// not just the unsigned-syllable OR convention), so the element can be
// read/written with one ordinary memory-reference instruction instead
// of computing its address at runtime first (`law`+`and`+`tad`, then a
// separate `dac __addr; lac __addr i` to actually use it). Returns
// nullopt for anything else - a Local/Param array (genuinely `__fp`-
// relative, computed fresh every call) or a pointer variable (its
// base is a runtime VALUE, not its own fixed storage address; see
// `computeElementAddress()`'s own comment) - where this doesn't apply.
std::optional<std::string> Pdp7Codegen::directElementOperand(const ir::Slot *slot, const ir::Expr &idxExpr) {
    if (slot->kind != ir::SlotKind::Global || slot->arraySize <= 0) return std::nullopt;
    if (idxExpr.kind != ir::ExprKind::IntLit) return std::nullopt;
    long offset = idxExpr.intValue * widthOfType(slot->type);
    if (offset == 0) return label(slot);
    // A leading syllable with no explicit sign combines via OR, not
    // addition (RccAsm.g4's own "unsigned means OR" default,
    // Pdp7Assembler.cpp's `char op = syl.sign ? syl.sign : '|';`) -
    // correct for this backend's own "jmp i F3"-style combination of
    // disjoint bit ranges, but wrong here: when `slot` is defined in a
    // different translation unit than this reference (confirmed
    // directly - this exact shape broke `namei()`'s own real
    // `dnode[D_NAME+N]`/`inode[...]` accesses against `sys/s8.c`'s
    // `inode`/`dnode`, both defined in a different file), the
    // assembler records that OR, not a real add, as the relocation
    // it hands the linker - silently wrong whenever the real linked
    // address and the literal offset don't happen to combine cleanly
    // under OR. An explicit leading '+' makes the relocation (and the
    // ordinary same-file case, where no relocation is needed at all
    // and this is a no-op) use real addition instead.
    return "+" + label(slot) + "+" + std::to_string(offset);
}

// &slot[idxExpr], pushed on the stack. slot is the array itself when
// it's a declared array (slot->arraySize > 0); for a plain pointer
// variable the base address to index from is its value (lac), not
// its storage address (law) - indexing a pointer parameter needs this
// distinction.
void Pdp7Codegen::computeElementAddress(const ir::Slot *slot, const ir::Expr &idxExpr) {
    computeElementAddressAC(slot, idxExpr);
    pushAC();
}

// The base address never calls or touches `__stmp2`, so the scaled
// index waits there while it's computed.
void Pdp7Codegen::computeElementAddressAC(const ir::Slot *slot, const ir::Expr &idxExpr) {
    int elemW = widthOfType(slot->type);
    if (idxExpr.kind == ir::ExprKind::IntLit) {
        long offset = idxExpr.intValue * elemW;
        emitSlotBaseAddress(slot);
        if (offset != 0) emit("tad", literalLabel(offset));
        return;
    }
    genExpr(idxExpr);  // AC = index (may itself call)
    // Scaled by the element width as elemW-1 self-adds rather than a
    // real multiply, cheap for widths 1/2/4.
    if (elemW > 1) {
        std::string Tidx = allocTemp();
        declareTemp(Tidx);
        emit("dac", Tidx);
        for (int i = 1; i < elemW; i++) emit("tad", Tidx);
    }
    emit("dac", "__stmp2");
    emitSlotBaseAddress(slot);
    emit("tad", "__stmp2");
}

void Pdp7Codegen::genIndex(const ir::Expr &e) {
    if (auto direct = directElementOperand(e.slot, *e.lhs)) {
        emit("lac", *direct);
        return;
    }
    computeElementAddressAC(e.slot, *e.lhs);
    emit("dac", "__addr");
    emit("lac", "__addr i");
}

// &operand: a plain variable/array, an array element, or a bare
// function name (whose entry label is the same address a direct
// `jms` call targets).
void Pdp7Codegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        emitLoadSlotAddress(operand.slot);
        return;
    }
    if (operand.kind == ir::ExprKind::Index) {
        computeElementAddressAC(operand.slot, *operand.lhs);
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        // See emitLoadSlotAddress's comment: law bakes its own opcode bits
        // into the result.
        emit("law", label(operand.func));
        emit("and", literalLabel(kAddressFieldLimit - 1));
        return;
    }
    err(operand.line, "internal: unhandled addressable-operand kind");
}

std::string Pdp7Codegen::prepareCompoundRhs(const ir::Expr &e) {
    const std::string &op = e.op;
    if (op == "+") {
        emit("dac", "__stmp2");
        return "__stmp2";
    }
    if (op == "-") {
        emit("cma");
        emit("tad", "__one");
        emit("dac", "__stmp2");
        return "__stmp2";
    }
    if (op == "&" || op == "^") {
        emit("dac", "__stmp2");
        return "__stmp2";
    }
    if (op == "|") {
        emit("cma");
        emit("dac", "__stmp2");
        return "__stmp2";
    }
    if (op == "*") {
        // Not declared via declareTemp(): same inline-operand
        // convention as genMul() - the "<Tr>: 0" declaration happens
        // in finishCompoundOp(), right after the "mul" it's the
        // operand word for.
        std::string Tr = allocTemp();
        emit("dac", Tr);
        return Tr;
    }
    if (op == "/" || op == "%") {
        emit("dac", "__dm_b");
        return "__dm_b";
    }
    if (op == "<<" || op == ">>") {
        std::string N = allocTemp();
        declareTemp(N);
        emit("dac", N);
        return N;
    }
    err(e.line, "internal: unhandled compound-assignment operator '" + op + "'");
}

void Pdp7Codegen::finishCompoundOp(const ir::Expr &e, const std::string &rhsCell) {
    const std::string &op = e.op;
    if (op == "+" || op == "-") {
        emit("tad", rhsCell);
        return;
    }
    if (op == "&" || op == "^") {
        emit((op == "&" ? "and" : "xor"), rhsCell);
        return;
    }
    if (op == "|") {
        emit("cma");
        emit("and", rhsCell);
        emit("cma");
        return;
    }
    if (op == "*") {
        emit("cll");
        emit("mul");
        emitRaw(cat(rhsCell, ": 0"));
        emit("lacq");
        return;
    }
    if (op == "/" || op == "%") {
        emit("dac", "__dm_a");
        callDivMod(e.type.isUnsigned);
        if (op == "%") emit("lac", "__dm_a");
        return;
    }
    if (op == "<<" || op == ">>") {
        std::string V = allocTemp();
        declareTemp(V);
        emit("dac", V);
        std::string Lstart = newLabel(), Lbody = newLabel(), Lend = newLabel();
        emitLabel(Lstart);
        emit("lac", rhsCell);
        emit("sza");
        emit("jmp", Lbody);
        emit("jmp", Lend);
        emitLabel(Lbody);
        if (op == "<<") {
            emit("lac", V);
            emit("tad", V);
            emit("dac", V);
        } else if (e.type.isUnsigned) {
            emit("lac", V);
            emit("cll");
            emit("rar");
            emit("dac", V);
        } else {
            std::string Lpos = newLabel(), Lneg = newLabel(), Lshift = newLabel();
            emit("lac", V);
            emit("sma");
            emit("jmp", Lpos);
            emit("jmp", Lneg);
            emitLabel(Lpos);
            emit("cll");
            emit("jmp", Lshift);
            emitLabel(Lneg);
            emit("stl");
            emitLabel(Lshift);
            emit("lac", V);
            emit("rar");
            emit("dac", V);
        }
        emit("lac", "__zero");
        emit("cma");
        emit("tad", rhsCell);
        emit("dac", rhsCell);
        emit("jmp", Lstart);
        emitLabel(Lend);
        emit("lac", V);
        return;
    }
    err(e.line, "internal: unhandled compound-assignment operator '" + op + "'");
}

// Same as genCompoundThroughCell()/genCompoundThroughStackedAddress(),
// but `operand` is already a direct, non-indirect memory reference
// (a Global array element at a compile-time-constant address) - no
// address to stage into a cell or protect across rhs's own
// evaluation, so this reads and writes it directly.
void Pdp7Codegen::genCompoundThroughDirectOperand(const ir::Expr &e, const std::string &operand) {
    genExpr(*e.rhs);
    std::string rhsCell = prepareCompoundRhs(e);
    emit("lac", operand);  // AC = old
    if (e.yieldOld) pushAC();
    finishCompoundOp(e, rhsCell);
    narrowAC(e.type);
    emit("dac", operand);
    if (e.yieldOld) popAC();
}

void Pdp7Codegen::genCompoundThroughCell(const ir::Expr &e) {
    std::string T = allocTemp();
    declareTemp(T);
    emit("dac", T);
    genExpr(*e.rhs);
    std::string rhsCell = prepareCompoundRhs(e);
    emit("lac", cat(T, " i"));  // AC = old
    if (e.yieldOld) pushAC();
    finishCompoundOp(e, rhsCell);
    narrowAC(e.type);
    emit("dac", cat(T, " i"));
    if (e.yieldOld) popAC();
}

void Pdp7Codegen::genCompoundThroughStackedAddress(const ir::Expr &e) {
    genExpr(*e.rhs);  // may itself call - the address is safe on the stack
    std::string rhsCell = prepareCompoundRhs(e);
    std::string addr = popAddressIntoScratch();
    emit("lac", cat(addr, " i"));  // AC = old
    if (e.yieldOld) pushAC();
    finishCompoundOp(e, rhsCell);
    narrowAC(e.type);
    emit("dac", cat(addr, " i"));
    if (e.yieldOld) popAC();
}

void Pdp7Codegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    if (op == "+" || op == "-") {
        std::string rhsCell = genOperandPair(*e.lhs, *e.rhs, op == "-" ? RhsXform::Negate : RhsXform::None);
        emit("tad", rhsCell);
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

void Pdp7Codegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs) {
    // A comparison's result type is always narrow Int even when its
    // operands are wide, so genExpr()'s isWide(e.type) guard doesn't
    // catch this - dispatch on the operands themselves here.
    if (isWide(lhs.type)) {
        genWideExpr(lhs);
        genWideExpr(rhs);
        genWideCompare(op, widthOfType(lhs.type), lhs.type.isUnsigned);
        return;
    }
    if (op == "==") return emitEq(lhs, rhs, /*negate=*/false);
    if (op == "!=") return emitEq(lhs, rhs, /*negate=*/true);
    if (op == "<") return emitLt(lhs, rhs, /*negate=*/false);
    if (op == ">=") return emitLt(lhs, rhs, /*negate=*/true);
    if (op == ">") return emitLt(rhs, lhs, /*negate=*/false);
    if (op == "<=") return emitLt(rhs, lhs, /*negate=*/true);
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

// Skip/branch primitives used below:
//   sad T   - skip next instruction if AC != M[T]  ("skip if different")
//   sma     - skip next instruction if AC is negative
//   sza     - skip next instruction if AC == 0

void Pdp7Codegen::emitEq(const ir::Expr &lhs, const ir::Expr &rhs, bool negate) {
    std::string rhsCell = genOperandPair(lhs, rhs, RhsXform::None);
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    emit("sad", rhsCell);
    emit("jmp", Ltrue);
    emit("jmp", Lfalse);
    emitLabel(Ltrue);
    emit("lac", "__one");
    emit("jmp", Ldone);
    emitLabel(Lfalse);
    emit("lac", "__zero");
    emitLabel(Ldone);
    if (negate) emit("xor", "__one");
}

void Pdp7Codegen::emitSignedLtCells(const std::string &A, const std::string &B, const std::string &Ltrue,
                                     const std::string &Lfalse, int signA, int signB, const std::string &negB,
                                     bool bIsZero) {
    // A<0 is a pure sign test on A, independent of B entirely (any
    // "same-sign subtract" of A-0 would just re-derive A's own sign a
    // second time) - skips the whole dispatch/subtract machinery
    // below. Always does the real runtime test on A, even when signA
    // claims a known sign: unlike every other dispatch in this
    // function (which only ever uses a known sign to skip a
    // *redundant* pre-check ahead of a subtraction that still reads
    // A/B live), this is the one case where the "redundant" pre-check
    // and the real check are the exact same instruction - fully
    // trusting signA here would mean trusting it with NO live check
    // left at all, which a provability bug upstream (a slot wrongly
    // marked knownNonNegative) could turn into a silent wrong answer
    // instead of just a missed optimization.
    if (bIsZero) {
        emit("lac", A);
        emit("sma");
        emit("jmp", Lfalse);
        emit("jmp", Ltrue);
        return;
    }
    // Neither operand's sign is known: the inline sequence below would
    // need both of signA's dispatch arms *and* B's own sign dispatch on
    // each of them (branchOnB() can't shortcut when signB == -1 either)
    // - 21 words, all duplicated at every such call site with no sharing
    // between them. Past the first use anywhere in the program, routing
    // through one shared `__cmp_lt` subroutine (pdp7RuntimeAsm()) is
    // smaller: ~8 words here (move A/B into its fixed operand cells,
    // `jms`, branch on the 0/1 result) against a one-time ~25-word
    // subroutine body, instead of 21 words inlined again at every site.
    // Not worth it when either sign IS known (those dispatches are
    // already cheap - see below - and would need their own subroutine
    // variants to still skip the now-unneeded sign test).
    if (signA == -1 && signB == -1) {
        needGenericLt_ = true;
        emit("lac", A);
        emit("dac", "__cmpA");
        emit("lac", B);
        emit("dac", "__cmpB");
        emit("jms", "__cmp_lt");
        emit("sna");
        emit("jmp", Lfalse);
        emit("jmp", Ltrue);
        return;
    }
    std::string Lsub = newLabel();
    bool subUsed = false;
    // What `branchOnB(ifNonNeg, ifNeg)` below would jump to, when B's sign
    // is statically known (signB == 0 or 1): a pure function of its two
    // arguments and signB, with no runtime component at all. A caller that
    // would otherwise jump to a label whose entire body is this one
    // collapsed `jmp` can use this to jump straight to the real target
    // instead, skipping that label (and the two instructions needed to
    // reach it) entirely.
    auto resolvedBranchOnB = [&](const std::string &ifNonNeg,
                                  const std::string &ifNeg) -> std::optional<std::string> {
        if (ifNonNeg == Lsub || ifNeg == Lsub) subUsed = true;
        if (signB == 0) return ifNonNeg;
        if (signB == 1) return ifNeg;
        return std::nullopt;
    };
    // Jumps to `ifNonNeg`/`ifNeg` on B's sign.
    auto branchOnB = [&](const std::string &ifNonNeg, const std::string &ifNeg) {
        if (auto direct = resolvedBranchOnB(ifNonNeg, ifNeg)) {
            emit("jmp", *direct);
        } else {
            emit("lac", B);
            emit("sma");
            emit("jmp", ifNonNeg);
            emit("jmp", ifNeg);
        }
    };
    // A<0<=B is true and A>=0>B false; same-sign operands subtract.
    if (signA == 1) {
        branchOnB(Ltrue, Lsub);
    } else if (signA == 0) {
        branchOnB(Lsub, Lfalse);
    } else {
        // Resolve both arms before emitting A's own sign dispatch: when
        // signB is known, both resolve (resolution depends only on signB,
        // not on which arm), so neither LaNeg nor LaPos - and the relay
        // jump each would otherwise be - is ever created; A's dispatch
        // jumps straight to the real destination. When signB is unknown,
        // neither resolves and this falls back to the original two-label
        // relay form, since branchOnB genuinely needs B's own runtime sign
        // check in that case.
        std::optional<std::string> direct1 = resolvedBranchOnB(Ltrue, Lsub);
        std::optional<std::string> direct2 = resolvedBranchOnB(Lsub, Lfalse);
        std::string LaNeg = direct1 ? std::string() : newLabel();
        std::string LaPos = direct2 ? std::string() : newLabel();
        emit("lac", A);
        emit("sma");
        emit("jmp", direct2 ? *direct2 : LaPos);
        emit("jmp", direct1 ? *direct1 : LaNeg);
        if (!direct1) {
            emitLabel(LaNeg);
            branchOnB(Ltrue, Lsub);
        }
        if (!direct2) {
            emitLabel(LaPos);
            branchOnB(Lsub, Lfalse);
        }
    }
    if (!subUsed) return;
    emitLabel(Lsub);
    if (!negB.empty()) {
        // B's negation is already a precomputed literal (B is itself a
        // compile-time constant) - add it directly instead of negating
        // B's cell at runtime.
        emit("lac", A);
        emit("tad", negB);
    } else {
        emit("lac", B);
        emit("cma");
        emit("tad", "__one");
        emit("dac", "__stmp2");
        emit("lac", A);
        emit("tad", "__stmp2");  // AC = A-B, safe: same sign, can't overflow
    }
    emit("sma");
    emit("jmp", Lfalse);
    emit("jmp", Ltrue);
}

void Pdp7Codegen::genCompareCells(const ir::Expr &a, const ir::Expr &b, bool unsignedCmp, std::string &A,
                                  std::string &B, int &signA, int &signB, std::string *negB, bool *bIsZero) {
    signA = signB = -1;
    if (bIsZero) *bIsZero = false;
    // B ends up as a literal cell (below) exactly when `b` is itself a
    // compile-time constant - precompute its negation the same way, so
    // emitSignedLtCells()'s same-sign subtract can add it directly
    // instead of negating B's cell at runtime.
    if (negB) negB->clear();
    if (b.kind == ir::ExprKind::IntLit) {
        long w = pdp7Word(b.intValue);
        if (unsignedCmp) w ^= 0400000L;
        if (negB) *negB = literalLabel(-w);
        if (bIsZero) *bIsZero = (w == 0);
    }
    auto flip = [&]() {
        if (unsignedCmp) emit("xor", literalLabel(0400000L));
    };
    // Whether `e`'s sign is knowable with no runtime cost at all - a
    // literal, a slot the induction-nonneg pass proved non-negative (see
    // IR.h's own `knownNonNegative`), or a boolean-valued expression (a
    // comparison/logical operator, or `!`), which always evaluates to 0 or
    // 1 regardless of its own operands' values. Only consulted as the
    // *other* operand in ready()'s own gate below, never to make `e` itself
    // a ready cell - simpleCell() still can't produce one for a Binary/
    // Unary node, so this only ever changes whether a sibling operand's
    // already-known sign gets applied, never what that sign is.
    static const std::unordered_set<std::string> kBoolOps = {"==", "!=", "<", "<=", ">", ">=", "&&", "||"};
    auto hasStaticSign = [&](const ir::Expr &e) {
        if (e.kind == ir::ExprKind::IntLit) return true;
        if (e.kind == ir::ExprKind::Var && e.slot && e.slot->knownNonNegative) return true;
        if (e.kind == ir::ExprKind::Unary && e.op == "!") return true;
        if (e.kind == ir::ExprKind::Binary && kBoolOps.count(e.op)) return true;
        return false;
    };
    // A cell already holding the operand's comparison value: a
    // constant (flipped at compile time) or, for a signed comparison,
    // a plain variable.
    auto ready = [&](const ir::Expr &e, int &sign) -> std::optional<std::string> {
        if (e.kind == ir::ExprKind::IntLit) {
            long w = pdp7Word(e.intValue);
            if (unsignedCmp) w ^= 0400000L;
            sign = (w & 0400000L) ? 1 : 0;
            return literalLabel(w);
        }
        if (unsignedCmp) return std::nullopt;
        // A slot the induction-nonneg pass proved always non-negative
        // (see InductionNonNegative.h) gets the same known-sign treatment
        // a literal does, letting emitSignedLtCells() skip its dynamic
        // sign dispatch for this operand - but only when the *other*
        // operand is also statically sign-known. Otherwise
        // emitSignedLtCells()'s own "either sign known" branch inlines a
        // dedicated subtract for this call site, which costs more than
        // the shared `__cmp_lt` subroutine the fully-unknown case already
        // routes through when the other operand needs its own runtime
        // sign check anyway - no sharing is gained by knowing just one
        // side here.
        if (e.kind == ir::ExprKind::Var && e.slot && e.slot->knownNonNegative && hasStaticSign(&e == &a ? b : a)) {
            sign = 0;
        }
        return simpleCell(e);
    };
    auto toTemp = [&]() {
        std::string T = allocTemp();
        declareTemp(T);
        emit("dac", T);
        return T;
    };
    std::optional<std::string> ra = ready(a, signA), rb = ready(b, signB);
    if (ra && rb) {
        A = *ra;
        B = *rb;
        return;
    }
    if (rb) {
        B = *rb;
        genExpr(a);
        flip();
        A = toTemp();
        return;
    }
    if (ra) {
        A = *ra;
        genExpr(b);
        flip();
        B = toTemp();
        return;
    }
    genExpr(a);
    flip();
    if (isCallFree(b)) {
        A = toTemp();
        genExpr(b);
        flip();
        B = toTemp();
        return;
    }
    pushAC();  // a call in `b` could re-enter this function and reuse a static temp
    genExpr(b);
    flip();
    B = toTemp();
    popAC();
    A = toTemp();
}

void Pdp7Codegen::emitLt(const ir::Expr &lhs, const ir::Expr &rhs, bool negate) {
    // Unsigned path: XOR each side's sign bit (0400000, bit 17)
    // before the signed-less-than test, mapping unsigned order onto
    // signed order. Delegates to emitSignedLtCells() rather than a
    // plain subtract-and-test-sign, since the flip routinely produces
    // operands at opposite extremes, which a naive subtract overflows.
    std::string A, B, negB;
    int signA, signB;
    bool bIsZero;
    genCompareCells(lhs, rhs, lhs.type.isUnsigned, A, B, signA, signB, &negB, &bIsZero);
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    emitSignedLtCells(A, B, Ltrue, Lfalse, signA, signB, negB, bIsZero);
    emitLabel(Ltrue);
    emit("lac", "__one");
    emit("jmp", Ldone);
    emitLabel(Lfalse);
    emit("lac", "__zero");
    emitLabel(Ldone);
    if (negate) emit("xor", "__one");
}

// See Pdp7Codegen.h's comment for what this is for. Each comparison
// computes AC = a - b (reusing emitLt()/emitEq()'s own instruction
// choices), then branches on that raw result directly instead of
// materializing a 0/1 value first: `<`/`>` use `sma` (skip if AC<0,
// swapping operands for `>`); `>=`/`<=`/`==` use a 3-word jump-either-
// way shape since `spa` is unused/unverified elsewhere in this file;
// `!=` uses `sad`'s natural "skip if different" sense directly.
// `&&` recurses, short-circuiting to the same `Lfalse`. `||` and
// anything else (`!`, a bare variable, a call, bitwise, ...) fall
// through to the generic path: evaluate normally, test the 0/1 value.
void Pdp7Codegen::genCondBranch(const ir::Expr &cond, const std::string &Lfalse) {
    // `!A` as a false-branching condition is exactly "jump to Lfalse
    // when A is true" - genCondBranchOnTrue()'s own job - instead of
    // materializing A's negation to a 0/1 value first (genLogicalNot())
    // and testing THAT.
    if (cond.kind == ir::ExprKind::Unary && cond.op == "!") {
        genCondBranchOnTrue(*cond.rhs, Lfalse);
        return;
    }
    if (cond.kind == ir::ExprKind::Binary) {
        const std::string &op = cond.op;
        if (op == "&&") {
            genCondBranch(*cond.lhs, Lfalse);
            genCondBranch(*cond.rhs, Lfalse);
            return;
        }
        if (op == "||") {
            // Short-circuits directly, the mirror image of `&&` above:
            // as soon as the left side is true, jump straight past the
            // right side entirely (genCondBranchOnTrue's own `Ldone`)
            // instead of materializing a 0/1 value for it first, the
            // way falling through to the generic path below would.
            std::string Ldone = newLabel();
            genCondBranchOnTrue(*cond.lhs, Ldone);
            genCondBranch(*cond.rhs, Lfalse);
            emitLabel(Ldone);
            return;
        }
        // A wide-operand comparison can't use the fused narrow
        // machinery below, since it never materializes a value
        // genExpr() could read without truncating it - fall through
        // to the generic path, which dispatches to genWideCompare().
        bool wideCmp = (op == "<" || op == ">" || op == ">=" || op == "<=" || op == "==" || op == "!=") &&
                       isWide(cond.lhs->type);
        if (!wideCmp && (op == "<" || op == ">" || op == ">=" || op == "<=")) {
            const ir::Expr *a = cond.lhs.get();
            const ir::Expr *b = cond.rhs.get();
            bool negate = (op == ">=" || op == "<=");
            if (op == ">" || op == "<=") std::swap(a, b);
            std::string A, B, negB;
            int signA, signB;
            bool bIsZero;
            genCompareCells(*a, *b, a->type.isUnsigned, A, B, signA, signB, &negB, &bIsZero);
            // emitSignedLtCells() tests A<B directly; negate just
            // picks which outcome continues to Lfalse.
            std::string Ltrue = newLabel();
            if (!negate) {
                emitSignedLtCells(A, B, Ltrue, Lfalse, signA, signB, negB, bIsZero);
                emitLabel(Ltrue);
            } else {
                emitSignedLtCells(A, B, Lfalse, Ltrue, signA, signB, negB, bIsZero);
                emitLabel(Ltrue);
            }
            return;
        }
        if (!wideCmp && (op == "==" || op == "!=")) {
            std::string rhsCell = genOperandPair(*cond.lhs, *cond.rhs, RhsXform::None);
            if (op == "!=") {
                emit("sad", rhsCell);
                emit("jmp", Lfalse);
            } else {
                std::string Ltrue = newLabel();
                emit("sad", rhsCell);
                emit("jmp", Ltrue);
                emit("jmp", Lfalse);
                emitLabel(Ltrue);
            }
            return;
        }
    }
    genExpr(cond);
    emit("sna");
    emit("jmp", Lfalse);
}

void Pdp7Codegen::genCondBranchOnTrue(const ir::Expr &cond, const std::string &Ltrue) {
    if (cond.kind == ir::ExprKind::Unary && cond.op == "!") {
        genCondBranch(*cond.rhs, Ltrue);
        return;
    }
    if (cond.kind == ir::ExprKind::Binary) {
        const std::string &op = cond.op;
        if (op == "||") {
            genCondBranchOnTrue(*cond.lhs, Ltrue);
            genCondBranchOnTrue(*cond.rhs, Ltrue);
            return;
        }
        if (op == "&&") {
            // `A && B` can't jump to Ltrue off `A` alone (still need
            // `B`) - skip straight past `B` (to `Lskip`, the overall
            // "false" fallthrough) when `A` is false, the mirror image
            // of `||`'s own shortcut in genCondBranch() above.
            std::string Lskip = newLabel();
            genCondBranch(*cond.lhs, Lskip);
            genCondBranchOnTrue(*cond.rhs, Ltrue);
            emitLabel(Lskip);
            return;
        }
        bool wideCmp = (op == "<" || op == ">" || op == ">=" || op == "<=" || op == "==" || op == "!=") &&
                       isWide(cond.lhs->type);
        if (!wideCmp && (op == "<" || op == ">" || op == ">=" || op == "<=")) {
            const ir::Expr *a = cond.lhs.get();
            const ir::Expr *b = cond.rhs.get();
            bool negate = (op == ">=" || op == "<=");
            if (op == ">" || op == "<=") std::swap(a, b);
            std::string A, B, negB;
            int signA, signB;
            bool bIsZero;
            genCompareCells(*a, *b, a->type.isUnsigned, A, B, signA, signB, &negB, &bIsZero);
            std::string Lfall = newLabel();
            if (!negate) {
                emitSignedLtCells(A, B, Ltrue, Lfall, signA, signB, negB, bIsZero);
            } else {
                emitSignedLtCells(A, B, Lfall, Ltrue, signA, signB, negB, bIsZero);
            }
            emitLabel(Lfall);
            return;
        }
        if (!wideCmp && (op == "==" || op == "!=")) {
            std::string rhsCell = genOperandPair(*cond.lhs, *cond.rhs, RhsXform::None);
            if (op == "!=") {
                std::string Lfall = newLabel();
                emit("sad", rhsCell);
                emit("jmp", Lfall);
                emit("jmp", Ltrue);
                emitLabel(Lfall);
            } else {
                emit("sad", rhsCell);
                emit("jmp", Ltrue);
            }
            return;
        }
    }
    genExpr(cond);
    emit("sza");
    emit("jmp", Ltrue);
}

void Pdp7Codegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    std::string Lcheckb = newLabel(), Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    emit("sza");
    emit("jmp", Lcheckb);
    emit("jmp", Lfalse);
    emitLabel(Lcheckb);
    genExpr(rhs);
    emit("sza");
    emit("jmp", Ltrue);
    emit("jmp", Lfalse);
    emitLabel(Ltrue);
    emit("lac", "__one");
    emit("jmp", Ldone);
    emitLabel(Lfalse);
    emit("lac", "__zero");
    emitLabel(Ldone);
}

void Pdp7Codegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    std::string Ltrue = newLabel(), Lcheckb = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    emit("sza");
    emit("jmp", Ltrue);
    emit("jmp", Lcheckb);
    emitLabel(Lcheckb);
    genExpr(rhs);
    emit("sza");
    emit("jmp", Ltrue);
    emit("jmp", Lfalse);
    emitLabel(Ltrue);
    emit("lac", "__one");
    emit("jmp", Ldone);
    emitLabel(Lfalse);
    emit("lac", "__zero");
    emitLabel(Ldone);
}

void Pdp7Codegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    emit("sza");
    emit("jmp", Lfalse);
    emit("jmp", Ltrue);
    emitLabel(Ltrue);
    emit("lac", "__one");
    emit("jmp", Ldone);
    emitLabel(Lfalse);
    emit("lac", "__zero");
    emitLabel(Ldone);
}

// Multiply via the hardware EAE "mul" instruction: it reads its
// multiplicand as the literal data word immediately following it in
// memory, leaving a double-length product in AC:MQ; "lacq" brings the
// low-order 18-bit half into AC. Only verified for non-negative
// operands.
void Pdp7Codegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string Tr = allocTemp();  // not declareTemp(): doubles as mul's operand slot
    genOperandPair(lhs, rhs, RhsXform::None, Tr);
    emit("cll");
    emit("mul");
    emitRaw(cat(Tr, ": 0"));
    emit("lacq");
}

// Division/modulo via shared routines (see pdp7RuntimeAsm()).
void Pdp7Codegen::ensureRuntimeRoutines() {
    std::istringstream lines(pdp7RuntimeAsm(needPushPop_, needDivMod_, needUDivMod_, needGenericLt_));
    for (std::string line; std::getline(lines, line);) trailer_.push_back(line);
}

void Pdp7Codegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder) {
    genOperandPair(lhs, rhs, RhsXform::None, "__dm_b");
    emit("dac", "__dm_a");
    callDivMod(lhs.type.isUnsigned);
    if (wantRemainder) emit("lac", "__dm_a");
}

void Pdp7Codegen::callDivMod(bool isUnsigned) {
    if (isUnsigned) {
        needUDivMod_ = true;
        emit("jms", "__udivmod");
    } else {
        needDivMod_ = true;
        emit("jms", "__divmod");
    }
}

// "&"/"^" are real pdp7 memory-reference instructions (AC = AC <op>
// M[addr]). "|" has no hardware equivalent, so it's synthesized via
// De Morgan: a|b = ~(~a & ~b).
void Pdp7Codegen::genBitwise(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs) {
    if (op == "|") {
        std::string rhsCell = genOperandPair(lhs, rhs, RhsXform::Complement);
        emit("cma");
        emit("and", rhsCell);
        emit("cma");
        return;
    }
    std::string rhsCell = genOperandPair(lhs, rhs, RhsXform::None);
    emit((op == "&" ? "and" : "xor"), rhsCell);
}

// A shift by a constant count uses the EAE shifts, whose vacated bits
// fill from the Link (`als`, `lrs`, cleared first) or, for `lrss`, the
// sign bit.
void Pdp7Codegen::emitConstantShift(const std::string &op, long count, bool isUnsigned) {
    if (count == 0) return;
    if (op == "<<") {
        emit("cll");
        emit("als", count);
    } else if (isUnsigned) {
        emit("cll");
        emit("lrs", count);
    } else {
        emit("lrss", count);
    }
}

// `<<`/`>>` have no variable-count hardware instruction (EAE
// `als`/`lrs` bake their count into the instruction word at assemble
// time), so a constant count uses emitConstantShift() and any other is
// a counted loop: `<<` is repeated `v+v`; `>>` is a single-bit rotate
// (`cll` then `rar`), a logical shift with no sign extension unless the
// link is seeded with the sign bit first.
void Pdp7Codegen::genShift(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs) {
    if (rhs.kind == ir::ExprKind::IntLit && rhs.intValue >= 0 && rhs.intValue < 18) {
        genExpr(lhs);
        emitConstantShift(op, rhs.intValue, lhs.type.isUnsigned);
        return;
    }
    std::string N = allocTemp();
    declareTemp(N);
    genOperandPair(lhs, rhs, RhsXform::None, N);  // AC = the value being shifted
    std::string V = allocTemp();
    declareTemp(V);
    emit("dac", V);
    std::string Lstart = newLabel(), Lbody = newLabel(), Lend = newLabel();
    emitLabel(Lstart);
    emit("lac", N);
    emit("sza");
    emit("jmp", Lbody);
    emit("jmp", Lend);
    emitLabel(Lbody);
    if (op == "<<") {
        emit("lac", V);
        emit("tad", V);
        emit("dac", V);
    } else if (lhs.type.isUnsigned) {
        emit("lac", V);
        emit("cll");
        emit("rar");
        emit("dac", V);
    } else {
        // Arithmetic right shift: seed the link with V's sign bit
        // before `rar` rotates it into the vacated top bit, so the
        // sign replicates instead of filling with 0.
        std::string Lpos = newLabel(), Lneg = newLabel(), Lshift = newLabel();
        emit("lac", V);
        emit("sma");
        emit("jmp", Lpos);
        emit("jmp", Lneg);
        emitLabel(Lpos);
        emit("cll");
        emit("jmp", Lshift);
        emitLabel(Lneg);
        emit("stl");
        emitLabel(Lshift);
        emit("lac", V);
        emit("rar");
        emit("dac", V);
    }
    emit("lac", "__zero");
    emit("cma");
    emit("tad", N);
    emit("dac", N);
    emit("jmp", Lstart);
    emitLabel(Lend);
    emit("lac", V);
}

// Always returns a value for every format assembledFormats() lists - there's no
// external assembler to fall back to.
std::optional<std::string> Pdp7Codegen::assembleInProcess(const std::string &asmText,
                                                           const std::string &format) const {
    if (format == "a7out") return assemblePdp7(asmText, Pdp7OutputFormat::A7Out);
    if (format == "list") return assemblePdp7(asmText, Pdp7OutputFormat::List);
    if (format == "ptr") return assemblePdp7(asmText, Pdp7OutputFormat::Ptr);
    if (format == "rim") return assemblePdp7(asmText, Pdp7OutputFormat::Rim);
    // `--format=coff` means real separate compilation: an undefined
    // symbol is deferred as a relocation for a future `--link` step.
    if (format == "coff") return assemblePdp7(asmText, Pdp7OutputFormat::Coff, /*allowExternal=*/true);
    return std::nullopt;
}

std::string pdp7RuntimeAsm(bool pushPop, bool divMod, bool uDivMod, bool genericLt) {
    std::ostringstream o;
    // `__sp` starts at 8191 (017777 octal), the highest word the 13-bit
    // direct-addressing field can reach, and grows downward toward
    // code/data. `__fp`'s starting value is never read before the first
    // call gives it one. `__stmp`/`__stmp2`/`__addr`/`__retval`/
    // `__retaddr` are scratch cells, each only held across a short,
    // call-free sequence. `__retvalW0-3` carry a wide return value past
    // the epilogue's `__sp = __fp` teardown.
    o << "__one: 1\n__zero: 0\n__mone: 0777777\n__sp: 8191\n__fp: 0\n"
         "__stmp: 0\n__stmp2: 0\n__addr: 0\n__retval: 0\n__retaddr: 0\n"
         "__retvalW0: 0\n__retvalW1: 0\n__retvalW2: 0\n__retvalW3: 0\n";
    if (pushPop) {
        // __push: pushes AC (leaving it unchanged). __pop: pops into AC.
        o << "__push: 0\n"
             "   dac __stmp\n   lac __sp\n   tad __mone\n   dac __sp\n"
             "   lac __stmp\n   dac __sp i\n   jmp i __push\n"
             "__pop: 0\n"
             "   lac __sp i\n   dac __stmp\n   lac __sp\n   tad __one\n   dac __sp\n"
             "   lac __stmp\n   jmp i __pop\n";
    }
    if (genericLt) {
        // __cmp_lt: signed `__cmpA < __cmpB`, neither operand's sign
        // known at compile time - the one case emitSignedLtCells() (see
        // its own comment) routes through a shared subroutine instead
        // of inlining this same dispatch-then-subtract sequence again
        // at every such comparison. Same structure as the inline
        // version: differing signs decided directly, same-sign operands
        // fall through to subtract-and-test-sign (safe: same-sign
        // 18-bit values can't differ by more than the representable
        // range).
        o << "__cmpA: 0\n__cmpB: 0\n"
             "__cmp_lt: 0\n"
             "   lac __cmpA\n   sma\n   jmp J_cl_apos\n   jmp J_cl_aneg\n"
             "J_cl_aneg:\n"
             "   lac __cmpB\n   sma\n   jmp J_cl_true\n   jmp J_cl_sub\n"
             "J_cl_apos:\n"
             "   lac __cmpB\n   sma\n   jmp J_cl_sub\n   jmp J_cl_false\n"
             "J_cl_sub:\n"
             "   lac __cmpB\n   cma\n   tad __one\n   dac __stmp2\n"
             "   lac __cmpA\n   tad __stmp2\n   sma\n   jmp J_cl_false\n   jmp J_cl_true\n"
             "J_cl_true:\n"
             "   lac __one\n   jmp i __cmp_lt\n"
             "J_cl_false:\n"
             "   lac __zero\n   jmp i __cmp_lt\n";
    }
    if (divMod || uDivMod) o << "__dm_a: 0\n__dm_b: 0\n";
    if (divMod || uDivMod) {
        // __udivmod: __dm_a / __dm_b, unsigned. The EAE `div`
        // instruction divides AC:MQ (here 0 and the dividend) by the
        // word that follows it, leaving the quotient in MQ and the
        // remainder in AC. Returns the quotient in AC and the remainder
        // in __dm_a.
        o << "__udivmod: 0\n"
             "   lac __dm_b\n   dac J_udm_d\n   lac __dm_a\n   lmq\n   cla\n   cll\n   div\n"
             "J_udm_d: 0\n   dac __dm_a\n   lacq\n   jmp i __udivmod\n";
    }
    if (divMod) {
        // __divmod: the signed version, C's truncating division. It
        // divides the magnitudes with __udivmod, then negates the
        // quotient when the operands' signs differ (the sign bit of
        // their xor, kept in __dm_q) and the remainder when the
        // dividend was negative (kept in __dm_s).
        o << "__dm_q: 0\n__dm_s: 0\n"
             "__divmod: 0\n"
             "   lac __dm_a\n   dac __dm_s\n   xor __dm_b\n   dac __dm_q\n"
             "   lac __dm_a\n   sma\n   jmp J_dm_a\n   cma\n   tad __one\n   dac __dm_a\n"
             "J_dm_a:\n"
             "   lac __dm_b\n   sma\n   jmp J_dm_b\n   cma\n   tad __one\n   dac __dm_b\n"
             "J_dm_b:\n"
             "   jms __udivmod\n   dac __dm_b\n"
             "   lac __dm_q\n   sma\n   jmp J_dm_q\n"
             "   lac __dm_b\n   cma\n   tad __one\n   dac __dm_b\n"
             "J_dm_q:\n"
             "   lac __dm_s\n   sma\n   jmp J_dm_r\n"
             "   lac __dm_a\n   cma\n   tad __one\n   dac __dm_a\n"
             "J_dm_r:\n"
             "   lac __dm_b\n   jmp i __divmod\n";
    }
    return o.str();
}

}  // namespace rcc
