#include "PowerPCCodegen.h"

#include <cstdint>
#include <stdexcept>

#include "PowerPCAssembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

// Where the whole program is loaded and starts executing; the test harness
// sets the simulated CPU's PC here directly.
constexpr uint32_t kBaseAddress = 0x00100000u;

// Where this backend's entry sequence points r1 before calling main(),
// above the loaded program, growing down.
constexpr uint32_t kInitialSP = 0x00200000u;
}  // namespace

// A literal element's decimal text; a symbol element is the referenced
// Slot's or Function's label. "dw" also records the same external-name/
// base-shift relocation a lis/ori address immediate gets.
std::string PowerPCCodegen::arrayElemText(const ir::ArrayInitElem &e) {
    if (!e.isSymbol) return std::to_string(static_cast<int32_t>(static_cast<uint32_t>(e.value)));
    return e.slot ? label(e.slot) : label(e.func);
}

void PowerPCCodegen::declareArray(const std::string &lbl, const std::vector<ir::ArrayInitElem> &values,
                                    Type elemType) {
    if (values.empty()) return;
    int w = widthOfType(elemType);
    const char *directive = w == 1 ? "db" : w == 2 ? "dh" : "dw";
    std::ostringstream line;
    line << lbl << ": " << directive << " ";
    bool first = true;
    for (const auto &e : values) {
        if (w == 8 && !e.isSymbol) {
            auto v = static_cast<uint64_t>(e.value);
            if (!first) line << ", ";
            line << static_cast<uint32_t>(v) << ", " << static_cast<uint32_t>(v >> 32);
            first = false;
            continue;
        }
        if (!first) line << ", ";
        line << arrayElemText(e);
        first = false;
    }
    trailer_.push_back(line.str());
}

void PowerPCCodegen::loadAddress(const std::string &reg, const std::string &lbl) {
    emit("lis", cat(reg, ",", lbl));
    emit("ori", cat(reg, ",", reg, ",", lbl));
}

void PowerPCCodegen::loadImmediate(const std::string &reg, int32_t v) {
    if (v >= -32768 && v <= 32767) {
        emit("li", cat(reg, ",", v));
        return;
    }
    uint32_t uv = static_cast<uint32_t>(v);
    emit("lis", cat(reg, ",", static_cast<int32_t>((uv >> 16) & 0xFFFFu)));
    emit("ori", cat(reg, ",", reg, ",", static_cast<int32_t>(uv & 0xFFFFu)));
}

void PowerPCCodegen::loadSlotValue(const std::string &reg, const ir::Slot *slot) {
    if (slot->kind == ir::SlotKind::Global) {
        loadAddress("t2", label(slot));
        loadOperand("t2", reg, slot->type);
        return;
    }
    loadOperandDisp(slotDisp(slot), reg, slot->type);
}

void PowerPCCodegen::storeSlotValue(const ir::Slot *slot, const std::string &reg) {
    if (slot->kind == ir::SlotKind::Global) {
        loadAddress("t2", label(slot));
        storeOperand("t2", reg, slot->type);
        return;
    }
    storeOperandDisp(slotDisp(slot), reg, slot->type);
}

std::string PowerPCCodegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    // `objectMode` means no object file emits this entry sequence itself;
    // PowerPCLinkerFactory.cpp synthesizes it once every object is merged.
    // An object file's base is `org 0` - placement is the linker's job.
    emitRaw(cat("org ", (objectMode ? 0 : kBaseAddress)));
    emitRaw("");
    if (!objectMode) {
        emitLabel("_start");
        emit("lis", cat("sp,", static_cast<int32_t>((kInitialSP >> 16) & 0xFFFFu)));
        emit("ori", cat("sp,sp,", static_cast<int32_t>(kInitialSP & 0xFFFFu)));
        emit("bl", label(mainFn));
        // The result sits in t0 the moment execution reaches `halt`.
        emitLabel("halt");
        emit("b", "halt");
        emitRaw("");
    }

    for (const auto &fb : prog.functionBodies) genFunction(fb);

    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        // An external global isn't defined in this file; it must never get
        // a storage word here, only be referenced by name, letting the
        // assembler's undefined-symbol handling defer it to a relocation.
        if (slot->isExternal) continue;
        const std::string lbl = label(slot.get());
        if (slot->arraySize > 0) {
            declareArray(lbl, slot->arrayInit, slot->type);
        } else if (slot->type.base == BaseType::Struct) {
            // No struct initializer exists in this subset, so this is
            // always zero-filled; reuses declareArray's own element-list
            // emission (one zero "db" per byte) rather than a new
            // directive, since this assembler dialect has no "reserve N
            // zero bytes" form of its own.
            declareArray(lbl, std::vector<ir::ArrayInitElem>(slot->type.structSize), Type{BaseType::Char, false});
        } else {
            long v = slot->hasInit ? slot->initValue : 0;
            int w = widthOfType(slot->type);
            if (w == 8) {
                auto uv = static_cast<uint64_t>(v);
                trailer_.push_back(lbl + ": dw " + std::to_string(static_cast<uint32_t>(uv)) + ", " +
                                    std::to_string(static_cast<uint32_t>(uv >> 32)) + "  ; " + slot->sourceName);
            } else {
                const char *directive = w == 1 ? "db" : w == 2 ? "dh" : "dw";
                trailer_.push_back(lbl + ": " + directive + " " +
                                    std::to_string(static_cast<int32_t>(static_cast<uint32_t>(v))) + "  ; " +
                                    slot->sourceName);
            }
        }
    }

    std::ostringstream out;
    out << code_.str();
    for (const auto &line : trailer_) out << line << "\n";
    return out.str();
}

// Single-file-only, as in MipsCodegen.cpp (no `--format=coff` here).
std::map<std::string, std::string> PowerPCCodegen::perFileAsm() const { return perFileAsm_; }

