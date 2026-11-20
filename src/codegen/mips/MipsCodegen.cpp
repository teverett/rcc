#include "MipsCodegen.h"

#include <cstdint>
#include <stdexcept>

#include "MipsAssembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

// Where the whole program loads; the test harness sets the simulated
// CPU's PC here directly.
constexpr uint32_t kBaseAddress = 0x80010000u;

// Initial $sp, comfortably above the loaded program and its data.
constexpr uint32_t kInitialSP = 0x80100000u;
}  // namespace

// A literal element's decimal text, or a symbol element's referenced
// Slot's/Function's label written out bare.
std::string MipsCodegen::arrayElemText(const ir::ArrayInitElem &e) {
    if (!e.isSymbol) return std::to_string(static_cast<int32_t>(static_cast<uint32_t>(e.value)));
    return e.slot ? label(e.slot) : label(e.func);
}

void MipsCodegen::declareArray(const std::string &lbl, const std::vector<ir::ArrayInitElem> &values, Type elemType) {
    if (values.empty()) return;
    int w = widthOfType(elemType);
    // No 64-bit data directive; a `long long` element is two "dw"s
    // (low word, then high) instead.
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

void MipsCodegen::loadAddress(const std::string &reg, const std::string &lbl) {
    emit("lui", cat(reg, ",", lbl));
    emit("ori", cat(reg, ",", reg, ",", lbl));
}

void MipsCodegen::loadImm32(const std::string &reg, int32_t v) {
    if (v >= -32768 && v <= 32767) {
        emit("addiu", cat(reg, ",zero,", v));
    } else {
        uint32_t uv = static_cast<uint32_t>(v);
        emit("lui", cat(reg, ",", static_cast<int32_t>((uv >> 16) & 0xFFFFu)));
        emit("ori", cat(reg, ",", reg, ",", static_cast<int32_t>(uv & 0xFFFFu)));
    }
}

// A Param lives at fp+frameOffset (caller's pushed argument space); a
// Local's address is its lowest one (see ir::localLowOffset()), its later
// bytes above it. No bracketed memory operand exists
// in this dialect, so the address is computed with a single ADDIU.
void MipsCodegen::loadSlotAddr(const std::string &reg, const ir::Slot *slot) {
    if (slot->kind == ir::SlotKind::Global) {
        loadAddress(reg, label(slot));
        return;
    }
    int offset = slot->kind == ir::SlotKind::Param ? slot->frameOffset : -ir::localLowOffset(*slot, typeWidths());
    emit("addiu", cat(reg, ",fp,", offset));
}

std::string MipsCodegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    // In objectMode, `prog` may not define `main`; the entry sequence
    // below is skipped since MipsLinkerFactory.cpp synthesizes it once
    // every object is linked. An object's base is `org 0`; the linker
    // places it.
    { emitRaw(cat("org ", (objectMode ? 0 : kBaseAddress))); emitRaw(""); }
    if (!objectMode) {
        emitLabel("_start");
        emit("lui", cat("sp,", static_cast<int32_t>((kInitialSP >> 16) & 0xFFFFu)));
        emit("ori", cat("sp,sp,", static_cast<int32_t>(kInitialSP & 0xFFFFu)));
        { emit("jal", label(mainFn)); emit("nop"); }
        // main's result sits in $t0 once execution reaches halt.
        { emitLabel("halt"); emit("beq", "zero,zero,halt"); emit("nop"); emitRaw(""); }
    }

    for (const auto &fb : prog.functionBodies) genFunction(fb);

    // MIPS raises an address-error exception on a misaligned
    // halfword/word access, so each dh/dw global needs padding bytes
    // first to reach its own natural alignment.
    size_t globalByteOffset = 0;
    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        // An external global isn't defined in this file; skip reserving
        // storage so references stay as undefined symbols/relocations.
        if (slot->isExternal) continue;
        const std::string lbl = label(slot.get());
        int elemW = widthOfType(slot->type);
        size_t totalBytes = slot->arraySize > 0 ? static_cast<size_t>(slot->arraySize) * elemW
                                                  : static_cast<size_t>(elemW);
        size_t align = elemW >= 4 ? 4 : static_cast<size_t>(elemW);
        while (align > 1 && (globalByteOffset % align) != 0) {
            trailer_.push_back("db 0  ; alignment padding");
            globalByteOffset += 1;
        }
        globalByteOffset += totalBytes;
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

std::map<std::string, std::string> MipsCodegen::perFileAsm() const { return perFileAsm_; }

// Every function saves/restores $ra, leaf or not: MIPS has exactly
// one link register, so a non-leaf function's own call would
// otherwise overwrite it before `jr ra` returns. The prologue also
// pushes the caller's $fp and points $fp at that saved word, giving
// this call (including recursive ones) its own frame; locals are then
// reserved by lowering $sp another frameSize words.
void MipsCodegen::genFunction(const ir::FunctionBody &fn) {
    emitLabel(label(fn.func));
    { emit("addiu", "sp,sp,-4"); emit("stind", "sp,ra"); }
    { emit("addiu", "sp,sp,-4"); emit("stind", "sp,fp"); }
    emit("addu", "fp,sp,zero");
    if (fn.func->frameSize > 0) emit("addiu", cat("sp,sp,-", fn.func->frameSize));

    genStmt(*fn.body);

    genEpilogue();
    emitRaw("");
}

// Deallocates locals ($sp back up to $fp), restores the caller's
// $fp/$ra from the two words $fp points at, then returns. $at briefly
// holds the saved $fp across restoring $ra.
void MipsCodegen::genEpilogue() {
    emit("addu", "sp,fp,zero");
    { emit("ldind", "sp,at"); emit("addiu", "sp,sp,4"); }
    { emit("ldind", "sp,ra"); emit("addiu", "sp,sp,4"); }
    emit("addu", "fp,at,zero");
    { emit("jr", "ra"); emit("nop"); }
}

void MipsCodegen::genStmt(const ir::Stmt &s) {
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
            { emit("beq", cat("t0,zero,", (s.elseStmt ? Lelse : Ldone))); emit("nop"); }
            genStmt(*s.thenStmt);
            if (s.elseStmt) {
                { emit("j", Ldone); emit("nop"); }
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
            { emit("beq", cat("t0,zero,", Lend)); emit("nop"); }
            loopLabels_.push_back({Lcont, Lend});
            genStmt(*s.loopBody);
            loopLabels_.pop_back();
            if (s.postStmt) {
                emitLabel(Lcont);
                genStmt(*s.postStmt);
            }
            { emit("j", Lstart); emit("nop"); }
            emitLabel(Lend);
            return;
        }
        case ir::StmtKind::Break: {
            { emit("j", loopLabels_.back().second); emit("nop"); }
            return;
        }
        case ir::StmtKind::Continue: {
            { emit("j", loopLabels_.back().first); emit("nop"); }
            return;
        }
        case ir::StmtKind::Return: {
            if (s.initOrReturnOrExpr) genExpr(*s.initOrReturnOrExpr);
            // Early exit, bypassing the fall-through epilogue; genEpilogue()
            // doesn't touch $t0, where the return value already sits.
            genEpilogue();
            return;
        }
        case ir::StmtKind::LocalDecl: {
            if (s.declSlot->arraySize > 0) {
                genLocalArrayInit(s.declSlot);
            } else if (s.initOrReturnOrExpr) {
                genExpr(*s.initOrReturnOrExpr);
                loadSlotAddr("t2", s.declSlot);
                storeOperand("t2", s.declSlot->type);
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
            emitRaw(s.asmText);
            if (!s.asmText.empty() && s.asmText.back() != '\n') emitRaw("");
            return;
        }
        case ir::StmtKind::Label: {
            emitLabel(irLabel(s.labelId));
            return;
        }
        case ir::StmtKind::Goto: {
            { emit("j", irLabel(s.labelId)); emit("nop"); }
            return;
        }
    }
}

void MipsCodegen::genExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit: {
            if (isWide(e.type)) {
                auto v = static_cast<uint64_t>(e.intValue);
                loadImm32("t0", static_cast<int32_t>(static_cast<uint32_t>(v)));
                loadImm32("t1", static_cast<int32_t>(static_cast<uint32_t>(v >> 32)));
                return;
            }
            loadImm32("t0", static_cast<int32_t>(static_cast<uint32_t>(e.intValue)));
            return;
        }
        case ir::ExprKind::Var: {
            loadSlotAddr("t2", e.slot);
            loadOperand("t2", e.slot->type);
            return;
        }
        case ir::ExprKind::Index: {
            genIndex(e);
            return;
        }
        case ir::ExprKind::Assign: {
            if (e.lhs) {  // slot[idx] = rhs
                genIndexAddr(e.slot, *e.lhs);  // t2 = target address
                { emit("addiu", "sp,sp,-4"); emit("stind", "sp,t2"); }
                genExpr(*e.rhs);
                { emit("ldind", "sp,t2"); emit("addiu", "sp,sp,4"); }
                storeOperand("t2", e.slot->type);
                return;
            }
            genExpr(*e.rhs);
            loadSlotAddr("t2", e.slot);
            storeOperand("t2", e.slot->type);
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            if (e.lhs) {  // slot[idx] op= rhs (including slot[idx]++/--)
                genIndexAddr(e.slot, *e.lhs);  // t2 = target address
                genCompoundThroughT2(e);
                return;
            }
            // MIPS has no absolute-addressing memory operand, so even a
            // scalar target's address is computed first.
            loadSlotAddr("t2", e.slot);
            genCompoundThroughT2(e);
            return;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            genExpr(*e.lhs);  // t0 = pointer value
            emit("addu", "t2,t0,zero");  // t2 = address
            genCompoundThroughT2(e);
            return;
        }
        case ir::ExprKind::Ternary: {
            genExpr(*e.lhs);
            // The condition's type can still be wide even though the
            // ternary's result type isn't, hence the OR-then-test.
            if (isWide(e.lhs->type)) emit("or", "t0,t0,t1");
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            { emit("beq", cat("t0,zero,", Lelse)); emit("nop"); }
            genExpr(*e.rhs);
            { emit("j", Ldone); emit("nop"); }
            emitLabel(Lelse);
            genExpr(*e.elseExpr);
            emitLabel(Ldone);
            return;
        }
        case ir::ExprKind::DerefAssign: {
            // Stores at e.type's width/signedness (the pointee type, or
            // native-int-width for untyped-int-as-address).
            genExpr(*e.lhs);  // t0 = pointer value
            { emit("addiu", "sp,sp,-4"); emit("stind", "sp,t0"); }
            genExpr(*e.rhs);  // t0, or t1:t0 if wide
            { emit("ldind", "sp,t2"); emit("addiu", "sp,sp,4"); }
            storeOperand("t2", e.type);
            return;
        }
        case ir::ExprKind::Unary: {
            if (e.op == "-") {
                genExpr(*e.rhs);
                if (isWide(e.rhs->type)) {
                    genNegate64();
                } else {
                    emit("subu", "t0,zero,t0");
                }
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                // Loads at e.type's width/signedness.
                genExpr(*e.rhs);  // t0 = pointer value
                emit("addu", "t2,t0,zero");
                loadOperand("t2", e.type);
            } else if (e.op == "~") {
                genExpr(*e.rhs);
                emit("nor", "t0,t0,zero");
                if (isWide(e.rhs->type)) emit("nor", "t1,t1,zero");
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
            // cdecl-style: push args right-to-left onto $sp (arg 0
            // lands at fp+8+4*i once the callee's prologue runs), caller
            // cleans up after the call returns. A wide argument pushes
            // high word first so its low word ends up at the lower
            // address.
            int totalBytes = 0;
            for (size_t i = e.args.size(); i-- > 0;) {
                genExpr(*e.args[i]);
                if (isWide(e.args[i]->type)) {
                    { emit("addiu", "sp,sp,-8"); emit("stind", "sp,t1"); emit("stind", "sp,t0,4"); }
                    totalBytes += 8;
                } else {
                    { emit("addiu", "sp,sp,-4"); emit("stind", "sp,t0"); }
                    totalBytes += 4;
                }
            }
            { emit("jal", label(e.func)); emit("nop"); }
            if (totalBytes > 0) emit("addiu", cat("sp,sp,", totalBytes));
            return;
        }
        case ir::ExprKind::CallPtr: {
            // jalr ra,t2 jumps to the address in t2 and leaves the
            // return address in ra, like jal for a runtime-known target.
            genExpr(*e.lhs);  // t0 = the callee's own entry address
            emit("addu", "t2,t0,zero");
            { emit("jalr", "ra,t2"); emit("nop"); }
            return;
        }
        case ir::ExprKind::Binary: {
            genBinary(e);
            return;
        }
        case ir::ExprKind::Intrinsic: {
            // No exception/interrupt setup on this bare-metal target; no-op.
            (void)e;
            return;
        }
        case ir::ExprKind::Func:
            err(e.line, "internal: Func reached genExpr - should only ever appear as '&'s operand");
    }
}

