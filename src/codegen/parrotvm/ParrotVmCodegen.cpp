#include "ParrotVmCodegen.h"

#include <stdexcept>
#include <tuple>
#include <vector>

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

// Headroom above the globals region for the per-call bump-allocated "stack" - see the class
// comment's "$regs" global. Generous and fixed, the same kind of documented bound every
// other backend's own small address space has (e.g. pdp1's 4096-word field).
constexpr long kStackReserve = 65536;
}  // namespace

int ParrotVmCodegen::funcIndex(const ir::Function *fn) {
    auto it = funcIndices_.find(fn);
    if (it != funcIndices_.end()) return it->second;
    int idx = static_cast<int>(funcTable_.size());
    funcIndices_[fn] = idx;
    funcTable_.push_back(fn);
    return idx;
}

std::string ParrotVmCodegen::narrow(const std::string &reg, Type to) {
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
        return reg;  // int/long/longlong: Parrot's native $I already covers every one of
                     // these widths in a single register - no masking needed.
    }
    std::string masked = newReg();
    code_ << "   band " << masked << ", " << reg << ", " << mask << "\n";
    if (to.isUnsigned) return masked;
    std::string signReg = newReg();
    code_ << "   band " << signReg << ", " << masked << ", " << signBit << "\n";
    std::string result = newReg();
    std::string Lneg = newLabel(), Ldone = newLabel();
    code_ << "   if " << signReg << " goto " << Lneg << "\n";
    code_ << "   " << result << " = " << masked << "\n";
    code_ << "   goto " << Ldone << "\n";
    code_ << Lneg << ":\n";
    code_ << "   sub " << result << ", " << masked << ", " << wrap << "\n";
    code_ << Ldone << ":\n";
    return result;
}

std::string ParrotVmCodegen::loadMem(const std::string &addrReg, Type t) {
    std::string reg = newReg();
    code_ << "   " << reg << " = mem[" << addrReg << "]\n";
    return narrow(reg, t);
}

void ParrotVmCodegen::storeMem(const std::string &addrReg, const std::string &valueReg, Type t) {
    std::string narrowed = narrow(valueReg, t);
    code_ << "   mem[" << addrReg << "] = " << narrowed << "\n";
}

std::string ParrotVmCodegen::slotAddress(const ir::Slot *slot) {
    std::string reg = newReg();
    if (slot->kind == ir::SlotKind::Global) {
        code_ << "   " << reg << " = " << globalOffsets_.at(slot) << "\n";
        return reg;
    }
    long off = localOffsets_.at(slot);
    if (off == 0) {
        code_ << "   " << reg << " = $I_fp\n";
    } else {
        code_ << "   add " << reg << ", $I_fp, " << off << "\n";
    }
    return reg;
}

std::string ParrotVmCodegen::elementAddress(const ir::Slot *slot, const ir::Expr &idxExpr) {
    std::string base = slotAddress(slot);
    std::string idx = genExpr(idxExpr);
    int elemW = widthOfType(slot->type);
    std::string scaled = idx;
    if (elemW != 1) {
        scaled = newReg();
        code_ << "   mul " << scaled << ", " << idx << ", " << elemW << "\n";
    }
    std::string addr = newReg();
    code_ << "   add " << addr << ", " << base << ", " << scaled << "\n";
    return addr;
}

std::string ParrotVmCodegen::loadSlot(const ir::Slot *slot) { return loadMem(slotAddress(slot), slot->type); }

void ParrotVmCodegen::storeSlot(const ir::Slot *slot, const std::string &valueReg) {
    storeMem(slotAddress(slot), valueReg, slot->type);
}

std::string ParrotVmCodegen::genAddress(const ir::Expr &e) {
    if (e.kind == ir::ExprKind::Var) return slotAddress(e.slot);
    if (e.kind == ir::ExprKind::Index) return elementAddress(e.slot, *e.lhs);
    if (e.kind == ir::ExprKind::Func) {
        std::string reg = newReg();
        code_ << "   " << reg << " = " << funcIndex(e.func) << "\n";
        return reg;
    }
    err(e.line, "internal: unhandled addressable-operand kind in parrotvm backend");
}

