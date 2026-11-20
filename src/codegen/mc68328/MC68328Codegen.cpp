#include "MC68328Codegen.h"

#include <cstdint>
#include <stdexcept>

#include "MC68328Assembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

// Initial A7, loaded by the reset exception from vector 0; well above the loaded program,
// growing down into unused RAM (tests/run_tests.sh gives the simulator 16MB).
constexpr uint32_t kInitialSP = 0x100000;

// `_start` sits right after the two-vector reset table at address 0, with no gap: `org` in
// MC68328Assembler.cpp only affects label resolution, never where bytes land in the output
// buffer, so this backend never calls `org` again to "jump" forward.
constexpr uint32_t kStartAddress = 8;
}  // namespace

// A literal element is its decimal text; a symbol element (a dispatch-table
// address-of-function/global) is the referenced Slot's or Function's label, written out bare.
std::string MC68328Codegen::arrayElemText(const ir::ArrayInitElem &e) {
    if (!e.isSymbol) return std::to_string(static_cast<int32_t>(static_cast<uint32_t>(e.value)));
    return e.slot ? label(e.slot) : label(e.func);
}

void MC68328Codegen::declareArray(const std::string &lbl, const std::vector<ir::ArrayInitElem> &values, Type elemType) {
    if (values.empty()) return;
    int w = widthOfType(elemType);
    // No 64-bit data directive: a `long long` literal element emits as two "dl"s (low dword
    // then high); a symbol element is always native-int-width, so it still gets one "dl".
    const char *directive = w == 1 ? "db" : w == 2 ? "dw" : "dl";
    std::ostringstream line;
    line << lbl << ": " << directive << " ";
    bool first = true;
    for (const auto &e : values) {
        if (w == 8 && !e.isSymbol) {
            auto v = static_cast<uint64_t>(e.value);
            if (!first) line << ", ";
            line << static_cast<int32_t>(static_cast<uint32_t>(v)) << ", "
                 << static_cast<int32_t>(static_cast<uint32_t>(v >> 32));
            first = false;
            continue;
        }
        if (!first) line << ", ";
        line << arrayElemText(e);
        first = false;
    }
    trailer_.push_back(line.str());
    // 68000 memory accesses need an even address; a `db` array is the only shape that can leave
    // the running offset odd, so pad it back to even here.
    if (w == 1 && (values.size() % 2) != 0) trailer_.push_back("db 0");
}

std::string MC68328Codegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    // In object mode, `prog` may not define `main` at all: MC68328LinkerFactory.cpp alone
    // synthesizes the reset vector table and entry sequence once objects are merged and
    // `main`'s linked address is resolvable. An object file's own base is `org 0`; the linker
    // places it.
    emitRaw("org 0");
    if (!objectMode) {
        // Reset vector table: vector 0 (initial SSP), vector 1 (initial PC); the CPU's reset
        // exception loads A7 and PC from these directly.
        emit("dl", kInitialSP);
        emit("dl", kStartAddress);
        emitLabel("_start");
        // Belt-and-suspenders reload of A7: some simulators (confirmed on qemu-system-m68k's
        // `-M none` board) honor the PC half of reset from vector 1 but not the A7 half.
        emit("movel", cat(kInitialSP, ",a7"));
        emit("jsr", label(mainFn));
        // The result sits in D0 the moment execution reaches `halt`.
        emitLabel("halt");
        emit("bra", "halt");
        emitRaw("");
    }

    for (const auto &fb : prog.functionBodies) genFunction(fb);

    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        // An external global isn't defined in this file: it must never get storage here, only
        // be referenced by name, letting the assembler defer it to a relocation.
        if (slot->isExternal) continue;
        const std::string lbl = label(slot.get());
        if (slot->arraySize > 0) {
            declareArray(lbl, slot->arrayInit, slot->type);
        } else if (slot->type.base == BaseType::Struct) {
            // No struct initializer exists in this subset, so this is
            // always zero-filled; reuses declareArray's own element-list
            // emission rather than a new directive, since this
            // assembler dialect has no "reserve N zero bytes" form of
            // its own.
            declareArray(lbl, std::vector<ir::ArrayInitElem>(slot->type.structSize), Type{BaseType::Char, false});
        } else {
            long v = slot->hasInit ? slot->initValue : 0;
            int w = widthOfType(slot->type);
            if (w == 8) {
                auto uv = static_cast<uint64_t>(v);
                trailer_.push_back(lbl + ": dl " + std::to_string(static_cast<int32_t>(static_cast<uint32_t>(uv))) +
                                    ", " + std::to_string(static_cast<int32_t>(static_cast<uint32_t>(uv >> 32))) +
                                    "  ; " + slot->sourceName);
            } else {
                const char *directive = w == 1 ? "db" : w == 2 ? "dw" : "dl";
                trailer_.push_back(lbl + ": " + directive + " " + std::to_string(v) + "  ; " + slot->sourceName);
                if (w == 1) trailer_.push_back("db 0");  // keep the running offset even
            }
        }
    }

    std::ostringstream out;
    out << code_.str();
    for (const auto &line : trailer_) out << line << "\n";
    return out.str();
}

std::map<std::string, std::string> MC68328Codegen::perFileAsm() const { return perFileAsm_; }

void MC68328Codegen::genFunction(const ir::FunctionBody &fn) {
    emitLabel(label(fn.func));
    // `linkl a6,-N` pushes the caller's a6, points this call's a6 at it, and reserves N bytes
    // below it for this function's Locals.
    emit("linkl", cat("a6,", (-fn.func->frameSize)));

    genStmt(*fn.body);

    emit("unlkl", "a6");
    emit("rts");
    emitRaw("");
}