// t2 ends up holding the computed address. arraySize > 0 means a
// compile-time-known array base (Global label or Local fp-relative);
// otherwise `slot` is a pointer variable, so the base is read from its
// stored value first.
//
// Both a Global and a Local array's element addresses increase with
// the index (base+4*i).
void MipsCodegen::genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
    genExpr(idxExpr);  // t0 = index
    int shift = elemShift(slot->type);
    if (shift > 0) {
        emit("sll", cat("t0,t0,", shift));
    } else if (shift < 0) {
        // Non-power-of-two element width (a struct's): scale by a real multiply instead.
        loadImm32("t3", widthOfType(slot->type));
        { emit("multu", "t0,t3"); emit("mflo", "t0"); }
    }
    if (slot->arraySize > 0) {
        if (slot->kind == ir::SlotKind::Global) {
            loadAddress("t2", label(slot));
            emit("addu", "t2,t2,t0");
        } else {
            loadSlotAddr("t2", slot);
            emit("addu", "t2,t2,t0");
        }
    } else {
        loadSlotAddr("t1", slot);
        emit("ldind", "t1,t1");  // t1 = the pointer variable's own stored value
        emit("addu", "t2,t1,t0");
    }
}

void MipsCodegen::genIndex(const ir::Expr &e) {
    genIndexAddr(e.slot, *e.lhs);
    loadOperand("t2", e.slot->type);
}