// Saves/restores the link register (via `mflr`/`mtlr` through `r0` -
// PowerPC's LR isn't directly memory-addressable) around every function,
// leaf or not, since any call overwrites LR.
//
// The prologue pushes the saved return address then the caller's `fp`,
// then points `fp` at the second pushed word: `fp+0` is the caller's `fp`,
// `fp+4` is the saved return address, `fp+8+4*i` is argument i (matching
// slotDisp()'s Param displacement). `addi sp,sp,-4*frameSize` then reserves
// this call's Locals below `fp` (matching slotDisp()'s Local displacement)
// - each call gets its own `fp`, chained through the stack, making
// recursion safe.
void PowerPCCodegen::genFunction(const ir::FunctionBody &fn) {
    emitLabel(label(fn.func));
    emit("mflr", "r0");
    emit("addi", "sp,sp,-4");
    emit("stind", "sp,r0");
    emit("addi", "sp,sp,-4");
    emit("stind", "sp,fp");
    emit("or", "fp,sp,sp");
    if (fn.func->frameSize > 0) emit("addi", cat("sp,sp,-", fn.func->frameSize));

    genStmt(*fn.body);

    emitEpilogue();
    emit("blr");
    emitRaw("");
}

// Restores `fp` and the link register, leaving `sp` at argument 0 again.
// The caller, not the callee, pops the arguments it pushed (caller-cleans-
// up convention, as in I386Codegen.cpp).
void PowerPCCodegen::emitEpilogue() {
    emit("ldind", "fp,t2");
    emit("addi", "sp,fp,4");
    emit("ldind", "sp,r0");
    emit("addi", "sp,sp,4");
    emit("mtlr", "r0");
    emit("or", "fp,t2,t2");
}

void PowerPCCodegen::genStmt(const ir::Stmt &s) {
    switch (s.kind) {
        case ir::StmtKind::Block: {
            for (const auto &child : s.body) genStmt(*child);
            return;
        }
        case ir::StmtKind::If: {
            genExpr(*s.cond);
            if (isWide(s.cond->type)) emit("or", "t0,t0,t1");
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            emit("cmpwi", "t0,0");
            emit("beq", (s.elseStmt ? Lelse : Ldone));
            genStmt(*s.thenStmt);
            if (s.elseStmt) {
                emit("b", Ldone);
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
            genExpr(*s.cond);
            if (isWide(s.cond->type)) emit("or", "t0,t0,t1");
            emit("cmpwi", "t0,0");
            emit("beq", Lend);
            loopLabels_.push_back({Lcont, Lend});
            genStmt(*s.loopBody);
            loopLabels_.pop_back();
            if (s.postStmt) {
                emitLabel(Lcont);
                genStmt(*s.postStmt);
            }
            emit("b", Lstart);
            emitLabel(Lend);
            return;
        }
        case ir::StmtKind::Break: {
            emit("b", loopLabels_.back().second);
            return;
        }
        case ir::StmtKind::Continue: {
            emit("b", loopLabels_.back().first);
            return;
        }
        case ir::StmtKind::Return: {
            if (s.initOrReturnOrExpr) genExpr(*s.initOrReturnOrExpr);
            // An early exit needs the same frame teardown inline before
            // `blr`; `t0` (the return value) is never touched by it.
            emitEpilogue();
            emit("blr");
            return;
        }
        case ir::StmtKind::LocalDecl: {
            if (s.declSlot->arraySize > 0) {
                genLocalArrayInit(s.declSlot);
            } else if (s.initOrReturnOrExpr) {
                genExpr(*s.initOrReturnOrExpr);
                storeSlotValue(s.declSlot, "t0");
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
            emit("b", irLabel(s.labelId));
            return;
        }
    }
}

void PowerPCCodegen::genExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit: {
            if (isWide(e.type)) {
                auto v = static_cast<uint64_t>(e.intValue);
                loadImmediate("t0", static_cast<int32_t>(static_cast<uint32_t>(v)));
                loadImmediate("t1", static_cast<int32_t>(static_cast<uint32_t>(v >> 32)));
                return;
            }
            loadImmediate("t0", static_cast<int32_t>(static_cast<uint32_t>(e.intValue)));
            return;
        }
        case ir::ExprKind::Var: {
            loadSlotValue("t0", e.slot);
            return;
        }
        case ir::ExprKind::Index: {
            genIndex(e);
            return;
        }
        case ir::ExprKind::Assign: {
            if (e.lhs) {  // slot[idx] = rhs
                genIndexAddr(e.slot, *e.lhs);  // t2 = target address
                emit("addi", "sp,sp,-4");
                emit("stind", "sp,t2");
                genExpr(*e.rhs);
                emit("ldind", "sp,t2");
                emit("addi", "sp,sp,4");
                storeOperand("t2", "t0", e.slot->type);
                return;
            }
            genExpr(*e.rhs);
            storeSlotValue(e.slot, "t0");
            return;
        }
        case ir::ExprKind::DerefAssign: {
            // Stores at e.type's width/signedness - the pointee type, or
            // native-int width for untyped-int-as-address.
            genExpr(*e.lhs);  // t0 = pointer value
            emit("addi", "sp,sp,-4");
            emit("stind", "sp,t0");
            genExpr(*e.rhs);  // t0, or t1:t0 if wide
            emit("ldind", "sp,t2");
            emit("addi", "sp,sp,4");
            storeOperand("t2", "t0", e.type);
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            if (e.lhs) {  // slot[idx] op= rhs (including slot[idx]++/--)
                genIndexAddr(e.slot, *e.lhs);  // t2 = address
                genCompoundThroughT2(e);
                return;
            }
            // loadSlotValue/storeSlotValue re-address a plain scalar target
            // fresh each time (no side effect to preserve), so rhs
            // evaluates first, same order as plain Assign above.
            genExpr(*e.rhs);  // t0 = rhs
            emit("or", "t1,t0,t0");  // t1 = t0 ("or rX,rY,rY" is PowerPC's register-move idiom)
            loadSlotValue("t0", e.slot);  // t0 = old
            if (e.yieldOld) {
                emit("addi", "sp,sp,-4");
                emit("stind", "sp,t0");
            }
            genCompoundOp(e.op, e.type);
            narrowT0To(e.type);
            storeSlotValue(e.slot, "t0");
            if (e.yieldOld) {
                emit("ldind", "sp,t0");
                emit("addi", "sp,sp,4");
            }
            return;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            genExpr(*e.lhs);  // t0 = pointer value
            emit("or", "t2,t0,t0");  // t2 = t0
            genCompoundThroughT2(e);
            return;
        }
        case ir::ExprKind::Ternary: {
            genExpr(*e.lhs);
            if (isWide(e.lhs->type)) emit("or", "t0,t0,t1");
            emit("cmpwi", "t0,0");
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            emit("beq", Lelse);
            genExpr(*e.rhs);
            emit("b", Ldone);
            emitLabel(Lelse);
            genExpr(*e.elseExpr);
            emitLabel(Ldone);
            return;
        }
        case ir::ExprKind::Unary: {
            if (e.op == "-") {
                genExpr(*e.rhs);
                if (isWide(e.rhs->type)) {
                    genNegate64();
                } else {
                    emit("neg", "t0,t0");
                }
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                // Loads at e.type's width/signedness - see DerefAssign above.
                genExpr(*e.rhs);  // t0 = pointer value
                emit("or", "t2,t0,t0");  // t2 = t0
                loadOperand("t2", "t0", e.type);
            } else if (e.op == "~") {
                genExpr(*e.rhs);
                emit("nor", "t0,t0,t0");
                if (isWide(e.rhs->type)) emit("nor", "t1,t1,t1");
            } else {  // "&"
                genAddrOf(*e.rhs);
            }
            return;
        }
        case ir::ExprKind::Convert: {
            genConvert(e);
            return;
        }
        case ir::ExprKind::Call: {
            // Pushes arguments right-to-left so argument 0 ends up closest
            // to the saved return address once the callee's prologue runs
            // (`bl` never touches `sp`). The caller cleans the arguments
            // back off afterward, since the callee's epilogue only
            // restores `sp` to where it was on entry.
            int totalBytes = 0;
            for (size_t i = e.args.size(); i-- > 0;) {
                genExpr(*e.args[i]);
                if (isWide(e.args[i]->type)) {
                    emit("addi", "sp,sp,-8");
                    emit("stind", "sp,t1");
                    emit("stindd", "sp,t0,4");
                    totalBytes += 8;
                } else {
                    emit("addi", "sp,sp,-4");
                    emit("stind", "sp,t0");
                    totalBytes += 4;
                }
            }
            emit("bl", label(e.func));
            if (totalBytes > 0) emit("addi", cat("sp,sp,", totalBytes));
            return;
        }
        case ir::ExprKind::CallPtr: {
            // Real PowerPC has no single-instruction indirect call - `bl`'s
            // target is always a compile-time-relative displacement, never
            // a register - so this uses the standard two-instruction idiom:
            // `mtctr` moves the callee's address into the Count Register,
            // then `bctrl` branches to it and sets the link register.
            genExpr(*e.lhs);  // t0 = the callee's entry address
            emit("mtctr", "t0");
            emit("bctrl");
            return;
        }
        case ir::ExprKind::Binary: {
            genBinary(e);
            return;
        }
        case ir::ExprKind::Intrinsic: {
            // No interrupt setup exists on this bare-metal target, so
            // interrupt enable/disable is a no-op, as in MipsCodegen.cpp.
            (void)e;
            return;
        }
        case ir::ExprKind::Func:
            err(e.line, "internal: Func reached genExpr - should only ever appear as '&'s operand");
    }
}

