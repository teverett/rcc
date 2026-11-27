#include "I8086Codegen.h"

#include <stdexcept>

#include "I8086Assembler.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}
}  // namespace

std::string I8086Codegen::allocTemp() { return "t" + std::to_string(tempCounter_++); }

std::string I8086Codegen::arrayElemText(const ir::ArrayInitElem &e) {
    if (!e.isSymbol) return std::to_string(e.value);
    return e.slot ? label(e.slot) : label(e.func);
}

void I8086Codegen::declareArray(const std::string &label, const std::vector<ir::ArrayInitElem> &values, Type elemType) {
    std::ostringstream line;
    line << label << ": " << (widthOfType(elemType) == 1 ? "db " : "dw ");
    for (size_t i = 0; i < values.size(); i++) {
        if (i > 0) line << ", ";
        line << arrayElemText(values[i]);
    }
    trailer_.push_back(line.str());
}

std::string I8086Codegen::newLabel() { return "L" + std::to_string(labelCounter_++); }

// `@addr` placement: pads forward to a byte offset from ORG (kI8086BaseAddress) via
// `times`. NASM's `$` only increases, so placement can only insert forward padding,
// never reorder.
void I8086Codegen::emitPlacement(long address) { emitRaw(cat("times (", address, "-($-$$)) db 0")); }

std::string I8086Codegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    // In objectMode, prog may have no `main`; otherwise IRBuilder guarantees one exists.

    // Tracks (sourceFile, start, end) ranges into code_/trailer_ for --dump-asm's
    // per-file output, without affecting the combined asmText returned below.
    std::vector<std::tuple<std::string, size_t, size_t>> codeRanges;
    std::vector<std::tuple<std::string, size_t, size_t>> trailerRanges;

    // Fallback file attribution for --dump-asm when nothing owns a Slot/Function.
    std::string homeFile = mainFn ? mainFn->sourceFile
                            : !prog.functionBodies.empty() ? prog.functionBodies.front().func->sourceFile
                            : !prog.slots.empty() ? prog.slots.front()->sourceFile
                                                   : std::string();

    size_t rangeStart = static_cast<size_t>(code_.tellp());
    emitRaw("BITS 16");
    // objectMode: no entry sequence here - the linker synthesizes one after linking. An
    // object's own base is `ORG 0`; placement is the linker's job.
    if (objectMode) {
        { emitRaw("ORG 0"); emitRaw(""); }
    } else if (dosAbi_) {
        // DOS's own loader already made CS=DS=ES=SS the one segment this process gets
        // (the PSP) and gave it a valid stack, so there's no segment/stack setup here -
        // just call main and exit via INT 21h/4Ch. This dialect's `mov` has no `ah`
        // register to load just the function number into (I8086Assembler.cpp has none
        // at all - see its own "movb"/"and" comments), so AX is reloaded wholesale with
        // `0x4C00`: function 4Ch, exit code 0 - main's own return value doesn't survive
        // as a real DOS exit code this way, only up until this mov.
        { emitRaw(cat("ORG 0x", std::hex, kI8086DosBaseAddress, std::dec)); emitRaw(""); }
        emitLabel("_start");
        emit("call", label(mainFn));
        emit("mov", "ax, 0x4C00");
        { emit("int", "0x21"); emitRaw(""); }
        codeRanges.push_back({mainFn->sourceFile, rangeStart, static_cast<size_t>(code_.tellp())});
    } else {
        { emitRaw(cat("ORG 0x", std::hex, kI8086BaseAddress, std::dec)); emitRaw(""); }
        emitLabel("_start");
        emit("cli");
        emit("xor", "ax, ax");
        emit("mov", "ds, ax");
        emit("mov", "es, ax");
        emit("mov", "ss, ax");
        emit("mov", "sp, 0xFFFE");  // top of the flat 64KB segment this program lives in
        emit("sti");
        emit("call", label(mainFn));
        emit("hlt");
        emitLabel("_hang");
        { emit("jmp", "_hang"); emitRaw(""); }
        codeRanges.push_back({mainFn->sourceFile, rangeStart, static_cast<size_t>(code_.tellp())});
    }

    for (const auto &fb : prog.functionBodies) {
        size_t fCodeStart = static_cast<size_t>(code_.tellp());
        size_t fTrailerStart = trailer_.size();
        if (fb.func->hasAddress) emitPlacement(fb.func->address);
        genFunction(fb);
        codeRanges.push_back({fb.func->sourceFile, fCodeStart, static_cast<size_t>(code_.tellp())});
        trailerRanges.push_back({fb.func->sourceFile, fTrailerStart, trailer_.size()});
    }

    // Fixed scratch for a wide Return value (its address may point into the frame
    // the epilogue is about to tear down); 4 words covers even a `long long`.
    trailer_.push_back("__wret: dw 0, 0, 0, 0");
    trailer_.push_back("__wargsp: dw 0");

    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        if (slot->isExternal) continue;  // not defined in this file
        size_t gCodeStart = static_cast<size_t>(code_.tellp());
        size_t gTrailerStart = trailer_.size();
        if (slot->hasAddress) emitPlacement(slot->address);
        if (debugMapEnabled_) debugSymbols_.push_back({label(slot.get()), /*isFunc=*/false});
        const std::string lbl = label(slot.get());
        int w = widthOfType(slot->type);
        if (slot->arraySize > 0) {
            std::ostringstream line;
            line << lbl << ": " << (w == 1 ? "db " : "dw ");
            for (size_t i = 0; i < slot->arrayInit.size(); i++) {
                if (i > 0) line << ", ";
                // A wide element's literal splits into N low-to-high words; a symbol
                // element (dispatch-table idiom) is always a plain native-width address.
                const ir::ArrayInitElem &elem = slot->arrayInit[i];
                if (w > 2 && !elem.isSymbol) {
                    auto v = static_cast<uint64_t>(elem.value);
                    for (int wi = 0; wi < w / 2; wi++) {
                        if (wi > 0) line << ", ";
                        line << static_cast<uint16_t>(v >> (16 * wi));
                    }
                } else {
                    line << arrayElemText(elem);
                }
            }
            line << "  ; " << slot->sourceName;
            trailer_.push_back(line.str());
        } else if (slot->type.base == BaseType::Struct) {
            // No struct initializer exists in this subset, so this is
            // always zero-filled; reuses declareArray's own element-list
            // emission (byte-exact for any size) rather than the
            // word-pair logic below, which assumes an even width.
            declareArray(lbl, std::vector<ir::ArrayInitElem>(slot->type.structSize), Type{BaseType::Char, false});
        } else if (w > 2) {
            auto v = static_cast<uint64_t>(slot->hasInit ? slot->initValue : 0);
            std::ostringstream line;
            line << lbl << ": dw ";
            for (int wi = 0; wi < w / 2; wi++) {
                if (wi > 0) line << ", ";
                line << static_cast<uint16_t>(v >> (16 * wi));
            }
            line << "  ; " << slot->sourceName;
            trailer_.push_back(line.str());
        } else {
            long v = slot->hasInit ? slot->initValue : 0;
            trailer_.push_back(lbl + ": " + (w == 1 ? "db " : "dw ") + std::to_string(v) + "  ; " + slot->sourceName);
        }
        codeRanges.push_back({slot->sourceFile, gCodeStart, static_cast<size_t>(code_.tellp())});
        trailerRanges.push_back({slot->sourceFile, gTrailerStart, trailer_.size()});
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

std::map<std::string, std::string> I8086Codegen::perFileAsm() const { return perFileAsm_; }