void MC68328Codegen::genStmt(const ir::Stmt &s) {
    switch (s.kind) {
        case ir::StmtKind::Block: {
            for (const auto &child : s.body) genStmt(*child);
            return;
        }
        case ir::StmtKind::If: {
            genExpr(*s.cond);
            if (isWide(s.cond->type)) emit("orl", "d1,d0");
            emit("cmpl", "0,d0");
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            emit("beq", (s.elseStmt ? Lelse : Ldone));
            genStmt(*s.thenStmt);
            if (s.elseStmt) {
                emit("bra", Ldone);
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
            if (isWide(s.cond->type)) emit("orl", "d1,d0");
            emit("cmpl", "0,d0");
            emit("beq", Lend);
            loopLabels_.push_back({Lcont, Lend});
            genStmt(*s.loopBody);
            loopLabels_.pop_back();
            if (s.postStmt) {
                emitLabel(Lcont);
                genStmt(*s.postStmt);
            }
            emit("bra", Lstart);
            emitLabel(Lend);
            return;
        }
        case ir::StmtKind::Break: {
            emit("bra", loopLabels_.back().second);
            return;
        }
        case ir::StmtKind::Continue: {
            emit("bra", loopLabels_.back().first);
            return;
        }
        case ir::StmtKind::Return: {
            if (s.initOrReturnOrExpr) genExpr(*s.initOrReturnOrExpr);
            emit("unlkl", "a6");
            emit("rts");
            return;
        }
        case ir::StmtKind::LocalDecl: {
            if (s.declSlot->arraySize > 0) {
                genLocalArrayInit(s.declSlot);
            } else if (s.initOrReturnOrExpr) {
                genExpr(*s.initOrReturnOrExpr);
                storeOperand(addr(s.declSlot), addr(s.declSlot, 4), s.declSlot->type);
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
            emit("bra", irLabel(s.labelId));
            return;
        }
    }
}

void MC68328Codegen::genExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit: {
            if (isWide(e.type)) {
                auto v = static_cast<uint64_t>(e.intValue);
                emit("movel", cat(static_cast<int32_t>(static_cast<uint32_t>(v)), ",d0"));
                emit("movel", cat(static_cast<int32_t>(static_cast<uint32_t>(v >> 32)), ",d1"));
                return;
            }
            emit("movel", cat(static_cast<int32_t>(static_cast<uint32_t>(e.intValue)), ",d0"));
            return;
        }
        case ir::ExprKind::Var: {
            loadOperand(operandOf(e.slot), operandOf(e.slot, 4), e.slot->type);
            return;
        }
        case ir::ExprKind::Index: {
            genIndex(e);
            return;
        }
        case ir::ExprKind::Assign: {
            if (e.lhs) {  // slot[idx] = rhs
                genIndexAddr(e.slot, *e.lhs);  // a0 = target address
                emit("pushl", "a0");
                genExpr(*e.rhs);
                emit("popl", "a0");
                storeIndirect(e.slot->type);
                return;
            }
            genExpr(*e.rhs);
            storeOperand(operandOf(e.slot), operandOf(e.slot, 4), e.slot->type);
            return;
        }
        case ir::ExprKind::DerefAssign: {
            // Stores at e.type's own width/signedness: the pointee type, or a native int word
            // for the untyped-int-as-address style.
            genExpr(*e.lhs);  // d0 = pointer value
            emit("movel", "d0,a0");
            emit("pushl", "a0");
            genExpr(*e.rhs);
            emit("popl", "a0");
            storeIndirect(e.type);
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            if (e.lhs) {  // slot[idx] op= rhs (including slot[idx]++/--)
                genIndexAddr(e.slot, *e.lhs);  // a0 = address
                genCompoundThroughA0(e);
                return;
            }
            // Plain scalar target: operandOf(slot) is a stable frame/absolute operand, so no
            // address needs to be held across rhs's evaluation.
            std::string mem = operandOf(e.slot);
            genExpr(*e.rhs);  // d0 = rhs
            emit("movel", "d0,d1");
            loadOperand(mem, "", e.type);  // d0 = old
            if (e.yieldOld) emit("pushl", "d0");
            genCompoundOp(e.op, e.type);
            narrowD0To(e.type);
            storeOperand(mem, "", e.type);
            if (e.yieldOld) emit("popl", "d0");
            return;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            genExpr(*e.lhs);  // d0 = pointer value
            emit("movel", "d0,a0");  // a0 = address
            genCompoundThroughA0(e);
            return;
        }
        case ir::ExprKind::Ternary: {
            genExpr(*e.lhs);
            if (isWide(e.lhs->type)) emit("orl", "d1,d0");
            emit("cmpl", "0,d0");
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            emit("beq", Lelse);
            genExpr(*e.rhs);
            emit("bra", Ldone);
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
                    emit("negl", "d0");
                }
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                // Loads at e.type's own width/signedness, same as DerefAssign above.
                genExpr(*e.rhs);  // d0 = pointer value
                emit("movel", "d0,a0");
                loadIndirect(e.type);
            } else if (e.op == "~") {
                genExpr(*e.rhs);
                emit("notl", "d0");
                if (isWide(e.rhs->type)) emit("notl", "d1");
            } else {  // "&"
                genAddrOf(*e.rhs);
            }
            return;
        }
        case ir::ExprKind::Call: {
            // Stack-passed arguments, pushed right-to-left so param 0 ends up closest to the
            // return address JSR pushes; caller cleans up with `adda <N>,a7` afterward (cdecl
            // style). A wide (`long long`) argument pushes its high word first so its low word
            // ends up at the lower address, matching a wide Param's frameOffset/frameOffset+4.
            int totalBytes = 0;
            for (size_t i = e.args.size(); i-- > 0;) {
                genExpr(*e.args[i]);
                if (isWide(e.args[i]->type)) {
                    emit("pushl", "d1");
                    emit("pushl", "d0");
                    totalBytes += 8;
                } else {
                    emit("pushl", "d0");
                    totalBytes += 4;
                }
            }
            emit("jsr", label(e.func));
            if (totalBytes > 0) emit("adda", cat(totalBytes, ",a7"));
            return;
        }
        case ir::ExprKind::CallPtr: {
            // e.args is always empty here: the callee isn't known at compile time, so there's
            // no per-callee cell to pass arguments through. "jsrind a0" is a near call through
            // whatever address a0 holds.
            genExpr(*e.lhs);  // d0 = the callee's own entry address
            emit("movel", "d0,a0");
            emit("jsrind", "a0");
            return;
        }
        case ir::ExprKind::Binary: {
            genBinary(e);
            return;
        }
        case ir::ExprKind::Intrinsic: {
            // The MC68328 has real maskable interrupts, but this bare compile target never sets
            // up an interrupt vector table, so `ion`/`iof` have nothing to toggle: no-op.
            (void)e;
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

// Leaves the computed address in A0. `slot->arraySize > 0` means a compile-time-known base
// (a Global's label via `leal`, or a Local's frame-relative addr()); either array's elements
// sit at increasing addresses, so both index base-plus (`adda`). Otherwise `slot` is a
// pointer variable, so the base is read from its stored value first.
void MC68328Codegen::genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
    genExpr(idxExpr);  // d0 = index
    // d0 *= widthOf(slot->type) via repeated doubling, or a real multiply for a
    // non-power-of-two width (a struct's).
    int doublings = elemDoublings(slot->type);
    if (doublings >= 0) {
        for (int i = 0; i < doublings; i++) emit("addl", "d0,d0");
    } else {
        scaleIndexByWidth(slot->type);
    }
    if (slot->arraySize > 0) {
        if (slot->kind == ir::SlotKind::Global) {
            emit("leal", cat(label(slot), ",a0"));
            emit("adda", "d0,a0");
        } else {
            emit("leal", cat(addr(slot), ",a0"));
            emit("adda", "d0,a0");
        }
    } else {
        emit("movel", cat(operandOf(slot), ",a0"));
        emit("adda", "d0,a0");
    }
}

