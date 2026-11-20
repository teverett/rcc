#include "I386Codegen.h"

#include <cstdint>
#include <cstring>
#include <stdexcept>

#include "I386Assembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

// IEEE-754 bit patterns for a float literal's `double`-typed host value (Expr::floatValue is
// always a host `double`, regardless of the C-level type an `f` suffix picked - see
// BuildAst.cpp's parseFloatLiteral()) - memcpy, not a reinterpret_cast, to stay defined
// behavior while still just copying bytes (no actual conversion logic of its own).
uint32_t floatBitsOf(double v) {
    float f = static_cast<float>(v);
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    return bits;
}
uint64_t doubleBitsOf(double v) {
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    return bits;
}

// Conventional Multiboot load address, above the BIOS/real-mode data area.
constexpr uint32_t kLoadAddr = 0x100000;

// Multiboot header (Multiboot Specification "OS image format") telling the bootloader
// (GRUB, or qemu-system-i386 -kernel) where to load and start this image, without this
// backend writing any real-mode/protected-mode transition code itself. `flags` sets only
// the "load raw bytes at a fixed address" bit (0x10000); header_addr/load_addr are both
// kLoadAddr; load_end_addr/bss_end_addr of 0 mean "load the whole file, no separate bss";
// entry_addr is `_start`, right after this 32-byte header.
constexpr uint32_t kMbMagic = 0x1BADB002;
constexpr uint32_t kMbFlags = 0x10000;
constexpr uint32_t kMbChecksum = static_cast<uint32_t>(-(static_cast<int64_t>(kMbMagic) + kMbFlags));
}  // namespace

// A literal element's decimal text, or a symbol element's bare label (the assembler's "dd"
// evaluates a symbol through the same relocation path as any other label-as-immediate).
std::string I386Codegen::arrayElemText(const ir::ArrayInitElem &e) {
    if (!e.isSymbol) return std::to_string(e.value);
    return e.slot ? label(e.slot) : label(e.func);
}

void I386Codegen::declareArray(const std::string &lbl, const std::vector<ir::ArrayInitElem> &values, Type elemType) {
    int w = widthOfType(elemType);
    bool isFloatElem = isFloatType(elemType);
    // No 64-bit data directive: a long long (or double) literal element is emitted as two
    // "dd"s (low, then high dword); a symbol element is always a native-int-width address
    // and gets one.
    const char *directive = w == 1 ? "db" : w == 2 ? "dw" : "dd";
    std::ostringstream line;
    line << lbl << ": " << directive << " ";
    bool first = true;
    for (const auto &e : values) {
        if (!first) line << ", ";
        first = false;
        if (e.isSymbol) {
            line << arrayElemText(e);
            continue;
        }
        if (isFloatElem) {
            // The literal's own kind doesn't have to match the array's declared element
            // type any more than a scalar initializer's does - IRBuilder.cpp's own
            // resolveArrayInit() already coerced e.value<->e.floatValue to match, same as
            // the scalar case I386Codegen::generate() handles right below.
            double v = e.isFloat ? e.floatValue : static_cast<double>(e.value);
            if (w == 4) {
                line << floatBitsOf(v);
            } else {
                uint64_t bits = doubleBitsOf(v);
                line << static_cast<uint32_t>(bits) << ", " << static_cast<uint32_t>(bits >> 32);
            }
            continue;
        }
        if (w == 8) {
            auto v = static_cast<uint64_t>(e.isFloat ? static_cast<int64_t>(e.floatValue) : e.value);
            line << static_cast<uint32_t>(v) << ", " << static_cast<uint32_t>(v >> 32);
            continue;
        }
        line << (e.isFloat ? static_cast<long>(e.floatValue) : e.value);
    }
    trailer_.push_back(line.str());
}

std::string I386Codegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    // Real separate compilation means `prog` may not define `main` at all; `objectMode`
    // suppresses the Multiboot header/entry sequence here since I386LinkerFactory
    // synthesizes exactly one once every object is merged. An object file's base is `org 0`;
    // placing it is the linker's job.
    emitRaw(cat("org ", (objectMode ? 0 : kLoadAddr)));
    emitRaw("");
    if (!objectMode) {
        emitRaw(cat("dd ", kMbMagic, ",", kMbFlags, ",", kMbChecksum));
        emitRaw(cat("dd ", kLoadAddr, ",", kLoadAddr, ",0,0"));
        emitRaw("dd _start");
        emitRaw("");
        emitLabel("_start");
        // Masks both 8259 PICs: this backend builds no IDT, so an unmasked timer interrupt
        // (possible once `_ion()` runs) would triple-fault.
        emit("mov", "al,0xFF");
        emit("out", "0x21,al");
        emit("mov", "al,0xFF");
        emit("out", "0xA1,al");
        // GRUB/qemu's own startup state for the x87 FPU isn't this backend's to assume -
        // FNINIT puts it in the documented post-reset state (empty register stack, default
        // control word) once, before any float/double code anywhere in the program runs.
        emit("fninit");
        emit("call", label(mainFn));
        emit("hlt");  // result sits in eax
        emitLabel("halt");
        emit("jmp", "halt");
        emitRaw("");
    }

    for (const auto &fb : prog.functionBodies) genFunction(fb);

    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        // An external global isn't defined in this file; skip it here so it's only ever
        // referenced by name, letting the assembler defer it to a relocation.
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
        } else if (isFloatType(slot->type)) {
            double v = slot->hasInit ? slot->floatInitValue : 0.0;
            if (widthOfType(slot->type) == 4) {
                trailer_.push_back(lbl + ": dd " + std::to_string(floatBitsOf(v)) + "  ; " + slot->sourceName);
            } else {
                uint64_t bits = doubleBitsOf(v);
                trailer_.push_back(lbl + ": dd " + std::to_string(static_cast<uint32_t>(bits)) + ", " +
                                    std::to_string(static_cast<uint32_t>(bits >> 32)) + "  ; " + slot->sourceName);
            }
        } else {
            long v = slot->hasInit ? slot->initValue : 0;
            int w = widthOfType(slot->type);
            if (w == 8) {
                auto uv = static_cast<uint64_t>(v);
                trailer_.push_back(lbl + ": dd " + std::to_string(static_cast<uint32_t>(uv)) + ", " +
                                    std::to_string(static_cast<uint32_t>(uv >> 32)) + "  ; " + slot->sourceName);
            } else {
                const char *directive = w == 1 ? "db" : w == 2 ? "dw" : "dd";
                trailer_.push_back(lbl + ": " + directive + " " + std::to_string(v) + "  ; " + slot->sourceName);
            }
        }
    }

    std::ostringstream out;
    out << code_.str();
    for (const auto &line : trailer_) out << line << "\n";
    return out.str();
}

std::map<std::string, std::string> I386Codegen::perFileAsm() const { return perFileAsm_; }

void I386Codegen::genFunction(const ir::FunctionBody &fn) {
    emitLabel(label(fn.func));
    emit("push", "ebp");
    emit("mov", "ebp,esp");
    if (fn.func->frameSize > 0) emit("sub", cat("esp,", fn.func->frameSize));
    if (hostedAbi_) {
        emit("push", "ebx");
        emit("push", "esi");
        emit("push", "edi");
    }

    genStmt(*fn.body);

    if (hostedAbi_) {
        emit("pop", "edi");
        emit("pop", "esi");
        emit("pop", "ebx");
    }
    emit("mov", "esp,ebp");
    emit("pop", "ebp");
    emit("ret");
    emitRaw("");
}