// Standard frame: push bp/mov bp,sp establishes the frame pointer Local/Param
// addressing is relative to; sub sp,N reserves locals. Epilogue tears it down in two
// instructions regardless of local count, giving each activation its own frame.
void I8086Codegen::genFunction(const ir::FunctionBody &fn) {
    curFunctionLabel_ = label(fn.func);
    lastDebugLine_ = 0;
    if (debugMapEnabled_) debugSymbols_.push_back({curFunctionLabel_, /*isFunc=*/true});
    emitLabel(curFunctionLabel_);
    // The body is generated first so the prologue can reserve one word below the Locals
    // for each nesting level of wideScope().
    curFrameSize_ = fn.func->frameSize;
    wideScopeDepth_ = 0;
    maxWideScopeDepth_ = 0;
    std::ostringstream body;
    std::swap(body, code_);
    genStmt(*fn.body);
    std::swap(body, code_);
    int frame = curFrameSize_ + 2 * maxWideScopeDepth_;
    { emit("push", "bp"); emit("mov", "bp, sp"); }
    if (frame > 0) emit("sub", cat("sp, ", frame));
    std::string bodyText = body.str();
    const std::string placeholder = kFrameBottomPlaceholder;
    for (size_t at = bodyText.find(placeholder); at != std::string::npos; at = bodyText.find(placeholder, at)) {
        bodyText.replace(at, placeholder.size(), std::to_string(frame));
    }
    code_ << bodyText;  // already-built multi-line blob, already newline-terminated

    { emit("mov", "sp, bp"); emit("pop", "bp"); }
    { emit("ret"); emitRaw(""); }
}

bool I8086Codegen::involvesWide(const ir::Expr &e) const {
    if (isWide(e.type) || (e.slot && isWide(e.slot->type))) return true;
    for (const ir::Expr *child : {e.lhs.get(), e.rhs.get(), e.elseExpr.get()}) {
        if (child && involvesWide(*child)) return true;
    }
    for (const auto &arg : e.args) {
        if (involvesWide(*arg)) return true;
    }
    return false;
}

void I8086Codegen::wideScope(const std::function<void()> &gen) {
    int cell = curFrameSize_ + 2 * (wideScopeDepth_ + 1);
    wideScopeDepth_++;
    maxWideScopeDepth_ = std::max(maxWideScopeDepth_, wideScopeDepth_);
    emit("mov", cat("[bp-", cell, "], sp"));
    gen();
    emit("mov", cat("sp, [bp-", cell, "]"));
    wideScopeDepth_--;
}

void I8086Codegen::resetStackToFrame() {
    emit("lea", cat("sp, [bp-", kFrameBottomPlaceholder, "]"));
}

void I8086Codegen::genTruthValue(const ir::Expr &e, bool atStatement) {
    if (!isWide(e.type)) {
        genExpr(e);
        return;
    }
    if (atStatement) {
        genExpr(e);
        genWideTruthify(e.type);
        resetStackToFrame();
        return;
    }
    wideScope([&] {
        genExpr(e);
        genWideTruthify(e.type);
    });
}