void MC68328Codegen::genCompoundOp(const std::string &op, Type type) {
    bool isUnsigned = type.isUnsigned;
    if (op == "+") { emit("addl", "d1,d0"); return; }
    if (op == "-") { emit("subl", "d1,d0"); return; }
    // MULS.W/MULU.W Dn,Dn: the only hardware multiply, 16-bit operands in, 32-bit product out.
    if (op == "*") { emit((isUnsigned ? "muluw" : "mulsw"), "d1,d0"); return; }
    if (op == "&") { emit("andl", "d1,d0"); return; }
    if (op == "|") { emit("orl", "d1,d0"); return; }
    // EOR.L Dn,<ea>: d1 is the source register field, d0 the destination.
    if (op == "^") { emit("eorl", "d1,d0"); return; }
    if (op == "<<" || op == ">>") {
        emit((op == "<<" ? "lsll" : (isUnsigned ? "lsrl" : "asrl")), "d1,d0");
        return;
    }
    // "/" or "%": d0 = old value (dividend), d1 = divisor.
    emit("movel", "d1,d5");
    emit("movel", "d0,d1");
    emit("movel", "d5,d0");
    emitDivMod32(isUnsigned);
    emit("movel", cat((op == "%" ? "d2" : "d3"), ",d0"));
}

// A0 must already hold the resolved address on entry; it's stashed on the stack across
// `e.rhs`'s evaluation since that can recursively clobber every data register and A0 itself.
void MC68328Codegen::genCompoundThroughA0(const ir::Expr &e) {
    emit("pushl", "a0");
    genExpr(*e.rhs);              // d0 = rhs
    emit("movel", "d0,d1");  // d1 = rhs, held across the a0 reload below
    emit("popl", "a0");  // a0 = address
    loadIndirect(e.type);             // d0 = old
    if (e.yieldOld) emit("pushl", "d0");
    genCompoundOp(e.op, e.type);         // d0 = new (raw)
    narrowD0To(e.type);
    if (e.yieldOld) {
        emit("movel", "d0,d2");  // d2 = new
        emit("popl", "d0");  // d0 = old
        emit("movel", "d0,d3");  // d3 = old, preserved across the store below
        emit("movel", "d2,d0");  // d0 = new (storeIndirect always stores from d0)
        storeIndirect(e.type);               // a0 still addresses the target (loadIndirect never bumps it - e.type is never wide)
        emit("movel", "d3,d0");  // d0 = old (final result)
    } else {
        storeIndirect(e.type);  // d0 already = new; a0 still addresses the target
    }
}

// Stores a Local array's initializer values directly into its frame slots (no static trailer
// storage, since a Local's storage is freshly reserved by `linkl` on each call).
void MC68328Codegen::genLocalArrayInit(const ir::Slot *slot) {
    int w = widthOfType(slot->type);
    for (size_t i = 0; i < slot->arrayInit.size(); i++) {
        const ir::ArrayInitElem &elem = slot->arrayInit[i];
        int byteOffset = static_cast<int>(i) * w;
        if (w == 8 && !elem.isSymbol) {
            auto v = static_cast<uint64_t>(elem.value);
            emit("movel", cat(static_cast<int32_t>(static_cast<uint32_t>(v)), ",", addr(slot, byteOffset)));
            emit("movel", cat(static_cast<int32_t>(static_cast<uint32_t>(v >> 32)), ",", addr(slot, byteOffset + 4)));
            continue;
        }
        const char *mnemonic = w == 1 ? "moveb" : w == 2 ? "movew" : "movel";
        emit(mnemonic, cat(arrayElemText(elem), ",", addr(slot, byteOffset)));
    }
}

void MC68328Codegen::genIndex(const ir::Expr &e) {
    genIndexAddr(e.slot, *e.lhs);
    loadIndirect(e.slot->type);
}

void MC68328Codegen::narrowD0To(Type to) {
    int w = widthOfType(to);
    if (w == 1) {
        emit("extw", "d0");
        emit("extl", "d0");
        if (to.isUnsigned) emit("andl", "255,d0");
    } else if (w == 2) {
        emit("extl", "d0");
        if (to.isUnsigned) emit("andl", "65535,d0");
    }
    // 4 bytes: nothing to do. 8 bytes never reaches here (see genConvert()).
}

void MC68328Codegen::loadOperand(const std::string &mem, const std::string &highMem, Type type) {
    int w = widthOfType(type);
    if (w == 1) {
        emit("moveb", cat(mem, ",d0"));
        if (type.isUnsigned) {
            emit("andl", "255,d0");
        } else {
            emit("extw", "d0");
            emit("extl", "d0");
        }
    } else if (w == 2) {
        emit("movew", cat(mem, ",d0"));
        if (type.isUnsigned) {
            emit("andl", "65535,d0");
        } else {
            emit("extl", "d0");
        }
    } else if (w == 4) {
        emit("movel", cat(mem, ",d0"));
    } else {  // 8 - long long: D0:D1 (lo:hi)
        emit("movel", cat(mem, ",d0"));
        emit("movel", cat(highMem, ",d1"));
    }
}