void I386Codegen::genStmt(const ir::Stmt &s) {
    switch (s.kind) {
        case ir::StmtKind::Block: {
            for (const auto &child : s.body) genStmt(*child);
            return;
        }
        case ir::StmtKind::If: {
            genTruthToEax(*s.cond);
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            emit("cmp", "eax,0");
            emit("je", (s.elseStmt ? Lelse : Ldone));
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
            std::string Lcont = s.postStmt ? newLabel() : Lstart;
            emitLabel(Lstart);
            genTruthToEax(*s.cond);
            emit("cmp", "eax,0");
            emit("je", Lend);
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
            if (s.initOrReturnOrExpr) genExpr(*s.initOrReturnOrExpr);
            if (hostedAbi_) {
                emit("pop", "edi");
                emit("pop", "esi");
                emit("pop", "ebx");
            }
            emit("mov", "esp,ebp");
            emit("pop", "ebp");
            emit("ret");
            return;
        }
        case ir::StmtKind::LocalDecl: {
            if (s.declSlot->arraySize > 0) {
                genLocalArrayInit(s.declSlot);
            } else if (s.initOrReturnOrExpr) {
                if (isFloatType(s.declSlot->type)) {
                    genFloatExpr(*s.initOrReturnOrExpr);
                    storeFloatOperand(addr(s.declSlot), s.declSlot->type, /*pop=*/true);
                } else {
                    genExpr(*s.initOrReturnOrExpr);
                    emit("mov", cat(addr(s.declSlot), ",eax"));
                }
            }
            return;
        }
        case ir::StmtKind::ExprStmt: {
            genExpr(*s.initOrReturnOrExpr);
            // A discarded float/double result is left sitting on the FPU register stack
            // (unlike eax, ST0 is a true stack - nothing else ever reclaims this slot), so
            // it must be popped explicitly here or repeated statements would eventually
            // overflow it.
            if (isFloatType(s.initOrReturnOrExpr->type)) emit("fdiscard");
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
            emit("jmp", irLabel(s.labelId));
            return;
        }
    }
}

void I386Codegen::genExpr(const ir::Expr &e) {
    if (isFloatType(e.type)) {
        genFloatExpr(e);
        return;
    }
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "internal: FloatLit reached genExpr with a non-float type");
        case ir::ExprKind::IntLit: {
            if (isWide(e.type)) {
                auto v = static_cast<uint64_t>(e.intValue);
                emit("mov", cat("eax,", static_cast<uint32_t>(v)));
                emit("mov", cat("edx,", static_cast<uint32_t>(v >> 32)));
                return;
            }
            emit("mov", cat("eax,", e.intValue));
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
                std::string addr = computeElementAddr(e.slot, *e.lhs);
                emit("push", "ebx");
                genExpr(*e.rhs);
                emit("pop", "ebx");
                storeOperand("[ebx]", "[ebx+4]", e.slot->type);
                return;
            }
            genExpr(*e.rhs);
            storeOperand(operandOf(e.slot), operandOf(e.slot, 4), e.slot->type);
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            if (e.lhs) {  // slot[idx] op= rhs (including slot[idx]++/--)
                computeElementAddr(e.slot, *e.lhs);  // ebx = address
                genCompoundThroughEbx(e);
                return;
            }
            // operandOf(slot) is a stable memory operand, so no address needs to be held
            // across rhs's evaluation.
            std::string mem = operandOf(e.slot);
            genExpr(*e.rhs);  // eax = rhs
            emit("mov", "ebx,eax");
            loadOperand(mem, "", e.type);  // eax = old
            if (e.yieldOld) emit("push", "eax");
            genCompoundOp(e.op, e.type);
            narrowEaxTo(e.type);
            storeOperand(mem, "", e.type);
            if (e.yieldOld) emit("pop", "eax");
            return;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            genExpr(*e.lhs);  // eax = pointer value
            emit("mov", "ebx,eax");  // ebx = address
            genCompoundThroughEbx(e);
            return;
        }
        case ir::ExprKind::Ternary: {
            // The condition can be wide, or float/double, even though this (int-typed)
            // branch's own result type never is.
            genTruthToEax(*e.lhs);
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            emit("cmp", "eax,0");
            emit("je", Lelse);
            genExpr(*e.rhs);
            emit("jmp", Ldone);
            emitLabel(Lelse);
            genExpr(*e.elseExpr);
            emitLabel(Ldone);
            return;
        }
        case ir::ExprKind::DerefAssign: {
            genExpr(*e.lhs);  // eax = pointer value
            emit("mov", "ebx,eax");
            emit("push", "ebx");
            genExpr(*e.rhs);  // eax, or edx:eax if wide = value to store
            emit("pop", "ebx");
            storeOperand("[ebx]", "[ebx+4]", e.type);
            return;
        }
        case ir::ExprKind::Unary: {
            if (e.op == "-") {
                genExpr(*e.rhs);
                if (isWide(e.rhs->type)) {
                    genNegate64();
                } else {
                    emit("neg", "eax");
                }
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                genExpr(*e.rhs);  // eax = pointer value
                emit("mov", "ebx,eax");
                loadOperand("[ebx]", "[ebx+4]", e.type);
            } else if (e.op == "~") {
                genExpr(*e.rhs);
                emit("not", "eax");
                if (isWide(e.rhs->type)) emit("not", "edx");
            } else {  // "&"
                genAddrOf(*e.rhs);
            }
            return;
        }
        case ir::ExprKind::Call: {
            genCall(e);
            return;
        }
        case ir::ExprKind::CallPtr: {
            genCallPtr(e);
            return;
        }
        case ir::ExprKind::Binary: {
            genBinary(e);
            return;
        }
        case ir::ExprKind::Intrinsic: {
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

void I386Codegen::genTruthToEax(const ir::Expr &e) {
    if (isFloatType(e.type)) {
        genFloatExpr(e);
        genFloatTruthToEax();
        return;
    }
    genExpr(e);
    if (isWide(e.type)) emit("or", "eax,edx");
}

void I386Codegen::genCall(const ir::Expr &e) {
    // cdecl: push right-to-left so param 0 ends up closest to the return address; caller
    // cleans up with `add esp,N`. A wide or double argument pushes its high dword first so
    // its low dword ends up at the lower address; a float argument is one dword.
    int totalBytes = 0;
    for (const auto &arg : e.args) {
        totalBytes += isFloatType(arg->type) ? widthOfType(arg->type) : (isWide(arg->type) ? 8 : 4);
    }
    if (hostedAbi_) {
        emit("push", "esi");
        emit("mov", "esi,esp");
        emit("and", "esp,-16");
        int padding = (16 - totalBytes % 16) % 16;
        if (padding > 0) emit("sub", cat("esp,", padding));
    }
    for (size_t i = e.args.size(); i-- > 0;) {
        const ir::Expr &arg = *e.args[i];
        if (isFloatType(arg.type)) {
            genFloatExpr(arg);
            std::string tmp = allocFloatTemp(arg.type);
            storeFloatOperand("[" + tmp + "]", arg.type, /*pop=*/true);
            if (widthOfType(arg.type) == 8) {
                emit("mov", cat("eax,[", tmp, "+4]"));
                emit("push", "eax");
                emit("mov", cat("eax,[", tmp, "]"));
                emit("push", "eax");
            } else {
                emit("mov", cat("eax,[", tmp, "]"));
                emit("push", "eax");
            }
        } else {
            genExpr(arg);
            if (isWide(arg.type)) {
                emit("push", "edx");
                emit("push", "eax");
            } else {
                emit("push", "eax");
            }
        }
    }
    emit("call", label(e.func));
    if (hostedAbi_) {
        emit("mov", "esp,esi");
        emit("pop", "esi");
    } else if (totalBytes > 0) {
        emit("add", cat("esp,", totalBytes));
    }
}

void I386Codegen::genCallPtr(const ir::Expr &e) {
    // e.args is always empty here: IRBuilder rejects any argument on this kind before it
    // reaches a backend, since the callee is never known at compile time.
    genExpr(*e.lhs);  // eax = the callee's entry address
    if (hostedAbi_) {
        emit("push", "esi");
        emit("mov", "esi,esp");
        emit("and", "esp,-16");
    }
    emit("call", "eax");
    if (hostedAbi_) {
        emit("mov", "esp,esi");
        emit("pop", "esi");
    }
}

std::string I386Codegen::computeElementAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
    genExpr(idxExpr);
    int shift = elemShift(slot->type);
    if (shift > 0) {
        emit("shl", cat("eax,", shift));
    } else if (shift < 0) {
        // Non-power-of-two element width (a struct's): scale by a real multiply instead -
        // this assembler's own `imul` is one-operand only (edx:eax = eax*reg), so the width
        // has to go through a register, not an immediate.
        emit("mov", cat("ebx,", widthOfType(slot->type)));
        emit("imul", "ebx");
    }
    emit("mov", "ebx,eax");
    if (slot->arraySize > 0) {
        if (slot->kind == ir::SlotKind::Global) {
            emit("add", cat("ebx,", label(slot)));
        } else {
            emit("lea", cat("eax,", localArrayAddr(slot)));
            emit("add", "ebx,eax");
        }
    } else {
        emit("add", cat("ebx,", operandOf(slot)));
    }
    return "[ebx]";
}

