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

// Generates RISC-V assembly (see Riscv64Assembler.h) for the IR built by
// IRBuilder, targeting RV64I plus the M extension. `generate()` emits a
// self-contained program loaded at a fixed address (`kBaseAddress`),
// halting in a self-jump loop after `main` returns. `int` is 32 bits;
// `long`/`long long`/every pointer are a full native 64-bit register
// width (`typeWidths()`'s `pointerUnits`/`longUnits`/`longLongUnits` are
// all 8), unlike riscv32's (and most other bare-metal backends') own
// "pointer == one native int" simplification.
//
// This backend passes every call argument on the stack, caller-cleanup
// style, rather than through `a0`-`a7` - the same deliberate
// simplification Riscv32Codegen.h/MipsCodegen.h/SparcCodegen.h share.
//
// Register convention (matching Riscv32Codegen.h's, since RV64's register
// file is the same 32 names, just 64 bits wide now):
//   t0 - genExpr()'s result register ("ac")
//   t1 - the other operand in a binary op
//   t2 - holds a computed address when dereferencing one
//   t4 - transient scratch, used only by loadImm64()'s own high-32-bit half
//   s1 - scratch for a comparison's SLTI, and for the caller's saved `fp`
//        while the epilogue restores it
//   sp/ra/fp - as riscv32's own header comment describes
//
// Unlike riscv32 (a genuinely 32-bit register machine, where a `long
// long` needs a software-emulated pair of 32-bit registers - see
// Riscv32Codegen.h's own `genWideAdd`/`genWideMul`/`genWideDivMod`), every
// C type up to 64 bits fits in one real RV64 register here: `genExpr()`
// always leaves its result in the single register t0, and every operator
// (including `*`/`/`/`%`, via the real RV64M `mul`/`mulh*`/`div*`/`rem*`
// instructions, and `<<`/`>>`, via `sll`/`srl`/`sra`) runs the same way
// regardless of whether the C type is `int` or `long`/`long long`/a
// pointer - only `narrowT0To()`'s post-op truncation differs (a no-op at
// 8 bytes, a sign/zero-extending shift pair otherwise). This mirrors
// AlphaCodegen.h's own single-register design (also 64-bit native
// registers), except RV64 - unlike Alpha - has a real hardware divide, so
// there's no bit-serial `genDivModCore()` fallback needed here at all.
//
// RV64I has no absolute-addressing memory operand, same as riscv32: every
// load/store goes through a base register plus a 12-bit displacement.
//
// Absolute addressing (globals, function labels) - `loadAddress()`:
// `lui`+`addi` only ever build a *sign-extended* 32-bit value (real RV64
// semantics: `lui`'s own 20-bit immediate, shifted into bits[31:12], is
// sign-extended across the rest of the 64-bit register - unlike, say,
// ARM64's `movz`, which zero-extends). Every address in this bare-metal
// target's universe fits in 32 bits unsigned (kBaseAddress plus whatever
// code+data+stack needs, like riscv32's own kRiscv32BaseAddress), but
// kBaseAddress's own bit 31 is set (0x80000000), so a plain `lui`+`addi`
// pair would leave the top 32 bits of the register all-ones instead of
// all-zero - fatal for any later use as a real memory address. Two more
// instructions fix this unconditionally: `slli reg,reg,32` followed by
// `srli reg,reg,32` shifts the low 32 bits up into the high half and back
// down logically, discarding whatever sign-extension garbage `lui`/`addi`
// left there and leaving exactly the intended unsigned 32-bit value,
// zero-extended. This is the RV64 analogue of Riscv32LinkerFactory.cpp's
// own `%hi`/`%lo` convention - same Hi/Lo relocation split and sign-
// rounding, see Riscv64Assembler.h - plus this one fixed, value-
// independent instruction pair that needs no relocation of its own.
class Riscv64Codegen : public Backend, protected TextCodegenBase {
   public:
    std::string generate(const ir::Program &prog, bool objectMode = false) override;
    std::string name() const override { return "riscv64"; }
    std::string description() const override { return "RISC-V RV64I+M"; }
    TypeWidths typeWidths() const override { return {1, 2, 4, 8, 8, 8, 8}; }
    std::string sourceExtension() const override { return ".sriscv64"; }
    std::vector<std::string> assembledFormats() const override {
        return {"raw", "coff", "elf", "macho", "aout", "objraw"};
    }
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
    void genLocalArrayInit(const ir::Slot *slot);
    void genCompoundOp(const std::string &op, Type type);
    void genCompoundThroughT2(const ir::Expr &e);