// Stores a Local array's compile-time initializer values into its
// frame slots, re-run on every call since the storage is freshly
// reserved each time. Element i lives w*i bytes above element 0.
void MipsCodegen::genLocalArrayInit(const ir::Slot *slot) {
    int w = widthOfType(slot->type);
    for (size_t i = 0; i < slot->arrayInit.size(); i++) {
        const ir::ArrayInitElem &elem = slot->arrayInit[i];
        int offset = -ir::localLowOffset(*slot, typeWidths()) + w * static_cast<int>(i);
        if (w == 8 && !elem.isSymbol) {
            // A long long element is split across both words.
            auto v = static_cast<uint64_t>(elem.value);
            loadImm32("t0", static_cast<int32_t>(static_cast<uint32_t>(v)));
            { emit("addiu", cat("t2,fp,", offset)); emit("stind", "t2,t0"); }
            loadImm32("t0", static_cast<int32_t>(static_cast<uint32_t>(v >> 32)));
            { emit("addiu", cat("t2,fp,", (offset + 4))); emit("stind", "t2,t0"); }
            continue;
        }
        if (elem.isSymbol) {
            loadAddress("t0", elem.slot ? label(elem.slot) : label(elem.func));
        } else {
            loadImm32("t0", static_cast<int32_t>(static_cast<uint32_t>(elem.value)));
        }
        emit("addiu", cat("t2,fp,", offset));
        if (w == 1) {
            emit("stindb", "t2,t0");
        } else if (w == 2) {
            emit("stindh", "t2,t0");
        } else {
            emit("stind", "t2,t0");
        }
    }
}

void MipsCodegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        loadSlotAddr("t2", operand.slot);
        emit("addu", "t0,t2,zero");
        return;
    }
    if (operand.kind == ir::ExprKind::Index) {
        genIndexAddr(operand.slot, *operand.lhs);
        emit("addu", "t0,t2,zero");
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        loadAddress("t2", label(operand.func));
        emit("addu", "t0,t2,zero");
        return;
    }
    err(operand.line, "internal: unhandled addressable-operand kind");
}

void MipsCodegen::loadOperand(const std::string &addrReg, Type type) {
    int w = widthOfType(type);
    if (w == 1) {
        emit((type.isUnsigned ? "ldindbu" : "ldindb"), cat(addrReg, ",t0"));
    } else if (w == 2) {
        emit((type.isUnsigned ? "ldindhu" : "ldindh"), cat(addrReg, ",t0"));
    } else if (w == 4) {
        emit("ldind", cat(addrReg, ",t0"));
    } else {  // 8 - long long: t1:t0 (hi:lo), low word first
        emit("ldind", cat(addrReg, ",t0"));
        emit("ldind", cat(addrReg, ",t1,4"));
    }
}