std::string ParrotVmCodegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string a = genExpr(lhs), b = genExpr(rhs), d = newReg();
    if (op == "==") {
        code_ << "   iseq " << d << ", " << a << ", " << b << "\n";
    } else if (op == "!=") {
        code_ << "   isne " << d << ", " << a << ", " << b << "\n";
    } else if (op == "<") {
        code_ << "   islt " << d << ", " << a << ", " << b << "\n";
    } else if (op == "<=") {
        code_ << "   isle " << d << ", " << a << ", " << b << "\n";
    } else if (op == ">") {
        code_ << "   isgt " << d << ", " << a << ", " << b << "\n";
    } else if (op == ">=") {
        code_ << "   isge " << d << ", " << a << ", " << b << "\n";
    } else {
        err(lhs.line, "internal: unhandled comparison operator '" + op + "' in parrotvm backend");
    }
    return d;
}

void ParrotVmCodegen::genCondBranch(const ir::Expr &cond, const std::string &Lfalse) {
    if (cond.kind == ir::ExprKind::Binary && cond.op == "&&") {
        genCondBranch(*cond.lhs, Lfalse);
        genCondBranch(*cond.rhs, Lfalse);
        return;
    }
    std::string reg = genExpr(cond);
    code_ << "   unless " << reg << " goto " << Lfalse << "\n";
}

std::string ParrotVmCodegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string result = newReg();
    std::string Lfalse = newLabel(), Ldone = newLabel();
    genCondBranch(lhs, Lfalse);
    genCondBranch(rhs, Lfalse);
    code_ << "   " << result << " = 1\n   goto " << Ldone << "\n";
    code_ << Lfalse << ":\n   " << result << " = 0\n";
    code_ << Ldone << ":\n";
    return result;
}

std::string ParrotVmCodegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string result = newReg();
    std::string Ltrue = newLabel(), Ldone = newLabel();
    std::string a = genExpr(lhs);
    code_ << "   if " << a << " goto " << Ltrue << "\n";
    std::string b = genExpr(rhs);
    code_ << "   if " << b << " goto " << Ltrue << "\n";
    code_ << "   " << result << " = 0\n   goto " << Ldone << "\n";
    code_ << Ltrue << ":\n   " << result << " = 1\n";
    code_ << Ldone << ":\n";
    return result;
}

std::string ParrotVmCodegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    if (op == "&&") return genLogicalAnd(*e.lhs, *e.rhs);
    if (op == "||") return genLogicalOr(*e.lhs, *e.rhs);
    if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
        return genCompare(op, *e.lhs, *e.rhs);
    }
    std::string a = genExpr(*e.lhs);
    std::string b = genExpr(*e.rhs);
    std::string d = newReg();
    if (op == "+") {
        code_ << "   add " << d << ", " << a << ", " << b << "\n";
    } else if (op == "-") {
        code_ << "   sub " << d << ", " << a << ", " << b << "\n";
    } else if (op == "*") {
        code_ << "   mul " << d << ", " << a << ", " << b << "\n";
    } else if (op == "/") {
        code_ << "   div " << d << ", " << a << ", " << b << "\n";
    } else if (op == "%") {
        code_ << "   mod " << d << ", " << a << ", " << b << "\n";
    } else if (op == "<<") {
        code_ << "   shl " << d << ", " << a << ", " << b << "\n";
    } else if (op == ">>") {
        code_ << "   shr " << d << ", " << a << ", " << b << "\n";
    } else if (op == "&") {
        code_ << "   band " << d << ", " << a << ", " << b << "\n";
    } else if (op == "^") {
        code_ << "   bxor " << d << ", " << a << ", " << b << "\n";
    } else if (op == "|") {
        code_ << "   bor " << d << ", " << a << ", " << b << "\n";
    } else {
        err(e.line, "internal: unhandled binary operator '" + op + "' in parrotvm backend");
    }
    return narrow(d, e.type);
}