void MC68328Codegen::storeOperand(const std::string &mem, const std::string &highMem, Type type) {
    int w = widthOfType(type);
    if (w == 1) {
        emit("moveb", cat("d0,", mem));
    } else if (w == 2) {
        emit("movew", cat("d0,", mem));
    } else if (w == 4) {
        emit("movel", cat("d0,", mem));
    } else {  // 8
        emit("movel", cat("d0,", mem));
        emit("movel", cat("d1,", highMem));
    }
}

// A0 already holds the effective address; indirect counterpart of loadOperand(). The wide case
// bumps A0 by 4 between the low and high word.
void MC68328Codegen::loadIndirect(Type type) {
    int w = widthOfType(type);
    if (w == 1) {
        emit("ldindb", "a0,d0");
        if (type.isUnsigned) {
            emit("andl", "255,d0");
        } else {
            emit("extw", "d0");
            emit("extl", "d0");
        }
    } else if (w == 2) {
        emit("ldindw", "a0,d0");
        if (type.isUnsigned) {
            emit("andl", "65535,d0");
        } else {
            emit("extl", "d0");
        }
    } else if (w == 4) {
        emit("ldind", "a0,d0");
    } else {  // 8
        emit("ldind", "a0,d0");
        emit("adda", "4,a0");
        emit("ldind", "a0,d1");
    }
}

void MC68328Codegen::storeIndirect(Type type) {
    int w = widthOfType(type);
    if (w == 1) {
        emit("stindb", "d0,a0");
    } else if (w == 2) {
        emit("stindw", "d0,a0");
    } else if (w == 4) {
        emit("stind", "d0,a0");
    } else {  // 8
        emit("stind", "d0,a0");
        emit("adda", "4,a0");
        emit("stind", "d1,a0");
    }
}

// An implicit or explicit type conversion. Operands narrower than `int` are already promoted
// by IRBuilder, so the only work here is crossing the native-int/`long long` boundary, or
// re-truncating a narrower target.
void MC68328Codegen::genConvert(const ir::Expr &e) {
    Type from = e.lhs->type;
    Type to = e.type;
    bool fromWide = isWide(from);
    bool toWide = isWide(to);
    genExpr(*e.lhs);  // d0, or d0:d1 if fromWide
    if (fromWide && !toWide) {
        narrowD0To(to);  // d1 is simply discarded
    } else if (!fromWide && toWide) {
        if (from.isUnsigned) {
            emit("movel", "0,d1");
        } else {
            // Sign-extend D0 into D1 (no "cdq"-equivalent instruction here): arithmetic-shift a
            // copy of D0 right by 31 to leave D1 all-0s or all-1s per D0's sign.
            emit("movel", "d0,d1");
            emit("movel", "31,d2");
            emit("asrl", "d2,d1");
        }
    } else if (!fromWide && !toWide) {
        narrowD0To(to);
    }
    // fromWide && toWide: the bit pattern is already correct.
}

// `long long` add/sub: evaluate both into D0:D1 (lo:hi), stashing lhs's words on the stack
// while rhs evaluates, then ripple the carry/borrow across the word pair with ADDX/SUBX.
void MC68328Codegen::genWideAdd(const ir::Expr &lhs, const ir::Expr &rhs, bool isAdd) {
    genExpr(lhs);  // d0:d1 = lhs
    emit("pushl", "d1");
    emit("pushl", "d0");  // stack (top->bottom): lhs_lo, lhs_hi
    genExpr(rhs);                              // d0:d1 = rhs
    emit("movel", "d0,d2");
    emit("movel", "d1,d3");  // d2=rhs_lo, d3=rhs_hi
    emit("popl", "d0");
    emit("popl", "d1");  // d0=lhs_lo, d1=lhs_hi
    if (isAdd) {
        emit("addl", "d2,d0");
        emit("addxl", "d3,d1");
    } else {
        emit("subl", "d2,d0");
        emit("subxl", "d3,d1");
    }
}