// `arraySize > 0` means `slot` itself is the array, so the base address is
// either a compile-time-known label (a Global) or `fp+slotDisp(slot)` (a
// Local); either way the scaled index is added. `arraySize == 0` means `slot` is a plain pointer
// variable, so the base comes from reading its stored value instead.
void PowerPCCodegen::genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
    genExpr(idxExpr);  // t0 = index
    int shift = elemShift(slot->type);
    if (shift > 0) {
        emit("slwi", cat("t0,t0,", shift));
    } else if (shift < 0) {
        // Non-power-of-two element width (a struct's): scale by a real multiply instead.
        loadImmediate("t1", widthOfType(slot->type));
        emit("mullw", "t0,t0,t1");
    }
    if (slot->arraySize > 0) {
        if (slot->kind == ir::SlotKind::Global) {
            loadAddress("t2", label(slot));
            emit("add", "t2,t2,t0");
        } else {
            emit("addi", cat("t2,fp,", slotDisp(slot)));
            emit("add", "t2,t2,t0");
        }
    } else {
        loadSlotValue("t1", slot);  // t1 = the pointer variable's own stored value
        emit("add", "t2,t1,t0");
    }
}

void PowerPCCodegen::genIndex(const ir::Expr &e) {
    genIndexAddr(e.slot, *e.lhs);
    loadOperand("t2", "t0", e.slot->type);
}

void PowerPCCodegen::genCompoundOp(const std::string &op, Type type) {
    bool isUnsigned = type.isUnsigned;
    if (op == "+") { emit("add", "t0,t1,t0"); return; }
    // subf t0,t1,t0 = t0 - t1 = old - rhs.
    if (op == "-") { emit("subf", "t0,t1,t0"); return; }
    if (op == "*") { emit("mullw", "t0,t1,t0"); return; }
    if (op == "&") { emit("and", "t0,t1,t0"); return; }
    if (op == "|") { emit("or", "t0,t1,t0"); return; }
    if (op == "^") { emit("xor", "t0,t1,t0"); return; }
    if (op == "<<" || op == ">>") {
        const char *insn = op == "<<" ? "slw" : (isUnsigned ? "srw" : "sraw");
        emit(insn, "t0,t0,t1");
        return;
    }
    // "/" or "%": t0 = old, t1 = rhs (divisor). "%" recovers the remainder
    // via multiply-subtract, since PowerPC has no remainder instruction.
    // t5 (never t2) holds the quotient scratch, since t2 is this backend's
    // dedicated address register, still needed intact for the store after.
    const char *divInsn = isUnsigned ? "divwu" : "divw";
    if (op == "%") {
        emit(divInsn, "t5,t0,t1");  // t5 = old / rhs
        emit("mullw", "t5,t5,t1");  // t5 = quotient * rhs
        emit("subf", "t0,t5,t0");  // t0 = old - quotient*rhs = remainder
    } else {
        emit(divInsn, "t0,t0,t1");
    }
}

