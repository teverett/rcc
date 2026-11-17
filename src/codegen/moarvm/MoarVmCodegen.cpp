#include "MoarVmCodegen.h"

#include <stdexcept>

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

// Headroom above the globals region for the per-call bump-allocated "stack", tracked in
// mem[0] - see MoarVmCodegen.h's class comment. Same role as ParrotVmCodegen.cpp's own
// kStackReserve.
constexpr long kStackReserve = 65536;

// MoarVM op numbers used directly as plain binOp/unOp opcodes below - see
// MoarVmAssembler.cpp's own op-number table comment for how these were obtained.
constexpr int kOpEq = 56, kOpNe = 57, kOpLt = 58, kOpLe = 59, kOpGt = 60, kOpGe = 61;
constexpr int kOpAdd = 63, kOpSub = 64, kOpMul = 65, kOpDiv = 66, kOpMod = 68;
constexpr int kOpNeg = 70;
constexpr int kOpBlshift = 80, kOpBrshift = 81;
constexpr int kOpBand = 76, kOpBor = 77, kOpBxor = 78;
constexpr int kOpChr = 229;
constexpr int kOpConcatS = 208;
}  // namespace

int MoarVmCodegen::newLocal(MoarVmAssembler::LocalType type) { return asm_.addLocal(type); }

int MoarVmCodegen::loadConst(long value) {
    int reg = newLocal();
    asm_.emitConstI64(reg, value);
    return reg;
}

int MoarVmCodegen::irLabel(int id) {
    auto it = irLabels_.find(id);
    if (it != irLabels_.end()) return it->second;
    int lbl = asm_.newLabel();
    irLabels_[id] = lbl;
    return lbl;
}

int MoarVmCodegen::frameIndexOf(const ir::Function *fn) {
    auto it = frameIndices_.find(fn);
    if (it == frameIndices_.end()) {
        throw std::runtime_error("internal: moarvm backend has no frame for function '" + fn->sourceName + "'");
    }
    return it->second;
}

int MoarVmCodegen::narrow(int reg, Type to) {
    if (isPointer(to)) return reg;
    long mask, signBit, wrap;
    if (to.base == BaseType::Char) {
        mask = 0xFF;
        signBit = 0x80;
        wrap = 0x100;
    } else if (to.base == BaseType::Short) {
        mask = 0xFFFF;
        signBit = 0x8000;
        wrap = 0x10000;
    } else {
        return reg;  // int/long/longlong: this backend's int64 register already covers
                     // every one of these widths - no masking needed.
    }
    int maskReg = loadConst(mask);
    int masked = newLocal();
    asm_.emitBinOp(kOpBand, masked, reg, maskReg);
    if (to.isUnsigned) return masked;
    int signBitReg = loadConst(signBit);
    int signReg = newLocal();
    asm_.emitBinOp(kOpBand, signReg, masked, signBitReg);
    int result = newLocal();
    int Lneg = asm_.newLabel(), Ldone = asm_.newLabel();
    asm_.emitIfI(signReg, Lneg);
    asm_.emitSet(result, masked);
    asm_.emitGoto(Ldone);
    asm_.placeLabel(Lneg);
    int wrapReg = loadConst(wrap);
    asm_.emitBinOp(kOpSub, result, masked, wrapReg);
    asm_.placeLabel(Ldone);
    return result;
}

int MoarVmCodegen::loadMem(int addrReg, Type t) {
    int reg = newLocal();
    asm_.emitAtposI(reg, memLocal_, addrReg);
    return narrow(reg, t);
}

void MoarVmCodegen::storeMem(int addrReg, int valueReg, Type t) {
    int narrowed = narrow(valueReg, t);
    asm_.emitBindposI(memLocal_, addrReg, narrowed);
}

int MoarVmCodegen::slotAddress(const ir::Slot *slot) {
    if (slot->kind == ir::SlotKind::Global) return loadConst(globalOffsets_.at(slot));
    long off = localOffsets_.at(slot);
    if (off == 0) {
        int reg = newLocal();
        asm_.emitSet(reg, fpLocal_);
        return reg;
    }
    int offReg = loadConst(off);
    int reg = newLocal();
    asm_.emitBinOp(kOpAdd, reg, fpLocal_, offReg);
    return reg;
}