void I8086Codegen::genStmt(const ir::Stmt &s) {
    emitSourceLine(s.line);
    switch (s.kind) {
        case ir::StmtKind::Block: {
            for (const auto &child : s.body) genStmt(*child);
            return;
        }
        case ir::StmtKind::If: {
            genTruthValue(*s.cond, /*atStatement=*/true);
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            { emit("cmp", "ax, 0"); emit("je", (s.elseStmt ? Lelse : Ldone)); }
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
            // continue runs the post-statement (desugared for-loop) before retesting,
            // if there is one; otherwise it jumps straight back to the condition.
            std::string Lcont = s.postStmt ? newLabel() : Lstart;
            emitLabel(Lstart);
            genTruthValue(*s.cond, /*atStatement=*/true);
            { emit("cmp", "ax, 0"); emit("je", Lend); }
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
            if (s.initOrReturnOrExpr) {
                genExpr(*s.initOrReturnOrExpr);
                if (isWide(s.initOrReturnOrExpr->type)) {
                    // The result's address may point into this frame, about to be torn
                    // down below, so copy it into the fixed __wret scratch first; safe
                    // since the caller consumes __wret immediately after the call returns.
                    int n = widthOfType(s.initOrReturnOrExpr->type) / 2;
                    emit("mov", "si, ax");  // si = source address value
                    for (int i = 0; i < n; i++) {
                        // Only bx/bp can address memory here, so bx is repointed per base.
                        { emit("mov", "bx, si"); emit("mov", cat("ax, [bx+", (2 * i), "]")); }
                        { emit("mov", "bx, __wret"); emit("mov", cat("[bx+", (2 * i), "], ax")); }
                    }
                    emit("mov", "ax, __wret");
                }
            }
            // Early exit: inline the same frame teardown genFunction()'s epilogue does.
            { emit("mov", "sp, bp"); emit("pop", "bp"); }
            emit("ret");
            return;
        }
        case ir::StmtKind::LocalDecl: {
            if (s.declSlot->arraySize > 0) {
                genLocalArrayInit(s.declSlot);
            } else if (s.initOrReturnOrExpr) {
                if (isWide(s.declSlot->type)) {
                    int n = widthOfType(s.declSlot->type) / 2;
                    genExpr(*s.initOrReturnOrExpr);  // ax = source address
                    emit("mov", "di, ax");
                    genWideSlotAddr(s.declSlot);  // ax = destination address
                    emit("mov", "si, ax");
                    for (int i = 0; i < n; i++) {
                        { emit("mov", "bx, di"); emit("mov", cat("ax, [bx+", (2 * i), "]")); }
                        { emit("mov", "bx, si"); emit("mov", cat("[bx+", (2 * i), "], ax")); }
                    }
                    resetStackToFrame();
                } else if (involvesWide(*s.initOrReturnOrExpr)) {
                    genExpr(*s.initOrReturnOrExpr);
                    resetStackToFrame();
                    storeOperand(addr(s.declSlot), s.declSlot->type);
                } else {
                    genExpr(*s.initOrReturnOrExpr);
                    storeOperand(addr(s.declSlot), s.declSlot->type);
                }
            }
            // An uninitialized scalar needs no code: its frame word already exists.
            return;
        }
        case ir::StmtKind::ExprStmt: {
            // A wide value's temporaries stay on the stack until its scope ends.
            genExpr(*s.initOrReturnOrExpr);
            if (involvesWide(*s.initOrReturnOrExpr)) resetStackToFrame();
            return;
        }
        case ir::StmtKind::Empty:
            return;
        case ir::StmtKind::Asm: {
            // Verbatim passthrough: emitted exactly as written, no reformatting.
            emitRaw(s.asmText);
            if (!s.asmText.empty() && s.asmText.back() != '\n') emitRaw("");
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

void I8086Codegen::genExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit: {
            if (isWide(e.type)) {
                // Real 8086 has no PUSH imm16 - load each word into AX first.
                int n = widthOfType(e.type) / 2;
                auto v = static_cast<uint64_t>(e.intValue);
                for (int i = n - 1; i >= 0; i--) {
                    emit("mov", cat("ax, ", static_cast<uint16_t>(v >> (16 * i))));
                    emit("push", "ax");
                }
                emit("mov", "ax, sp");
                return;
            }
            emit("mov", cat("ax, ", e.intValue));
            return;
        }
        case ir::ExprKind::Var: {
            // IRBuilder rejects a bare array Var outside of `&`, so e.slot is scalar here.
            if (isWide(e.slot->type)) {
                genWideSlotAddr(e.slot);
                return;
            }
            loadOperand(operandOf(e.slot), e.slot->type);
            return;
        }
        case ir::ExprKind::Index: {
            genIndex(e);
            return;
        }
        case ir::ExprKind::Assign: {
            if (isWide(e.slot->type)) {
                int n = widthOfType(e.slot->type) / 2;
                // rhs must be evaluated before the destination address: a wide rhs
                // (genWideAdd/Mul/Compare) uses si/di as scratch, which would clobber
                // a destination address stashed there first.
                genExpr(*e.rhs);       // ax = source address
                emit("mov", "bx, ax");
                copyWideBlock(n, n, /*isUnsignedExt=*/false);  // ax = fresh copy's address
                emit("mov", "di, ax");  // di = fresh copy's own address
                // An indexed destination needs the element's own address (bx, from
                // computeElementAddr); an unindexed one uses genWideSlotAddr instead.
                if (e.lhs) {
                    computeElementAddr(e.slot, *e.lhs);
                    emit("mov", "si, bx");
                } else {
                    genWideSlotAddr(e.slot);
                    emit("mov", "si, ax");
                }
                // Copy the fresh stack copy (di) into the real destination (si), then
                // free the stack copy's space.
                for (int i = 0; i < n; i++) {
                    { emit("mov", "bx, di"); emit("mov", cat("ax, [bx+", (2 * i), "]")); }
                    { emit("mov", "bx, si"); emit("mov", cat("[bx+", (2 * i), "], ax")); }
                }
                emit("add", cat("sp, ", (2 * n)));
                emit("mov", "ax, si");
                return;
            }
            if (e.lhs) {  // slot[idx] = rhs
                std::string memOperand = computeElementAddr(e.slot, *e.lhs);
                emit("push", "bx");
                genExpr(*e.rhs);
                emit("pop", "bx");
                storeOperand(memOperand, e.slot->type);
                return;
            }
            genExpr(*e.rhs);
            storeOperand(operandOf(e.slot), e.slot->type);
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            if (e.lhs) {  // slot[idx] op= rhs (including slot[idx]++/--)
                computeElementAddr(e.slot, *e.lhs);  // bx = address
                genCompoundThroughBx(e);
                return;
            }
            // operandOf(slot) is a stable memory operand, not a clobberable register,
            // so no address needs to be held across rhs's own evaluation.
            std::string mem = operandOf(e.slot);
            genExpr(*e.rhs);  // ax = rhs
            emit("mov", "bx, ax");
            loadOperand(mem, e.type);  // ax = old
            if (e.yieldOld) emit("push", "ax");
            genCompoundOp(e.op, e.type);
            narrowAxTo(e.type);
            storeOperand(mem, e.type);
            if (e.yieldOld) emit("pop", "ax");
            return;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            genExpr(*e.lhs);  // ax = pointer value
            emit("mov", "bx, ax");  // bx = address
            genCompoundThroughBx(e);
            return;
        }
        case ir::ExprKind::Ternary: {
            genTruthValue(*e.lhs);
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            { emit("cmp", "ax, 0"); emit("je", Lelse); }
            genExpr(*e.rhs);
            emit("jmp", Ldone);
            emitLabel(Lelse);
            genExpr(*e.elseExpr);
            emitLabel(Ldone);
            return;
        }
        case ir::ExprKind::DerefAssign: {
            // Stores at e.type's own width (the pointee type). A wide pointee reuses
            // the "fresh protected copy, then copy to real destination" shape of a
            // wide Assign, except the destination address comes from evaluating e.lhs
            // (the pointer, always narrow) rather than genWideSlotAddr/computeElementAddr.
            if (isWide(e.type)) {
                int n = widthOfType(e.type) / 2;
                genExpr(*e.rhs);  // ax = source address
                emit("mov", "bx, ax");
                copyWideBlock(n, n, /*isUnsignedExt=*/false);  // ax = fresh protected copy's address
                emit("mov", "di, ax");
                genExpr(*e.lhs);  // ax = pointer value = destination address
                emit("mov", "si, ax");
                for (int i = 0; i < n; i++) {
                    { emit("mov", "bx, di"); emit("mov", cat("ax, [bx+", (2 * i), "]")); }
                    { emit("mov", "bx, si"); emit("mov", cat("[bx+", (2 * i), "], ax")); }
                }
                emit("add", cat("sp, ", (2 * n)));
                emit("mov", "ax, si");
                return;
            }
            genExpr(*e.lhs);  // ax = pointer value
            { emit("mov", "bx, ax"); emit("push", "bx"); }
            genExpr(*e.rhs);  // ax = value to store
            emit("pop", "bx");
            storeOperand("[bx]", e.type);
            return;
        }
        case ir::ExprKind::Unary: {
            if (e.op == "-") {
                genExpr(*e.rhs);
                if (isWide(e.rhs->type)) {
                    emit("mov", "bx, ax");
                    genWideNegate(widthOfType(e.rhs->type) / 2);
                } else {
                    emit("neg", "ax");
                }
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                // Loads at e.type's own width. A wide pointee's evaluation already
                // leaves its storage address in ax, which is exactly the pointer value,
                // so there's nothing further to load.
                genExpr(*e.rhs);  // ax = pointer value
                if (isWide(e.type)) return;
                emit("mov", "bx, ax");
                loadOperand("[bx]", e.type);
            } else if (e.op == "~") {
                genExpr(*e.rhs);
                if (isWide(e.rhs->type)) {
                    emit("mov", "bx, ax");
                    for (int i = 0; i < widthOfType(e.rhs->type) / 2; i++) {
                        { emit("mov", cat("ax, [bx+", (2 * i), "]")); emit("not", "ax"); }
                        emit("mov", cat("[bx+", (2 * i), "], ax"));
                    }
                    emit("mov", "ax, bx");
                } else {
                    emit("not", "ax");
                }
            } else {  // "&"
                genAddrOf(*e.rhs);
            }
            return;
        }
        case ir::ExprKind::Call: {
            // cdecl: arguments pushed right-to-left so the first ends up closest to the
            // return address, matching Param addressing (paramIndex 0 at bp+4); the
            // caller cleans up afterward (add sp,N).
            int totalBytes = 0;
            for (size_t i = e.args.size(); i-- > 0;) {
                if (isWide(e.args[i]->type)) {
                    // A wide argument's evaluation leaves its own temp block on the
                    // stack. Since several arguments are pushed within this same
                    // statement, an earlier temp would be displaced by a later one's -
                    // so each argument's real copy is written at (sp before it ran) -
                    // n words, and sp is reset there, reclaiming the temp.
                    int n = widthOfType(e.args[i]->type) / 2;
                    emit("mov", "[__wargsp], sp");
                    genExpr(*e.args[i]);  // ax = this argument's own address
                    emit("mov", "si, ax");
                    { emit("mov", "ax, [__wargsp]"); emit("sub", cat("ax, ", (2 * n))); emit("mov", "di, ax"); }
                    for (int w = 0; w < n; w++) {
                        { emit("mov", "bx, si"); emit("mov", cat("ax, [bx+", (2 * w), "]")); }
                        { emit("mov", "bx, di"); emit("mov", cat("[bx+", (2 * w), "], ax")); }
                    }
                    emit("mov", "sp, di");
                    totalBytes += 2 * n;
                } else {
                    genExpr(*e.args[i]);
                    emit("push", "ax");
                    totalBytes += 2;
                }
            }
            emit("call", label(e.func));
            if (totalBytes > 0) emit("add", cat("sp, ", totalBytes));
            return;
        }
        case ir::ExprKind::CallPtr: {
            // e.args is always empty (IRBuilder rejects arguments on this kind).
            // "call ax" is a real near-indirect call through a register (FF /2).
            genExpr(*e.lhs);  // ax = the callee's own entry address
            emit("call", "ax");
            return;
        }
        case ir::ExprKind::Binary: {
            genBinary(e);
            return;
        }
        case ir::ExprKind::Intrinsic: {
            // Real 8086 interrupt enable/disable (sti/cli).
            emit((e.op == "ion" ? "sti" : "cli"));
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

// slot[idxExpr]'s effective-address operand text ("[bx]"), with bx loaded with the
// final address (idx*2 words, plus the array's/pointer's own base).
//
// slot is the base address itself only for a declared array; slot[idx] on a plain
// variable indexes through that variable's runtime pointer VALUE instead, via
// operandOf(). A Global array's base is a compile-time immediate; a Local array's
// needs `lea` (only known relative to bp at runtime).
std::string I8086Codegen::computeElementAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
    genExpr(idxExpr);
    int shift = elemShift(slot->type);
    if (shift >= 0) {
        // shl only takes a literal count of 1 (or cl) - a wider shift is that many in a row.
        for (int i = 0; i < shift; i++) emit("shl", "ax, 1");
    } else {
        scaleIndexByWidth(slot->type);  // non-power-of-two width (a struct's)
    }
    emit("mov", "bx, ax");
    if (slot->arraySize > 0) {
        if (slot->kind == ir::SlotKind::Global) {
            emit("add", cat("bx, ", label(slot)));
        } else {
            { emit("lea", cat("ax, ", addr(slot))); emit("add", "bx, ax"); }
        }
    } else {
        emit("add", cat("bx, ", operandOf(slot)));
    }
    return "[bx]";
}

void I8086Codegen::genCompoundOp(const std::string &op, Type type) {
    bool isUnsigned = type.isUnsigned;
    if (op == "+") { emit("add", "ax, bx"); return; }
    if (op == "-") { emit("sub", "ax, bx"); return; }
    if (op == "*") { emit((isUnsigned ? "mul" : "imul"), "bx"); return; }  // dx:ax = ax*bx
    if (op == "&" || op == "|" || op == "^") {
        const char *insn = (op == "&") ? "and" : (op == "|") ? "or" : "xor";
        emit(insn, "ax, bx");
        return;
    }
    if (op == "<<" || op == ">>") {
        emit("mov", "cx, bx");
        emit((op == "<<" ? "shl" : (isUnsigned ? "shr" : "sar")), "ax, cl");
        return;
    }
    // "/" or "%": div/idiv only forbid ax/dx (the dividend) as the divisor.
    emit((isUnsigned ? "xor dx, dx\n" : "cwd\n"));
    emit((isUnsigned ? "div" : "idiv"), "bx");
    if (op == "%") emit("mov", "ax, dx");
}

// bx must already hold the resolved address. Only `[bx]` is a valid memory base here,
// so the address is stashed on the stack (not held in a register) across rhs's own
// evaluation, which may use any register.
void I8086Codegen::genCompoundThroughBx(const ir::Expr &e) {
    emit("push", "bx");  // stash address
    genExpr(*e.rhs);            // ax = rhs
    emit("push", "ax");    // stash rhs
    emit("pop", "cx");      // cx = rhs
    emit("pop", "bx");       // bx = address
    loadOperand("[bx]", e.type);    // ax = old
    emit("push", "bx");        // re-stash address for the store below
    if (e.yieldOld) emit("push", "ax");  // stash old too
    emit("mov", "bx, cx");                // bx = rhs
    genCompoundOp(e.op, e.type);                  // ax = new (raw)
    narrowAxTo(e.type);
    if (e.yieldOld) {
        emit("mov", "cx, ax");  // cx = new
        emit("pop", "ax");       // ax = old
        emit("pop", "bx");        // bx = address
        emit("mov", "dx, ax");     // dx = old (preserved across the store below)
        emit("mov", "ax, cx");      // ax = new (storeOperand always stores from ax)
        storeOperand("[bx]", e.type);
        emit("mov", "ax, dx");        // ax = old (final result)
    } else {
        emit("pop", "bx");  // bx = address
        storeOperand("[bx]", e.type);  // ax already = new
    }
}

void I8086Codegen::genIndex(const ir::Expr &e) {
    std::string memOperand = computeElementAddr(e.slot, *e.lhs);  // bx = element address
    if (isWide(e.slot->type)) {
        // Every wide expression leaves its address in ax - bx already holds it.
        emit("mov", "ax, bx");
        return;
    }
    loadOperand(memOperand, e.slot->type);
}

// &operand: a variable/array, an array element, or a bare function name. A Global's
// address is a compile-time immediate; a Local/Param's needs `lea` since it's only
// known at runtime relative to bp.
void I8086Codegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        if (operand.slot->kind == ir::SlotKind::Global) {
            emit("mov", cat("ax, ", label(operand.slot)));
        } else {
            emit("lea", cat("ax, ", addr(operand.slot)));
        }
        return;
    }
    if (operand.kind == ir::ExprKind::Index) {
        genExpr(*operand.lhs);
        {
            int shift = elemShift(operand.slot->type);
            if (shift >= 0) {
                for (int i = 0; i < shift; i++) emit("shl", "ax, 1");
            } else {
                scaleIndexByWidth(operand.slot->type);  // non-power-of-two width (a struct's)
            }
        }
        if (operand.slot->arraySize > 0) {
            if (operand.slot->kind == ir::SlotKind::Global) {
                emit("add", cat("ax, ", label(operand.slot)));
            } else {
                { emit("mov", "bx, ax"); emit("lea", cat("ax, ", addr(operand.slot))); emit("add", "ax, bx"); }
            }
        } else {
            emit("add", cat("ax, ", operandOf(operand.slot)));
        }
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        emit("mov", cat("ax, ", label(operand.func)));
        return;
    }
    err(operand.line, "internal: unhandled addressable-operand kind");
}

void I8086Codegen::narrowAxTo(Type to) {
    int w = widthOfType(to);
    if (w == 1) {
        // No movzx/movsx on real 8086, and no "ah" operand in this dialect - zero-extend
        // by masking out bits above AL; sign-extend with CBW (AL into AH:AX).
        if (to.isUnsigned) {
            emit("and", "ax, 0x00FF");
        } else {
            emit("cbw");
        }
    }
    // 2 bytes: ax already holds the full value. A wide type never reaches here.
}

void I8086Codegen::loadOperand(const std::string &mem, Type type) {
    int w = widthOfType(type);
    if (w == 1) {
        if (type.isUnsigned) {
            emit("xor", "ax, ax");  // movb loads al only, never touches ah
            emit("movb", cat("ax, ", mem));
        } else {
            emit("movb", cat("ax, ", mem));
            emit("cbw");
        }
    } else {
        emit("mov", cat("ax, ", mem));
    }
}

void I8086Codegen::storeOperand(const std::string &mem, Type type) {
    if (widthOfType(type) == 1) {
        emit("movb", cat(mem, ", ax"));
    } else {
        emit("mov", cat(mem, ", ax"));
    }
}

// Leaves the address of slot's N-word storage in ax, words low-to-high as address
// increases. Global/Param storage already grows that way, so ax is just the base
// address. A Local's storage grows DOWN from bp as size increases, so its word 0 is
// the farthest word from bp: ax = bp - frameOffset - (N-1)*2, not bp - frameOffset.
void I8086Codegen::genWideSlotAddr(const ir::Slot *slot) {
    if (slot->kind == ir::SlotKind::Global) {
        emit("mov", cat("ax, ", label(slot)));
        return;
    }
    if (slot->kind == ir::SlotKind::Param) {
        emit("lea", cat("ax, ", addr(slot)));
        return;
    }
    int n = widthOfType(slot->type) / 2;
    emit("lea", cat("ax, ", addr(slot, 2 * (n - 1))));
}

// Builds a fresh toN-word low-to-high block on the stack from the fromN-word block
// at the address in bx (covers both a same-width copy and a widening/narrowing
// Convert). Pushes extension words first (highest addresses), then the real words
// high-to-low, so the least significant word ends up at the lowest address. Leaves
// the new block's address in ax.
void I8086Codegen::copyWideBlock(int fromN, int toN, bool isUnsignedExt) {
    int copyN = fromN < toN ? fromN : toN;
    if (toN > fromN) {
        emit("mov", cat("cx, [bx+", (2 * (fromN - 1)), "]"));  // cx = old MSW
        if (isUnsignedExt) {
            emit("xor", "dx, dx");
        } else {
            { emit("mov", "ax, cx"); emit("cwd"); }  // dx = sign-extension of old MSW
        }
        for (int i = 0; i < toN - fromN; i++) emit("push", "dx");
    }
    for (int i = copyN - 1; i >= 0; i--) {
        emit("mov", cat("cx, [bx+", (2 * i), "]"));
        emit("push", "cx");
    }
    emit("mov", "ax, sp");
}

// IRBuilder already promotes narrower operands before Binary/Unary sees them, so the
// only work here is crossing the native-int/wide boundary, or re-extending via narrowAxTo.
void I8086Codegen::genConvert(const ir::Expr &e) {
    Type from = e.lhs->type;
    Type to = e.type;
    bool fromWide = isWide(from);
    bool toWide = isWide(to);
    if (fromWide && !toWide) {
        wideScope([&] {
            genExpr(*e.lhs);
            { emit("mov", "bx, ax"); emit("mov", "ax, [bx]"); }  // least-significant word only
        });
        narrowAxTo(to);
        return;
    }
    genExpr(*e.lhs);  // ax, or an address if fromWide
    if (fromWide && toWide) {
        int fromN = widthOfType(from) / 2, toN = widthOfType(to) / 2;
        if (fromN == toN) return;  // same width - bit pattern unchanged
        emit("mov", "bx, ax");
        copyWideBlock(fromN, toN, from.isUnsigned);
    } else if (!fromWide && toWide) {
        int toN = widthOfType(to) / 2;
        if (from.isUnsigned) {
            emit("xor", "dx, dx");
        } else {
            emit("cwd");
        }
        for (int i = 0; i < toN - 2; i++) emit("push", "dx");
        { emit("push", "dx"); emit("push", "ax"); }
        emit("mov", "ax, sp");
    } else {
        narrowAxTo(to);
    }
}

// Two's complement across the N-word value at bx: NOT every word, then +1 with carry
// rippling through the rest.
void I8086Codegen::genWideNegate(int n) {
    for (int i = 0; i < n; i++) {
        { emit("mov", cat("ax, [bx+", (2 * i), "]")); emit("not", "ax"); emit("mov", cat("[bx+", (2 * i), "], ax")); }
    }
    { emit("mov", "ax, [bx]"); emit("add", "ax, 1"); emit("mov", "[bx], ax"); }
    for (int i = 1; i < n; i++) {
        { emit("mov", cat("ax, [bx+", (2 * i), "]")); emit("adc", "ax, 0"); emit("mov", cat("[bx+", (2 * i), "], ax")); }
    }
}

// long/long long add/sub: ripple carry/borrow across N words low-to-high via ADC/SBB.
void I8086Codegen::genWideAdd(const ir::Expr &lhs, const ir::Expr &rhs, bool isAdd) {
    int n = widthOfType(lhs.type) / 2;
    genExpr(lhs);
    // si holds the address as a value (not a bracket base), so rhs's own evaluation
    // (which may push its own scratch, moving sp) can't disturb it.
    emit("mov", "si, ax");
    genExpr(rhs);
    emit("mov", "di, ax");  // di = rhs address
    emit("sub", cat("sp, ", (2 * n)));
    // Only bx/bp can address memory here, so bx is repointed per base via `mov`
    // (never `add`, since `mov` doesn't touch FLAGS - preserving the carry chain).
    for (int i = 0; i < n; i++) {
        { emit("mov", "bx, si"); emit("mov", cat("ax, [bx+", (2 * i), "]")); }
        { emit("mov", "bx, di"); emit("mov", cat("cx, [bx+", (2 * i), "]")); }
        emit((i == 0 ? (isAdd ? "add" : "sub") : (isAdd ? "adc" : "sbb")), "ax, cx");
        { emit("mov", "bx, sp"); emit("mov", cat("[bx+", (2 * i), "], ax")); }
    }
    emit("mov", "ax, sp");
}

// long/long long multiply: schoolbook O(n^2) cross-multiply with hardware MUL for
// each 16x16->32 partial product, keeping only the low N words (discarding overflow).
void I8086Codegen::genWideMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    int n = widthOfType(lhs.type) / 2;
    genExpr(lhs);
    emit("mov", "si, ax");  // si = lhs address, held across rhs's own evaluation
    genExpr(rhs);
    emit("mov", "di, ax");  // di = rhs address
    emit("sub", cat("sp, ", (2 * n)));
    emit("mov", "bx, sp");
    emit("mov", "ax, 0");
    for (int i = 0; i < n; i++) emit("mov", cat("[bx+", (2 * i), "], ax"));
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n - i; j++) {
            // bx is repointed per base via `mov` (not `add`, which would disturb FLAGS).
            { emit("mov", "bx, si"); emit("mov", cat("ax, [bx+", (2 * i), "]")); }
            { emit("mov", "bx, di"); emit("mov", cat("cx, [bx+", (2 * j), "]")); }
            emit("mul", "cx");  // dx:ax = word_i(lhs) * word_j(rhs)
            // Add dx:ax into the result at word i+j, carry rippling up through the rest.
            emit("mov", "bx, sp");
            emit("mov", cat("cx, [bx+", (2 * (i + j)), "]"));
            emit("add", "cx, ax");
            emit("mov", cat("[bx+", (2 * (i + j)), "], cx"));
            if (i + j + 1 < n) {
                emit("mov", cat("cx, [bx+", (2 * (i + j + 1)), "]"));
                emit("adc", "cx, dx");
                emit("mov", cat("[bx+", (2 * (i + j + 1)), "], cx"));
                for (int k = i + j + 2; k < n; k++) {
                    { emit("mov", cat("cx, [bx+", (2 * k), "]")); emit("adc", "cx, 0"); emit("mov", cat("[bx+", (2 * k), "], cx")); }
                }
            }
        }
    }
    emit("mov", "ax, sp");
}