void MipsCodegen::storeOperand(const std::string &addrReg, Type type) {
    int w = widthOfType(type);
    if (w == 1) {
        emit("stindb", cat(addrReg, ",t0"));
    } else if (w == 2) {
        emit("stindh", cat(addrReg, ",t0"));
    } else if (w == 4) {
        emit("stind", cat(addrReg, ",t0"));
    } else {
        emit("stind", cat(addrReg, ",t0"));
        emit("stind", cat(addrReg, ",t1,4"));
    }
}

void MipsCodegen::narrowT0To(Type to) {
    int w = widthOfType(to);
    if (w == 1) {
        emit("sll", "t0,t0,24");
        emit((to.isUnsigned ? "srl" : "sra"), "t0,t0,24");
    } else if (w == 2) {
        emit("sll", "t0,t0,16");
        emit((to.isUnsigned ? "srl" : "sra"), "t0,t0,16");
    }
    // 4 bytes (Int/Long): t0 already holds the full value.
}

// Crosses the native-int/`long long` boundary (sign/zero-extending t0
// into t1, or dropping t1), or re-truncates t0 for a narrower target.
void MipsCodegen::genConvert(const ir::Expr &e) {
    Type from = e.lhs->type;
    Type to = e.type;
    bool fromWide = isWide(from);
    bool toWide = isWide(to);
    genExpr(*e.lhs);  // t0, or t1:t0 if fromWide
    if (fromWide && !toWide) {
        narrowT0To(to);  // t1 is simply discarded
    } else if (!fromWide && toWide) {
        if (from.isUnsigned) {
            emit("addu", "t1,zero,zero");
        } else {
            emit("sra", "t1,t0,31");  // sign-extend t0 into t1
        }
    } else if (!fromWide && !toWide) {
        narrowT0To(to);
    }
    // fromWide && toWide: the bit pattern is already correct.
}

// `long long` add/sub: stash lhs on the stack while rhs evaluates,
// then ripple carry/borrow across the word pair. MIPS has no carry
// flag, so the carry out of the low-word add is recovered via sltu.
void MipsCodegen::genWideAdd(const ir::Expr &lhs, const ir::Expr &rhs, bool isAdd) {
    genExpr(lhs);  // t1:t0 = lhs
    { emit("addiu", "sp,sp,-8"); emit("stind", "sp,t0"); emit("stind", "sp,t1,4"); }
    genExpr(rhs);  // t1:t0 = rhs
    { emit("addu", "t3,t0,zero"); emit("addu", "t4,t1,zero"); }  // t3:t4 = rhs (lo:hi)
    { emit("ldind", "sp,t0"); emit("ldind", "sp,t1,4"); emit("addiu", "sp,sp,8"); }  // t1:t0 = lhs
    if (isAdd) {
        emit("addu", "t5,t0,t3");          // t5 = lo sum
        emit("sltu", "t6,t5,t0");           // t6 = carry out
        { emit("addu", "t1,t1,t4"); emit("addu", "t1,t1,t6"); }
        emit("addu", "t0,t5,zero");
    } else {
        emit("sltu", "t6,t0,t3");           // t6 = borrow
        emit("subu", "t5,t0,t3");
        { emit("subu", "t1,t1,t4"); emit("subu", "t1,t1,t6"); }
        emit("addu", "t0,t5,zero");
    }
}

// `long long` multiply, keeping the low 64 bits of the 128-bit
// product via the standard 32x32 cross-multiply decomposition; each
// cross term's own MFHI overflow only affects bit 64+ and is discarded.
void MipsCodegen::genWideMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);  // t1:t0 = lhs
    { emit("addiu", "sp,sp,-8"); emit("stind", "sp,t0"); emit("stind", "sp,t1,4"); }
    genExpr(rhs);  // t1:t0 = rhs
    { emit("addu", "t5,t0,zero"); emit("addu", "t6,t1,zero"); }  // t5:t6 = rhs (lo:hi)
    { emit("ldind", "sp,t3"); emit("ldind", "sp,t4,4"); emit("addiu", "sp,sp,8"); }  // t3:t4 = lhs (lo:hi)
    { emit("mult", "t3,t6"); emit("mflo", "t0"); }  // lhs_lo*rhs_hi
    { emit("addiu", "sp,sp,-4"); emit("stind", "sp,t0"); }
    { emit("mult", "t4,t5"); emit("mflo", "t0"); }  // lhs_hi*rhs_lo
    { emit("addiu", "sp,sp,-4"); emit("stind", "sp,t0"); }
    { emit("mult", "t3,t5"); emit("mflo", "t0"); emit("mfhi", "t1"); }  // lhs_lo*rhs_lo (final result_lo/carry)
    { emit("ldind", "sp,t6"); emit("addiu", "sp,sp,4"); }   // crossB_lo (lhs_hi*rhs_lo)
    { emit("ldind", "sp,t5"); emit("addiu", "sp,sp,4"); }   // crossA_lo (lhs_lo*rhs_hi)
    { emit("addu", "t1,t1,t6"); emit("addu", "t1,t1,t5"); }
}

void MipsCodegen::genNegate64() {
    { emit("nor", "t0,t0,zero"); emit("nor", "t1,t1,zero"); }
    emit("addiu", "t0,t0,1");
    emit("sltiu", "t3,t0,1");  // t3 = 1 if t0 wrapped to 0 (i.e. the +1 carried out)
    emit("addu", "t1,t1,t3");
}