int MoarVmCodegen::elementAddress(const ir::Slot *slot, const ir::Expr &idxExpr) {
    int base = slotAddress(slot);
    int idx = genExpr(idxExpr);
    int elemW = widthOfType(slot->type);
    int scaled = idx;
    if (elemW != 1) {
        int elemWReg = loadConst(elemW);
        scaled = newLocal();
        asm_.emitBinOp(kOpMul, scaled, idx, elemWReg);
    }
    int addr = newLocal();
    asm_.emitBinOp(kOpAdd, addr, base, scaled);
    return addr;
}

int MoarVmCodegen::loadSlot(const ir::Slot *slot) { return loadMem(slotAddress(slot), slot->type); }

void MoarVmCodegen::storeSlot(const ir::Slot *slot, int valueReg) { storeMem(slotAddress(slot), valueReg, slot->type); }

int MoarVmCodegen::genAddress(const ir::Expr &e) {
    if (e.kind == ir::ExprKind::Var) return slotAddress(e.slot);
    if (e.kind == ir::ExprKind::Index) return elementAddress(e.slot, *e.lhs);
    if (e.kind == ir::ExprKind::Func) return loadConst(funcIndices_.at(e.func));
    err(e.line, "internal: unhandled addressable-operand kind in moarvm backend");
}

int MoarVmCodegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs) {
    int a = genExpr(lhs), b = genExpr(rhs), d = newLocal();
    int opcode;
    if (op == "==") {
        opcode = kOpEq;
    } else if (op == "!=") {
        opcode = kOpNe;
    } else if (op == "<") {
        opcode = kOpLt;
    } else if (op == "<=") {
        opcode = kOpLe;
    } else if (op == ">") {
        opcode = kOpGt;
    } else if (op == ">=") {
        opcode = kOpGe;
    } else {
        err(lhs.line, "internal: unhandled comparison operator '" + op + "' in moarvm backend");
    }
    asm_.emitBinOp(opcode, d, a, b);
    return d;
}

void MoarVmCodegen::genCondBranch(const ir::Expr &cond, int Lfalse) {
    if (cond.kind == ir::ExprKind::Binary && cond.op == "&&") {
        genCondBranch(*cond.lhs, Lfalse);
        genCondBranch(*cond.rhs, Lfalse);
        return;
    }
    int reg = genExpr(cond);
    asm_.emitUnlessI(reg, Lfalse);
}

int MoarVmCodegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    int result = newLocal();
    int Lfalse = asm_.newLabel(), Ldone = asm_.newLabel();
    genCondBranch(lhs, Lfalse);
    genCondBranch(rhs, Lfalse);
    asm_.emitSet(result, loadConst(1));
    asm_.emitGoto(Ldone);
    asm_.placeLabel(Lfalse);
    asm_.emitSet(result, loadConst(0));
    asm_.placeLabel(Ldone);
    return result;
}

int MoarVmCodegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    int result = newLocal();
    int Ltrue = asm_.newLabel(), Ldone = asm_.newLabel();
    int a = genExpr(lhs);
    asm_.emitIfI(a, Ltrue);
    int b = genExpr(rhs);
    asm_.emitIfI(b, Ltrue);
    asm_.emitSet(result, loadConst(0));
    asm_.emitGoto(Ldone);
    asm_.placeLabel(Ltrue);
    asm_.emitSet(result, loadConst(1));
    asm_.placeLabel(Ldone);
    return result;
}