// `long long` multiply: 68000 has no 32x32 hardware multiply (only 16x16->32 MULS.W/MULU.W,
// which a `long long`'s 32-bit halves don't fit), so this is a shift-and-add multiply over the
// full 64 bits, bit by bit. Two's-complement multiplication is sign-agnostic at the bit-pattern
// level, so no sign handling is needed, unlike genWideDivMod(). Addressed via `a0` over a stack
// scratch block: [a0+0/4] multiplicand (shifted left each iteration), [a0+8/12] multiplier
// (shifted right, consumed bit by bit), [a0+16/20] accumulating result. Result in D0(lo):D1(hi).
void MC68328Codegen::genWideMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    emit("pushl", "d1");
    emit("pushl", "d0");  // mcand_hi, mcand_lo (lo on top)
    genExpr(rhs);
    emit("pushl", "d1");
    emit("pushl", "d0");  // mplier_hi, mplier_lo (lo on top)
    emit("suba", "24,a7");
    emit("movel", "a7,a0");
    emit("movel", "[a0+32],d0");
    emit("movel", "d0,[a0+0]");  // mcand_lo
    emit("movel", "[a0+36],d0");
    emit("movel", "d0,[a0+4]");  // mcand_hi
    emit("movel", "[a0+24],d0");
    emit("movel", "d0,[a0+8]");  // mplier_lo
    emit("movel", "[a0+28],d0");
    emit("movel", "d0,[a0+12]");  // mplier_hi
    emit("movel", "0,d0");
    emit("movel", "d0,[a0+16]");
    emit("movel", "d0,[a0+20]");  // result = 0
    emit("movel", "1,d3");
    emit("movel", "31,d6");  // fixed shift-count registers
    std::string Lloop = newLabel(), Ldone = newLabel(), LnoAdd = newLabel();
    emit("movel", "64,d1");  // loop counter
    emitLabel(Lloop);
    emit("cmpl", "0,d1");
    emit("beq", Ldone);
    emit("subl", "1,d1");

    // if the multiplier's bit 0 is set, result += multiplicand (64-bit add via ADD/ADDX).
    emit("movel", "[a0+8],d0");
    emit("andl", "1,d0");
    emit("cmpl", "0,d0");
    emit("beq", LnoAdd);
    emit("movel", "[a0+16],d0");
    emit("movel", "[a0+0],d2");
    emit("addl", "d2,d0");
    emit("movel", "d0,[a0+16]");
    emit("movel", "[a0+20],d0");
    emit("movel", "[a0+4],d2");
    emit("addxl", "d2,d0");
    emit("movel", "d0,[a0+20]");
    emitLabel(LnoAdd);

    // multiplicand <<= 1: ADD.L Dn,Dn doubles Dn and sets the carry from bit 31; ADDX.L Dn,Dn
    // doubles the high word while folding that carry in.
    emit("movel", "[a0+0],d0");
    emit("addl", "d0,d0");
    emit("movel", "d0,[a0+0]");
    emit("movel", "[a0+4],d0");
    emit("addxl", "d0,d0");
    emit("movel", "d0,[a0+4]");

    // multiplier >>= 1: the high word's outgoing bit 0 is carried into the low word's new bit 31
    // by hand.
    emit("movel", "[a0+12],d4");  // d4 = mplier_hi (before shifting)
    emit("movel", "d4,d0");
    emit("lsrl", "d3,d0");
    emit("movel", "d0,[a0+12]");  // mplier_hi >>= 1
    emit("andl", "1,d4");
    emit("lsll", "d6,d4");  // d4 = (old mplier_hi & 1) << 31
    emit("movel", "[a0+8],d0");
    emit("lsrl", "d3,d0");
    emit("orl", "d4,d0");
    emit("movel", "d0,[a0+8]");

    emit("bra", Lloop);
    emitLabel(Ldone);
    emit("movel", "[a0+16],d0");
    emit("movel", "[a0+20],d1");  // d0:d1 = result
    emit("adda", "40,a7");  // release the 6-word scratch block and the 4 earlier stashed operand words
}

void MC68328Codegen::genNegate64() {
    emit("notl", "d0");
    emit("notl", "d1");
    emit("movel", "0,d2");
    emit("addl", "1,d0");
    emit("addxl", "d2,d1");
}