// Bit-serial shift/compare/subtract long division, using $sp-relative
// scratch (ldind/stind already take a displacement). Signed division
// operates on magnitudes: each negative operand is negated in place
// first, then the sign is restored on the result per C's truncating rule.
void MipsCodegen::genWideDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    { emit("addiu", "sp,sp,-8"); emit("stind", "sp,t1"); emit("stind", "sp,t0,4"); }  // [sp]=lhs_hi,[sp+4]=lhs_lo
    genExpr(rhs);
    { emit("addiu", "sp,sp,-8"); emit("stind", "sp,t1"); emit("stind", "sp,t0,4"); }  // [sp]=rhs_hi,[sp+4]=rhs_lo
    emit("addiu", "sp,sp,-36");
    // Scratch layout at sp+0: n_lo,n_hi,d_lo,d_hi,r_lo,r_hi,q_lo,q_hi,cnt (9 words, 36 bytes)
    { emit("ldind", "sp,t0,44"); emit("stind", "sp,t0,0"); }   // n_lo = lhs_lo
    { emit("ldind", "sp,t0,40"); emit("stind", "sp,t0,4"); }   // n_hi = lhs_hi
    { emit("ldind", "sp,t0,36"); emit("stind", "sp,t0,8"); }   // d_lo = rhs_lo
    { emit("ldind", "sp,t0,32"); emit("stind", "sp,t0,12"); }  // d_hi = rhs_hi
    { emit("stind", "sp,zero,16"); emit("stind", "sp,zero,20"); }  // r = 0
    { emit("stind", "sp,zero,24"); emit("stind", "sp,zero,28"); }  // q = 0
    { emit("addiu", "t0,zero,32"); emit("stind", "sp,t0,32"); }    // cnt = 32 (per-word, so 2*32=64 total)

    if (!isUnsigned) {
        { emit("ldind", "sp,t0,4"); emit("srl", "t0,t0,31"); emit("stind", "sp,t0,40"); }    // [sp+40] = lhsSign
        { emit("ldind", "sp,t0,12"); emit("srl", "t0,t0,31"); emit("stind", "sp,t0,44"); }   // [sp+44] = rhsSign
        std::string LskipNegN = newLabel(), LskipNegD = newLabel();
        { emit("ldind", "sp,t0,40"); emit("beq", cat("t0,zero,", LskipNegN)); emit("nop"); }
        { emit("ldind", "sp,t0,0"); emit("ldind", "sp,t1,4"); }
        genNegate64();
        { emit("stind", "sp,t0,0"); emit("stind", "sp,t1,4"); }
        emitLabel(LskipNegN);
        { emit("ldind", "sp,t0,44"); emit("beq", cat("t0,zero,", LskipNegD)); emit("nop"); }
        { emit("ldind", "sp,t0,8"); emit("ldind", "sp,t1,12"); }
        genNegate64();
        { emit("stind", "sp,t0,8"); emit("stind", "sp,t1,12"); }
        emitLabel(LskipNegD);
    }

    std::string Lloop = newLabel(), Ldone = newLabel();
    std::string LhiDiff = newLabel(), LsubYes = newLabel(), LsubNo = newLabel(), LshiftQ = newLabel();
    emitLabel(Lloop);
    { emit("ldind", "sp,t0,32"); emit("beq", cat("t0,zero,", Ldone)); emit("nop"); }
    { emit("addiu", "t0,t0,-1"); emit("stind", "sp,t0,32"); }

    // n <<= 1 (0-filled), capturing its own old top bit into t3.
    { emit("ldind", "sp,t0,4"); emit("srl", "t0,t0,31"); emit("addu", "t3,t0,zero"); }
    { emit("ldind", "sp,t0,0"); emit("srl", "t0,t0,31"); emit("addu", "t4,t0,zero"); }
    { emit("ldind", "sp,t0,0"); emit("sll", "t0,t0,1"); emit("stind", "sp,t0,0"); }
    { emit("ldind", "sp,t0,4"); emit("sll", "t0,t0,1"); emit("or", "t0,t0,t4"); emit("stind", "sp,t0,4"); }

    // r = (r << 1) | (n's old top bit, in t3).
    { emit("ldind", "sp,t0,16"); emit("srl", "t0,t0,31"); emit("addu", "t5,t0,zero"); }
    { emit("ldind", "sp,t0,16"); emit("sll", "t0,t0,1"); emit("or", "t0,t0,t3"); emit("stind", "sp,t0,16"); }
    { emit("ldind", "sp,t0,20"); emit("sll", "t0,t0,1"); emit("or", "t0,t0,t5"); emit("stind", "sp,t0,20"); }

    // if (r >= d) { r -= d; quotient bit = 1 } else { quotient bit = 0 }
    { emit("ldind", "sp,t0,20"); emit("ldind", "sp,t1,12"); emit("bne", cat("t0,t1,", LhiDiff)); emit("nop"); }
    { emit("ldind", "sp,t0,16"); emit("ldind", "sp,t1,8"); emit("sltu", "t3,t0,t1"); }
    { emit("beq", cat("t3,zero,", LsubYes)); emit("nop"); }
    { emit("j", LsubNo); emit("nop"); }
    emitLabel(LhiDiff);
    { emit("sltu", "t3,t0,t1"); emit("beq", cat("t3,zero,", LsubYes)); emit("nop"); }
    { emit("j", LsubNo); emit("nop"); }
    emitLabel(LsubYes);
    { emit("ldind", "sp,t0,16"); emit("ldind", "sp,t1,8"); emit("sltu", "t6,t0,t1"); emit("subu", "t0,t0,t1"); emit("stind", "sp,t0,16"); }
    { emit("ldind", "sp,t0,20"); emit("ldind", "sp,t1,12"); emit("subu", "t0,t0,t1"); emit("subu", "t0,t0,t6"); emit("stind", "sp,t0,20"); }
    { emit("addiu", "t3,zero,1"); emit("j", LshiftQ); emit("nop"); }
    { emitLabel(LsubNo); emit("addu", "t3,zero,zero"); }
    emitLabel(LshiftQ);

    // q = (q << 1) | quotient bit (in t3).
    { emit("ldind", "sp,t0,24"); emit("srl", "t0,t0,31"); emit("addu", "t5,t0,zero"); }
    { emit("ldind", "sp,t0,24"); emit("sll", "t0,t0,1"); emit("or", "t0,t0,t3"); emit("stind", "sp,t0,24"); }
    { emit("ldind", "sp,t0,28"); emit("sll", "t0,t0,1"); emit("or", "t0,t0,t5"); emit("stind", "sp,t0,28"); }
    { emit("j", Lloop); emit("nop"); }

    emitLabel(Ldone);
    if (!isUnsigned) {
        std::string LskipNegQ = newLabel(), LskipNegR = newLabel();
        { emit("ldind", "sp,t3,40"); emit("ldind", "sp,t0,44"); emit("xor", "t3,t3,t0"); }
        { emit("beq", cat("t3,zero,", LskipNegQ)); emit("nop"); }
        { emit("ldind", "sp,t0,24"); emit("ldind", "sp,t1,28"); }
        genNegate64();
        { emit("stind", "sp,t0,24"); emit("stind", "sp,t1,28"); }
        emitLabel(LskipNegQ);
        { emit("ldind", "sp,t3,40"); emit("beq", cat("t3,zero,", LskipNegR)); emit("nop"); }
        { emit("ldind", "sp,t0,16"); emit("ldind", "sp,t1,20"); }
        genNegate64();
        { emit("stind", "sp,t0,16"); emit("stind", "sp,t1,20"); }
        emitLabel(LskipNegR);
    }
    { emit("ldind", "sp,t0,24"); emit("ldind", "sp,t1,28"); }  // quotient
    { emit("ldind", "sp,t4,16"); emit("ldind", "sp,t5,20"); }  // remainder (saved past the scratch release)
    emit("addiu", "sp,sp,52");  // release the 9-word scratch + the 4 earlier stashed operand words
    if (wantRemainder) {
        { emit("addu", "t0,t4,zero"); emit("addu", "t1,t5,zero"); }
    }
}