int MoarVmCodegen::applyBinOp(const std::string &op, int a, int b, int line) {
    int opcode;
    if (op == "+") {
        opcode = kOpAdd;
    } else if (op == "-") {
        opcode = kOpSub;
    } else if (op == "*") {
        opcode = kOpMul;
    } else if (op == "/") {
        opcode = kOpDiv;
    } else if (op == "%") {
        opcode = kOpMod;
    } else if (op == "<<") {
        opcode = kOpBlshift;
    } else if (op == ">>") {
        opcode = kOpBrshift;
    } else if (op == "&") {
        opcode = kOpBand;
    } else if (op == "^") {
        opcode = kOpBxor;
    } else if (op == "|") {
        opcode = kOpBor;
    } else {
        err(line, "internal: unhandled binary operator '" + op + "' in moarvm backend");
    }
    int d = newLocal();
    asm_.emitBinOp(opcode, d, a, b);
    return d;
}

int MoarVmCodegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    if (op == "&&") return genLogicalAnd(*e.lhs, *e.rhs);
    if (op == "||") return genLogicalOr(*e.lhs, *e.rhs);
    if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
        return genCompare(op, *e.lhs, *e.rhs);
    }
    int a = genExpr(*e.lhs);
    int b = genExpr(*e.rhs);
    return narrow(applyBinOp(op, a, b, e.line), e.type);
}

int MoarVmCodegen::applyCompoundOp(const ir::Expr &e, int oldVal, int rhsVal) {
    return narrow(applyBinOp(e.op, oldVal, rhsVal, e.line), e.type);
}

void MoarVmCodegen::genSayBuiltin(int addrReg) {
    int idx = newLocal();
    asm_.emitSet(idx, addrReg);
    int acc = newLocal(MoarVmAssembler::LocalType::Str);
    asm_.emitConstStr(acc, asm_.internString(""));
    int Lstart = asm_.newLabel(), Lend = asm_.newLabel();
    asm_.placeLabel(Lstart);
    int byteVal = newLocal();
    asm_.emitAtposI(byteVal, memLocal_, idx);
    int cond = newLocal();
    asm_.emitBinOp(kOpEq, cond, byteVal, loadConst(0));
    asm_.emitIfI(cond, Lend);
    int ch = newLocal(MoarVmAssembler::LocalType::Str);
    asm_.emitUnOp(kOpChr, ch, byteVal);
    asm_.emitBinOp(kOpConcatS, acc, acc, ch);
    asm_.emitBinOp(kOpAdd, idx, idx, loadConst(1));
    asm_.emitGoto(Lstart);
    asm_.placeLabel(Lend);
    asm_.emitSay(acc);
}