// t2 must already hold the resolved address on entry; it is stashed on the
// real hardware stack (PowerPC has no dedicated push/pop) across evaluating
// `e.rhs`, which may recursively clobber every other register.
void PowerPCCodegen::genCompoundThroughT2(const ir::Expr &e) {
    emit("addi", "sp,sp,-4");
    emit("stind", "sp,t2");
    genExpr(*e.rhs);               // t0 = rhs
    emit("or", "t1,t0,t0");  // t1 = rhs, held across the t2 reload below
    emit("ldind", "sp,t2");
    emit("addi", "sp,sp,4");  // t2 = address
    loadOperand("t2", "t0", e.type);  // t0 = old
    if (e.yieldOld) {
        emit("addi", "sp,sp,-4");
        emit("stind", "sp,t0");
    }
    genCompoundOp(e.op, e.type);          // t0 = new (raw)
    narrowT0To(e.type);
    if (e.yieldOld) {
        emit("or", "t3,t0,t0");  // t3 = new
        emit("ldind", "sp,t0");
        emit("addi", "sp,sp,4");  // t0 = old
        emit("or", "t4,t0,t0");  // t4 = old, preserved across the store below
        emit("or", "t0,t3,t3");  // t0 = new (storeOperand always stores from t0)
        storeOperand("t2", "t0", e.type);
        emit("or", "t0,t4,t4");  // t0 = old (final result)
    } else {
        storeOperand("t2", "t0", e.type);  // t0 already = new; t2 still addresses the target
    }
}

// A Local array's address isn't a compile-time-resolvable label the way a
// Global array's is, so (unlike declareArray()'s `dw` trailer line) this
// runs as real code, one element at a time, each time this is reached.
void PowerPCCodegen::genLocalArrayInit(const ir::Slot *slot) {
    int w = widthOfType(slot->type);
    for (size_t i = 0; i < slot->arrayInit.size(); i++) {
        const ir::ArrayInitElem &elem = slot->arrayInit[i];
        int disp = slotDisp(slot) + w * static_cast<int>(i);
        if (w == 8 && !elem.isSymbol) {
            auto v = static_cast<uint64_t>(elem.value);
            loadImmediate("t0", static_cast<int32_t>(static_cast<uint32_t>(v)));
            emit("stindd", cat("fp,t0,", disp));
            loadImmediate("t0", static_cast<int32_t>(static_cast<uint32_t>(v >> 32)));
            emit("stindd", cat("fp,t0,", (disp + 4)));
            continue;
        }
        if (elem.isSymbol) {
            loadAddress("t0", elem.slot ? label(elem.slot) : label(elem.func));
        } else {
            loadImmediate("t0", static_cast<int32_t>(static_cast<uint32_t>(elem.value)));
        }
        if (w == 1) {
            emit("stindb", cat("fp,t0,", disp));
        } else if (w == 2) {
            emit("stindh", cat("fp,t0,", disp));
        } else {
            emit("stindd", cat("fp,t0,", disp));
        }
    }
}

void PowerPCCodegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        if (operand.slot->kind == ir::SlotKind::Global) {
            loadAddress("t0", label(operand.slot));
        } else {
            emit("addi", cat("t0,fp,", slotDisp(operand.slot)));
        }
        return;
    }
    if (operand.kind == ir::ExprKind::Index) {
        genIndexAddr(operand.slot, *operand.lhs);
        emit("or", "t0,t2,t2");  // t0 = t2
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        loadAddress("t2", label(operand.func));
        emit("or", "t0,t2,t2");  // t0 = t2
        return;
    }
    err(operand.line, "internal: unhandled addressable-operand kind");
}

void PowerPCCodegen::loadOperand(const std::string &addrReg, const std::string &reg, Type type) {
    int w = widthOfType(type);
    if (w == 1) {
        emit("ldindb", cat(addrReg, ",", reg));
        if (!type.isUnsigned) {
            emit("slwi", cat(reg, ",", reg, ",24"));
            emit("srawi", cat(reg, ",", reg, ",24"));
        }
    } else if (w == 2) {
        emit((type.isUnsigned ? "ldindhu" : "ldindh"), cat(addrReg, ",", reg));
    } else if (w == 4) {
        emit("ldind", cat(addrReg, ",", reg));
    } else {  // 8 - long long: t1:t0 (hi:lo), low word first (reg is always "t0" here)
        emit("ldind", cat(addrReg, ",", reg));
        emit("ldindd", cat(addrReg, ",t1,4"));
    }
}

void PowerPCCodegen::storeOperand(const std::string &addrReg, const std::string &reg, Type type) {
    int w = widthOfType(type);
    if (w == 1) {
        emit("stindb", cat(addrReg, ",", reg));
    } else if (w == 2) {
        emit("stindh", cat(addrReg, ",", reg));
    } else if (w == 4) {
        emit("stind", cat(addrReg, ",", reg));
    } else {
        emit("stind", cat(addrReg, ",", reg));
        emit("stindd", cat(addrReg, ",t1,4"));
    }
}

void PowerPCCodegen::loadOperandDisp(int disp, const std::string &reg, Type type) {
    int w = widthOfType(type);
    if (w == 1) {
        emit("ldindb", cat("fp,", reg, ",", disp));
        if (!type.isUnsigned) {
            emit("slwi", cat(reg, ",", reg, ",24"));
            emit("srawi", cat(reg, ",", reg, ",24"));
        }
    } else if (w == 2) {
        emit((type.isUnsigned ? "ldindhu" : "ldindh"), cat("fp,", reg, ",", disp));
    } else if (w == 4) {
        emit("ldindd", cat("fp,", reg, ",", disp));
    } else {
        emit("ldindd", cat("fp,", reg, ",", disp));
        emit("ldindd", cat("fp,t1,", (disp + 4)));
    }
}