void I386Codegen::genCompoundOp(const std::string &op, Type type) {
    bool isUnsigned = type.isUnsigned;
    if (op == "+") { emit("add", "eax,ebx"); return; }
    if (op == "-") { emit("sub", "eax,ebx"); return; }
    // One-operand multiply: edx:eax = eax*ebx, keep eax.
    if (op == "*") { emit((isUnsigned ? "mul" : "imul"), "ebx"); return; }
    if (op == "&") { emit("and", "eax,ebx"); return; }
    if (op == "|") { emit("or", "eax,ebx"); return; }
    if (op == "^") { emit("xor", "eax,ebx"); return; }
    if (op == "<<" || op == ">>") {
        emit("mov", "ecx,ebx");
        emit((op == "<<" ? "shl" : (isUnsigned ? "shr" : "sar")), "eax,cl");
        return;
    }
    // "/" or "%": div/idiv only forbid eax/edx (the dividend), so ebx is a valid divisor.
    if (isUnsigned) {
        emit("xor", "edx,edx");
    } else {
        emit("cdq");
    }
    emit((isUnsigned ? "div" : "idiv"), "ebx");
    if (op == "%") emit("mov", "eax,edx");
}

// ebx must already hold the resolved address on entry. Only [ebx] is a valid memory base
// in this dialect, so the address is shuttled back into ebx right before each memory access
// rather than held in another register across rhs's evaluation.
void I386Codegen::genCompoundThroughEbx(const ir::Expr &e) {
    emit("push", "ebx");  // stash address
    genExpr(*e.rhs);             // eax = rhs
    emit("push", "eax");  // stash rhs
    emit("pop", "ecx");  // ecx = rhs
    emit("pop", "ebx");  // ebx = address
    loadOperand("[ebx]", "", e.type);  // eax = old
    emit("push", "ebx");  // re-stash address for the store below
    if (e.yieldOld) emit("push", "eax");  // stash old too
    emit("mov", "ebx,ecx");  // ebx = rhs
    genCompoundOp(e.op, e.type);                   // eax = new (raw)
    narrowEaxTo(e.type);
    if (e.yieldOld) {
        emit("mov", "ecx,eax");  // ecx = new
        emit("pop", "eax");  // eax = old
        emit("pop", "ebx");  // ebx = address
        emit("mov", "edx,eax");  // edx = old (preserved across the store below)
        emit("mov", "eax,ecx");  // eax = new (storeOperand always stores from eax)
        storeOperand("[ebx]", "", e.type);
        emit("mov", "eax,edx");  // eax = old (final result)
    } else {
        emit("pop", "ebx");  // ebx = address
        storeOperand("[ebx]", "", e.type);  // eax already = new
    }
}

void I386Codegen::narrowEaxTo(Type to) {
    int w = widthOfType(to);
    if (w == 1) {
        if (to.isUnsigned) {
            emit("movzx", "eax,al");
        } else {
            emit("movsx", "eax,al");
        }
    } else if (w == 2) {
        if (to.isUnsigned) {
            emit("movzx", "eax,ax");
        } else {
            emit("movsx", "eax,ax");
        }
    }
    // 4 bytes: eax already holds the full value, nothing to do. 8 bytes never reaches here.
}

void I386Codegen::loadOperand(const std::string &mem, const std::string &highMem, Type type) {
    int w = widthOfType(type);
    if (w == 1) {
        emit(cat((type.isUnsigned ? "movzx eax,byte " : "movsx eax,byte "), mem));
    } else if (w == 2) {
        emit(cat((type.isUnsigned ? "movzx eax,word " : "movsx eax,word "), mem));
    } else if (w == 4) {
        emit("mov", cat("eax,", mem));
    } else {  // 8 - long long: edx:eax, low dword first
        emit("mov", cat("eax,", mem));
        emit("mov", cat("edx,", highMem));
    }
}

void I386Codegen::storeOperand(const std::string &mem, const std::string &highMem, Type type) {
    int w = widthOfType(type);
    if (w == 1) {
        emit("mov", cat("byte ", mem, ",al"));
    } else if (w == 2) {
        emit("mov", cat("word ", mem, ",ax"));
    } else if (w == 4) {
        emit("mov", cat(mem, ",eax"));
    } else {  // 8
        emit("mov", cat(mem, ",eax"));
        emit("mov", cat(highMem, ",edx"));
    }
}

void I386Codegen::genConvert(const ir::Expr &e) {
    Type from = e.lhs->type;
    Type to = e.type;
    if (isFloatType(from) || isFloatType(to)) {
        genFloatConvert(e);
        return;
    }
    bool fromWide = isWide(from);
    bool toWide = isWide(to);
    genExpr(*e.lhs);  // eax, or edx:eax if fromWide
    if (fromWide && !toWide) {
        narrowEaxTo(to);  // edx is simply discarded
    } else if (!fromWide && toWide) {
        if (from.isUnsigned) {
            emit("xor", "edx,edx");
        } else {
            emit("cdq");
        }
    } else if (!fromWide && !toWide) {
        narrowEaxTo(to);
    }
    // fromWide && toWide: the bit pattern is already correct, nothing to do.
}