int MoarVmCodegen::genExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit:
            return loadConst(e.intValue);
        case ir::ExprKind::Var:
            return loadSlot(e.slot);
        case ir::ExprKind::Index:
            return loadMem(elementAddress(e.slot, *e.lhs), e.slot->type);
        case ir::ExprKind::Assign: {
            if (e.lhs) {
                int addr = elementAddress(e.slot, *e.lhs);
                int val = genExpr(*e.rhs);
                storeMem(addr, val, e.slot->type);
                return val;
            }
            int val = genExpr(*e.rhs);
            storeSlot(e.slot, val);
            return val;
        }
        case ir::ExprKind::DerefAssign: {
            int addr = genExpr(*e.lhs);
            int val = genExpr(*e.rhs);
            storeMem(addr, val, e.type);
            return val;
        }
        case ir::ExprKind::CompoundAssign: {
            int addr = -1, oldVal;
            if (e.lhs) {
                addr = elementAddress(e.slot, *e.lhs);
                oldVal = loadMem(addr, e.slot->type);
            } else {
                oldVal = loadSlot(e.slot);
            }
            int rhsVal = genExpr(*e.rhs);
            int newVal = applyCompoundOp(e, oldVal, rhsVal);
            if (e.lhs) {
                storeMem(addr, newVal, e.slot->type);
            } else {
                storeSlot(e.slot, newVal);
            }
            return e.yieldOld ? oldVal : newVal;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            int addr = genExpr(*e.lhs);
            int oldVal = loadMem(addr, e.type);
            int rhsVal = genExpr(*e.rhs);
            int newVal = applyCompoundOp(e, oldVal, rhsVal);
            storeMem(addr, newVal, e.type);
            return e.yieldOld ? oldVal : newVal;
        }
        case ir::ExprKind::Unary: {
            if (e.op == "-") {
                int v = genExpr(*e.rhs);
                int d = newLocal();
                asm_.emitUnOp(kOpNeg, d, v);
                return narrow(d, e.type);
            }
            if (e.op == "!") {
                int v = genExpr(*e.rhs);
                int d = newLocal();
                asm_.emitBinOp(kOpEq, d, v, loadConst(0));
                return d;
            }
            if (e.op == "~") {
                // C's '~x' is the VALUE -x-1, representation-independent (same identity
                // ParrotVmCodegen/Pdp1Codegen use for the same operator).
                int v = genExpr(*e.rhs);
                int t = newLocal();
                asm_.emitUnOp(kOpNeg, t, v);
                int d = newLocal();
                asm_.emitBinOp(kOpSub, d, t, loadConst(1));
                return narrow(d, e.type);
            }
            if (e.op == "*") {
                int addr = genExpr(*e.rhs);
                return loadMem(addr, e.type);
            }
            return genAddress(*e.rhs);  // "&"
        }
        case ir::ExprKind::Binary:
            return genBinary(e);
        case ir::ExprKind::Call: {
            if (e.func->isExternal && e.func->sourceName == "moarvm_say" && e.args.size() == 1) {
                genSayBuiltin(genExpr(*e.args[0]));
                return loadConst(0);
            }
            if (e.func->isExternal) {
                err(e.line, "moarvm backend has no runtime library - only 'moarvm_say(char*)' is "
                            "supported as an extern call (see docs/known-bugs.md)");
            }
            std::vector<int> argRegs = {memLocal_, funcsLocal_};
            std::vector<uint8_t> flags = {MoarVmAssembler::kArgObj, MoarVmAssembler::kArgObj,
                                           MoarVmAssembler::kArgObj};
            for (const auto &a : e.args) {
                argRegs.push_back(genExpr(*a));
                flags.push_back(MoarVmAssembler::kArgInt);
            }
            int callee = newLocal(MoarVmAssembler::LocalType::Obj);
            asm_.emitGetCode(callee, frameIndexOf(e.func));
            std::vector<int> fullArgs = {callee};
            fullArgs.insert(fullArgs.end(), argRegs.begin(), argRegs.end());
            int cs = asm_.internCallsite(flags);
            int d = newLocal();
            asm_.emitDispatchI(d, "lang-call", cs, fullArgs);
            return d;
        }
        case ir::ExprKind::CallPtr: {
            int idx = genExpr(*e.lhs);
            int codeObj = newLocal(MoarVmAssembler::LocalType::Obj);
            asm_.emitAtposO(codeObj, funcsLocal_, idx);
            std::vector<int> fullArgs = {codeObj, memLocal_, funcsLocal_};
            std::vector<uint8_t> flags = {MoarVmAssembler::kArgObj, MoarVmAssembler::kArgObj,
                                           MoarVmAssembler::kArgObj};
            int d = newLocal();
            int cs = asm_.internCallsite(flags);
            asm_.emitDispatchI(d, "lang-call", cs, fullArgs);
            return d;
        }
        case ir::ExprKind::Intrinsic:
            // No such hardware concept exists on this target - a no-op value, same as
            // ParrotVmCodegen's own handling.
            (void)e.op;
            return loadConst(0);
        case ir::ExprKind::Convert:
            return narrow(genExpr(*e.lhs), e.type);
        case ir::ExprKind::Ternary: {
            int result = newLocal();
            int Lelse = asm_.newLabel(), Ldone = asm_.newLabel();
            genCondBranch(*e.lhs, Lelse);
            int thenVal = genExpr(*e.rhs);
            asm_.emitSet(result, thenVal);
            asm_.emitGoto(Ldone);
            asm_.placeLabel(Lelse);
            int elseVal = genExpr(*e.elseExpr);
            asm_.emitSet(result, elseVal);
            asm_.placeLabel(Ldone);
            return result;
        }
        case ir::ExprKind::Func:
            err(e.line, "internal: Func reached genExpr - should only ever appear as '&'s operand");
    }
    err(e.line, "internal: unhandled expression kind in moarvm backend");
}