// Classic bit-serial shift/compare/subtract long division, addressed via a0 over a 9-word
// scratch block.
void MC68328Codegen::genWideDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    emit("pushl", "d1");
    emit("pushl", "d0");  // lhs_hi, lhs_lo (lhs_lo on top)
    genExpr(rhs);
    emit("pushl", "d1");
    emit("pushl", "d0");  // rhs_hi, rhs_lo (rhs_lo on top)
    emit("suba", "36,a7");
    emit("movel", "a7,a0");
    // Fetch the stashed operands from just above the scratch block.
    emit("movel", "[a0+44],d0");
    emit("movel", "d0,[a0+0]");  // n_lo = lhs_lo
    emit("movel", "[a0+48],d0");
    emit("movel", "d0,[a0+4]");  // n_hi = lhs_hi
    emit("movel", "[a0+36],d0");
    emit("movel", "d0,[a0+8]");  // d_lo = rhs_lo
    emit("movel", "[a0+40],d0");
    emit("movel", "d0,[a0+12]");  // d_hi = rhs_hi
    emit("movel", "0,d0");
    emit("movel", "d0,[a0+16]");
    emit("movel", "d0,[a0+20]");  // r = 0
    emit("movel", "d0,[a0+24]");
    emit("movel", "d0,[a0+28]");  // q = 0
    emit("movel", "64,d0");
    emit("movel", "d0,[a0+32]");  // 64 iterations

    // d6/d7 are fixed shift counts for the whole routine, never rewritten below.
    emit("movel", "1,d6");
    emit("movel", "31,d7");

    std::string LskipNegN = newLabel(), LskipNegD = newLabel();
    if (!isUnsigned) {
        // lhsSign/rhsSign reuse [a0+36]/[a0+40], the rhs staging words, never read again for
        // that purpose.
        emit("movel", "[a0+4],d0");
        emit("lsrl", "d7,d0");
        emit("movel", "d0,[a0+36]");  // lhsSign
        emit("movel", "[a0+12],d0");
        emit("lsrl", "d7,d0");
        emit("movel", "d0,[a0+40]");  // rhsSign
        emit("movel", "[a0+36],d0");
        emit("cmpl", "0,d0");
        emit("beq", LskipNegN);
        emit("movel", "[a0+0],d0");
        emit("movel", "[a0+4],d1");
        genNegate64();
        emit("movel", "d0,[a0+0]");
        emit("movel", "d1,[a0+4]");
        emitLabel(LskipNegN);
        emit("movel", "[a0+40],d0");
        emit("cmpl", "0,d0");
        emit("beq", LskipNegD);
        emit("movel", "[a0+8],d0");
        emit("movel", "[a0+12],d1");
        genNegate64();
        emit("movel", "d0,[a0+8]");
        emit("movel", "d1,[a0+12]");
        emitLabel(LskipNegD);
    }

    std::string Lloop = newLabel(), Ldone = newLabel();
    std::string LhiDiff = newLabel(), LsubYes = newLabel(), LsubNo = newLabel(), LshiftQ = newLabel();
    emitLabel(Lloop);
    emit("movel", "[a0+32],d0");
    emit("cmpl", "0,d0");
    emit("beq", Ldone);
    emit("subl", "1,d0");
    emit("movel", "d0,[a0+32]");

    // n <<= 1 (0-filled), capturing its own old top bit into d1.
    emit("movel", "[a0+4],d0");
    emit("lsrl", "d7,d0");
    emit("movel", "d0,d1");  // d1 = n's old top bit
    emit("movel", "[a0+0],d0");
    emit("lsrl", "d7,d0");
    emit("movel", "d0,d2");  // d2 = n_lo's old top bit
    emit("movel", "[a0+0],d0");
    emit("lsll", "d6,d0");
    emit("movel", "d0,[a0+0]");
    emit("movel", "[a0+4],d0");
    emit("lsll", "d6,d0");
    emit("orl", "d2,d0");
    emit("movel", "d0,[a0+4]");

    // r = (r << 1) | (n's old top bit, already in d1).
    emit("movel", "[a0+16],d0");
    emit("lsrl", "d7,d0");
    emit("movel", "d0,d3");  // d3 = r_lo's old top bit
    emit("movel", "[a0+16],d0");
    emit("lsll", "d6,d0");
    emit("orl", "d1,d0");
    emit("movel", "d0,[a0+16]");
    emit("movel", "[a0+20],d0");
    emit("lsll", "d6,d0");
    emit("orl", "d3,d0");
    emit("movel", "d0,[a0+20]");

    // if (r >= d) { r -= d; quotient bit = 1 } else { quotient bit = 0 } - always an unsigned
    // compare, since n and d were already negated to a non-negative magnitude above.
    emit("movel", "[a0+20],d0");
    emit("movel", "[a0+12],d1");
    emit("cmpl", "d1,d0");
    emit("bne", LhiDiff);
    emit("movel", "[a0+16],d0");
    emit("movel", "[a0+8],d1");
    emit("cmpl", "d1,d0");
    emit("bcc", LsubYes);
    emit("bra", LsubNo);
    emitLabel(LhiDiff);
    emit("bcc", LsubYes);
    emit("bra", LsubNo);
    emitLabel(LsubYes);
    emit("movel", "[a0+16],d0");
    emit("movel", "[a0+8],d1");
    emit("subl", "d1,d0");
    emit("movel", "d0,[a0+16]");
    emit("movel", "[a0+20],d0");
    emit("movel", "[a0+12],d1");
    emit("subxl", "d1,d0");
    emit("movel", "d0,[a0+20]");
    emit("movel", "1,d5");
    emit("bra", LshiftQ);
    emitLabel(LsubNo);
    emit("movel", "0,d5");
    emitLabel(LshiftQ);

    // q = (q << 1) | quotient bit (in d5).
    emit("movel", "[a0+24],d0");
    emit("lsrl", "d7,d0");
    emit("movel", "d0,d2");  // d2 = q_lo's old top bit
    emit("movel", "[a0+24],d0");
    emit("lsll", "d6,d0");
    emit("orl", "d5,d0");
    emit("movel", "d0,[a0+24]");
    emit("movel", "[a0+28],d0");
    emit("lsll", "d6,d0");
    emit("orl", "d2,d0");
    emit("movel", "d0,[a0+28]");
    emit("bra", Lloop);

    emitLabel(Ldone);
    if (!isUnsigned) {
        // Quotient is negative iff the operands' signs differed; the
        // remainder always takes the dividend's (lhs) own sign.
        std::string LskipNegQ = newLabel(), LskipNegR = newLabel();
        emit("movel", "[a0+36],d5");
        emit("movel", "[a0+40],d0");
        emit("eorl", "d5,d0");
        emit("cmpl", "0,d0");
        emit("beq", LskipNegQ);
        emit("movel", "[a0+24],d0");
        emit("movel", "[a0+28],d1");
        genNegate64();
        emit("movel", "d0,[a0+24]");
        emit("movel", "d1,[a0+28]");
        emitLabel(LskipNegQ);
        emit("movel", "[a0+36],d0");
        emit("cmpl", "0,d0");
        emit("beq", LskipNegR);
        emit("movel", "[a0+16],d0");
        emit("movel", "[a0+20],d1");
        genNegate64();
        emit("movel", "d0,[a0+16]");
        emit("movel", "d1,[a0+20]");
        emitLabel(LskipNegR);
    }
    emit("movel", "[a0+24],d0");
    emit("movel", "[a0+28],d1");  // d0:d1 = quotient
    emit("movel", "[a0+16],d2");
    emit("movel", "[a0+20],d3");  // d2:d3 = remainder
    emit("adda", "52,a7");  // release the 9-word scratch block and the 4 earlier stashed operand words
    if (wantRemainder) {
        emit("movel", "d2,d0");
        emit("movel", "d3,d1");
    }
}

// A `long long` shifted by a runtime count: no native instruction shifts more than 32 bits at
// once, so this is a counted loop of 1-bit shifts, carrying the bit across the word boundary
// by hand.
void MC68328Codegen::genWideShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned) {
    genExpr(lhs);
    emit("pushl", "d1");
    emit("pushl", "d0");
    genExpr(rhs);                   // d0 = shift count (already promoted to a plain int)
    emit("movel", "d0,d2");  // d2 = remaining count
    emit("popl", "d0");
    emit("popl", "d1");  // d0:d1 restored = the value being shifted (lo:hi)
    emit("movel", "1,d6");
    emit("movel", "31,d7");  // fixed shift-count registers (register-count-only dialect)
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("cmpl", "0,d2");
    emit("beq", Ldone);
    emit("subl", "1,d2");
    if (isLeft) {
        emit("movel", "d0,d3");
        emit("lsrl", "d7,d3");  // d3 = d0's old top bit
        emit("lsll", "d6,d1");
        emit("orl", "d3,d1");
        emit("lsll", "d6,d0");
    } else {
        emit("movel", "d1,d3");
        emit("lsll", "d7,d3");  // d3 = (d1&1)<<31
        emit("lsrl", "d6,d0");
        emit("orl", "d3,d0");
        emit((isUnsigned ? "lsrl" : "asrl"), "d6,d1");
    }
    emit("bra", Lloop);
    emitLabel(Ldone);
}