// long long add/sub: evaluate both into edx:eax, stashing lhs's dwords on the stack while
// rhs evaluates, then ripple the carry/borrow across the dword pair with adc/sbb.
void I386Codegen::genWideAdd(const ir::Expr &lhs, const ir::Expr &rhs, bool isAdd) {
    genExpr(lhs);                       // edx:eax = lhs
    emit("push", "edx");
    emit("push", "eax");  // stack (top->bottom): lhs_lo, lhs_hi
    genExpr(rhs);                       // edx:eax = rhs
    emit("mov", "ebx,eax");
    emit("mov", "ecx,edx");  // ebx=rhs_lo, ecx=rhs_hi
    emit("pop", "eax");
    emit("pop", "edx");  // eax=lhs_lo, edx=lhs_hi
    if (isAdd) {
        emit("add", "eax,ebx");
        emit("adc", "edx,ecx");
    } else {
        emit("sub", "eax,ebx");
        emit("sbb", "edx,ecx");
    }
}

// long long multiply, keeping the low 64 bits of the true 128-bit product via the standard
// 32-on-32 cross-multiply decomposition: result_lo = lo(lhs)*lo(rhs) mod 2^32; result_hi =
// hi32(lo(lhs)*lo(rhs)) + lo(lhs)*hi(rhs) + hi(lhs)*lo(rhs), all mod 2^32. Each cross term's
// own mul leaves overflow in edx that's discarded (it only affects bit 64 and beyond).
void I386Codegen::genWideMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);
    emit("push", "edx");
    emit("push", "eax");  // lhs_lo, lhs_hi
    genExpr(rhs);
    emit("mov", "edi,edx");
    emit("mov", "esi,eax");  // edi=rhs_hi, esi=rhs_lo
    emit("pop", "ebx");
    emit("pop", "ecx");  // ebx=lhs_lo (top of stack), ecx=lhs_hi
    emit("mov", "eax,ebx");
    emit("mul", "edi");  // edx:eax = lhs_lo*rhs_hi
    emit("push", "eax");  // crossA_lo
    emit("mov", "eax,ecx");
    emit("mul", "esi");  // edx:eax = lhs_hi*rhs_lo
    emit("push", "eax");  // crossB_lo
    emit("mov", "eax,ebx");
    emit("mul", "esi");  // edx:eax = lhs_lo*rhs_lo (final result_lo/carry)
    emit("pop", "ebx");
    emit("pop", "ecx");  // ebx=crossB_lo, ecx=crossA_lo
    emit("add", "edx,ebx");
    emit("add", "edx,ecx");
}

void I386Codegen::genNegate64() {
    emit("not", "eax");
    emit("not", "edx");
    emit("xor", "ecx,ecx");
    emit("add", "eax,1");
    emit("adc", "edx,ecx");
}

// Bit-serial shift/compare/subtract long division: no native instruction here is wider
// than 32 bits, so each of the 64 bits is shifted/compared/subtracted one at a time via a
// pair of 32-bit operations. `ebx` addresses a 9-dword scratch block for the routine's
// duration: [ebx+0/4] dividend (consumed, shifted left MSB-first), [ebx+8/12] divisor,
// [ebx+16/20] remainder being built, [ebx+24/28] quotient being built, [ebx+32] iteration
// countdown.
//
// `isUnsigned` false divides magnitudes: lhs/rhs sign bits are stashed in [ebx+36]/[ebx+40]
// (reusing the rhs staging dwords, no longer needed once copied into d), each operand
// negated if its sign bit was set, then restored on the result per C's truncating-division
// rule: the quotient is negative iff the operands' signs differed, the remainder always
// takes the dividend's sign.
void I386Codegen::genWideDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    emit("push", "edx");
    emit("push", "eax");  // lhs_hi, lhs_lo (lhs_lo on top)
    genExpr(rhs);
    emit("push", "edx");
    emit("push", "eax");  // rhs_hi, rhs_lo (rhs_lo on top)
    emit("sub", "esp,36");
    emit("mov", "ebx,esp");
    // Fetch the operands stashed just above the scratch block.
    emit("mov", "eax,[ebx+44]");
    emit("mov", "[ebx+0],eax");  // n_lo = lhs_lo
    emit("mov", "eax,[ebx+48]");
    emit("mov", "[ebx+4],eax");  // n_hi = lhs_hi
    emit("mov", "eax,[ebx+36]");
    emit("mov", "[ebx+8],eax");  // d_lo = rhs_lo
    emit("mov", "eax,[ebx+40]");
    emit("mov", "[ebx+12],eax");  // d_hi = rhs_hi
    emit("xor", "eax,eax");
    emit("mov", "[ebx+16],eax");
    emit("mov", "[ebx+20],eax");  // r = 0
    emit("mov", "[ebx+24],eax");
    emit("mov", "[ebx+28],eax");  // q = 0
    emit("mov", "eax,64");
    emit("mov", "[ebx+32],eax");  // 64 iterations

    std::string LskipNegN = newLabel(), LskipNegD = newLabel();
    if (!isUnsigned) {
        emit("mov", "eax,[ebx+4]");
        emit("shr", "eax,31");
        emit("mov", "[ebx+36],eax");  // lhsSign
        emit("mov", "eax,[ebx+12]");
        emit("shr", "eax,31");
        emit("mov", "[ebx+40],eax");  // rhsSign
        emit("mov", "eax,[ebx+36]");
        emit("cmp", "eax,0");
        emit("je", LskipNegN);
        emit("mov", "eax,[ebx+0]");
        emit("mov", "edx,[ebx+4]");
        genNegate64();
        emit("mov", "[ebx+0],eax");
        emit("mov", "[ebx+4],edx");
        emitLabel(LskipNegN);
        emit("mov", "eax,[ebx+40]");
        emit("cmp", "eax,0");
        emit("je", LskipNegD);
        emit("mov", "eax,[ebx+8]");
        emit("mov", "edx,[ebx+12]");
        genNegate64();
        emit("mov", "[ebx+8],eax");
        emit("mov", "[ebx+12],edx");
        emitLabel(LskipNegD);
    }

    std::string Lloop = newLabel(), Ldone = newLabel();
    std::string LhiDiff = newLabel(), LsubYes = newLabel(), LsubNo = newLabel(), LshiftQ = newLabel();
    emitLabel(Lloop);
    emit("mov", "eax,[ebx+32]");
    emit("cmp", "eax,0");
    emit("je", Ldone);
    emit("sub", "eax,1");
    emit("mov", "[ebx+32],eax");

    // n <<= 1 (0-filled), capturing its own old top bit into edx.
    emit("mov", "eax,[ebx+4]");
    emit("shr", "eax,31");
    emit("mov", "edx,eax");  // edx = n's old top bit
    emit("mov", "eax,[ebx+0]");
    emit("shr", "eax,31");
    emit("mov", "esi,eax");  // esi = n_lo's old top bit
    emit("mov", "eax,[ebx+0]");
    emit("shl", "eax,1");
    emit("mov", "[ebx+0],eax");
    emit("mov", "eax,[ebx+4]");
    emit("shl", "eax,1");
    emit("or", "eax,esi");
    emit("mov", "[ebx+4],eax");

    // r = (r << 1) | (n's old top bit, already in edx).
    emit("mov", "eax,[ebx+16]");
    emit("shr", "eax,31");
    emit("mov", "edi,eax");  // edi = r_lo's old top bit
    emit("mov", "eax,[ebx+16]");
    emit("shl", "eax,1");
    emit("or", "eax,edx");
    emit("mov", "[ebx+16],eax");
    emit("mov", "eax,[ebx+20]");
    emit("shl", "eax,1");
    emit("or", "eax,edi");
    emit("mov", "[ebx+20],eax");

    // if (r >= d) { r -= d; quotient bit = 1 } else { quotient bit = 0 } - always an
    // unsigned magnitude compare, since n and d were negated to non-negative above.
    emit("mov", "eax,[ebx+20]");
    emit("mov", "edx,[ebx+12]");
    emit("cmp", "eax,edx");
    emit("jne", LhiDiff);
    emit("mov", "eax,[ebx+16]");
    emit("mov", "edx,[ebx+8]");
    emit("cmp", "eax,edx");
    emit("jae", LsubYes);
    emit("jmp", LsubNo);
    emitLabel(LhiDiff);
    emit("jae", LsubYes);
    emit("jmp", LsubNo);
    emitLabel(LsubYes);
    emit("mov", "eax,[ebx+16]");
    emit("mov", "edx,[ebx+8]");
    emit("sub", "eax,edx");
    emit("mov", "[ebx+16],eax");
    emit("mov", "eax,[ebx+20]");
    emit("mov", "edx,[ebx+12]");
    emit("sbb", "eax,edx");
    emit("mov", "[ebx+20],eax");
    emit("mov", "ecx,1");
    emit("jmp", LshiftQ);
    emitLabel(LsubNo);
    emit("mov", "ecx,0");
    emitLabel(LshiftQ);

    // q = (q << 1) | quotient bit (in ecx).
    emit("mov", "eax,[ebx+24]");
    emit("shr", "eax,31");
    emit("mov", "esi,eax");  // esi = q_lo's old top bit
    emit("mov", "eax,[ebx+24]");
    emit("shl", "eax,1");
    emit("or", "eax,ecx");
    emit("mov", "[ebx+24],eax");
    emit("mov", "eax,[ebx+28]");
    emit("shl", "eax,1");
    emit("or", "eax,esi");
    emit("mov", "[ebx+28],eax");
    emit("jmp", Lloop);

    emitLabel(Ldone);
    if (!isUnsigned) {
        std::string LskipNegQ = newLabel(), LskipNegR = newLabel();
        emit("mov", "ecx,[ebx+36]");
        emit("mov", "eax,[ebx+40]");
        emit("xor", "ecx,eax");
        emit("cmp", "ecx,0");
        emit("je", LskipNegQ);
        emit("mov", "eax,[ebx+24]");
        emit("mov", "edx,[ebx+28]");
        genNegate64();
        emit("mov", "[ebx+24],eax");
        emit("mov", "[ebx+28],edx");
        emitLabel(LskipNegQ);
        emit("mov", "ecx,[ebx+36]");
        emit("cmp", "ecx,0");
        emit("je", LskipNegR);
        emit("mov", "eax,[ebx+16]");
        emit("mov", "edx,[ebx+20]");
        genNegate64();
        emit("mov", "[ebx+16],eax");
        emit("mov", "[ebx+20],edx");
        emitLabel(LskipNegR);
    }
    emit("mov", "eax,[ebx+24]");
    emit("mov", "edx,[ebx+28]");  // eax:edx = quotient
    emit("mov", "esi,[ebx+16]");
    emit("mov", "edi,[ebx+20]");  // esi:edi = remainder
    emit("add", "esp,52");  // release the 9-dword scratch block and the 4 earlier stashed operand dwords
    if (wantRemainder) {
        emit("mov", "eax,esi");
        emit("mov", "edx,edi");
    }
}