// Classic bit-serial shift/compare/subtract long division, generalized to N words
// (N=2 for `long`, N=4 for `long long`). Scratch block layout: [0..N-1]=dividend being
// consumed, [N..2N-1]=divisor, [2N..3N-1]=remainder being built, [3N..4N-1]=quotient
// being built, [4N]=iteration countdown.
//
// Signed division operates on magnitudes: negates each operand in place if its sign
// word is negative, remembers both signs, then restores them on the result per C's
// truncating-division rule (quotient negative iff signs differed; remainder takes the
// dividend's sign).
//
// This function is a thin call site: copy lhs/rhs into the shared N-word scratch block,
// call the one shared subroutine for this N, then take a fresh stack copy of whichever
// result was asked for (needed since a later division could overwrite the same scratch).
void I8086Codegen::genWideDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    int n = widthOfType(lhs.type) / 2;
    emitWideDivModSubroutine(n);
    std::string blk = "__wdm" + std::to_string(n) + "_blk";
    genExpr(lhs);
    emit("mov", "si, ax");  // lhs address
    genExpr(rhs);
    emit("mov", "di, ax");  // rhs address
    emit("mov", cat("ax, ", (isUnsigned ? 1 : 0)));
    { emit("mov", cat("bx, ", blk)); emit("mov", cat("[bx+", (2 * (4 * n + 3)), "], ax")); }
    for (int w = 0; w < n; w++) {
        { emit("mov", "bx, si"); emit("mov", cat("ax, [bx+", (2 * w), "]")); }
        { emit("mov", cat("bx, ", blk)); emit("mov", cat("[bx+", (2 * w), "], ax")); }
    }
    for (int w = 0; w < n; w++) {
        { emit("mov", "bx, di"); emit("mov", cat("ax, [bx+", (2 * w), "]")); }
        { emit("mov", cat("bx, ", blk)); emit("mov", cat("[bx+", (2 * (n + w)), "], ax")); }
    }
    emit("call", cat("__wdivmod", n));
    int resultOff = wantRemainder ? (2 * n) : (3 * n);
    { emit("mov", cat("bx, ", blk)); emit("lea", cat("ax, [bx+", (2 * resultOff), "]")); }
    emit("mov", "bx, ax");
    copyWideBlock(n, n, /*isUnsignedExt=*/false);  // fresh copy, since __wdmN_blk may be reused by a later division
}