// A `long long` shifted by a runtime count: a loop of 1-bit shifts,
// each carrying the bit across the word boundary by hand (SLLV/SRLV
// only shift one 32-bit word).
void MipsCodegen::genWideShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned) {
    genExpr(lhs);
    { emit("addiu", "sp,sp,-8"); emit("stind", "sp,t0"); emit("stind", "sp,t1,4"); }
    genExpr(rhs);  // t0 = shift count (already promoted to a plain int)
    emit("addu", "t3,t0,zero");
    { emit("ldind", "sp,t0"); emit("ldind", "sp,t1,4"); emit("addiu", "sp,sp,8"); }  // t1:t0 restored = the value being shifted
    std::string Lloop = newLabel(), Ldone = newLabel();
    { emitLabel(Lloop); emit("beq", cat("t3,zero,", Ldone)); emit("nop"); emit("addiu", "t3,t3,-1"); }
    if (isLeft) {
        emit("srl", "t4,t0,31");       // t4 = t0's old top bit, carries into t1
        emit("sll", "t0,t0,1");
        { emit("sll", "t1,t1,1"); emit("or", "t1,t1,t4"); }
    } else {
        emit("sll", "t4,t1,31");       // t4 = (t1&1)<<31, carries into t0's top
        { emit("srl", "t0,t0,1"); emit("or", "t0,t0,t4"); }
        emit((isUnsigned ? "srl" : "sra"), "t1,t1,1");
    }
    { emit("j", Lloop); emit("nop"); }
    emitLabel(Ldone);
}

// A `long long` comparison: compare high words first (signed/unsigned
// per isUnsigned), falling back to an unsigned low-word compare only
// when the high words are equal.
void MipsCodegen::genWideCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned) {
    genExpr(lhs);
    { emit("addiu", "sp,sp,-8"); emit("stind", "sp,t0"); emit("stind", "sp,t1,4"); }
    genExpr(rhs);
    { emit("addu", "t5,t0,zero"); emit("addu", "t6,t1,zero"); }  // t5:t6 = rhs (lo:hi)
    { emit("ldind", "sp,t3"); emit("ldind", "sp,t4,4"); emit("addiu", "sp,sp,8"); }  // t3:t4 = lhs (lo:hi)
    if (op == "==" || op == "!=") {
        std::string Lfalse = newLabel(), Ldone = newLabel();
        const char *onEqual = op == "==" ? "1" : "0";
        const char *onDiffer = op == "==" ? "0" : "1";
        { emit("bne", cat("t3,t5,", Lfalse)); emit("nop"); }
        { emit("bne", cat("t4,t6,", Lfalse)); emit("nop"); }
        { emit("addiu", cat("t0,zero,", onEqual)); emit("j", Ldone); emit("nop"); }
        { emitLabel(Lfalse); emit("addiu", cat("t0,zero,", onDiffer)); }
        emitLabel(Ldone);
        return;
    }
    // Once the high words are equal, the low 32 bits are an unsigned
    // magnitude regardless of the overall type's signedness.
    std::string LhiDiff = newLabel(), Ldone = newLabel();
    { emit("bne", cat("t4,t6,", LhiDiff)); emit("nop"); }
    if (op == "<") {
        emit("sltu", "t0,t3,t5");
    } else if (op == ">") {
        emit("sltu", "t0,t5,t3");
    } else if (op == "<=") {
        { emit("sltu", "t0,t5,t3"); emit("addiu", "t1,zero,1"); emit("xor", "t0,t1,t0"); }  // !(rhs<lhs)
    } else {  // ">="
        { emit("sltu", "t0,t3,t5"); emit("addiu", "t1,zero,1"); emit("xor", "t0,t1,t0"); }  // !(lhs<rhs)
    }
    { emit("j", Ldone); emit("nop"); }
    emitLabel(LhiDiff);
    const char *hiInsn = isUnsigned ? "sltu" : "slt";
    if (op == "<") {
        emit(hiInsn, "t0,t4,t6");
    } else if (op == ">") {
        emit(hiInsn, "t0,t6,t4");
    } else if (op == "<=") {
        { emit(hiInsn, "t0,t6,t4"); emit("addiu", "t1,zero,1"); emit("xor", "t0,t1,t0"); }
    } else {  // ">="
        { emit(hiInsn, "t0,t4,t6"); emit("addiu", "t1,zero,1"); emit("xor", "t0,t1,t0"); }
    }
    emitLabel(Ldone);
}