std::string ParrotVmCodegen::applyCompoundOp(const ir::Expr &e, const std::string &oldVal,
                                              const std::string &rhsVal) {
    const std::string &op = e.op;
    std::string d = newReg();
    if (op == "+") {
        code_ << "   add " << d << ", " << oldVal << ", " << rhsVal << "\n";
    } else if (op == "-") {
        code_ << "   sub " << d << ", " << oldVal << ", " << rhsVal << "\n";
    } else if (op == "*") {
        code_ << "   mul " << d << ", " << oldVal << ", " << rhsVal << "\n";
    } else if (op == "/") {
        code_ << "   div " << d << ", " << oldVal << ", " << rhsVal << "\n";
    } else if (op == "%") {
        code_ << "   mod " << d << ", " << oldVal << ", " << rhsVal << "\n";
    } else if (op == "<<") {
        code_ << "   shl " << d << ", " << oldVal << ", " << rhsVal << "\n";
    } else if (op == ">>") {
        code_ << "   shr " << d << ", " << oldVal << ", " << rhsVal << "\n";
    } else if (op == "&") {
        code_ << "   band " << d << ", " << oldVal << ", " << rhsVal << "\n";
    } else if (op == "^") {
        code_ << "   bxor " << d << ", " << oldVal << ", " << rhsVal << "\n";
    } else if (op == "|") {
        code_ << "   bor " << d << ", " << oldVal << ", " << rhsVal << "\n";
    } else {
        err(e.line, "internal: unhandled compound-assignment operator '" + op + "' in parrotvm backend");
    }
    return narrow(d, e.type);
}