// The N-word bit-serial divide itself, emitted once per N as a shared subroutine
// (addressed through a fixed global block, not the stack) - `isUnsigned` is a runtime
// flag word in that block, since one routine body serves both signed and unsigned calls.
void I8086Codegen::emitWideDivModSubroutine(int n) {
    if (!wideDivModEmitted_.insert(n).second) return;
    std::string blk = "__wdm" + std::to_string(n) + "_blk";
    trailer_.push_back(blk + ": dw " + [n] {
        std::string s;
        for (int i = 0; i < 4 * n + 4; i++) s += (i ? ", 0" : "0");
        return s;
    }());
    // The subroutine's code must land in trailer_, not mid-way through the caller's own
    // code_ - so code_ is swapped for a fresh buffer here and swapped back at the end.
    std::ostringstream callerCode;
    callerCode << code_.str();
    code_.str("");
    code_.clear();
    emitLabel(cat("__wdivmod", n));
    emit("mov", cat("bx, ", blk));
    int signLhsOff = 4 * n + 1, signRhsOff = 4 * n + 2, unsOff = 4 * n + 3;
    emit("mov", "ax, 0");
    for (int w = 0; w < n; w++) emit("mov", cat("[bx+", (2 * (2 * n + w)), "], ax"));  // r = 0
    for (int w = 0; w < n; w++) emit("mov", cat("[bx+", (2 * (3 * n + w)), "], ax"));  // q = 0
    { emit("mov", cat("ax, ", (16 * n))); emit("mov", cat("[bx+", (2 * 4 * n), "], ax")); }    // cnt
    std::string LskipSignEntry = newLabel();
    { emit("mov", cat("ax, [bx+", (2 * unsOff), "]")); emit("cmp", "ax, 0"); emit("jne", LskipSignEntry); }
    {
        std::string LskipNegN = newLabel(), LskipNegD = newLabel();
        { emit("mov", cat("ax, [bx+", (2 * (n - 1)), "]")); emit("cmp", "ax, 0"); emit("mov", "cx, 0"); emit("jge", LskipNegN); emit("mov", "cx, 1"); emitLabel(LskipNegN); emit("mov", cat("[bx+", (2 * signLhsOff), "], cx")); }
        { emit("mov", cat("ax, [bx+", (2 * (2 * n - 1)), "]")); emit("cmp", "ax, 0"); emit("mov", "cx, 0"); emit("jge", LskipNegD); emit("mov", "cx, 1"); emitLabel(LskipNegD); emit("mov", cat("[bx+", (2 * signRhsOff), "], cx")); }
        genWideNegateIf(0, n, signLhsOff);
        genWideNegateIf(n, n, signRhsOff);
    }
    emitLabel(LskipSignEntry);
    std::string Lloop = newLabel(), Ldone = newLabel();
    std::string LhiDiff = newLabel(), LsubYes = newLabel(), LsubNo = newLabel();
    emitLabel(Lloop);
    { emit("mov", cat("ax, [bx+", (2 * 4 * n), "]")); emit("cmp", "ax, 0"); }
    // jne-around-a-near-jmp reaches Ldone regardless of distance, since a 4-word
    // loop body can exceed a short jump's +-127-byte range.
    {
        std::string Lcontinue = newLabel();
        { emit("jne", Lcontinue); emit("jmp", Ldone); emitLabel(Lcontinue); }
    }
    { emit("sub", "ax, 1"); emit("mov", cat("[bx+", (2 * 4 * n), "], ax")); }
    // n <<= 1, capturing its old top bit via the carry flag (shl/rcl chain, same as
    // genWideShift); `jc` reads that pending carry into cx as a plain 0/1.
    { emit("mov", "ax, [bx+0]"); emit("shl", "ax, 1"); emit("mov", "[bx+0], ax"); }
    for (int w = 1; w < n; w++) {
        { emit("mov", cat("ax, [bx+", (2 * w), "]")); emit("rcl", "ax, 1"); emit("mov", cat("[bx+", (2 * w), "], ax")); }
    }
    std::string Lcarry0 = newLabel(), Lcarry1 = newLabel();
    { emit("jc", Lcarry1); emit("mov", "cx, 0"); emit("jmp", Lcarry0); }
    { emitLabel(Lcarry1); emit("mov", "cx, 1"); emitLabel(Lcarry0); }  // cx = n's own old top bit (the one shifted out)
    // r = (r << 1) | cx
    { emit("mov", cat("ax, [bx+", (2 * 2 * n), "]")); emit("shl", "ax, 1"); emit("or", "ax, cx"); emit("mov", cat("[bx+", (2 * 2 * n), "], ax")); }
    for (int w = 1; w < n; w++) {
        { emit("mov", cat("ax, [bx+", (2 * (2 * n + w)), "]")); emit("rcl", "ax, 1"); emit("mov", cat("[bx+", (2 * (2 * n + w)), "], ax")); }
    }
    // if (r >= d) { r -= d; quotient bit = 1 } else { quotient bit = 0 } - always an unsigned
    // magnitude compare, N words, most significant first.
    for (int k = n - 1; k >= 0; k--) {
        emit("mov", cat("ax, [bx+", (2 * (2 * n + k)), "]"));
        emit("mov", cat("cx, [bx+", (2 * (n + k)), "]"));
        emit("cmp", "ax, cx");
        { emit("ja", LsubYes); emit("jb", LsubNo); }
        // equal at this word - fall through to compare the next, less
        // significant one
    }
    emit("jmp", LsubYes);  // every word equal -> r == d -> subtract
    emitLabel(LsubYes);
    for (int w = 0; w < n; w++) {
        emit("mov", cat("ax, [bx+", (2 * (2 * n + w)), "]"));
        emit("mov", cat("cx, [bx+", (2 * (n + w)), "]"));
        emit((w == 0 ? "sub" : "sbb"), "ax, cx");
        emit("mov", cat("[bx+", (2 * (2 * n + w)), "], ax"));
    }
    { emit("mov", "cx, 1"); emit("jmp", LhiDiff); }
    { emitLabel(LsubNo); emit("mov", "cx, 0"); }
    emitLabel(LhiDiff);
    // q = (q << 1) | cx
    { emit("mov", cat("ax, [bx+", (2 * 3 * n), "]")); emit("shl", "ax, 1"); emit("or", "ax, cx"); emit("mov", cat("[bx+", (2 * 3 * n), "], ax")); }
    for (int w = 1; w < n; w++) {
        { emit("mov", cat("ax, [bx+", (2 * (3 * n + w)), "]")); emit("rcl", "ax, 1"); emit("mov", cat("[bx+", (2 * (3 * n + w)), "], ax")); }
    }
    emit("jmp", Lloop);
    emitLabel(Ldone);
    std::string LskipSignExit = newLabel();
    { emit("mov", cat("ax, [bx+", (2 * unsOff), "]")); emit("cmp", "ax, 0"); emit("jne", LskipSignExit); }
    {
        std::string LskipNegQ = newLabel(), LskipNegR = newLabel();
        { emit("mov", cat("ax, [bx+", (2 * signLhsOff), "]")); emit("mov", cat("cx, [bx+", (2 * signRhsOff), "]")); }
        { emit("cmp", "ax, cx"); emit("je", LskipNegQ); }
        genWideNegateAt(3 * n, n);
        emitLabel(LskipNegQ);
        { emit("mov", cat("ax, [bx+", (2 * signLhsOff), "]")); emit("cmp", "ax, 0"); emit("je", LskipNegR); }
        genWideNegateAt(2 * n, n);
        emitLabel(LskipNegR);
    }
    emitLabel(LskipSignExit);
    emit("ret");
    trailer_.push_back(code_.str());
    code_.str("");
    code_.clear();
    emitRaw(callerCode.str());
}