// A long long shifted by a runtime count: a counted loop of 1-bit shifts, carrying the bit
// that crosses the dword boundary by hand (no rcl/rcr in this dialect). `isUnsigned` only
// matters for `>>`: the high dword's shift is sar (sign-preserving) or shr (zero-filling).
void I386Codegen::genWideShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned) {
    genExpr(lhs);
    emit("push", "edx");
    emit("push", "eax");
    genExpr(rhs);                   // eax = shift count (already promoted to a plain int)
    emit("mov", "ebx,eax");
    emit("pop", "eax");
    emit("pop", "edx");  // eax:edx restored = the value being shifted
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("cmp", "ebx,0");
    emit("je", Ldone);
    emit("sub", "ebx,1");
    if (isLeft) {
        emit("mov", "ecx,eax");
        emit("shr", "ecx,31");  // ecx = eax's old top bit, carries into edx
        emit("shl", "eax,1");
        emit("shl", "edx,1");
        emit("or", "edx,ecx");
    } else {
        emit("mov", "ecx,edx");
        emit("shl", "ecx,31");  // ecx = (edx&1)<<31, carries into eax's top
        emit("shr", "eax,1");
        emit("or", "eax,ecx");
        if (isUnsigned) {
            emit("shr", "edx,1");
        } else {
            emit("sar", "edx,1");
        }
    }
    emit("jmp", Lloop);
    emitLabel(Ldone);
}

// A long long comparison: compare the high dwords first (signed or unsigned jcc per
// isUnsigned, since that's where the sign bit lives); only if those are equal does the low
// dword matter, and that compare is always unsigned regardless of the type's signedness.
void I386Codegen::genWideCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned) {
    genExpr(lhs);
    emit("push", "edx");
    emit("push", "eax");
    genExpr(rhs);
    emit("mov", "esi,eax");
    emit("mov", "edi,edx");  // esi:edi = rhs (lo:hi)
    emit("pop", "ebx");
    emit("pop", "ecx");  // ebx:ecx = lhs (lo:hi)
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    if (op == "==" || op == "!=") {
        // == and != want opposite results on "equal", so swap which literal each label
        // writes instead of duplicating the comparison.
        const char *onEqual = op == "==" ? "1" : "0";
        const char *onDiffer = op == "==" ? "0" : "1";
        emit("cmp", "ebx,esi");
        emit("jne", Lfalse);
        emit("cmp", "ecx,edi");
        emit("jne", Lfalse);
        emit("mov", cat("eax,", onEqual));
        emit("jmp", Ldone);
        emitLabel(Lfalse);
        emit("mov", cat("eax,", onDiffer));
        emitLabel(Ldone);
        return;
    }
    std::string signedJcc = op == "<" ? "jl" : op == "<=" ? "jle" : op == ">" ? "jg" : "jge";
    std::string unsignedJcc = op == "<" ? "jb" : op == "<=" ? "jbe" : op == ">" ? "ja" : "jae";
    std::string hiJcc = isUnsigned ? unsignedJcc : signedJcc;
    std::string LhiDiff = newLabel();
    emit("cmp", "ecx,edi");
    emit("jne", LhiDiff);
    emit("cmp", "ebx,esi");
    emit(unsignedJcc, Ltrue);
    emit("jmp", Lfalse);
    emitLabel(LhiDiff);
    emit(hiJcc, Ltrue);
    emit("jmp", Lfalse);
    emitLabel(Lfalse);
    emit("mov", "eax,0");
    emit("jmp", Ldone);
    emitLabel(Ltrue);
    emit("mov", "eax,1");
    emitLabel(Ldone);
}

std::string I386Codegen::allocFloatTemp(Type type) {
    std::string lbl = allocTemp();
    trailer_.push_back(lbl + (widthOfType(type) == 4 ? ": dd 0" : ": dd 0, 0"));
    return lbl;
}