void PowerPCCodegen::storeOperandDisp(int disp, const std::string &reg, Type type) {
    int w = widthOfType(type);
    if (w == 1) {
        emit("stindb", cat("fp,", reg, ",", disp));
    } else if (w == 2) {
        emit("stindh", cat("fp,", reg, ",", disp));
    } else if (w == 4) {
        emit("stindd", cat("fp,", reg, ",", disp));
    } else {
        emit("stindd", cat("fp,", reg, ",", disp));
        emit("stindd", cat("fp,t1,", (disp + 4)));
    }
}

void PowerPCCodegen::narrowT0To(Type to) {
    int w = widthOfType(to);
    if (w == 1) {
        emit("slwi", "t0,t0,24");
        emit((to.isUnsigned ? "srwi" : "srawi"), "t0,t0,24");
    } else if (w == 2) {
        emit("slwi", "t0,t0,16");
        emit((to.isUnsigned ? "srwi" : "srawi"), "t0,t0,16");
    }
}

// IRBuilder already promotes any operand narrower than `int`, so the only
// real work here is crossing the native-int/`long long` boundary (sign/
// zero-extending t0 into t1, or dropping t1), or re-narrowing t0.
void PowerPCCodegen::genConvert(const ir::Expr &e) {
    Type from = e.lhs->type;
    Type to = e.type;
    bool fromWide = isWide(from);
    bool toWide = isWide(to);
    genExpr(*e.lhs);
    if (fromWide && !toWide) {
        narrowT0To(to);
    } else if (!fromWide && toWide) {
        if (from.isUnsigned) {
            emit("li", "t1,0");
        } else {
            emit("srawi", "t1,t0,31");
        }
    } else if (!fromWide && !toWide) {
        narrowT0To(to);
    }
}

// `long long` add/sub. This dialect exposes no carry-flag instruction, so
// the carry/borrow out of the low-word op is recovered via `cmplw` plus a
// conditional branch, the same way a comparison materializes a boolean.
void PowerPCCodegen::genWideAdd(const ir::Expr &lhs, const ir::Expr &rhs, bool isAdd) {
    genExpr(lhs);  // t1:t0 = lhs
    emit("addi", "sp,sp,-8");
    emit("stind", "sp,t0");
    emit("stindd", "sp,t1,4");
    genExpr(rhs);  // t1:t0 = rhs
    emit("or", "t3,t0,t0");
    emit("or", "t4,t1,t1");  // t3:t4 = rhs (lo:hi)
    emit("ldind", "sp,t0");
    emit("ldindd", "sp,t1,4");
    emit("addi", "sp,sp,8");  // t1:t0 = lhs
    std::string Lcarry = newLabel(), Ldone = newLabel();
    if (isAdd) {
        emit("add", "t5,t0,t3");  // t5 = lo sum
        emit("cmplw", "t5,t0");
        emit("bge", Lcarry);  // unsigned: sum >= lhs_lo means no carry
        emit("li", "t6,1");
        emit("b", Ldone);
        emitLabel(Lcarry);
        emit("li", "t6,0");
        emitLabel(Ldone);
        emit("add", "t1,t1,t4");
        emit("add", "t1,t1,t6");
        emit("or", "t0,t5,t5");
    } else {
        emit("cmplw", "t0,t3");
        emit("bge", Lcarry);  // unsigned: lhs_lo >= rhs_lo means no borrow
        emit("li", "t6,1");
        emit("b", Ldone);
        emitLabel(Lcarry);
        emit("li", "t6,0");
        emitLabel(Ldone);
        emit("subf", "t5,t3,t0");  // t5 = t0 - t3
        emit("subf", "t1,t4,t1");  // t1 = t1 - t4
        emit("subf", "t1,t6,t1");  // t1 -= borrow
        emit("or", "t0,t5,t5");
    }
}

// `long long` multiply, keeping the low 64 bits of the true 128-bit product
// via the standard 32-on-32 cross-multiply decomposition.
void PowerPCCodegen::genWideMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    emit("addi", "sp,sp,-8");
    emit("stind", "sp,t0");
    emit("stindd", "sp,t1,4");
    genExpr(rhs);
    emit("or", "t5,t0,t0");
    emit("or", "t6,t1,t1");  // t5:t6 = rhs (lo:hi)
    emit("ldind", "sp,t3");
    emit("ldindd", "sp,t4,4");
    emit("addi", "sp,sp,8");  // t3:t4 = lhs (lo:hi)
    emit("mullw", "t0,t3,t6");  // lhs_lo*rhs_hi
    emit("addi", "sp,sp,-4");
    emit("stind", "sp,t0");
    emit("mullw", "t0,t4,t5");  // lhs_hi*rhs_lo
    emit("addi", "sp,sp,-4");
    emit("stind", "sp,t0");
    emit("mullw", "t0,t3,t5");
    emit("mulhwu", "t1,t3,t5");  // lhs_lo*rhs_lo (final result_lo/carry)
    emit("ldind", "sp,t6");
    emit("addi", "sp,sp,4");  // crossB_lo
    emit("ldind", "sp,t5");
    emit("addi", "sp,sp,4");  // crossA_lo
    emit("add", "t1,t1,t6");
    emit("add", "t1,t1,t5");
}

void PowerPCCodegen::genNegate64() {
    emit("nor", "t0,t0,t0");
    emit("nor", "t1,t1,t1");
    emit("addi", "t0,t0,1");
    std::string Lcarry = newLabel(), Ldone = newLabel();
    emit("cmpwi", "t0,0");
    emit("bne", Lcarry);
    emit("addi", "t1,t1,1");
    emit("b", Ldone);
    emitLabel(Lcarry);
    emitLabel(Ldone);
}