// Negates the N words at scratch offset wordOffset (bx = scratch base) only if the
// flag word at flagOffset is non-zero.
void I8086Codegen::genWideNegateIf(int wordOffset, int n, int flagOffset) {
    std::string Lskip = newLabel();
    { emit("mov", cat("ax, [bx+", (2 * flagOffset), "]")); emit("cmp", "ax, 0"); emit("je", Lskip); }
    genWideNegateAt(wordOffset, n);
    emitLabel(Lskip);
}

// Negates the N words at scratch offset wordOffset (bx = scratch base) in place.
void I8086Codegen::genWideNegateAt(int wordOffset, int n) {
    for (int w = 0; w < n; w++) {
        { emit("mov", cat("ax, [bx+", (2 * (wordOffset + w)), "]")); emit("not", "ax"); emit("mov", cat("[bx+", (2 * (wordOffset + w)), "], ax")); }
    }
    { emit("mov", cat("ax, [bx+", (2 * wordOffset), "]")); emit("add", "ax, 1"); emit("mov", cat("[bx+", (2 * wordOffset), "], ax")); }
    for (int w = 1; w < n; w++) {
        { emit("mov", cat("ax, [bx+", (2 * (wordOffset + w)), "]")); emit("adc", "ax, 0"); emit("mov", cat("[bx+", (2 * (wordOffset + w)), "], ax")); }
    }
}

