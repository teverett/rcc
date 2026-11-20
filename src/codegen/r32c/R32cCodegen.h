#pragma once

#include <map>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

#include "codegen/Codegen.h"
#include "ir/IR.h"

namespace rcc {

// Generates Renesas R32C assembly (this project's own dialect - see
// R32cAssembler.h) for the IR built by IRBuilder. `generate()` returns a
// complete, self-contained program loaded at a fixed address
// (`kBaseAddress`), halting in an infinite loop.
//
// Registers: `R0`-`R3` general, `A0`/`A1` dedicated address registers, `FB`
// frame-base, `SP` a real hardware stack (`PUSH.W`/`POP.W`/`JSR.W`/`RTS`) -
// the same register file M32cCodegen.h already documents, since R32C is
// documented as mnemonic/register-file compatible with its M16C/M32C
// ancestors (see R32cAssembler.h's own header comment). R32C additionally
// lets `R0`/`R2` and `R1`/`R3` be read/written together as the 32-bit pairs
// `R2R0`/`R3R1`, exactly as M32C does. The one place this backend's own
// generated code differs in shape from M32cCodegen.cpp's is address fields:
// every jump/call/label reference is a full 32-bit field (R32cAssembler.h's
// `kR32cRelocAbs32`), not M32C's 16-bit one.
//
// `int` is 16 bits, same as M16C/M32C (matching the shared family C ABI).
// `R0` holds genExpr()'s result ("ac"); `R1` holds "the other operand" in a
// binary op. `A0` holds a computed address when dereferencing one.
//
// `long`/`unsigned long` (4 bytes = 2 words) load both halves into the
// `R2R0`/`R3R1` pair registers and use the real 32-bit `*l` instruction
// family (`addl`/`subl`/`mull`/`divul`/`divxl`/...), the same fast path
// M32cCodegen.h describes - see genWideAdd() et al.'s own per-function
// comments for exactly how. `long long` (8 bytes = 4 words) has no
// corresponding 64-bit hardware, so it keeps the M16C/M32C-style
// multi-word loop strategy - `long long` multiply/divide/modulo remain
// unimplemented on this target too, same known gap as M16C/M32C (see
// genWideMul()/genWideDivMod()'s own comments and docs/known-bugs.md).
//
// `*`/`/` on a native `int` use hardware `MUL.W`/`DIVU.W` exactly as
// M16cCodegen.h describes. `<<`/`>>` use hardware `SHL.W`/`SHA.W` directly.
// Comparisons use `CMP.W`/`CMP.L` plus a signed/unsigned conditional jump.
//
// Every jump/conditional jump encodes a full 32-bit absolute address, so
// there's no branch-range limit to work around.
class R32cCodegen : public Backend, protected TextCodegenBase {
   public:
    std::string generate(const ir::Program &prog, bool objectMode = false) override;
    std::string name() const override { return "r32c"; }
    std::string description() const override { return "Renesas R32C"; }
    TypeWidths typeWidths() const override { return {1, 2, 2, 4, 8}; }
    std::string sourceExtension() const override { return ".sr32c"; }
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
    void genExpr(const ir::Expr &e);  // leaves the result in R0

    void genBinary(const ir::Expr &e);
    void genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned);
    void emitCompare(const ir::Expr &lhs, const ir::Expr &rhs, const std::string &jumpIfTrue);
    void genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs);
    void genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs);
    void genLogicalNot(const ir::Expr &rhs);
    void testTruthy(const ir::Expr &e);
    void genMul(const ir::Expr &lhs, const ir::Expr &rhs);
    void genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned);
    void genShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned);
    void genDivModCore(bool wantRemainder, bool isUnsigned);
    // The `long`-only (2-word) counterpart of genDivModCore(), operating on
    // the 32-bit pair registers `r2r0`(dividend)/`r3r1`(divisor) - see
    // genWideDivMod()'s comment.
    void genDivModCoreLong(bool wantRemainder, bool isUnsigned);
    void genIndex(const ir::Expr &e);
    void genAddrOf(const ir::Expr &operand);
    void genLocalArrayInit(const ir::Slot *slot);
    void genConvert(const ir::Expr &e);
    void genCompoundOp(const std::string &op, Type type);
    void genCompoundThroughA0(const ir::Expr &e);
    void narrowR0To(Type to);

    int widthOfType(Type t) const { return rcc::widthOf(t, typeWidths()); }
    bool isWide(Type t) const { return widthOfType(t) > widthOfType(Type{BaseType::Int, false}); }
    // True for `long`/`unsigned long` specifically (not `long long`) - the
    // width that gets the real-32-bit-register fast path; see this class's
    // own header comment.
    bool isDword(Type t) const { return widthOfType(t) == 4; }
    int wordCountOf(Type t) const { return widthOfType(t) / 2; }
    int elemShift(Type t) const {
        switch (widthOfType(t)) {
            case 1: return 0;
            case 2: return 1;
            case 4: return 2;
            default: return 3;
        }
    }

    void loadOperand(const std::string &mem, Type type);
    void storeOperand(const std::string &mem, Type type);

    // `lhs == nullptr` means the caller has already pushed the left
    // operand's own `words`-word block itself (`lhsAlreadyPushed`) - see
    // R32cCodegen.cpp's own comment on genWideAdd(), reused by
    // CompoundAssign's wide path. `opOverride`/`isUnsignedOverride` let
    // genWideBitwise()/genWideMul()/genWideDivMod() reuse genWideAdd()'s
    // `r2r0`/`r3r1` fast path (`words == 2`) for every binary operator
    // without duplicating it.
    void genWideAdd(const ir::Expr *lhs, const ir::Expr &rhs, bool isAdd, int words, bool lhsAlreadyPushed = false,
                     const std::string &opOverride = "", bool isUnsignedOverride = false);
    void genWideDivMod(const ir::Expr *lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned, int words);
    void genWideMul(const ir::Expr *lhs, const ir::Expr &rhs, int words);
    void genWideShift(const ir::Expr *lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned, int words,
                       bool lhsAlreadyPushed = false);
    void genWideCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned, int words);
    void genWideBitwise(const ir::Expr *lhs, const ir::Expr &rhs, const std::string &op, int words);
    void genWideNegate(int words);
    void genWideLoad(const ir::Slot *slot, int words);
    void genWideLoadLiteral(long value, int words);
    void genWideStore(const ir::Slot *slot, int words);
    std::string wideWordOperand(const ir::Slot *slot, int wordIndex) const;
    void genSignFillWord();

    void genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr);

    static std::string label(const ir::Slot *slot) { return "g_" + slot->sourceName; }
    static std::string label(const ir::Function *fn) { return "fn_" + fn->sourceName; }

    std::string frameOperand(const ir::Slot *slot) const {
        if (slot->kind == ir::SlotKind::Param) {
            return "[fb+" + std::to_string(slot->frameOffset) + "]";
        }
        return "[fb-" + std::to_string(ir::localOffsetDescendingWide(*slot, typeWidths())) + "]";
    }

    std::string operandOf(const ir::Slot *slot) const {
        return slot->kind == ir::SlotKind::Global ? label(slot) : frameOperand(slot);
    }

    std::string allocTemp() { return "t" + std::to_string(tempCounter_++); }
    void declareTemp(const std::string &lbl);
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

    int tempCounter_ = 0;
    int labelCounter_ = 0;
    std::vector<std::pair<std::string, std::string>> loopLabels_;
    std::map<int, std::string> irLabels_;
};

}  // namespace rcc