std::string ParrotVmCodegen::genExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::IntLit: {
            std::string reg = newReg();
            code_ << "   " << reg << " = " << e.intValue << "\n";
            return reg;
        }
        case ir::ExprKind::Var:
            return loadSlot(e.slot);
        case ir::ExprKind::Index:
            return loadMem(elementAddress(e.slot, *e.lhs), e.slot->type);
        case ir::ExprKind::Assign: {
            if (e.lhs) {
                std::string addr = elementAddress(e.slot, *e.lhs);
                std::string val = genExpr(*e.rhs);
                storeMem(addr, val, e.slot->type);
                return val;
            }
            std::string val = genExpr(*e.rhs);
            storeSlot(e.slot, val);
            return val;
        }
        case ir::ExprKind::DerefAssign: {
            std::string addr = genExpr(*e.lhs);
            std::string val = genExpr(*e.rhs);
            storeMem(addr, val, e.type);
            return val;
        }
        case ir::ExprKind::CompoundAssign: {
            std::string addr, oldVal;
            if (e.lhs) {
                addr = elementAddress(e.slot, *e.lhs);
                oldVal = loadMem(addr, e.slot->type);
            } else {
                oldVal = loadSlot(e.slot);
            }
            std::string rhsVal = genExpr(*e.rhs);
            std::string newVal = applyCompoundOp(e, oldVal, rhsVal);
            if (e.lhs) {
                storeMem(addr, newVal, e.slot->type);
            } else {
                storeSlot(e.slot, newVal);
            }
            return e.yieldOld ? oldVal : newVal;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            std::string addr = genExpr(*e.lhs);
            std::string oldVal = loadMem(addr, e.type);
            std::string rhsVal = genExpr(*e.rhs);
            std::string newVal = applyCompoundOp(e, oldVal, rhsVal);
            storeMem(addr, newVal, e.type);
            return e.yieldOld ? oldVal : newVal;
        }
        case ir::ExprKind::Unary: {
            if (e.op == "-") {
                std::string v = genExpr(*e.rhs);
                std::string d = newReg();
                code_ << "   neg " << d << ", " << v << "\n";
                return narrow(d, e.type);
            }
            if (e.op == "!") {
                std::string v = genExpr(*e.rhs);
                std::string d = newReg();
                code_ << "   iseq " << d << ", " << v << ", 0\n";
                return d;
            }
            if (e.op == "~") {
                // C's '~x' is the VALUE -x-1, representation-independent (same identity
                // Pdp1Codegen uses for the same operator).
                std::string v = genExpr(*e.rhs);
                std::string t = newReg();
                code_ << "   neg " << t << ", " << v << "\n";
                std::string d = newReg();
                code_ << "   sub " << d << ", " << t << ", 1\n";
                return narrow(d, e.type);
            }
            if (e.op == "*") {
                std::string addr = genExpr(*e.rhs);
                return loadMem(addr, e.type);
            }
            return genAddress(*e.rhs);  // "&"
        }
        case ir::ExprKind::Binary:
            return genBinary(e);
        case ir::ExprKind::Call: {
            std::vector<std::string> argRegs;
            argRegs.reserve(e.args.size());
            for (const auto &a : e.args) argRegs.push_back(genExpr(*a));
            std::string argList;
            for (size_t i = 0; i < argRegs.size(); i++) argList += (i ? ", " : "") + argRegs[i];
            if (e.func->isVoid) {
                code_ << "   '" << label(e.func) << "'(" << argList << ")\n";
                std::string d = newReg();
                code_ << "   " << d << " = 0\n";
                return d;
            }
            std::string d = newReg();
            code_ << "   " << d << " = '" << label(e.func) << "'(" << argList << ")\n";
            return d;
        }
        case ir::ExprKind::CallPtr: {
            std::string idx = genExpr(*e.lhs);
            std::string sub = newReg("P");
            code_ << "   " << sub << " = funcs[" << idx << "]\n";
            std::string d = newReg();
            code_ << "   " << d << " = " << sub << "()\n";
            return d;
        }
        case ir::ExprKind::Intrinsic: {
            // No interrupt-mask/IO instruction exists on this target (no such hardware
            // concept) - a no-op value, same as a target with no real instruction for it.
            (void)e.op;
            std::string d = newReg();
            code_ << "   " << d << " = 0\n";
            return d;
        }
        case ir::ExprKind::Convert:
            return narrow(genExpr(*e.lhs), e.type);
        case ir::ExprKind::Ternary: {
            std::string result = newReg();
            std::string Lelse = newLabel(), Ldone = newLabel();
            genCondBranch(*e.lhs, Lelse);
            std::string thenVal = genExpr(*e.rhs);
            code_ << "   " << result << " = " << thenVal << "\n   goto " << Ldone << "\n";
            code_ << Lelse << ":\n";
            std::string elseVal = genExpr(*e.elseExpr);
            code_ << "   " << result << " = " << elseVal << "\n";
            code_ << Ldone << ":\n";
            return result;
        }
        case ir::ExprKind::Func:
            err(e.line, "internal: Func reached genExpr - should only ever appear as '&'s operand");
    }
    err(e.line, "internal: unhandled expression kind in parrotvm backend");
}