// long/long long shift by a runtime count: a counted loop of 1-bit shifts, chaining
// across the N words via rcl/rcr and the carry flag. `>>` uses sar (signed) or shr
// (unsigned) on the most significant word; `<<` doesn't care about signedness.
void I8086Codegen::genWideShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned) {
    int n = widthOfType(lhs.type) / 2;
    genExpr(lhs);
    emit("mov", "si, ax");  // si = value's address, held across rhs's own evaluation
    genExpr(rhs);                  // ax = shift count
    emit("mov", "cx, ax");
    std::string Lloop = newLabel(), Ldone = newLabel();
    { emitLabel(Lloop); emit("cmp", "cx, 0"); emit("je", Ldone); emit("sub", "cx, 1"); }
    if (isLeft) {
        { emit("mov", "bx, si"); emit("mov", "ax, [bx+0]"); emit("shl", "ax, 1"); emit("mov", "[bx+0], ax"); }
        for (int w = 1; w < n; w++) {
            { emit("mov", "bx, si"); emit("mov", cat("ax, [bx+", (2 * w), "]")); emit("rcl", "ax, 1"); emit("mov", cat("[bx+", (2 * w), "], ax")); }
        }
    } else {
        { emit("mov", "bx, si"); emit("mov", cat("ax, [bx+", (2 * (n - 1)), "]")); emit((isUnsigned ? "shr" : "sar"), "ax, 1"); emit("mov", cat("[bx+", (2 * (n - 1)), "], ax")); }
        for (int w = n - 2; w >= 0; w--) {
            { emit("mov", "bx, si"); emit("mov", cat("ax, [bx+", (2 * w), "]")); emit("rcr", "ax, 1"); emit("mov", cat("[bx+", (2 * w), "], ax")); }
        }
    }
    { emit("jmp", Lloop); emitLabel(Ldone); }
    emit("mov", "ax, si");
}

// long/long long comparison: compares the most significant word first with the
// signed/unsigned jcc (the sign bit lives there); once a word differs, every word
// below is an unsigned magnitude compare.
void I8086Codegen::genWideCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned) {
    int n = widthOfType(lhs.type) / 2;
    genExpr(lhs);
    emit("mov", "si, ax");  // si = lhs address, held across rhs's own evaluation
    genExpr(rhs);
    emit("mov", "di, ax");
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    if (op == "==" || op == "!=") {
        for (int k = 0; k < n; k++) {
            { emit("mov", "bx, si"); emit("mov", cat("ax, [bx+", (2 * k), "]")); }
            { emit("mov", "bx, di"); emit("mov", cat("cx, [bx+", (2 * k), "]")); }
            { emit("cmp", "ax, cx"); emit("jne", Lfalse); }
        }
        { emit("mov", cat("ax, ", (op == "==" ? "1" : "0"))); emit("jmp", Ldone); }
        { emitLabel(Lfalse); emit("mov", cat("ax, ", (op == "==" ? "0" : "1"))); }
        emitLabel(Ldone);
        return;
    }
    bool resultIfLess = (op == "<" || op == "<=");
    bool resultIfGreater = (op == ">" || op == ">=");
    for (int k = n - 1; k >= 0; k--) {
        { emit("mov", "bx, si"); emit("mov", cat("ax, [bx+", (2 * k), "]")); }
        { emit("mov", "bx, di"); emit("mov", cat("cx, [bx+", (2 * k), "]")); }
        emit("cmp", "ax, cx");
        std::string lessJcc = (k == n - 1 && !isUnsigned) ? "jl" : "jb";
        std::string greaterJcc = (k == n - 1 && !isUnsigned) ? "jg" : "ja";
        emit(lessJcc, (resultIfLess ? Ltrue : Lfalse));
        emit(greaterJcc, (resultIfGreater ? Ltrue : Lfalse));
    }
    // Every word equal.
    { emit("mov", cat("ax, ", ((op == "<=" || op == ">=") ? "1" : "0"))); emit("jmp", Ldone); }
    { emitLabel(Ltrue); emit("mov", "ax, 1"); emit("jmp", Ldone); }
    { emitLabel(Lfalse); emit("mov", "ax, 0"); }
    emitLabel(Ldone);
}

void I8086Codegen::genLocalArrayInit(const ir::Slot *slot) {
    int w = widthOfType(slot->type);
    for (size_t i = 0; i < slot->arrayInit.size(); i++) {
        emit("mov", cat("ax, ", arrayElemText(slot->arrayInit[i])));
        std::string dst = addr(slot, w * static_cast<int>(i));
        if (w == 1) {
            emit("movb", cat(dst, ", ax"));
        } else {
            emit("mov", cat(dst, ", ax"));
        }
    }
}