// Entry: t0 = old, t1 = rhs (both at `type`'s width/signedness).
// Neither MULT/MFLO nor DIV/MFHI/MFLO touch t2, so the address
// genCompoundThroughT2() holds there survives this call.
void MipsCodegen::genCompoundOp(const std::string &op, Type type) {
    bool isUnsigned = type.isUnsigned;
    if (op == "+") { emit("addu", "t0,t0,t1"); return; }
    if (op == "-") { emit("subu", "t0,t0,t1"); return; }
    if (op == "*") {
        emit((isUnsigned ? "multu" : "mult"), "t0,t1");
        emit("mflo", "t0");
        return;
    }
    if (op == "&") { emit("and", "t0,t0,t1"); return; }
    if (op == "|") { emit("or", "t0,t0,t1"); return; }
    if (op == "^") { emit("xor", "t0,t0,t1"); return; }
    if (op == "<<" || op == ">>") {
        const char *insn = op == "<<" ? "sllv" : (isUnsigned ? "srlv" : "srav");
        emit(insn, "t0,t0,t1");
        return;
    }
    // "/" or "%"
    emit((isUnsigned ? "divu" : "div"), "t0,t1");
    emit((op == "%" ? "mfhi" : "mflo"), "t0");
}

// t2 must already hold the resolved address on entry; it's stashed on
// the stack across evaluating e.rhs (which may clobber t0-t4) and never
// recomputed, so the target's address sub-expression runs exactly once.
void MipsCodegen::genCompoundThroughT2(const ir::Expr &e) {
    { emit("addiu", "sp,sp,-4"); emit("stind", "sp,t2"); }  // stash address
    genExpr(*e.rhs);                                     // t0 = rhs
    emit("addu", "t1,t0,zero");                      // t1 = rhs
    { emit("ldind", "sp,t2"); emit("addiu", "sp,sp,4"); }         // t2 = address (restored)
    loadOperand("t2", e.type);                                 // t0 = old
    if (e.yieldOld) { emit("addiu", "sp,sp,-4"); emit("stind", "sp,t0"); }  // stash old
    genCompoundOp(e.op, e.type);                                 // t0 = new (raw)
    narrowT0To(e.type);
    if (e.yieldOld) {
        emit("addu", "t1,t0,zero");                          // t1 = new
        { emit("ldind", "sp,t0"); emit("addiu", "sp,sp,4"); }             // t0 = old
        emit("addu", "t3,t0,zero");                              // t3 = old, preserved across the store below
        emit("addu", "t0,t1,zero");                                // t0 = new - storeOperand always stores from t0
        storeOperand("t2", e.type);
        emit("addu", "t0,t3,zero");                                  // t0 = old (final result)
    } else {
        storeOperand("t2", e.type);  // t0 already = new
    }
}

void MipsCodegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    // Both operands share one common type (IRBuilder's usual-
    // arithmetic-conversion pass), except shift (only LHS matters)
    // and &&/|| (width-independent, handled below).
    Type operandType = e.lhs->type;
    bool wide = isWide(operandType);
    bool isUnsigned = operandType.isUnsigned;
    if (op == "+" || op == "-") {
        if (wide) {
            genWideAdd(*e.lhs, *e.rhs, op == "+");
            return;
        }
        genExpr(*e.lhs);
        { emit("addiu", "sp,sp,-4"); emit("stind", "sp,t0"); }
        genExpr(*e.rhs);
        { emit("ldind", "sp,t1"); emit("addiu", "sp,sp,4"); }  // t1 = lhs, t0 = rhs
        if (op == "+") {
            emit("addu", "t0,t1,t0");
        } else {
            emit("subu", "t0,t1,t0");
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
            { emit("addiu", "sp,sp,-8"); emit("stind", "sp,t0"); emit("stind", "sp,t1,4"); }
            genExpr(*e.rhs);
            { emit("addu", "t3,t0,zero"); emit("addu", "t4,t1,zero"); }
            { emit("ldind", "sp,t0"); emit("ldind", "sp,t1,4"); emit("addiu", "sp,sp,8"); }
            { emit(insn, "t0,t0,t3"); emit(insn, "t1,t1,t4"); }
            return;
        }
        genExpr(*e.lhs);
        { emit("addiu", "sp,sp,-4"); emit("stind", "sp,t0"); }
        genExpr(*e.rhs);
        { emit("ldind", "sp,t1"); emit("addiu", "sp,sp,4"); }
        emit(insn, "t0,t1,t0");
        return;
    }
    if (op == "<<" || op == ">>") {
        if (wide) {
            genWideShift(*e.lhs, *e.rhs, op == "<<", isUnsigned);
            return;
        }
        genExpr(*e.lhs);
        { emit("addiu", "sp,sp,-4"); emit("stind", "sp,t0"); }
        genExpr(*e.rhs);  // t0 = shift count
        emit("addu", "t1,t0,zero");
        { emit("ldind", "sp,t0"); emit("addiu", "sp,sp,4"); }  // t0 = value to shift
        const char *insn = op == "<<" ? "sllv" : (isUnsigned ? "srlv" : "srav");
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

// Real hardware multiply (MULT/MULTU+MFLO), a full 32x32->32 product.
void MipsCodegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    bool isUnsigned = lhs.type.isUnsigned;
    genExpr(lhs);
    { emit("addiu", "sp,sp,-4"); emit("stind", "sp,t0"); }
    genExpr(rhs);
    { emit("ldind", "sp,t1"); emit("addiu", "sp,sp,4"); }  // t1 = lhs, t0 = rhs
    emit((isUnsigned ? "multu" : "mult"), "t1,t0");
    emit("mflo", "t0");
}