void I386Codegen::loadFloatOperand(const std::string &mem, Type type) {
    emit("fld", cat((widthOfType(type) == 4 ? "dword " : "qword "), mem));
}

void I386Codegen::storeFloatOperand(const std::string &mem, Type type, bool pop) {
    emit(cat("f", (pop ? "stp " : "st "), (widthOfType(type) == 4 ? "dword " : "qword "), mem));
}

void I386Codegen::genFloatTruthToEax() {
    // ST0 holds the condition's value; FCOMPP against a freshly-pushed 0.0 compares and
    // pops both, leaving the FPU status word's C0/C2/C3 bits to read via the same
    // FNSTSW+SAHF idiom genFloatCompare() uses - ZF ends up set exactly when ST0 was 0.0.
    emit("fldz");
    emit("fcompp");
    emit("fnstsw");
    emit("sahf");
    std::string Lzero = newLabel(), Ldone = newLabel();
    emit("je", Lzero);
    emit("mov", "eax,1");
    emit("jmp", Ldone);
    emitLabel(Lzero);
    emit("mov", "eax,0");
    emitLabel(Ldone);
}

void I386Codegen::applyFloatOp(const std::string &op, const ir::Expr &rhs, Type resultType) {
    std::string lhsTmp = allocFloatTemp(resultType);
    storeFloatOperand("[" + lhsTmp + "]", resultType, /*pop=*/true);  // stash lhs; ST0 empty
    genFloatExpr(rhs);                                                // ST0 = rhs
    std::string rhsTmp = allocFloatTemp(resultType);
    storeFloatOperand("[" + rhsTmp + "]", resultType, /*pop=*/true);  // stash rhs; ST0 empty
    loadFloatOperand("[" + lhsTmp + "]", resultType);                 // ST0 = lhs
    const char *sizeWord = widthOfType(resultType) == 4 ? "dword " : "qword ";
    const char *insn = op == "+" ? "fadd" : op == "-" ? "fsub" : op == "*" ? "fmul" : op == "/" ? "fdiv" : nullptr;
    if (!insn) err(rhs.line, "unsupported operator '" + op + "' on float/double operands");
    emit(insn, cat(sizeWord, "[", rhsTmp, "]"));
}

void I386Codegen::genFloatCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs) {
    Type t = lhs.type;
    genFloatExpr(lhs);
    std::string lhsTmp = allocFloatTemp(t);
    storeFloatOperand("[" + lhsTmp + "]", t, /*pop=*/true);  // stash lhs; ST0 empty
    genFloatExpr(rhs);                                       // ST0 = rhs
    std::string rhsTmp = allocFloatTemp(t);
    storeFloatOperand("[" + rhsTmp + "]", t, /*pop=*/true);  // stash rhs; ST0 empty
    loadFloatOperand("[" + lhsTmp + "]", t);                  // ST0 = lhs
    const char *sizeWord = widthOfType(t) == 4 ? "dword " : "qword ";
    // FCOMP pops ST0 after comparing it against the memory operand (lhs cmp rhs); the same
    // FNSTSW+SAHF idiom genFloatTruthToEax() uses maps the FPU's C0/C2/C3 bits onto
    // CF/PF/ZF as if this were an unsigned integer comparison, so the existing jcc
    // mnemonics below need no float-specific variants.
    emit("fcomp", cat(sizeWord, "[", rhsTmp, "]"));
    emit("fnstsw");
    emit("sahf");
    std::string jumpIfTrue = op == "==" ? "je"
                              : op == "!=" ? "jne"
                              : op == "<" ? "jb"
                              : op == "<=" ? "jbe"
                              : op == ">" ? "ja"
                              : op == ">=" ? "jae"
                              : "";
    if (jumpIfTrue.empty()) err(lhs.line, "internal: unhandled float comparison operator '" + op + "'");
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit(jumpIfTrue, Ltrue);
    emit("mov", "eax,0");
    emit("jmp", Ldone);
    emitLabel(Ltrue);
    emit("mov", "eax,1");
    emitLabel(Ldone);
}

void I386Codegen::genFloatBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
        genFloatCompare(op, *e.lhs, *e.rhs);
        return;
    }
    if (op == "+" || op == "-" || op == "*" || op == "/") {
        genFloatExpr(*e.lhs);
        applyFloatOp(op, *e.rhs, e.type);
        return;
    }
    err(e.line, "unsupported operator '" + op + "' on float/double operands");
}

// int<->float/double and float<->double, in either direction.
void I386Codegen::genFloatConvert(const ir::Expr &e) {
    Type from = e.lhs->type;
    Type to = e.type;
    bool fromFloat = isFloatType(from);
    bool toFloat = isFloatType(to);
    if (fromFloat && toFloat) {
        // Round-tripping through a scratch cell sized for `to` gets single<->double
        // conversion's rounding for free from the store/load themselves - x87 always
        // computes in 80-bit extended precision internally, so a widening (float->double)
        // "conversion" is really just reading the same value back at the wider size.
        genFloatExpr(*e.lhs);
        std::string tmp = allocFloatTemp(to);
        storeFloatOperand("[" + tmp + "]", to, /*pop=*/true);
        loadFloatOperand("[" + tmp + "]", to);
        return;
    }
    if (fromFloat && !toFloat) {
        genFloatExpr(*e.lhs);
        // FISTP alone rounds per the FPU's default control word (round-to-nearest), not
        // C's truncating cast semantics (round toward zero) - e.g. 7.5 would come back as
        // 8. Saving the control word, forcing rounding-control bits 10-11 to "truncate"
        // (binary 11) around the one FISTP, then restoring it, gets truncation without
        // disturbing any float arithmetic elsewhere in the program that still wants the
        // default rounding mode.
        std::string savedCw = allocTemp();
        trailer_.push_back(savedCw + ": dd 0");
        std::string truncCw = allocTemp();
        trailer_.push_back(truncCw + ": dd 0");
        emit("fstcw", cat("word [", savedCw, "]"));
        emit("mov", cat("eax,[", savedCw, "]"));
        emit("mov", "ebx,0xC00");
        emit("or", "eax,ebx");
        emit("mov", cat("[", truncCw, "],eax"));
        emit("fldcw", cat("word [", truncCw, "]"));
        std::string tmp = allocTemp();
        trailer_.push_back(tmp + ": dd 0");
        emit("fistp", cat("dword [", tmp, "]"));
        emit("fldcw", cat("word [", savedCw, "]"));
        emit("mov", cat("eax,[", tmp, "]"));
        narrowEaxTo(to);
        return;
    }
    // !fromFloat && toFloat: always goes through FILD qword (a 64-bit integer source),
    // even for a 32-bit `from` - x87's FILD has no unsigned-32-bit form, so a plain 32-bit
    // FILD would misread an `unsigned int` with its top bit set as negative; zero/sign
    // extending into a 64-bit scratch first (per `from`'s own signedness) sidesteps that
    // for every source width this subset has. A genuinely unsigned 64-bit source (`from`
    // itself an unsigned `long long`) is the one case this still can't represent exactly
    // for a value above 2^63 - FILD qword has no unsigned form at any width - a documented
    // limitation, not a bug: real-world unsigned 64-bit values that large are rare, and no
    // instruction exists here to fix it short of manual bias correction.
    bool fromWide = isWide(from);
    genExpr(*e.lhs);
    std::string tmp = allocTemp();
    trailer_.push_back(tmp + ": dd 0, 0");
    emit("mov", cat("[", tmp, "],eax"));
    if (fromWide) {
        emit("mov", cat("[", tmp, "+4],edx"));
    } else if (from.isUnsigned) {
        emit("mov", "eax,0");
        emit("mov", cat("[", tmp, "+4],eax"));
    } else {
        emit("cdq");
        emit("mov", cat("[", tmp, "+4],edx"));
    }
    emit("fild", cat("qword [", tmp, "]"));
}

