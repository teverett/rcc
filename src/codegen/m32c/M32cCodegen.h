#pragma once

#include <map>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

#include "codegen/Codegen.h"
#include "ir/IR.h"

namespace rcc {

// Generates Renesas M32C assembly (this project's own dialect - see
// M32cAssembler.h) for the IR built by IRBuilder. `generate()` returns a
// complete, self-contained program loaded at a fixed address
// (`kBaseAddress`), halting in an infinite loop.
//
// Registers: `R0`-`R3` general, `A0`/`A1` dedicated address registers, `FB`
// frame-base, `SP` a real hardware stack (`PUSH.W`/`POP.W`/`JSR.W`/`RTS`) -
// all identical to M16cCodegen.h's own register model, since M32C is
// binary-upward-compatible with the M16C's 16-bit instruction subset. M32C
// additionally lets `R0`/`R2` and `R1`/`R3` be read/written together as the
// 32-bit pairs `R2R0`/`R3R1`.
//
// `int` is 16 bits, same as M16C (matching M32C's own C ABI, which keeps
// `int` binary-compatible with M16C object code). `R0` holds genExpr()'s
// result ("ac"); `R1` holds "the other operand" in a binary op. `A0` holds a
// computed address when dereferencing one.
//
// `long`/`unsigned long` (4 bytes = 2 words) are the one place this backend
// diverges from M16cCodegen.h's strategy: instead of M16C's manual
// word-at-a-time carry-chain loop (which that backend uses because real
// M16C silicon has no 32-bit ALU), every `long` arithmetic/logic/shift/
// compare/multiply/divide operation here loads both halves into the
// `R2R0`/`R3R1` pair registers and uses the real 32-bit `*l` instruction
// family (`addl`/`subl`/`mull`/`divul`/`divxl`/...) - see genWideAdd() et
// al.'s own per-function comments for exactly how. `long long` (8 bytes = 4
// words) has no corresponding 64-bit hardware, so it keeps M16cCodegen.h's
// original multi-word loop strategy - `long long` multiply/divide/modulo
// remain unimplemented on this target too, same known gap as M16C (see
// genWideMul()/genWideDivMod()'s own comments and docs/known-bugs.md).
//
// `*`/`/` on a native `int` use hardware `MUL.W`/`DIVU.W` exactly as
// M16cCodegen.h describes. `<<`/`>>` use hardware `SHL.W`/`SHA.W` directly.
// Comparisons use `CMP.W`/`CMP.L` plus a signed/unsigned conditional jump.
//
// Every jump/conditional jump always encodes a full absolute address (a
// deliberate simplification - real M32C conditional jumps are short and
// PC-relative), so there's no branch-range limit to work around.
class M32cCodegen : public Backend, protected TextCodegenBase {
   public:
    std::string generate(const ir::Program &prog, bool objectMode = false) override;
    std::string name() const override { return "m32c"; }
    std::string description() const override { return "Renesas M32C"; }
    TypeWidths typeWidths() const override { return {1, 2, 2, 4, 8}; }
    std::string sourceExtension() const override { return ".s32c"; }
    std::vector<std::string> assembledFormats() const override {
        return {"raw", "coff", "elf", "macho", "aout", "objraw"};
    }
    bool isObjectFormat(const std::string &format) const override { return format != "raw" && format != "asm"; }
    std::map<std::string, std::string> perFileAsm() const override;
    std::optional<std::string> assembleInProcess(const std::string &asmText,
                                                  const std::string &format) const override;
    // Only the whole-program-final format(s) this target lists in
    // assembledFormats() (not a real relocatable object - see
    // Backend::debugMap()'s own comment) get a debug map; main.cpp's own
    // isObjectFormat() gate already keeps this from ever being called for
    // an object format, so no format check is needed here.
    std::optional<std::string> debugMap(const std::string &asmText, const std::string &format,
                                         const std::string &sourcePath) const override;
    void setDebugMapEnabled(bool enabled) override { debugMapEnabled_ = enabled; }
    std::optional<std::string> debugMapPreLink(const std::string &asmText,

                                                const std::string &sourcePath) const override;

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
    // M32cCodegen.cpp's own comment on genWideAdd(), reused by
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

    // Debug-map-only - see Pdp7Codegen.h's own comment on the identical
    // members there, which this mirrors exactly.
    int lastDebugLine_ = 0;
    bool debugMapEnabled_ = false;
    std::vector<std::pair<std::string, bool>> debugSymbols_;
    void emitSourceLine(int line);
};

}  // namespace rcc