void ParrotVmCodegen::genStmt(const ir::Stmt &s) {
    switch (s.kind) {
        case ir::StmtKind::Block: {
            for (const auto &child : s.body) genStmt(*child);
            return;
        }
        case ir::StmtKind::If: {
            std::string Lelse = newLabel(), Ldone = newLabel();
            genCondBranch(*s.cond, s.elseStmt ? Lelse : Ldone);
            genStmt(*s.thenStmt);
            if (s.elseStmt) {
                code_ << "   goto " << Ldone << "\n";
                code_ << Lelse << ":\n";
                genStmt(*s.elseStmt);
            }
            code_ << Ldone << ":\n";
            return;
        }
        case ir::StmtKind::While: {
            std::string Lstart = newLabel(), Lend = newLabel();
            std::string Lcont = s.postStmt ? newLabel() : Lstart;
            code_ << Lstart << ":\n";
            genCondBranch(*s.cond, Lend);
            loopLabels_.push_back({Lcont, Lend});
            genStmt(*s.loopBody);
            loopLabels_.pop_back();
            if (s.postStmt) {
                code_ << Lcont << ":\n";
                genStmt(*s.postStmt);
            }
            code_ << "   goto " << Lstart << "\n";
            code_ << Lend << ":\n";
            return;
        }
        case ir::StmtKind::Break:
            code_ << "   goto " << loopLabels_.back().second << "\n";
            return;
        case ir::StmtKind::Continue:
            code_ << "   goto " << loopLabels_.back().first << "\n";
            return;
        case ir::StmtKind::Return: {
            bool hasValue = s.initOrReturnOrExpr != nullptr;
            std::string retReg = hasValue ? genExpr(*s.initOrReturnOrExpr) : std::string();
            emitEpilogueAndReturn(hasValue, retReg);
            return;
        }
        case ir::StmtKind::LocalDecl: {
            if (s.declSlot->arraySize > 0) {
                int elemW = widthOfType(s.declSlot->type);
                for (size_t i = 0; i < s.declSlot->arrayInit.size(); i++) {
                    const ir::ArrayInitElem &elem = s.declSlot->arrayInit[i];
                    std::string addr = newReg();
                    long offset = static_cast<long>(i) * elemW;
                    if (offset == 0) {
                        code_ << "   " << addr << " = " << slotAddress(s.declSlot) << "\n";
                    } else {
                        std::string base = slotAddress(s.declSlot);
                        code_ << "   add " << addr << ", " << base << ", " << offset << "\n";
                    }
                    std::string val = newReg();
                    if (!elem.isSymbol) {
                        code_ << "   " << val << " = " << elem.value << "\n";
                    } else if (elem.slot) {
                        code_ << "   " << val << " = " << slotAddress(elem.slot) << "\n";
                    } else {
                        code_ << "   " << val << " = " << funcIndex(elem.func) << "\n";
                    }
                    code_ << "   mem[" << addr << "] = " << val << "\n";
                }
                return;
            }
            std::string val = s.initOrReturnOrExpr ? genExpr(*s.initOrReturnOrExpr) : newReg();
            if (!s.initOrReturnOrExpr) code_ << "   " << val << " = 0\n";
            storeSlot(s.declSlot, val);
            return;
        }
        case ir::StmtKind::ExprStmt:
            genExpr(*s.initOrReturnOrExpr);
            return;
        case ir::StmtKind::Empty:
            return;
        case ir::StmtKind::Asm: {
            code_ << s.asmText;
            if (!s.asmText.empty() && s.asmText.back() != '\n') code_ << "\n";
            return;
        }
        case ir::StmtKind::Label:
            code_ << irLabel(s.labelId) << ":\n";
            return;
        case ir::StmtKind::Goto:
            code_ << "   goto " << irLabel(s.labelId) << "\n";
            return;
    }
}