void MoarVmCodegen::genStmt(const ir::Stmt &s) {
    switch (s.kind) {
        case ir::StmtKind::Block:
            for (const auto &child : s.body) genStmt(*child);
            return;
        case ir::StmtKind::If: {
            int Lelse = asm_.newLabel(), Ldone = asm_.newLabel();
            genCondBranch(*s.cond, s.elseStmt ? Lelse : Ldone);
            genStmt(*s.thenStmt);
            if (s.elseStmt) {
                asm_.emitGoto(Ldone);
                asm_.placeLabel(Lelse);
                genStmt(*s.elseStmt);
            }
            asm_.placeLabel(Ldone);
            return;
        }
        case ir::StmtKind::While: {
            int Lstart = asm_.newLabel(), Lend = asm_.newLabel();
            int Lcont = s.postStmt ? asm_.newLabel() : Lstart;
            asm_.placeLabel(Lstart);
            genCondBranch(*s.cond, Lend);
            loopLabels_.push_back({Lcont, Lend});
            genStmt(*s.loopBody);
            loopLabels_.pop_back();
            if (s.postStmt) {
                asm_.placeLabel(Lcont);
                genStmt(*s.postStmt);
            }
            asm_.emitGoto(Lstart);
            asm_.placeLabel(Lend);
            return;
        }
        case ir::StmtKind::Break:
            asm_.emitGoto(loopLabels_.back().second);
            return;
        case ir::StmtKind::Continue:
            asm_.emitGoto(loopLabels_.back().first);
            return;
        case ir::StmtKind::Return: {
            bool hasValue = s.initOrReturnOrExpr != nullptr;
            int retReg = hasValue ? genExpr(*s.initOrReturnOrExpr) : -1;
            emitEpilogueAndReturn(hasValue, retReg);
            return;
        }
        case ir::StmtKind::LocalDecl: {
            if (s.declSlot->arraySize > 0) {
                int elemW = widthOfType(s.declSlot->type);
                for (size_t i = 0; i < s.declSlot->arrayInit.size(); i++) {
                    const ir::ArrayInitElem &elem = s.declSlot->arrayInit[i];
                    long offset = static_cast<long>(i) * elemW;
                    int base = slotAddress(s.declSlot);
                    int addr = base;
                    if (offset != 0) {
                        addr = newLocal();
                        asm_.emitBinOp(kOpAdd, addr, base, loadConst(offset));
                    }
                    int val;
                    if (!elem.isSymbol) {
                        val = loadConst(elem.value);
                    } else if (elem.slot) {
                        val = loadConst(globalOffsets_.at(elem.slot));
                    } else {
                        val = loadConst(funcIndices_.at(elem.func));
                    }
                    asm_.emitBindposI(memLocal_, addr, val);
                }
                return;
            }
            int val = s.initOrReturnOrExpr ? genExpr(*s.initOrReturnOrExpr) : loadConst(0);
            storeSlot(s.declSlot, val);
            return;
        }
        case ir::StmtKind::ExprStmt:
            genExpr(*s.initOrReturnOrExpr);
            return;
        case ir::StmtKind::Empty:
            return;
        case ir::StmtKind::Asm:
            err(s.line, "moarvm backend has no textual assembly form, so 'asm {}' blocks are not "
                        "supported (see docs/known-bugs.md)");
        case ir::StmtKind::Label:
            asm_.placeLabel(irLabel(s.labelId));
            return;
        case ir::StmtKind::Goto:
            asm_.emitGoto(irLabel(s.labelId));
            return;
    }
}

