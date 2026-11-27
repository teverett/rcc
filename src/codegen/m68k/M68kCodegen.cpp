#include "M68kCodegen.h"

#include <cstdint>
#include <tuple>
#include <stdexcept>

#include "M68kAssembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

// Load address; the test harness sets the simulated CPU's PC here directly
// instead of via a reset vector.
constexpr uint32_t kBaseAddress = 0x1000;

// Initial A7 (stack pointer), set before calling main(); grows down into unused RAM.
constexpr uint32_t kInitialSP = 0x100000;
}  // namespace

// A literal element is its decimal text; a symbol element is the referenced
// Slot's or Function's label, resolved by `dl`'s expression evaluator the same
// way `leal label,a0` resolves one into a register.
std::string M68kCodegen::arrayElemText(const ir::ArrayInitElem &e) {
    if (!e.isSymbol) return std::to_string(static_cast<int32_t>(static_cast<uint32_t>(e.value)));
    return e.slot ? label(e.slot) : label(e.func);
}

void M68kCodegen::declareArray(const std::string &lbl, const std::vector<ir::ArrayInitElem> &values, Type elemType) {
    if (values.empty()) return;
    int w = widthOfType(elemType);
    // No 64-bit data directive: a `long long` literal element emits two dl's (low,
    // then high); a symbol element is always native-int width, so it still gets one dl.
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
    // 68000 memory accesses need an even address (odd ones fault); only a `db`
    // array can leave the running offset odd, so pad it back to even here.
    if (w == 1 && (values.size() % 2) != 0) trailer_.push_back("db 0");
}

std::string M68kCodegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    // objectMode: no entry sequence is emitted here; M68kLinkerFactory synthesizes
    // one after linking, once main's address is resolvable. An object file's base
    // is org 0; placing it is the linker's job.
    emitRaw(cat("org ", (objectMode ? 0 : kBaseAddress)));
    emitRaw("");
    if (!objectMode) {
        emitLabel("_start");
        emit("movel", cat(kInitialSP, ",a7"));
        emit("jsr", label(mainFn));
        // The result sits in D0 once execution reaches `halt`, a directly
        // inspectable register.
        emitLabel("halt");
        emit("bra", "halt");
        emitRaw("");
    }

    // --dump-asm wants one file per input, not one combined dump - see
    // Pdp7Codegen.cpp's own generate() for the pattern this mirrors
    // (tellp()-position ranges into code_, index ranges into trailer_,
    // sliced into perFileAsm_ at the end).
    std::vector<std::tuple<std::string, size_t, size_t>> codeRanges;
    std::vector<std::tuple<std::string, size_t, size_t>> trailerRanges;

    for (const auto &fb : prog.functionBodies) {
        size_t fCodeStart = static_cast<size_t>(code_.tellp());
        genFunction(fb);
        codeRanges.push_back({fb.func->sourceFile, fCodeStart, static_cast<size_t>(code_.tellp())});
    }

    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        // An external global is never defined here; it's only referenced by name,
        // letting the assembler defer it to a relocation.
        if (slot->isExternal) continue;
        if (debugMapEnabled_) debugSymbols_.push_back({label(slot.get()), /*isFunc=*/false});
        size_t gTrailerStart = trailer_.size();
        const std::string lbl = label(slot.get());
        if (slot->arraySize > 0) {
            declareArray(lbl, slot->arrayInit, slot->type);
        } else if (slot->type.base == BaseType::Struct) {
            // No struct initializer exists in this subset, so this is
            // always zero-filled; reuses declareArray's own element-list
            // emission rather than the 1/2/4/8-bucketed logic below,
            // which doesn't cover every struct size.
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
        trailerRanges.push_back({slot->sourceFile, gTrailerStart, trailer_.size()});
    }

    std::string codeStr = code_.str();
    perFileAsm_.clear();
    for (const auto &[file, s, e] : codeRanges) perFileAsm_[file] += codeStr.substr(s, e - s);
    for (const auto &[file, s, e] : trailerRanges) {
        for (size_t i = s; i < e; i++) perFileAsm_[file] += trailer_[i] + "\n";
    }

    std::ostringstream out;
    out << code_.str();
    for (const auto &line : trailer_) out << line << "\n";
    return out.str();
}

std::map<std::string, std::string> M68kCodegen::perFileAsm() const { return perFileAsm_; }

void M68kCodegen::genFunction(const ir::FunctionBody &fn) {
    lastDebugLine_ = 0;
    if (debugMapEnabled_) debugSymbols_.push_back({label(fn.func), /*isFunc=*/true});
    emitLabel(label(fn.func));
    // `linkl a6,-N` pushes the caller's a6, points this call's a6 at it, and
    // reserves N bytes of locals below it.
    emit("linkl", cat("a6,", (-fn.func->frameSize)));

    genStmt(*fn.body);

    emit("unlkl", "a6");
    emit("rts");
    emitRaw("");
}