// Classic bit-serial shift/compare/subtract long division, as in
// I386Codegen.cpp's/MipsCodegen.cpp's genWideDivMod(), using the stack for
// scratch state via `ldindd`/`stindd`'s displacement.
void PowerPCCodegen::genWideDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    emit("addi", "sp,sp,-8");
    emit("stind", "sp,t1");
    emit("stindd", "sp,t0,4");
    genExpr(rhs);
    emit("addi", "sp,sp,-8");
    emit("stind", "sp,t1");
    emit("stindd", "sp,t0,4");
    emit("addi", "sp,sp,-36");
    emit("ldindd", "sp,t0,44");
    emit("stindd", "sp,t0,0");
    emit("ldindd", "sp,t0,40");
    emit("stindd", "sp,t0,4");
    emit("ldindd", "sp,t0,36");
    emit("stindd", "sp,t0,8");
    emit("ldindd", "sp,t0,32");
    emit("stindd", "sp,t0,12");
    emit("li", "t0,0");
    emit("stindd", "sp,t0,16");
    emit("stindd", "sp,t0,20");
    emit("stindd", "sp,t0,24");
    emit("stindd", "sp,t0,28");
    emit("li", "t0,32");
    emit("stindd", "sp,t0,32");

    std::string LskipNegN = newLabel(), LskipNegD = newLabel();
    if (!isUnsigned) {
        emit("ldindd", "sp,t0,4");
        emit("srwi", "t0,t0,31");
        emit("stindd", "sp,t0,40");
        emit("ldindd", "sp,t0,12");
        emit("srwi", "t0,t0,31");
        emit("stindd", "sp,t0,44");
        emit("ldindd", "sp,t0,40");
        emit("cmpwi", "t0,0");
        emit("beq", LskipNegN);
        emit("ldindd", "sp,t0,0");
        emit("ldindd", "sp,t1,4");
        genNegate64();
        emit("stindd", "sp,t0,0");
        emit("stindd", "sp,t1,4");
        emitLabel(LskipNegN);
        emit("ldindd", "sp,t0,44");
        emit("cmpwi", "t0,0");
        emit("beq", LskipNegD);
        emit("ldindd", "sp,t0,8");
        emit("ldindd", "sp,t1,12");
        genNegate64();
        emit("stindd", "sp,t0,8");
        emit("stindd", "sp,t1,12");
        emitLabel(LskipNegD);
    }

    std::string Lloop = newLabel(), Ldone = newLabel();
    std::string LhiDiff = newLabel(), LsubYes = newLabel(), LsubNo = newLabel(), LshiftQ = newLabel();
    emitLabel(Lloop);
    emit("ldindd", "sp,t0,32");
    emit("cmpwi", "t0,0");
    emit("beq", Ldone);
    emit("addi", "t0,t0,-1");
    emit("stindd", "sp,t0,32");

    emit("ldindd", "sp,t0,4");
    emit("srwi", "t0,t0,31");
    emit("or", "t3,t0,t0");
    emit("ldindd", "sp,t0,0");
    emit("srwi", "t0,t0,31");
    emit("or", "t4,t0,t0");
    emit("ldindd", "sp,t0,0");
    emit("slwi", "t0,t0,1");
    emit("stindd", "sp,t0,0");
    emit("ldindd", "sp,t0,4");
    emit("slwi", "t0,t0,1");
    emit("or", "t0,t0,t4");
    emit("stindd", "sp,t0,4");

    emit("ldindd", "sp,t0,16");
    emit("srwi", "t0,t0,31");
    emit("or", "t5,t0,t0");
    emit("ldindd", "sp,t0,16");
    emit("slwi", "t0,t0,1");
    emit("or", "t0,t0,t3");
    emit("stindd", "sp,t0,16");
    emit("ldindd", "sp,t0,20");
    emit("slwi", "t0,t0,1");
    emit("or", "t0,t0,t5");
    emit("stindd", "sp,t0,20");

    emit("ldindd", "sp,t0,20");
    emit("ldindd", "sp,t1,12");
    emit("cmpw", "t0,t1");
    emit("bne", LhiDiff);
    emit("ldindd", "sp,t0,16");
    emit("ldindd", "sp,t1,8");
    emit("cmplw", "t0,t1");
    emit("blt", LsubNo);
    emit("b", LsubYes);
    emitLabel(LhiDiff);
    emit("cmplw", "t0,t1");
    emit("blt", LsubNo);
    emit("b", LsubYes);
    emitLabel(LsubYes);
    emit("ldindd", "sp,t0,16");
    emit("ldindd", "sp,t1,8");
    emit("cmplw", "t0,t1");
    emit("bge", cat(LshiftQ, "_nb"));
    emit("li", "t6,1");
    emit("b", cat(LshiftQ, "_bdone"));
    emitRaw(cat(LshiftQ, "_nb:"));
    emit("li", "t6,0");
    emitRaw(cat(LshiftQ, "_bdone:"));
    emit("subf", "t0,t1,t0");
    emit("stindd", "sp,t0,16");
    emit("ldindd", "sp,t0,20");
    emit("ldindd", "sp,t1,12");
    emit("subf", "t0,t1,t0");
    emit("subf", "t0,t6,t0");
    emit("stindd", "sp,t0,20");
    emit("li", "t3,1");
    emit("b", LshiftQ);
    emitLabel(LsubNo);
    emit("li", "t3,0");
    emitLabel(LshiftQ);

    emit("ldindd", "sp,t0,24");
    emit("srwi", "t0,t0,31");
    emit("or", "t5,t0,t0");
    emit("ldindd", "sp,t0,24");
    emit("slwi", "t0,t0,1");
    emit("or", "t0,t0,t3");
    emit("stindd", "sp,t0,24");
    emit("ldindd", "sp,t0,28");
    emit("slwi", "t0,t0,1");
    emit("or", "t0,t0,t5");
    emit("stindd", "sp,t0,28");
    emit("b", Lloop);

    emitLabel(Ldone);
    if (!isUnsigned) {
        std::string LskipNegQ = newLabel(), LskipNegR = newLabel();
        emit("ldindd", "sp,t3,40");
        emit("ldindd", "sp,t0,44");
        emit("xor", "t3,t3,t0");
        emit("cmpwi", "t3,0");
        emit("beq", LskipNegQ);
        emit("ldindd", "sp,t0,24");
        emit("ldindd", "sp,t1,28");
        genNegate64();
        emit("stindd", "sp,t0,24");
        emit("stindd", "sp,t1,28");
        emitLabel(LskipNegQ);
        emit("ldindd", "sp,t3,40");
        emit("cmpwi", "t3,0");
        emit("beq", LskipNegR);
        emit("ldindd", "sp,t0,16");
        emit("ldindd", "sp,t1,20");
        genNegate64();
        emit("stindd", "sp,t0,16");
        emit("stindd", "sp,t1,20");
        emitLabel(LskipNegR);
    }
    emit("ldindd", "sp,t0,24");
    emit("ldindd", "sp,t1,28");
    emit("ldindd", "sp,t4,16");
    emit("ldindd", "sp,t5,20");
    emit("addi", "sp,sp,52");
    if (wantRemainder) {
        emit("or", "t0,t4,t4");
        emit("or", "t1,t5,t5");
    }
}