// Real hardware divide (DIV/DIVU); MFLO/MFHI pull the quotient and
// remainder straight back out.
void MipsCodegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    { emit("addiu", "sp,sp,-4"); emit("stind", "sp,t0"); }
    genExpr(rhs);
    { emit("ldind", "sp,t1"); emit("addiu", "sp,sp,4"); }  // t1 = lhs (dividend), t0 = rhs (divisor)
    emit((isUnsigned ? "divu" : "div"), "t1,t0");
    emit((wantRemainder ? "mfhi" : "mflo"), "t0");
}

// SLT/SLTU directly produce the 0/1 boolean needed, no branch required.
// isUnsigned picks SLT vs SLTU for the ordered forms; ==/!= need no
// such choice.
void MipsCodegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs) {
    if (isWide(lhs.type)) {
        genWideCompare(op, lhs, rhs, lhs.type.isUnsigned);
        return;
    }
    bool isUnsigned = lhs.type.isUnsigned;
    const char *sltInsn = isUnsigned ? "sltu" : "slt";
    genExpr(lhs);
    { emit("addiu", "sp,sp,-4"); emit("stind", "sp,t0"); }
    genExpr(rhs);
    { emit("ldind", "sp,t1"); emit("addiu", "sp,sp,4"); }  // t1 = lhs, t0 = rhs
    if (op == "<") {
        emit(sltInsn, "t0,t1,t0");  // t0 = (lhs < rhs)
        return;
    }
    if (op == ">") {
        emit(sltInsn, "t0,t0,t1");  // t0 = (rhs < lhs) = (lhs > rhs)
        return;
    }
    if (op == "<=") {
        emit(sltInsn, "at,t0,t1");           // at = (rhs < lhs)
        { emit("addiu", "t0,zero,1"); emit("xor", "t0,t0,at"); }  // t0 = !(rhs<lhs) = (lhs<=rhs)
        return;
    }
    if (op == ">=") {
        emit(sltInsn, "at,t1,t0");   // at = (lhs < rhs)
        { emit("addiu", "t0,zero,1"); emit("xor", "t0,t0,at"); }  // t0 = !(lhs<rhs) = (lhs>=rhs)
        return;
    }
    if (op == "==") {
        emit("subu", "at,t1,t0");    // at = lhs - rhs
        emit("sltiu", "t0,at,1");    // t0 = (unsigned(at) < 1) = (at == 0)
        return;
    }
    if (op == "!=") {
        emit("subu", "at,t1,t0");    // at = lhs - rhs
        emit("sltu", "t0,zero,at");  // t0 = (0 < unsigned(at)) = (at != 0)
        return;
    }
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

// A wide operand's truthiness needs both words; `or t0,t0,t1` folds
// them together before the usual zero test.
void MipsCodegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    if (isWide(lhs.type)) emit("or", "t0,t0,t1");
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    { emit("beq", cat("t0,zero,", Lfalse)); emit("nop"); }
    genExpr(rhs);
    if (isWide(rhs.type)) emit("or", "t0,t0,t1");
    { emit("beq", cat("t0,zero,", Lfalse)); emit("nop"); }
    { emit("j", Ltrue); emit("nop"); }
    { emitLabel(Lfalse); emit("addiu", "t0,zero,0"); emit("j", Ldone); emit("nop"); }
    { emitLabel(Ltrue); emit("addiu", "t0,zero,1"); }
    emitLabel(Ldone);
}

void MipsCodegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    if (isWide(lhs.type)) emit("or", "t0,t0,t1");
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    { emit("bne", cat("t0,zero,", Ltrue)); emit("nop"); }
    genExpr(rhs);
    if (isWide(rhs.type)) emit("or", "t0,t0,t1");
    { emit("bne", cat("t0,zero,", Ltrue)); emit("nop"); }
    { emit("j", Lfalse); emit("nop"); }
    { emitLabel(Ltrue); emit("addiu", "t0,zero,1"); emit("j", Ldone); emit("nop"); }
    { emitLabel(Lfalse); emit("addiu", "t0,zero,0"); }
    emitLabel(Ldone);
}

// SLTIU t0,t0,1 computes (unsigned(t0) < 1), i.e. logical not, in one
// instruction.
void MipsCodegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    if (isWide(rhs.type)) emit("or", "t0,t0,t1");
    emit("sltiu", "t0,t0,1");
}

std::optional<std::string> MipsCodegen::assembleInProcess(const std::string &asmText,
                                                            const std::string &format) const {
    if (format == "raw") return assembleMips(asmText);
    // coff/elf/macho/aout/objraw: build one format-neutral ObjectFile,
    // then pick that format's FileFormatWriter to serialize it.
    if (auto kind = fileFormatKindFromName(format)) {
        ObjectFile obj = assembleMipsObject(asmText);
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

}  // namespace rcc