void I386Codegen::genFloatExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::FloatLit: {
            std::string lbl = allocTemp();
            if (e.type.base == BaseType::Float) {
                trailer_.push_back(lbl + ": dd " + std::to_string(floatBitsOf(e.floatValue)));
                emit("fld", cat("dword [", lbl, "]"));
            } else {
                uint64_t bits = doubleBitsOf(e.floatValue);
                trailer_.push_back(lbl + ": dd " + std::to_string(static_cast<uint32_t>(bits)) + ", " +
                                    std::to_string(static_cast<uint32_t>(bits >> 32)));
                emit("fld", cat("qword [", lbl, "]"));
            }
            return;
        }
        case ir::ExprKind::Var:
            loadFloatOperand(operandOf(e.slot), e.slot->type);
            return;
        case ir::ExprKind::Index: {
            std::string mem = computeElementAddr(e.slot, *e.lhs);
            loadFloatOperand(mem, e.type);
            return;
        }
        case ir::ExprKind::Assign: {
            if (e.lhs) {  // slot[idx] = rhs
                std::string mem = computeElementAddr(e.slot, *e.lhs);
                emit("push", "ebx");
                genFloatExpr(*e.rhs);
                emit("pop", "ebx");
                storeFloatOperand("[ebx]", e.slot->type, /*pop=*/false);
                return;
            }
            genFloatExpr(*e.rhs);
            storeFloatOperand(operandOf(e.slot), e.slot->type, /*pop=*/false);
            return;
        }
        case ir::ExprKind::DerefAssign: {
            genExpr(*e.lhs);  // eax = pointer value (an address is always an int)
            emit("mov", "ebx,eax");
            emit("push", "ebx");
            genFloatExpr(*e.rhs);
            emit("pop", "ebx");
            storeFloatOperand("[ebx]", e.type, /*pop=*/false);
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            if (e.lhs) {
                err(e.line, "compound assignment through a float/double array element isn't supported yet on this backend");
            }
            const ir::Slot *slot = e.slot;
            std::string mem = operandOf(slot);
            if (e.yieldOld) {
                loadFloatOperand(mem, slot->type);
                std::string oldTmp = allocFloatTemp(slot->type);
                storeFloatOperand("[" + oldTmp + "]", slot->type, /*pop=*/true);
                loadFloatOperand(mem, slot->type);
                applyFloatOp(e.op, *e.rhs, e.type);
                storeFloatOperand(mem, slot->type, /*pop=*/true);
                loadFloatOperand("[" + oldTmp + "]", slot->type);
                return;
            }
            loadFloatOperand(mem, slot->type);
            applyFloatOp(e.op, *e.rhs, e.type);
            storeFloatOperand(mem, slot->type, /*pop=*/false);
            return;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            err(e.line, "compound assignment through a float/double pointer isn't supported yet on this backend");
        }
        case ir::ExprKind::Unary: {
            if (e.op == "-") {
                genFloatExpr(*e.rhs);
                emit("fchs");
            } else if (e.op == "*") {
                genExpr(*e.rhs);  // eax = pointer value
                emit("mov", "ebx,eax");
                loadFloatOperand("[ebx]", e.type);
            } else {
                err(e.line, "unsupported unary operator '" + e.op + "' on a float/double operand");
            }
            return;
        }
        case ir::ExprKind::Binary:
            genFloatBinary(e);
            return;
        case ir::ExprKind::Call:
            genCall(e);
            return;
        case ir::ExprKind::CallPtr:
            genCallPtr(e);
            return;
        case ir::ExprKind::Convert:
            genConvert(e);
            return;
        case ir::ExprKind::Ternary: {
            genTruthToEax(*e.lhs);
            std::string Lelse = newLabel(), Ldone = newLabel();
            emit("cmp", "eax,0");
            emit("je", Lelse);
            genFloatExpr(*e.rhs);
            emit("jmp", Ldone);
            emitLabel(Lelse);
            genFloatExpr(*e.elseExpr);
            emitLabel(Ldone);
            return;
        }
        default:
            err(e.line, "internal: unexpected expression kind reached genFloatExpr");
    }
}

void I386Codegen::genLocalArrayInit(const ir::Slot *slot) {
    int w = widthOfType(slot->type);
    bool isFloatElem = isFloatType(slot->type);
    for (size_t i = 0; i < slot->arrayInit.size(); i++) {
        const ir::ArrayInitElem &elem = slot->arrayInit[i];
        int byteOffset = static_cast<int>(i) * w;
        if (isFloatElem && !elem.isSymbol) {
            // Just the raw bit pattern, straight into memory via eax/edx - no FPU
            // instruction needed at all for a literal (constant) initializer value.
            double v = elem.isFloat ? elem.floatValue : static_cast<double>(elem.value);
            if (w == 4) {
                emit("mov", cat("eax,", floatBitsOf(v)));
                emit("mov", cat(localArrayAddr(slot, byteOffset), ",eax"));
            } else {
                uint64_t bits = doubleBitsOf(v);
                emit("mov", cat("eax,", static_cast<uint32_t>(bits)));
                emit("mov", cat(localArrayAddr(slot, byteOffset), ",eax"));
                emit("mov", cat("eax,", static_cast<uint32_t>(bits >> 32)));
                emit("mov", cat(localArrayAddr(slot, byteOffset + 4), ",eax"));
            }
            continue;
        }
        if (w == 8 && !elem.isSymbol) {
            // A long long literal element is split across both dwords; a symbol element is
            // always a native-int-width address, handled by the general case below.
            auto v = static_cast<uint64_t>(elem.value);
            emit("mov", cat("eax,", static_cast<uint32_t>(v)));
            emit("mov", cat(localArrayAddr(slot, byteOffset), ",eax"));
            emit("mov", cat("eax,", static_cast<uint32_t>(v >> 32)));
            emit("mov", cat(localArrayAddr(slot, byteOffset + 4), ",eax"));
            continue;
        }
        emit("mov", cat("eax,", arrayElemText(elem)));
        if (w == 1) {
            emit("mov", cat("byte ", localArrayAddr(slot, byteOffset), ",al"));
        } else if (w == 2) {
            emit("mov", cat("word ", localArrayAddr(slot, byteOffset), ",ax"));
        } else {
            emit("mov", cat(localArrayAddr(slot, byteOffset), ",eax"));
        }
    }
}

void I386Codegen::genIndex(const ir::Expr &e) {
    std::string memOperand = computeElementAddr(e.slot, *e.lhs);
    std::string highOperand = memOperand.substr(0, memOperand.size() - 1) + "+4]";
    loadOperand(memOperand, highOperand, e.slot->type);
}