// A `long long` shifted by a runtime count - a counted loop of 1-bit
// shifts, each carrying the bit that crosses the word boundary by
// hand (no hardware 64-bit shift).
void PowerPCCodegen::genWideShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned) {
    genExpr(lhs);
    emit("addi", "sp,sp,-8");
    emit("stind", "sp,t0");
    emit("stindd", "sp,t1,4");
    genExpr(rhs);  // t0 = shift count
    emit("or", "t3,t0,t0");
    emit("ldind", "sp,t0");
    emit("ldindd", "sp,t1,4");
    emit("addi", "sp,sp,8");
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("cmpwi", "t3,0");
    emit("beq", Ldone);
    emit("addi", "t3,t3,-1");
    if (isLeft) {
        emit("srwi", "t4,t0,31");
        emit("slwi", "t0,t0,1");
        emit("slwi", "t1,t1,1");
        emit("or", "t1,t1,t4");
    } else {
        emit("slwi", "t4,t1,31");
        emit("srwi", "t0,t0,1");
        emit("or", "t0,t0,t4");
        emit((isUnsigned ? "srwi" : "srawi"), "t1,t1,1");
    }
    emit("b", Lloop);
    emitLabel(Ldone);
}

// A `long long` comparison - compare the high words first (signed or
// unsigned per `isUnsigned`), falling back to an always-unsigned
// compare of the low words only when the high words are equal.
void PowerPCCodegen::genWideCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs,
                                      bool isUnsigned) {
    genExpr(lhs);
    emit("addi", "sp,sp,-8");
    emit("stind", "sp,t0");
    emit("stindd", "sp,t1,4");
    genExpr(rhs);
    emit("or", "t5,t0,t0");
    emit("or", "t6,t1,t1");  // t5:t6 = rhs (lo:hi)
    emit("ldind", "sp,t3");
    emit("ldindd", "sp,t4,4");
    emit("addi", "sp,sp,8");  // t3:t4 = lhs (lo:hi)
    if (op == "==" || op == "!=") {
        std::string Lfalse = newLabel(), Ldone = newLabel();
        const char *onEqual = op == "==" ? "1" : "0";
        const char *onDiffer = op == "==" ? "0" : "1";
        emit("cmpw", "t3,t5");
        emit("bne", Lfalse);
        emit("cmpw", "t4,t6");
        emit("bne", Lfalse);
        emit("li", cat("t0,", onEqual));
        emit("b", Ldone);
        emitLabel(Lfalse);
        emit("li", cat("t0,", onDiffer));
        emitLabel(Ldone);
        return;
    }
    std::string LhiDiff = newLabel(), Ldone = newLabel(), Lt = newLabel(), Lf = newLabel();
    emit("cmpw", "t4,t6");
    emit("bne", LhiDiff);
    const char *loBranch = op == "<" ? "blt" : op == ">" ? "bgt" : op == "<=" ? "ble" : "bge";
    emit("cmplw", "t3,t5");
    emit(loBranch, Lt);
    emit("b", Lf);
    emitLabel(Lt);
    emit("li", "t0,1");
    emit("b", Ldone);
    emitLabel(Lf);
    emit("li", "t0,0");
    emit("b", Ldone);
    emitLabel(LhiDiff);
    const char *hiCmp = isUnsigned ? "cmplw" : "cmpw";
    emit(hiCmp, "t4,t6");
    emit(loBranch, Lt);
    emit("b", Lf);
    emitLabel(Ldone);
}

void PowerPCCodegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    Type operandType = e.lhs->type;
    bool wide = isWide(operandType);
    bool isUnsigned = operandType.isUnsigned;
    if (op == "+" || op == "-") {
        if (wide) {
            genWideAdd(*e.lhs, *e.rhs, op == "+");
            return;
        }
        genExpr(*e.lhs);
        emit("addi", "sp,sp,-4");
        emit("stind", "sp,t0");
        genExpr(*e.rhs);
        emit("ldind", "sp,t1");
        emit("addi", "sp,sp,4");  // t1 = lhs, t0 = rhs
        if (op == "+") {
            emit("add", "t0,t1,t0");
        } else {
            emit("subf", "t0,t0,t1");  // t0 = t1 - t0 = lhs - rhs
        }
        return;
    }
    if (op == "*") {
        if (wide) {
            genWideMul(*e.lhs, *e.rhs);
        } else {
            genMul(*e.lhs, *e.rhs);
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
        const char *insn = op == "&" ? "and" : op == "|" ? "or" : "xor";
        if (wide) {
            genExpr(*e.lhs);
            emit("addi", "sp,sp,-8");
            emit("stind", "sp,t0");
            emit("stindd", "sp,t1,4");
            genExpr(*e.rhs);
            emit("or", "t3,t0,t0");
            emit("or", "t4,t1,t1");
            emit("ldind", "sp,t0");
            emit("ldindd", "sp,t1,4");
            emit("addi", "sp,sp,8");
            emit(insn, "t0,t0,t3");
            emit(insn, "t1,t1,t4");
            return;
        }
        genExpr(*e.lhs);
        emit("addi", "sp,sp,-4");
        emit("stind", "sp,t0");
        genExpr(*e.rhs);
        emit("ldind", "sp,t1");
        emit("addi", "sp,sp,4");
        emit(insn, "t0,t1,t0");
        return;
    }
    if (op == "<<" || op == ">>") {
        if (wide) {
            genWideShift(*e.lhs, *e.rhs, op == "<<", isUnsigned);
            return;
        }
        genExpr(*e.lhs);
        emit("addi", "sp,sp,-4");
        emit("stind", "sp,t0");
        genExpr(*e.rhs);  // t0 = shift count
        emit("or", "t1,t0,t0");
        emit("ldind", "sp,t0");
        emit("addi", "sp,sp,4");  // t0 = value to shift
        const char *insn = op == "<<" ? "slw" : (isUnsigned ? "srw" : "sraw");
        emit(insn, "t0,t0,t1");
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

// mullw's low-order 32 bits are the same whether operands are signed or
// unsigned.
void PowerPCCodegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    emit("addi", "sp,sp,-4");
    emit("stind", "sp,t0");
    genExpr(rhs);
    emit("ldind", "sp,t1");
    emit("addi", "sp,sp,4");  // t1 = lhs, t0 = rhs
    emit("mullw", "t0,t1,t0");
}

// `divw`/`divwu` give only the quotient - unlike MIPS's `div` (HI/LO) -
// so `%` is synthesized: divide, `mullw` the quotient back out, `subf` to
// recover the remainder as `dividend - quotient * divisor`.
void PowerPCCodegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    const char *divInsn = isUnsigned ? "divwu" : "divw";
    genExpr(lhs);
    emit("addi", "sp,sp,-4");
    emit("stind", "sp,t0");
    genExpr(rhs);
    emit("ldind", "sp,t1");
    emit("addi", "sp,sp,4");  // t1 = lhs (dividend), t0 = rhs (divisor)
    if (!wantRemainder) {
        emit(divInsn, "t0,t1,t0");
        return;
    }
    emit(divInsn, "t2,t1,t0");  // t2 = quotient (t1/t0)
    emit("mullw", "t2,t2,t0");  // t2 = quotient * divisor
    emit("subf", "t0,t2,t1");  // t0 = t1 - t2 = dividend - quotient*divisor
}

// PowerPC has no compare-and-set instruction (unlike MIPS's `slt`), so this
// needs `cmpw`/`cmplw` plus a conditional branch to materialize the boolean.
void PowerPCCodegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs) {
    if (isWide(lhs.type)) {
        genWideCompare(op, lhs, rhs, lhs.type.isUnsigned);
        return;
    }
    bool isUnsigned = lhs.type.isUnsigned;
    genExpr(lhs);
    emit("addi", "sp,sp,-4");
    emit("stind", "sp,t0");
    genExpr(rhs);
    emit("ldind", "sp,t1");
    emit("addi", "sp,sp,4");  // t1 = lhs, t0 = rhs
    emit((isUnsigned ? "cmplw" : "cmpw"), "t1,t0");
    std::string branch;
    if (op == "<") branch = "blt";
    else if (op == ">") branch = "bgt";
    else if (op == "<=") branch = "ble";
    else if (op == ">=") branch = "bge";
    else if (op == "==") branch = "beq";
    else if (op == "!=") branch = "bne";
    else err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit(branch, Ltrue);
    emit("li", "t0,0");
    emit("b", Ldone);
    emitLabel(Ltrue);
    emit("li", "t0,1");
    emitLabel(Ldone);
}

void PowerPCCodegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    if (isWide(lhs.type)) emit("or", "t0,t0,t1");
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    emit("cmpwi", "t0,0");
    emit("beq", Lfalse);
    genExpr(rhs);
    if (isWide(rhs.type)) emit("or", "t0,t0,t1");
    emit("cmpwi", "t0,0");
    emit("beq", Lfalse);
    emit("b", Ltrue);
    emitLabel(Lfalse);
    emit("li", "t0,0");
    emit("b", Ldone);
    emitLabel(Ltrue);
    emit("li", "t0,1");
    emitLabel(Ldone);
}

void PowerPCCodegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    if (isWide(lhs.type)) emit("or", "t0,t0,t1");
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    emit("cmpwi", "t0,0");
    emit("bne", Ltrue);
    genExpr(rhs);
    if (isWide(rhs.type)) emit("or", "t0,t0,t1");
    emit("cmpwi", "t0,0");
    emit("bne", Ltrue);
    emit("b", Lfalse);
    emitLabel(Ltrue);
    emit("li", "t0,1");
    emit("b", Ldone);
    emitLabel(Lfalse);
    emit("li", "t0,0");
    emitLabel(Ldone);
}

void PowerPCCodegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    if (isWide(rhs.type)) emit("or", "t0,t0,t1");
    std::string Lfalse = newLabel(), Ldone = newLabel();
    emit("cmpwi", "t0,0");
    emit("bne", Lfalse);
    emit("li", "t0,1");
    emit("b", Ldone);
    emitLabel(Lfalse);
    emit("li", "t0,0");
    emitLabel(Ldone);
}

std::optional<std::string> PowerPCCodegen::assembleInProcess(const std::string &asmText,
                                                               const std::string &format) const {
    if (format == "raw") return assemblePowerPC(asmText);
    // `coff`/`elf`/`macho`/`aout`/`objraw`: builds one format-neutral
    // ObjectFile, then picks that format's FileFormatWriter to serialize it.
    if (auto kind = fileFormatKindFromName(format)) {
        ObjectFile obj = assemblePowerPCObject(asmText);
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

}  // namespace rcc