void MoarVmCodegen::collectLocalDecls(const ir::Stmt *s) {
    if (!s) return;
    switch (s->kind) {
        case ir::StmtKind::Block:
            for (const auto &c : s->body) collectLocalDecls(c.get());
            return;
        case ir::StmtKind::If:
            collectLocalDecls(s->thenStmt.get());
            collectLocalDecls(s->elseStmt.get());
            return;
        case ir::StmtKind::While:
            collectLocalDecls(s->loopBody.get());
            collectLocalDecls(s->postStmt.get());
            return;
        case ir::StmtKind::LocalDecl:
            localOffsets_[s->declSlot] = currentFrameSize_;
            currentFrameSize_ += slotUnits(s->declSlot);
            return;
        default:
            return;
    }
}

void MoarVmCodegen::emitEpilogueAndReturn(bool hasValue, int retReg) {
    int newSp = newLocal();
    asm_.emitBinOp(kOpAdd, newSp, fpLocal_, loadConst(currentFrameSize_));
    asm_.emitBindposI(memLocal_, loadConst(0), newSp);
    // Every frame returns via return_i, even a void C function, so every call site -
    // direct or through a function pointer, where the callee's actual voidness is not
    // statically known - can uniformly use dispatch_i. Mirrors ParrotVmCodegen's own single
    // $I-register convention, which works the same way for the same reason.
    asm_.emitReturnI(hasValue ? retReg : loadConst(0));
}

void MoarVmCodegen::genFunction(const ir::FunctionBody &fn) {
    const ir::Function *f = fn.func;

    localOffsets_.clear();
    currentFrameSize_ = 0;
    for (ir::Slot *p : f->params) {
        localOffsets_[p] = currentFrameSize_;
        currentFrameSize_ += slotUnits(p);
    }
    collectLocalDecls(fn.body.get());

    int expectedIdx = frameIndexOf(f);
    int gotIdx = asm_.beginFrame(f->sourceName);
    (void)expectedIdx;
    (void)gotIdx;

    memLocal_ = newLocal(MoarVmAssembler::LocalType::Obj);
    funcsLocal_ = newLocal(MoarVmAssembler::LocalType::Obj);
    std::vector<int> paramRegs;
    paramRegs.reserve(f->params.size());
    for (size_t i = 0; i < f->params.size(); i++) paramRegs.push_back(newLocal());

    int16_t total = static_cast<int16_t>(2 + f->params.size());
    asm_.emitCheckarity(total, total);
    asm_.emitParamRpO(memLocal_, 0);
    asm_.emitParamRpO(funcsLocal_, 1);
    for (size_t i = 0; i < f->params.size(); i++) {
        asm_.emitParamRpI(paramRegs[i], static_cast<int16_t>(2 + i));
    }

    int spVal = newLocal();
    asm_.emitAtposI(spVal, memLocal_, loadConst(0));
    fpLocal_ = newLocal();
    if (currentFrameSize_ > 0) {
        asm_.emitBinOp(kOpSub, fpLocal_, spVal, loadConst(currentFrameSize_));
    } else {
        asm_.emitSet(fpLocal_, spVal);
    }
    asm_.emitBindposI(memLocal_, loadConst(0), fpLocal_);

    for (size_t i = 0; i < f->params.size(); i++) {
        int addr = slotAddress(f->params[i]);
        storeMem(addr, paramRegs[i], f->params[i]->type);
    }

    genStmt(*fn.body);
    emitEpilogueAndReturn(false, -1);
    asm_.endFrame();
}