void ParrotVmCodegen::collectLocalDecls(const ir::Stmt *s) {
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

void ParrotVmCodegen::emitEpilogueAndReturn(bool hasValue, const std::string &retReg) {
    std::string newSp = newReg();
    code_ << "   add " << newSp << ", $I_fp, " << currentFrameSize_ << "\n";
    code_ << "   regs[0] = " << newSp << "\n";
    if (hasValue) {
        code_ << "   .return (" << retReg << ")\n";
    } else {
        code_ << "   .return ()\n";
    }
}

void ParrotVmCodegen::genFunction(const ir::FunctionBody &fn) {
    const ir::Function *f = fn.func;

    localOffsets_.clear();
    currentFrameSize_ = 0;
    for (ir::Slot *p : f->params) {
        localOffsets_[p] = currentFrameSize_;
        currentFrameSize_ += slotUnits(p);
    }
    collectLocalDecls(fn.body.get());

    code_ << ".sub '" << label(f) << "'\n";
    for (ir::Slot *p : f->params) {
        code_ << "   .param int p" << p->paramIndex << "\n";
    }
    code_ << "   .local pmc mem\n";
    code_ << "   .local pmc regs\n";
    code_ << "   .local pmc funcs\n";
    code_ << "   mem = get_global '$mem'\n";
    code_ << "   regs = get_global '$regs'\n";
    code_ << "   funcs = get_global '$funcs'\n";
    code_ << "   .local int $I_sp\n";
    code_ << "   .local int $I_fp\n";
    code_ << "   $I_sp = regs[0]\n";
    if (currentFrameSize_ > 0) code_ << "   sub $I_sp, $I_sp, " << currentFrameSize_ << "\n";
    code_ << "   $I_fp = $I_sp\n";
    code_ << "   regs[0] = $I_sp\n";
    for (ir::Slot *p : f->params) {
        std::string addr = slotAddress(p);
        code_ << "   mem[" << addr << "] = p" << p->paramIndex << "\n";
    }

    genStmt(*fn.body);
    emitEpilogueAndReturn(false, "");
    code_ << ".end\n";
}

std::string ParrotVmCodegen::generate(const ir::Program &prog, bool objectMode) {
    (void)objectMode;  // this backend has no separate-compilation/object mode - always whole-program,
                        // same as Pdp1Codegen.

    globalOffsets_.clear();
    long globalsSize = 0;
    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global || slot->isExternal) continue;
        globalOffsets_[slot.get()] = globalsSize;
        globalsSize += slotUnits(slot.get());
    }

    funcIndices_.clear();
    funcTable_.clear();
    for (const auto &fb : prog.functionBodies) funcIndex(fb.func);

    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }

    std::string homeFile = mainFn ? mainFn->sourceFile
                            : !prog.functionBodies.empty() ? prog.functionBodies.front().func->sourceFile
                            : !prog.slots.empty()          ? prog.slots.front()->sourceFile
                                                            : std::string();

    std::vector<std::tuple<std::string, size_t, size_t>> ranges;

    long memSize = globalsSize + kStackReserve;
    size_t startPos = static_cast<size_t>(code_.tellp());
    code_ << ".sub '_start' :main\n";
    code_ << "   .local pmc mem\n";
    code_ << "   mem = new ['ResizableIntegerArray']\n";
    code_ << "   mem = " << memSize << "\n";
    code_ << "   set_global '$mem', mem\n";
    code_ << "   .local pmc regs\n";
    code_ << "   regs = new ['ResizableIntegerArray']\n";
    code_ << "   regs = 1\n";
    code_ << "   regs[0] = " << memSize << "\n";
    code_ << "   set_global '$regs', regs\n";
    code_ << "   .local pmc funcs\n";
    code_ << "   funcs = new ['ResizablePMCArray']\n";
    code_ << "   funcs = " << funcTable_.size() << "\n";
    for (size_t i = 0; i < funcTable_.size(); i++) {
        code_ << "   funcs[" << i << "] = find_sub '" << label(funcTable_[i]) << "'\n";
    }
    code_ << "   set_global '$funcs', funcs\n";
    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global || slot->isExternal) continue;
        long base = globalOffsets_[slot.get()];
        if (slot->arraySize > 0) {
            int elemW = widthOfType(slot->type);
            for (size_t i = 0; i < slot->arrayInit.size(); i++) {
                const ir::ArrayInitElem &elem = slot->arrayInit[i];
                long addr = base + static_cast<long>(i) * elemW;
                if (!elem.isSymbol) {
                    code_ << "   mem[" << addr << "] = " << elem.value << "\n";
                } else if (elem.slot) {
                    code_ << "   mem[" << addr << "] = " << globalOffsets_[elem.slot] << "\n";
                } else {
                    code_ << "   mem[" << addr << "] = " << funcIndex(elem.func) << "\n";
                }
            }
        } else if (slot->hasInit) {
            code_ << "   mem[" << base << "] = " << slot->initValue << "\n";
        }
    }
    if (mainFn) {
        code_ << "   $I0 = '" << label(mainFn) << "'()\n";
    }
    code_ << "   .return ()\n";
    code_ << ".end\n";
    ranges.push_back({homeFile, startPos, static_cast<size_t>(code_.tellp())});

    for (const auto &fb : prog.functionBodies) {
        size_t fStart = static_cast<size_t>(code_.tellp());
        genFunction(fb);
        ranges.push_back({fb.func->sourceFile, fStart, static_cast<size_t>(code_.tellp())});
    }

    std::string text = code_.str();
    perFileAsm_.clear();
    for (const auto &[file, s, e] : ranges) perFileAsm_[file] += text.substr(s, e - s);
    return text;
}

}  // namespace rcc
