#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

#include "codegen/Codegen.h"
#include "ir/IR.h"

namespace rcc {

// Generates RISC-V assembly (see Riscv32Assembler.h) for the IR built by
// IRBuilder, targeting RV32I plus the M extension (mul/div/rem).
// `generate()` emits a self-contained program loaded at a fixed address
// (`kBaseAddress`), halting in a self-jump loop after `main` returns.
// `int` is 32 bits.
//
// This backend passes every call argument on the stack, caller-cleanup
// style, rather than through `a0`-`a7`: a deliberate simplification shared
// with this project's MipsCodegen.cpp/SparcCodegen.cpp siblings, not the
// real RV32 calling convention.
//
// Register convention:
//   t0 - genExpr()'s result register ("ac")
//   t1 - the other operand in a binary op
//   t2 - holds a computed address when dereferencing one
//   s1 - scratch for a comparison's SLTI, and for the caller's saved `fp`
//        while the epilogue restores it (this dialect's own stand-in for
//        MipsCodegen.h's `$at`/SparcCodegen.h's internal scratch register)
//   sp - hardware stack pointer: used both to save an operand across
//        evaluating the other in a binary expression, and for calls
//   ra - hardware link register `jal`/`call` sets and `jr`/`ret` reads
//   fp - this call's frame pointer (`s0`); every prologue points it at
//        its own saved-fp word, giving each call (including recursive
//        ones) independent Local/Param storage
//
// Frame layout relative to `fp`: fp+0 is the caller's saved `fp`, fp+4 the
// saved `ra`, param i at fp+8+4*i (right-to-left push, caller cleanup);
// locals are below fp, reserved by the prologue's `addi sp,sp,-N`, with
// Slot::frameOffset K at fp-4*K.
//
// RV32I has no absolute-addressing memory operand: every load/store goes
// through a base register plus a 12-bit displacement, so accessing any
// slot first loads its address into t2 via `lui`+`addi`, then uses
// `lw`/`sw t2,t0,0` (a fixed zero displacement).
//
// `*`/`/`/`%` use the real RV32M `mul`/`div`/`divu`/`rem`/`remu`
// instructions directly - no `mfhi`/`mflo` step, unlike MIPS. `<<`/`>>`
// use the register-form shift `sll`/`srl` (`>>` is logical, `srl` not
// `sra`); a fixed index*4 scale uses `slli` instead. Comparisons use
// `slt`/`sltu`/`sltiu` directly as the 1/0 boolean result, no branch
// needed.
//
// RISC-V has no branch-delay slot (unlike MIPS/SPARC), so every
// `beq`/`bne`/`jal`/`jalr` here takes effect immediately, with no
// synthesized `nop` needed afterward.
class Riscv32Codegen : public Backend {
   public:
    std::string generate(const ir::Program &prog, bool objectMode = false) override;
    std::string name() const override { return "riscv32"; }
    std::string description() const override { return "RISC-V RV32I+M"; }
    TypeWidths typeWidths() const override { return {1, 2, 4, 4, 8}; }
    std::string sourceExtension() const override { return ".sriscv32"; }
    std::vector<std::string> assembledFormats() const override {
        return {"raw", "coff", "elf", "macho", "aout", "objraw"};
    }
    // `raw` is a whole-program flat binary; the other five are real
    // relocatable object files for separate compilation.
    bool isObjectFormat(const std::string &format) const override { return format != "raw" && format != "asm"; }
    std::map<std::string, std::string> perFileAsm() const override;
    std::optional<std::string> assembleInProcess(const std::string &asmText,
                                                  const std::string &format) const override;

   private:
    void genFunction(const ir::FunctionBody &fn);
    void genStmt(const ir::Stmt &s);
    void genExpr(const ir::Expr &e);  // leaves the result in t0