std::string MoarVmCodegen::generate(const ir::Program &prog, bool objectMode) {
    (void)objectMode;  // whole-program only, like ParrotVmCodegen and VexCodegen.

    globalOffsets_.clear();
    long globalsSize = 0;
    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global || slot->isExternal) continue;
        globalOffsets_[slot.get()] = 1 + globalsSize;  // mem[0] is reserved for the sp cell
        globalsSize += slotUnits(slot.get());
    }

    funcIndices_.clear();
    funcTable_.clear();
    frameIndices_.clear();
    for (size_t i = 0; i < prog.functionBodies.size(); i++) {
        const ir::Function *fn = prog.functionBodies[i].func;
        funcIndices_[fn] = static_cast<int>(i);
        funcTable_.push_back(fn);
        frameIndices_[fn] = static_cast<int>(i) + 1;  // the entry frame takes index 0
    }

    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }

    std::string homeFile = mainFn ? mainFn->sourceFile
                            : !prog.functionBodies.empty() ? prog.functionBodies.front().func->sourceFile
                            : !prog.slots.empty()          ? prog.slots.front()->sourceFile
                                                            : std::string();

    std::vector<std::pair<std::string, size_t>> fileLogStarts;

    asm_.internString("");  // required empty string at string-heap index 0

    size_t entryLogStart = static_cast<size_t>(log_.tellp());
    int entryFrame = asm_.beginFrame("entry");
    (void)entryFrame;  // always 0 - the first frame generated

    int memType = newLocal(MoarVmAssembler::LocalType::Obj);
    memLocal_ = newLocal(MoarVmAssembler::LocalType::Obj);
    int funcsType = newLocal(MoarVmAssembler::LocalType::Obj);
    funcsLocal_ = newLocal(MoarVmAssembler::LocalType::Obj);
    asm_.emitCheckarity(0, 0);
    asm_.emitBootIntArray(memType);
    asm_.emitCreate(memLocal_, memType);
    asm_.emitBootArray(funcsType);
    asm_.emitCreate(funcsLocal_, funcsType);
    asm_.emitBindposI(memLocal_, loadConst(0), loadConst(1 + globalsSize + kStackReserve));

    for (size_t i = 0; i < funcTable_.size(); i++) {
        int code = newLocal(MoarVmAssembler::LocalType::Obj);
        asm_.emitGetCode(code, frameIndices_.at(funcTable_[i]));
        asm_.emitBindposO(funcsLocal_, loadConst(static_cast<long>(i)), code);
    }

    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global || slot->isExternal) continue;
        long base = globalOffsets_[slot.get()];
        if (slot->arraySize > 0) {
            int elemW = widthOfType(slot->type);
            for (size_t i = 0; i < slot->arrayInit.size(); i++) {
                const ir::ArrayInitElem &elem = slot->arrayInit[i];
                long addr = base + static_cast<long>(i) * elemW;
                int val;
                if (!elem.isSymbol) {
                    val = loadConst(elem.value);
                } else if (elem.slot) {
                    val = loadConst(globalOffsets_[elem.slot]);
                } else {
                    val = loadConst(funcIndices_.at(elem.func));
                }
                asm_.emitBindposI(memLocal_, loadConst(addr), val);
            }
        } else if (slot->hasInit) {
            asm_.emitBindposI(memLocal_, loadConst(base), loadConst(slot->initValue));
        }
    }

    if (mainFn) {
        int callee = newLocal(MoarVmAssembler::LocalType::Obj);
        asm_.emitGetCode(callee, frameIndices_.at(mainFn));
        int cs = asm_.internCallsite({MoarVmAssembler::kArgObj, MoarVmAssembler::kArgObj, MoarVmAssembler::kArgObj});
        asm_.emitDispatchV("lang-call", cs, {callee, memLocal_, funcsLocal_});
    }
    asm_.emitReturnVoid();
    asm_.endFrame();
    fileLogStarts.push_back({homeFile, entryLogStart});

    for (const auto &fb : prog.functionBodies) {
        size_t fStart = static_cast<size_t>(log_.tellp());
        genFunction(fb);
        fileLogStarts.push_back({fb.func->sourceFile, fStart});
    }

    log("; moarvm backend: " + std::to_string(frameIndices_.size() + 1) + " frame(s) generated");
    std::string logText = log_.str();
    perFileAsm_.clear();
    for (size_t i = 0; i < fileLogStarts.size(); i++) {
        size_t start = fileLogStarts[i].second;
        size_t end = i + 1 < fileLogStarts.size() ? fileLogStarts[i + 1].second : logText.size();
        perFileAsm_[fileLogStarts[i].first] += logText.substr(start, end - start);
    }

    asm_.setMainFrame(entryFrame);
    return asm_.finish();
}

}  // namespace rcc