void I8086Codegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    // e.lhs->type names the common operand type (IRBuilder's usual-arithmetic-conversion
    // pass), except for shift (only LHS matters) and &&/|| (handled separately below).
    Type operandType = e.lhs->type;
    bool wide = isWide(operandType);
    bool isUnsigned = operandType.isUnsigned;
    if (op == "+" || op == "-") {
        if (wide) {
            genWideAdd(*e.lhs, *e.rhs, op == "+");
            return;
        }
        genExpr(*e.lhs);
        emit("push", "ax");
        genExpr(*e.rhs);
        emit("pop", "bx");
        if (op == "+") {
            emit("add", "ax, bx");
        } else {
            { emit("sub", "bx, ax"); emit("mov", "ax, bx"); }
        }
        return;
    }
    if (op == "*") {
        if (wide) {
            genWideMul(*e.lhs, *e.rhs);
        } else {
            genMul(*e.lhs, *e.rhs, isUnsigned);
        }
        return;
    }
    if (op == "/" || op == "%") {
        if (wide) {
            genWideDivMod(*e.lhs, *e.rhs, /*wantRemainder=*/op == "%", isUnsigned);
        } else {
            genDivMod(*e.lhs, *e.rhs, /*wantRemainder=*/op == "%", isUnsigned);
        }
        return;
    }
    if (op == "&" || op == "|" || op == "^") {
        const char *insn = (op == "&") ? "and" : (op == "|") ? "or" : "xor";
        if (wide) {
            int n = widthOfType(operandType) / 2;
            genExpr(*e.lhs);
            emit("mov", "si, ax");  // si = lhs address, held across rhs's own evaluation
            genExpr(*e.rhs);
            emit("mov", "di, ax");
            emit("sub", cat("sp, ", (2 * n)));
            for (int i = 0; i < n; i++) {
                { emit("mov", "bx, si"); emit("mov", cat("ax, [bx+", (2 * i), "]")); }
                { emit("mov", "bx, di"); emit("mov", cat("cx, [bx+", (2 * i), "]")); }
                emit(insn, "ax, cx");
                { emit("mov", "bx, sp"); emit("mov", cat("[bx+", (2 * i), "], ax")); }
            }
            emit("mov", "ax, sp");
            return;
        }
        genExpr(*e.lhs);
        emit("push", "ax");
        genExpr(*e.rhs);
        emit("pop", "bx");
        emit(insn, "ax, bx");
        return;
    }
    if (op == "<<" || op == ">>") {
        if (wide) {
            genWideShift(*e.lhs, *e.rhs, op == "<<", isUnsigned);
            return;
        }
        // Real hardware variable-count shift: shl/shr/sar take the count from cl.
        genExpr(*e.lhs);
        emit("push", "ax");
        genExpr(*e.rhs);
        emit("mov", "cx, ax");
        emit("pop", "ax");
        const char *insn = op == "<<" ? "shl" : (isUnsigned ? "shr" : "sar");
        emit(insn, "ax, cl");
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

void I8086Codegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs) {
    bool isUnsigned = lhs.type.isUnsigned;
    if (isWide(lhs.type)) {
        wideScope([&] { genWideCompare(op, lhs, rhs, isUnsigned); });
        return;
    }
    if (op == "==") return emitCompare(lhs, rhs, "je", isUnsigned);
    if (op == "!=") return emitCompare(lhs, rhs, "jne", isUnsigned);
    if (op == "<") return emitCompare(lhs, rhs, isUnsigned ? "jb" : "jl", isUnsigned);
    if (op == "<=") return emitCompare(lhs, rhs, isUnsigned ? "jbe" : "jle", isUnsigned);
    if (op == ">") return emitCompare(lhs, rhs, isUnsigned ? "ja" : "jg", isUnsigned);
    if (op == ">=") return emitCompare(lhs, rhs, isUnsigned ? "jae" : "jge", isUnsigned);
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

// cmp bx, ax compares lhs-rhs (bx=lhs, ax=rhs), so jumpIfTrue matches C's comparison
// semantics directly. isUnsigned is already baked into jumpIfTrue by the caller.
void I8086Codegen::emitCompare(const ir::Expr &lhs, const ir::Expr &rhs, const std::string &jumpIfTrue, bool) {
    genExpr(lhs);
    emit("push", "ax");
    genExpr(rhs);
    emit("pop", "bx");
    emit("cmp", "bx, ax");
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit(jumpIfTrue, Ltrue);
    { emit("mov", "ax, 0"); emit("jmp", Ldone); }
    { emitLabel(Ltrue); emit("mov", "ax, 1"); }
    emitLabel(Ldone);
}

void I8086Codegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    genTruthValue(lhs);
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    { emit("cmp", "ax, 0"); emit("je", Lfalse); }
    genTruthValue(rhs);
    { emit("cmp", "ax, 0"); emit("je", Lfalse); }
    emit("jmp", Ltrue);
    { emitLabel(Lfalse); emit("mov", "ax, 0"); emit("jmp", Ldone); }
    { emitLabel(Ltrue); emit("mov", "ax, 1"); }
    emitLabel(Ldone);
}

void I8086Codegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    genTruthValue(lhs);
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    { emit("cmp", "ax, 0"); emit("jne", Ltrue); }
    genTruthValue(rhs);
    { emit("cmp", "ax, 0"); emit("jne", Ltrue); }
    emit("jmp", Lfalse);
    { emitLabel(Ltrue); emit("mov", "ax, 1"); emit("jmp", Ldone); }
    { emitLabel(Lfalse); emit("mov", "ax, 0"); }
    emitLabel(Ldone);
}

void I8086Codegen::genLogicalNot(const ir::Expr &rhs) {
    genTruthValue(rhs);
    std::string Ltrue = newLabel(), Ldone = newLabel();
    { emit("cmp", "ax, 0"); emit("je", Ltrue); }
    { emit("mov", "ax, 0"); emit("jmp", Ldone); }
    { emitLabel(Ltrue); emit("mov", "ax, 1"); }
    emitLabel(Ldone);
}


// Reduces the N-word value at the address in ax to a single word in ax that's zero
// iff every word was zero (every wide helper leaves an address in ax, not a value).
void I8086Codegen::genWideTruthify(Type t) {
    int n = widthOfType(t) / 2;
    { emit("mov", "bx, ax"); emit("mov", "ax, [bx]"); }
    for (int i = 1; i < n; i++) {
        { emit("mov", cat("cx, [bx+", (2 * i), "]")); emit("or", "ax, cx"); }
    }
}

void I8086Codegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned) {
    genExpr(lhs);
    emit("push", "ax");
    genExpr(rhs);
    emit("pop", "bx");
    emit((isUnsigned ? "mul" : "imul"), "bx");  // dx:ax = ax*bx; keep ax
}

void I8086Codegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    emit("push", "ax");
    genExpr(rhs);
    emit("mov", "bx, ax");  // bx = divisor
    emit("pop", "ax");      // ax = dividend
    emit((isUnsigned ? "xor dx, dx\n" : "cwd\n"));
    emit((isUnsigned ? "div" : "idiv"), "bx");  // ax = quotient, dx = remainder
    if (wantRemainder) emit("mov", "ax, dx");
}

// A from-scratch x86 instruction encoder, no external assembler needed.
//
// `--format=raw`, like every other target here, is a plain whole-program flat binary -
// no boot sector, no padding, no signature (building a bootable image is out of scope
// for this project; see I8086LinkerFactory.cpp's own entry-stub comment).
std::optional<std::string> I8086Codegen::assembleInProcess(const std::string &asmText,
                                                             const std::string &format) const {
    const uint16_t machineType = dosAbi_ ? kI8086DosCoffMachine : kI8086CoffMachine;
    if (format == "raw") return assembleI8086(asmText, I8086OutputFormat::Bin);
    // `--format=coff` is real separate compilation: an undefined symbol is deferred
    // as a relocation for a future `--link` step.
    if (format == "coff") {
        return assembleI8086(asmText, I8086OutputFormat::Coff, /*allowExternal=*/true, machineType);
    }
    // `--format=elf` is also a complete self-contained program: an undefined symbol is
    // a hard error.
    if (format == "elf") return assembleI8086(asmText, I8086OutputFormat::Elf, /*allowExternal=*/false, machineType);
    return std::nullopt;
}

std::optional<std::string> I8086Codegen::debugMap(const std::string &asmText, const std::string &format,
                                                    const std::string &sourcePath) const {
    if (format == "coff") return std::nullopt;
    return assembleI8086DebugMap(asmText, sourcePath, debugSymbols_);
}

std::optional<std::string> I8086Codegen::debugMapPreLink(const std::string &asmText,
                                                    const std::string &sourcePath) const {
    return assembleI8086DebugMap(asmText, sourcePath, debugSymbols_, /*allowExternal=*/true);
}

// Only emits a marker when `debugMapEnabled_` (set via setDebugMapEnabled(),
// before generate() runs) and `line` actually differs from the last one
// emitted - see Pdp7Codegen.cpp's identical emitSourceLine(), which this
// mirrors exactly.
void I8086Codegen::emitSourceLine(int line) {
    if (!debugMapEnabled_ || line == 0 || line == lastDebugLine_) return;
    emitRaw(cat("__line ", line));
    lastDebugLine_ = line;
}

}  // namespace rcc