// A `long long` comparison: compare the high words first (signed or unsigned branch per
// `isUnsigned`, since that's where the sign bit lives); if equal, the low words compare as
// unsigned regardless of the overall type's signedness.
void MC68328Codegen::genWideCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned) {
    genExpr(lhs);
    emit("pushl", "d1");
    emit("pushl", "d0");
    genExpr(rhs);
    emit("movel", "d0,d2");
    emit("movel", "d1,d3");  // d2:d3 = rhs (lo:hi)
    emit("popl", "d0");
    emit("popl", "d1");  // d0:d1 = lhs (lo:hi)
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    if (op == "==" || op == "!=") {
        const char *onEqual = op == "==" ? "1" : "0";
        const char *onDiffer = op == "==" ? "0" : "1";
        emit("cmpl", "d2,d0");
        emit("bne", Lfalse);
        emit("cmpl", "d3,d1");
        emit("bne", Lfalse);
        emit("movel", cat(onEqual, ",d0"));
        emit("bra", Ldone);
        emitLabel(Lfalse);
        emit("movel", cat(onDiffer, ",d0"));
        emitLabel(Ldone);
        return;
    }
    std::string signedBcc = op == "<" ? "blt" : op == "<=" ? "ble" : op == ">" ? "bgt" : "bge";
    std::string unsignedBcc = op == "<" ? "bcs" : op == "<=" ? "bls" : op == ">" ? "bhi" : "bcc";
    std::string hiBcc = isUnsigned ? unsignedBcc : signedBcc;
    std::string LhiDiff = newLabel();
    emit("cmpl", "d3,d1");
    emit("bne", LhiDiff);
    emit("cmpl", "d2,d0");
    emit(unsignedBcc, Ltrue);
    emit("bra", Lfalse);
    emitLabel(LhiDiff);
    emit(hiBcc, Ltrue);
    emit("bra", Lfalse);
    emitLabel(Lfalse);
    emit("movel", "0,d0");
    emit("bra", Ldone);
    emitLabel(Ltrue);
    emit("movel", "1,d0");
    emitLabel(Ldone);
}

void MC68328Codegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        emit("leal", cat(operandOf(operand.slot), ",a0"));
        emit("movel", "a0,d0");
        return;
    }
    if (operand.kind == ir::ExprKind::Index) {
        genIndexAddr(operand.slot, *operand.lhs);
        emit("movel", "a0,d0");
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        emit("leal", cat(label(operand.func), ",a0"));
        emit("movel", "a0,d0");
        return;
    }
    err(operand.line, "internal: unhandled addressable-operand kind");
}

void MC68328Codegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    // Arithmetic/bitwise/comparison operands already share one common type (`e.lhs->type`),
    // except shift (only the LHS type matters) and &&/|| (handled by genLogicalAnd/Or below).
    Type operandType = e.lhs->type;
    bool wide = isWide(operandType);
    bool isUnsigned = operandType.isUnsigned;
    if (op == "+" || op == "-") {
        if (wide) {
            genWideAdd(*e.lhs, *e.rhs, op == "+");
            return;
        }
        genExpr(*e.lhs);
        emit("pushl", "d0");
        genExpr(*e.rhs);
        emit("popl", "d1");  // d1 = lhs, d0 = rhs
        if (op == "+") {
            emit("addl", "d1,d0");  // d0 = d0 + d1 = rhs + lhs
        } else {
            emit("subl", "d0,d1");  // d1 = d1 - d0 = lhs - rhs
            emit("movel", "d1,d0");
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
        const char *insn = op == "&" ? "andl" : op == "|" ? "orl" : "eorl";
        if (wide) {
            genExpr(*e.lhs);
            emit("pushl", "d1");
            emit("pushl", "d0");
            genExpr(*e.rhs);
            emit("movel", "d0,d2");
            emit("movel", "d1,d3");
            emit("popl", "d0");
            emit("popl", "d1");
            emit(insn, "d2,d0");
            emit(insn, "d3,d1");
            return;
        }
        genExpr(*e.lhs);
        emit("pushl", "d0");
        genExpr(*e.rhs);
        emit("popl", "d1");  // d1 = lhs, d0 = rhs
        // EOR's source/dest convention (Dn is always source) still gives the right result for
        // this commutative op regardless of operand order.
        emit(insn, "d1,d0");
        return;
    }
    if (op == "<<" || op == ">>") {
        if (wide) {
            genWideShift(*e.lhs, *e.rhs, op == "<<", isUnsigned);
            return;
        }
        genExpr(*e.lhs);
        emit("pushl", "d0");
        genExpr(*e.rhs);
        emit("movel", "d0,d1");  // d1 = shift count
        emit("popl", "d0");  // d0 = value to shift
        const char *insn = op == "<<" ? "lsll" : (isUnsigned ? "lsrl" : "asrl");
        emit(insn, "d1,d0");
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

// Real hardware multiply (`MULS.W`/`MULU.W`): 16-bit operands only.
void MC68328Codegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    bool isUnsigned = lhs.type.isUnsigned;
    genExpr(lhs);
    emit("pushl", "d0");
    genExpr(rhs);
    emit("popl", "d1");  // d1 = lhs, d0 = rhs
    emit((isUnsigned ? "muluw" : "mulsw"), "d1,d0");  // d0 = d0 * d1 = rhs * lhs
}

// `int`/`long` are 32 bits here, wider than DIVS.W/DIVU.W's 32/16
// divide with a 16-bit quotient, so division is a bit-serial loop
// (emitDivMod32()).
void MC68328Codegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    emit("pushl", "d0");
    genExpr(rhs);
    emit("popl", "d1");  // d1 = lhs (dividend), d0 = rhs (divisor)
    emitDivMod32(isUnsigned);
    emit("movel", cat((wantRemainder ? "d2" : "d3"), ",d0"));
}