    void genBinary(const ir::Expr &e);
    void genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs);
    void genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs);
    void genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs);
    void genLogicalNot(const ir::Expr &rhs);
    void genMul(const ir::Expr &lhs, const ir::Expr &rhs);
    void genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned);
    void genIndex(const ir::Expr &e);         // slot[idx] read
    void genAddrOf(const ir::Expr &operand);  // &slot or &slot[idx]
    void genLocalArrayInit(const ir::Slot *slot);  // stores arrayInit into a Local's own frame slots
    // CompoundAssign/CompoundDerefAssign's "new = old <op> rhs" step:
    // old in t0, rhs in t1, leaves the untruncated new value in t0.
    void genCompoundOp(const std::string &op, Type type);
    // Shared tail for CompoundAssign/CompoundDerefAssign; t2 must
    // already hold the resolved target address on entry.
    void genCompoundThroughT2(const ir::Expr &e);

    int widthOfType(Type t) const { return rcc::widthOf(t, typeWidths()); }
    bool isWide(Type t) const { return widthOfType(t) > widthOfType(Type{BaseType::Int, false}); }
    // log2 of an element type's width (1/2/4/8 bytes -> 0/1/2/3).
    int elemShift(Type t) const {
        switch (widthOfType(t)) {
            case 1: return 0;
            case 2: return 1;
            case 4: return 2;
            case 8: return 3;
            default: return -1;
        }
    }
    // Loads a value of `type` from `addrReg` into t0 (or t1:t0 for a
    // `long long`), sign- or zero-extending per its signedness.
    void loadOperand(const std::string &addrReg, Type type);
    // Stores t0 (or t1:t0) into `addrReg`, truncating to `type`'s width.
    void storeOperand(const std::string &addrReg, Type type);
    // Re-extends t0 from its low widthOf(to) bytes, per `to`'s signedness.
    void narrowT0To(Type to);
    void genConvert(const ir::Expr &e);
    void genWideAdd(const ir::Expr &lhs, const ir::Expr &rhs, bool isAdd);
    void genWideMul(const ir::Expr &lhs, const ir::Expr &rhs);
    // Software 64-bit divide/modulo (no 32-bit RV32 instruction is wide
    // enough): bit-serial shift/compare/subtract long division. Leaves
    // the result in t1:t0.
    void genWideDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned);
    void genWideShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned);
    void genWideCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned);
    // Negates the 64-bit value already in t1:t0 (hi:lo), in place.
    void genNegate64();

    // Leaves slot[idxExpr]'s effective address in t2.
    void genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr);

    void loadAddress(const std::string &reg, const std::string &lbl);

    // Loads a plain (non-array) Global/Local/Param slot's address into
    // `reg`: a Global's fixed label address, or a Local/Param's
    // fp-relative address computed in one `addi`.
    void loadSlotAddr(const std::string &reg, const ir::Slot *slot);

    // Emits a 32-bit literal load into `reg`: `addi` when it fits in 12
    // bits (sign-extended), `lui`+`addi` otherwise.
    void loadImm32(const std::string &reg, int32_t v);

    // Shared function-exit sequence (deallocate locals, restore
    // caller's `fp`/`ra`, `ret`); used by both the fall-through epilogue
    // and every explicit early `return`.
    void genEpilogue();

    // A Global's name-based label, or a Function's entry label. A
    // Local/Param never gets one; it's addressed via loadSlotAddr().
    static std::string label(const ir::Slot *slot) { return "g_" + slot->sourceName; }
    static std::string label(const ir::Function *fn) { return "fn_" + fn->sourceName; }

    void declareArray(const std::string &lbl, const std::vector<ir::ArrayInitElem> &values, Type elemType);
    std::string arrayElemText(const ir::ArrayInitElem &e);
    std::string newLabel() { return "L" + std::to_string(labelCounter_++); }
    std::string irLabel(int id) {
        auto it = irLabels_.find(id);
        if (it != irLabels_.end()) return it->second;
        std::string name = newLabel();
        irLabels_[id] = name;
        return name;
    }

    std::ostringstream code_;
    std::vector<std::string> trailer_;  // "label: dw v0, v1, ..." data lines, emitted once at the end
    std::map<std::string, std::string> perFileAsm_;

    int labelCounter_ = 0;
    // One entry per currently-open loop (innermost last): {continue
    // label, break label}.
    std::vector<std::pair<std::string, std::string>> loopLabels_;
    std::map<int, std::string> irLabels_;  // see irLabel()
};

}  // namespace rcc