    int widthOfType(Type t) const { return rcc::widthOf(t, typeWidths()); }
    bool isWide(Type t) const { return widthOfType(t) > widthOfType(Type{BaseType::Int, false}); }
    int elemShift(Type t) const {
        switch (widthOfType(t)) {
            case 1: return 0;
            case 2: return 1;
            case 4: return 2;
            case 8: return 3;
            default: return -1;
        }
    }
    // Loads a value of `type` from `addrReg` into t0, sign/zero-extending
    // per its own width/signedness (a no-op extension at 8 bytes).
    void loadOperand(const std::string &addrReg, Type type);
    // Stores t0 into `addrReg`, truncated to `type`'s width.
    void storeOperand(const std::string &addrReg, Type type);
    // Re-extends t0 from its low widthOf(to) bytes, per `to`'s signedness.
    void narrowT0To(Type to);
    void genConvert(const ir::Expr &e);

    void genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr);  // leaves slot[idx]'s address in t2

    // Loads `lbl`'s address into `reg`, zero-extended - see this class's
    // own header comment above.
    void loadAddress(const std::string &reg, const std::string &lbl);

    // Re-zero-extends `reg`'s own low 32 bits, discarding whatever is
    // currently in its high half - applied to every runtime-computed
    // pointer value right before it's used as a memory address (a
    // dereference's target, or an indirect call's target). This C
    // subset lets a plain, narrower-than-pointer-width integer type
    // (most often `int`) stand in for an address - e.g. `int p; p =
    // &x; *p = 1;`, or a dispatch table declared `int table[N]`
    // (see tests/pointers.c, tests/test_func_ptr_array_loop.c) - and
    // loadOperand() necessarily sign-extends such a value per its own
    // declared (signed) type when read back, same as any other `int`.
    // Since every address in this target's universe starts at
    // kBaseAddress (0x80000000, bit 31 set), that sign-extension always
    // corrupts it into the wrong 64-bit address. A value that's
    // genuinely already a correct, zero-extended pointer (loaded via
    // loadAddress()/a true 8-byte `ld`) is unaffected - the low 32 bits
    // are already all this call keeps.
    void canonicalizeAddress(const std::string &reg);

    void loadSlotAddr(const std::string &reg, const ir::Slot *slot);

    // Emits a 32-bit literal load into `reg`: `addi` when it fits in 12
    // bits (sign-extended), `lui`+`addi` otherwise - always sign-extended
    // across the rest of the 64-bit register, matching plain `int`
    // arithmetic's own semantics (the caller re-narrows/re-extends via
    // narrowT0To() as needed, same as AlphaCodegen.h's own
    // `loadImm32()`/`narrowT0To()` split).
    void loadImm32(const std::string &reg, int32_t v);
    // A frame offset or `-frameSize` can exceed `addi`'s own 12-bit signed range for a large
    // local array, so this routes through `s1` (this dialect's scratch register) when the
    // plain immediate wouldn't fit.
    void addImm(const std::string &dst, const std::string &src, int32_t v);
    // Emits a full 64-bit literal load into `reg`, for a `long`/`long
    // long` literal that doesn't fit in `loadImm32()`'s 32-bit range:
    // the low 32 bits via loadImm32()+a zero-extend, the high 32 bits
    // likewise into a scratch register, shifted up and OR'd in.
    void loadImm64(const std::string &reg, uint64_t v);

    void genEpilogue();

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

    std::vector<std::string> trailer_;
    std::map<std::string, std::string> perFileAsm_;

    int labelCounter_ = 0;
    std::vector<std::pair<std::string, std::string>> loopLabels_;
    std::map<int, std::string> irLabels_;
};

}  // namespace rcc