// Restoring shift-subtract division, one quotient bit per iteration.
// The remainder's own top bit is checked before each shift: if set,
// the shifted value exceeds 32 bits and so exceeds the divisor, and
// the subtraction's modular result is still exact. Signed operands
// are divided as magnitudes; the quotient is negated when the signs
// differ and the remainder takes the dividend's sign.
void MC68328Codegen::emitDivMod32(bool isUnsigned) {
    emit("pushl", "a0");  // a compound assignment's target address
    emit("movel", "1,d6");
    emit("movel", "31,d7");
    std::string LskipN = newLabel(), LskipD = newLabel();
    if (!isUnsigned) {
        emit("movel", "d1,d5");
        emit("lsrl", "d7,d5");
        emit("pushl", "d5");  // dividend sign
        emit("movel", "d0,d5");
        emit("lsrl", "d7,d5");
        emit("pushl", "d5");  // divisor sign
        emit("movel", "a7,a0");  // [a0+0] divisor sign, [a0+4] dividend sign
        emit("movel", "[a0+4],d5");
        emit("cmpl", "0,d5");
        emit("beq", LskipN);
        emit("notl", "d1");
        emit("addl", "1,d1");
        emitLabel(LskipN);
        emit("movel", "[a0+0],d5");
        emit("cmpl", "0,d5");
        emit("beq", LskipD);
        emit("notl", "d0");
        emit("addl", "1,d0");
        emitLabel(LskipD);
    }
    std::string Lloop = newLabel(), Ldone = newLabel(), Lsub = newLabel(), Lnext = newLabel();
    emit("movel", "0,d2");
    emit("movel", "0,d3");
    emit("movel", "32,d4");
    emitLabel(Lloop);
    emit("cmpl", "0,d4");
    emit("beq", Ldone);
    emit("subl", "1,d4");
    emit("movel", "d1,d5");
    emit("lsrl", "d7,d5");
    emit("lsll", "d6,d1");  // d5 = dividend's outgoing bit
    emit("movel", "d2,a0");  // a0 = remainder before shifting
    emit("lsll", "d6,d2");
    emit("orl", "d5,d2");
    emit("lsll", "d6,d3");
    emit("movel", "a0,d5");
    emit("lsrl", "d7,d5");
    emit("cmpl", "0,d5");
    emit("bne", Lsub);
    emit("cmpl", "d0,d2");
    emit("bcc", Lsub);
    emit("bra", Lnext);
    emitLabel(Lsub);
    emit("subl", "d0,d2");
    emit("orl", "d6,d3");
    emitLabel(Lnext);
    emit("bra", Lloop);
    emitLabel(Ldone);
    if (!isUnsigned) {
        std::string LskipQ = newLabel(), LskipR = newLabel();
        emit("movel", "a7,a0");
        emit("movel", "[a0+0],d5");
        emit("movel", "[a0+4],d4");
        emit("eorl", "d4,d5");
        emit("cmpl", "0,d5");
        emit("beq", LskipQ);
        emit("notl", "d3");
        emit("addl", "1,d3");
        emitLabel(LskipQ);
        emit("cmpl", "0,d4");
        emit("beq", LskipR);
        emit("notl", "d2");
        emit("addl", "1,d2");
        emitLabel(LskipR);
        emit("adda", "8,a7");
    }
    emit("popl", "a0");
}

void MC68328Codegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs) {
    bool isUnsigned = lhs.type.isUnsigned;
    if (isWide(lhs.type)) {
        genWideCompare(op, lhs, rhs, isUnsigned);
        return;
    }
    if (op == "==") return emitCompare(lhs, rhs, "beq");
    if (op == "!=") return emitCompare(lhs, rhs, "bne");
    if (op == "<") return emitCompare(lhs, rhs, isUnsigned ? "bcs" : "blt");
    if (op == "<=") return emitCompare(lhs, rhs, isUnsigned ? "bls" : "ble");
    if (op == ">") return emitCompare(lhs, rhs, isUnsigned ? "bhi" : "bgt");
    if (op == ">=") return emitCompare(lhs, rhs, isUnsigned ? "bcc" : "bge");
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

// `CMP.L <ea>,Dn` sets condition codes for `Dn - ea`; `branchIfTrue` is already picked per
// signedness by genCompare() (BHI/BLS/BCC/BCS test carry for unsigned, BGT/BLE/BGE/BLT test
// N^V for signed), so it's applied directly here.
void MC68328Codegen::emitCompare(const ir::Expr &lhs, const ir::Expr &rhs, const std::string &branchIfTrue) {
    genExpr(lhs);
    emit("pushl", "d0");
    genExpr(rhs);
    emit("popl", "d1");  // d1 = lhs, d0 = rhs
    emit("cmpl", "d0,d1");  // flags = d1 - d0 = lhs - rhs
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit(branchIfTrue, Ltrue);
    emit("movel", "0,d0");
    emit("bra", Ldone);
    emitLabel(Ltrue);
    emit("movel", "1,d0");
    emitLabel(Ldone);
}

// A wide (`long long`) operand's truthiness needs both words: `orl d1,d0` folds them together
// before the usual "cmpl 0,d0".
void MC68328Codegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    if (isWide(lhs.type)) emit("orl", "d1,d0");
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    emit("cmpl", "0,d0");
    emit("beq", Lfalse);
    genExpr(rhs);
    if (isWide(rhs.type)) emit("orl", "d1,d0");
    emit("cmpl", "0,d0");
    emit("beq", Lfalse);
    emit("bra", Ltrue);
    emitLabel(Lfalse);
    emit("movel", "0,d0");
    emit("bra", Ldone);
    emitLabel(Ltrue);
    emit("movel", "1,d0");
    emitLabel(Ldone);
}

void MC68328Codegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    if (isWide(lhs.type)) emit("orl", "d1,d0");
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    emit("cmpl", "0,d0");
    emit("bne", Ltrue);
    genExpr(rhs);
    if (isWide(rhs.type)) emit("orl", "d1,d0");
    emit("cmpl", "0,d0");
    emit("bne", Ltrue);
    emit("bra", Lfalse);
    emitLabel(Ltrue);
    emit("movel", "1,d0");
    emit("bra", Ldone);
    emitLabel(Lfalse);
    emit("movel", "0,d0");
    emitLabel(Ldone);
}

void MC68328Codegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    if (isWide(rhs.type)) emit("orl", "d1,d0");
    emit("cmpl", "0,d0");
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit("beq", Ltrue);
    emit("movel", "0,d0");
    emit("bra", Ldone);
    emitLabel(Ltrue);
    emit("movel", "1,d0");
    emitLabel(Ldone);
}

std::optional<std::string> MC68328Codegen::assembleInProcess(const std::string &asmText,
                                                            const std::string &format) const {
    if (format == "raw") return assembleMC68328(asmText);
    // `coff`/`elf`/`macho`/`aout`/`objraw`: build one format-neutral ObjectFile, then pick that
    // format's FileFormatWriter to serialize it.
    if (auto kind = fileFormatKindFromName(format)) {
        ObjectFile obj = assembleMC68328Object(asmText);
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

}  // namespace rcc