void I386Codegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        if (operand.slot->kind == ir::SlotKind::Global) {
            emit("mov", cat("eax,", label(operand.slot)));
        } else if (operand.slot->kind == ir::SlotKind::Local && operand.slot->arraySize > 0) {
            emit("lea", cat("eax,", localArrayAddr(operand.slot)));
        } else {
            emit("lea", cat("eax,", addr(operand.slot)));
        }
        return;
    }
    if (operand.kind == ir::ExprKind::Index) {
        genExpr(*operand.lhs);
        int shift = elemShift(operand.slot->type);
        if (shift > 0) {
            emit("shl", cat("eax,", shift));
        } else if (shift < 0) {
            // Non-power-of-two element width (a struct's) - see computeElementAddr()'s own
            // comment on why this goes through a register, not an immediate.
            emit("mov", cat("ebx,", widthOfType(operand.slot->type)));
            emit("imul", "ebx");
        }
        if (operand.slot->kind != ir::SlotKind::Global) {
            emit("mov", "ebx,eax");
            emit("lea", cat("eax,", localArrayAddr(operand.slot)));
            emit("add", "eax,ebx");
            return;
        }
        std::string lbl = label(operand.slot);
        if (operand.slot->arraySize > 0) {
            emit("add", cat("eax,", lbl));
        } else {
            emit("add", cat("eax,[", lbl, "]"));
        }
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        emit("mov", cat("eax,", label(operand.func)));
        return;
    }
    err(operand.line, "internal: unhandled addressable-operand kind");
}

void I386Codegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    // e.lhs->type names the common operand type (IRBuilder's usual-arithmetic-conversion
    // pass), except for shift (only the LHS's type matters) and &&/|| (handled separately).
    Type operandType = e.lhs->type;
    bool wide = isWide(operandType);
    bool isUnsigned = operandType.isUnsigned;
    if (op == "&&") {
        genLogicalAnd(*e.lhs, *e.rhs);
        return;
    }
    if (op == "||") {
        genLogicalOr(*e.lhs, *e.rhs);
        return;
    }
    if (isFloatType(operandType)) {
        genFloatBinary(e);
        return;
    }
    if (op == "+" || op == "-") {
        if (wide) {
            genWideAdd(*e.lhs, *e.rhs, op == "+");
            return;
        }
        genExpr(*e.lhs);
        emit("push", "eax");
        genExpr(*e.rhs);
        emit("pop", "ebx");
        if (op == "+") {
            emit("add", "eax,ebx");
        } else {
            emit("sub", "ebx,eax");
            emit("mov", "eax,ebx");
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
        const char *insn = (op == "&") ? "and" : (op == "|") ? "or" : "xor";
        if (wide) {
            genExpr(*e.lhs);
            emit("push", "edx");
            emit("push", "eax");
            genExpr(*e.rhs);
            emit("mov", "ebx,eax");
            emit("mov", "ecx,edx");
            emit("pop", "eax");
            emit("pop", "edx");
            emit(insn, "eax,ebx");
            emit(insn, "edx,ecx");
            return;
        }
        genExpr(*e.lhs);
        emit("push", "eax");
        genExpr(*e.rhs);
        emit("pop", "ebx");
        emit(insn, "eax,ebx");
        return;
    }
    if (op == "<<" || op == ">>") {
        if (wide) {
            genWideShift(*e.lhs, *e.rhs, op == "<<", isUnsigned);
            return;
        }
        genExpr(*e.lhs);
        emit("push", "eax");
        genExpr(*e.rhs);
        emit("mov", "ecx,eax");
        emit("pop", "eax");
        const char *insn = op == "<<" ? "shl" : (isUnsigned ? "shr" : "sar");
        emit(insn, "eax,cl");
        return;
    }
    genCompare(op, *e.lhs, *e.rhs);
}

void I386Codegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs) {
    bool isUnsigned = lhs.type.isUnsigned;
    if (isWide(lhs.type)) {
        genWideCompare(op, lhs, rhs, isUnsigned);
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

// cmp ebx,eax compares lhs-rhs (ebx=lhs, eax=rhs after the pop below); `isUnsigned` is
// unused since the caller already baked it into `jumpIfTrue`.
void I386Codegen::emitCompare(const ir::Expr &lhs, const ir::Expr &rhs, const std::string &jumpIfTrue, bool) {
    genExpr(lhs);
    emit("push", "eax");
    genExpr(rhs);
    emit("pop", "ebx");
    emit("cmp", "ebx,eax");
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit(jumpIfTrue, Ltrue);
    emit("mov", "eax,0");
    emit("jmp", Ldone);
    emitLabel(Ltrue);
    emit("mov", "eax,1");
    emitLabel(Ldone);
}

// A wide operand's truthiness needs both dwords: `or eax,edx` folds them together before
// the usual "cmp eax,0".
void I386Codegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    genTruthToEax(lhs);
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    emit("cmp", "eax,0");
    emit("je", Lfalse);
    genTruthToEax(rhs);
    emit("cmp", "eax,0");
    emit("je", Lfalse);
    emit("jmp", Ltrue);
    emitLabel(Lfalse);
    emit("mov", "eax,0");
    emit("jmp", Ldone);
    emitLabel(Ltrue);
    emit("mov", "eax,1");
    emitLabel(Ldone);
}

void I386Codegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    genTruthToEax(lhs);
    std::string Ltrue = newLabel(), Lfalse = newLabel(), Ldone = newLabel();
    emit("cmp", "eax,0");
    emit("jne", Ltrue);
    genTruthToEax(rhs);
    emit("cmp", "eax,0");
    emit("jne", Ltrue);
    emit("jmp", Lfalse);
    emitLabel(Ltrue);
    emit("mov", "eax,1");
    emit("jmp", Ldone);
    emitLabel(Lfalse);
    emit("mov", "eax,0");
    emitLabel(Ldone);
}

void I386Codegen::genLogicalNot(const ir::Expr &rhs) {
    genTruthToEax(rhs);
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit("cmp", "eax,0");
    emit("je", Ltrue);
    emit("mov", "eax,0");
    emit("jmp", Ldone);
    emitLabel(Ltrue);
    emit("mov", "eax,1");
    emitLabel(Ldone);
}

void I386Codegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    bool isUnsigned = lhs.type.isUnsigned;
    genExpr(lhs);
    emit("push", "eax");
    genExpr(rhs);
    emit("pop", "ebx");
    emit((isUnsigned ? "mul" : "imul"), "ebx");  // edx:eax = eax*ebx; keep eax
}

void I386Codegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);
    emit("push", "eax");
    genExpr(rhs);
    emit("mov", "ebx,eax");  // ebx = divisor
    emit("pop", "eax");  // eax = dividend
    if (isUnsigned) {
        emit("xor", "edx,edx");
    } else {
        emit("cdq");
    }
    emit((isUnsigned ? "div" : "idiv"), "ebx");  // eax = quotient, edx = remainder
    if (wantRemainder) emit("mov", "eax,edx");
}

std::optional<std::string> I386Codegen::assembleInProcess(const std::string &asmText,
                                                            const std::string &format) const {
    if (format == "raw") return assembleI386(asmText);
    // coff/elf/macho/aout/objraw: build one format-neutral ObjectFile, then pick that
    // format's FileFormatWriter to serialize it.
    if (auto kind = fileFormatKindFromName(format)) {
        ObjectFile obj = assembleI386Object(asmText);
        if (windowsAbi_) obj.machineType = 0x014C;
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

}  // namespace rcc