void M68kCodegen::genStmt(const ir::Stmt &s) {
    emitSourceLine(s.line);
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

void M68kCodegen::genExpr(const ir::Expr &e) {
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
            // Stores at e.type's width/signedness: the pointee type, or a plain
            // int word for untyped-int-as-address.
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
            // operandOf(slot) is a stable frame/absolute operand, so no address
            // needs preserving across rhs's evaluation.
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
                // Loads at e.type's width/signedness (see DerefAssign above).
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
            // Arguments are pushed right-to-left so param 0 ends up closest to the
            // return address; the caller cleans up with `adda <N>,a7` afterward
            // (cdecl-style). Each argument reserves at least one native int; a wide
            // argument pushes its high word first so its low word lands at the
            // lower address, matching a wide Param's layout.
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
            // e.args is always empty here (IRBuilder rejects arguments on
            // CallPtr). "jsrind a0" is the (An)-indirect JSR encoding, a call
            // through whatever address a0 holds.
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
            // No vector table is set up, so there's nothing for ion/iof to
            // meaningfully toggle; treated as a no-op.
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

// A0 holds the computed address. For an array (slot->arraySize > 0), the base is a
// compile-time-known address, and both a Global and a Local array's elements sit
// at increasing addresses as the index increases. Otherwise `slot` is a plain
// pointer variable, so the base comes from reading its stored value.
void M68kCodegen::genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
    genExpr(idxExpr);  // d0 = index
    // d0 *= element width, via repeated doubling, or a real multiply for a
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

void M68kCodegen::genCompoundOp(const std::string &op, Type type) {
    bool isUnsigned = type.isUnsigned;
    if (op == "+") { emit("addl", "d1,d0"); return; }
    if (op == "-") { emit("subl", "d1,d0"); return; }
    // MULS.W/MULU.W Dn,Dn: 16-bit operands in, correct 32-bit product out.
    if (op == "*") { emit((isUnsigned ? "muluw" : "mulsw"), "d1,d0"); return; }
    if (op == "&") { emit("andl", "d1,d0"); return; }
    if (op == "|") { emit("orl", "d1,d0"); return; }
    // EOR.L Dn,<ea>: d1 is the source, d0 the destination.
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

// A0 must hold the resolved address on entry; it's stashed on the stack across
// e.rhs's evaluation since that can recursively clobber every register.
void M68kCodegen::genCompoundThroughA0(const ir::Expr &e) {
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
        storeIndirect(e.type);               // a0 still addresses the target (e.type is never wide)
        emit("movel", "d3,d0");  // d0 = old (final result)
    } else {
        storeIndirect(e.type);  // d0 already = new; a0 still addresses the target
    }
}

// Stores each initializer value directly into its frame slot (unlike a Global
// array's static declareArray()); MOVE accepts an immediate/label source straight
// into a frame-relative destination, one instruction per element.
void M68kCodegen::genLocalArrayInit(const ir::Slot *slot) {
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

void M68kCodegen::genIndex(const ir::Expr &e) {
    genIndexAddr(e.slot, *e.lhs);
    loadIndirect(e.slot->type);
}

void M68kCodegen::narrowD0To(Type to) {
    int w = widthOfType(to);
    if (w == 1) {
        emit("extw", "d0");
        emit("extl", "d0");
        if (to.isUnsigned) emit("andl", "255,d0");
    } else if (w == 2) {
        emit("extl", "d0");
        if (to.isUnsigned) emit("andl", "65535,d0");
    }
    // 4 bytes: nothing to do. 8 bytes never reaches here.
}

void M68kCodegen::loadOperand(const std::string &mem, const std::string &highMem, Type type) {
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

void M68kCodegen::storeOperand(const std::string &mem, const std::string &highMem, Type type) {
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

// A0 already holds the effective address. The wide case bumps A0 by 4 between
// the low and high word.
void M68kCodegen::loadIndirect(Type type) {
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

void M68kCodegen::storeIndirect(Type type) {
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

// Only real work is crossing the int/long long boundary, or re-truncating a
// narrower target; narrower operands are already promoted by IRBuilder.
void M68kCodegen::genConvert(const ir::Expr &e) {
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
            // 68000 has no cdq-equivalent, so sign-extend D0 into D1 by
            // arithmetic-shifting a copy right 31 bits, leaving D1 all-0s or all-1s.
            emit("movel", "d0,d1");
            emit("movel", "31,d2");
            emit("asrl", "d2,d1");
        }
    } else if (!fromWide && !toWide) {
        narrowD0To(to);
    }
    // fromWide && toWide: the bit pattern is already correct, nothing
    // to do.
}

// Evaluates both operands into D0:D1, stashing lhs on the stack while rhs
// evaluates, then ripples carry/borrow across the word pair with ADDX/SUBX.
void M68kCodegen::genWideAdd(const ir::Expr &lhs, const ir::Expr &rhs, bool isAdd) {
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

// 68000 has no 32x32 hardware multiply, so this is a full 64-bit shift-and-add
// multiply, one bit at a time. Two's-complement multiplication is sign-agnostic at
// the bit-pattern level, so no sign handling is needed (unlike genWideDivMod()).
// Scratch block at [a0]: +0/4 multiplicand, +8/12 multiplier, +16/20 result.
// Leaves the result in D0(lo):D1(hi).
void M68kCodegen::genWideMul(const ir::Expr &lhs, const ir::Expr &rhs) {
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

    // if multiplier's bit 0 is set, result += multiplicand (ADD/ADDX ripple-carry).
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

    // multiplicand <<= 1: ADD.L Dn,Dn doubles Dn and sets the carry from bit 31;
    // ADDX.L folds that carry into the high word, a 64-bit shift in two instructions.
    emit("movel", "[a0+0],d0");
    emit("addl", "d0,d0");
    emit("movel", "d0,[a0+0]");
    emit("movel", "[a0+4],d0");
    emit("addxl", "d0,d0");
    emit("movel", "d0,[a0+4]");

    // multiplier >>= 1: the high word's outgoing bit 0 is carried into the low
    // word's new bit 31 by hand.
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

void M68kCodegen::genNegate64() {
    emit("notl", "d0");
    emit("notl", "d1");
    emit("movel", "0,d2");
    emit("addl", "1,d0");
    emit("addxl", "d2,d1");
}

// Bit-serial shift/compare/subtract long division via a0 over a 9-word scratch
// block, with a signed-magnitude wrapper.
void M68kCodegen::genWideDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
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

    // d6/d7 are fixed shift counts for the whole routine, set once since this
    // dialect only has register-count shifts.
    emit("movel", "1,d6");
    emit("movel", "31,d7");

    std::string LskipNegN = newLabel(), LskipNegD = newLabel();
    if (!isUnsigned) {
        // lhsSign/rhsSign reuse [a0+36]/[a0+40] (the rhs staging words, already
        // copied out and not needed again).
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

    // if (r >= d) { r -= d; quotient bit = 1 }, always an unsigned magnitude
    // compare (n/d already negated to non-negative above).
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

// No native instruction shifts more than 32 bits at once, so this is a counted
// loop of 1-bit shifts, carrying the crossing bit by hand.
void M68kCodegen::genWideShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned) {
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

// Compares high words first (signed/unsigned per isUnsigned, since that's where
// the sign bit lives); only if equal does the low word matter, always as an
// unsigned compare.
void M68kCodegen::genWideCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned) {
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

void M68kCodegen::genAddrOf(const ir::Expr &operand) {
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

void M68kCodegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    // Operands already share one common type via IRBuilder's usual-arithmetic-
    // conversion pass, except shift (only LHS's type matters) and &&/||
    // (width-independent).
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
        // EOR's source/dest convention still gives the right result since this op
        // is commutative.
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

// MULS.W/MULU.W: 16-bit-operand hardware multiply.
void M68kCodegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
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
void M68kCodegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
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
void M68kCodegen::emitDivMod32(bool isUnsigned) {
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

void M68kCodegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs) {
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

// CMP.L <ea>,Dn sets condition codes for Dn-ea; branchIfTrue (BHI/BLS/BCC/BCS
// unsigned, BGT/BLE/BGE/BLT signed) is applied directly, no fixup needed.
void M68kCodegen::emitCompare(const ir::Expr &lhs, const ir::Expr &rhs, const std::string &branchIfTrue) {
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

// A wide operand's truthiness needs both words: `orl d1,d0` folds them before
// cmpl, discarding the combined value safely.
void M68kCodegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
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

void M68kCodegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
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

void M68kCodegen::genLogicalNot(const ir::Expr &rhs) {
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

std::optional<std::string> M68kCodegen::assembleInProcess(const std::string &asmText,
                                                            const std::string &format) const {
    if (format == "raw") return assembleM68k(asmText);
    // Other formats: build one format-neutral ObjectFile, then serialize it with
    // that format's FileFormatWriter.
    if (auto kind = fileFormatKindFromName(format)) {
        ObjectFile obj = assembleM68kObject(asmText);
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

std::optional<std::string> M68kCodegen::debugMap(const std::string &asmText, const std::string &format,
        const std::string &sourcePath) const {
    (void)format;
    return assembleM68kDebugMap(asmText, sourcePath, debugSymbols_);
}

std::optional<std::string> M68kCodegen::debugMapPreLink(const std::string &asmText,
                                                    const std::string &sourcePath) const {
    return assembleM68kDebugMap(asmText, sourcePath, debugSymbols_, /*allowExternal=*/true);
}

// Only emits a marker when `debugMapEnabled_` (set via setDebugMapEnabled(),
// before generate() runs) and `line` actually differs from the last one
// emitted - see Pdp7Codegen.cpp's identical emitSourceLine(), which this
// mirrors exactly.
void M68kCodegen::emitSourceLine(int line) {
    if (!debugMapEnabled_ || line == 0 || line == lastDebugLine_) return;
    emitRaw(cat("__line ", line));
    lastDebugLine_ = line;
}

}  // namespace rcc
