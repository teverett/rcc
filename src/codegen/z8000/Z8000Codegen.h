#pragma once

#include <map>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

#include "codegen/Codegen.h"
#include "ir/IR.h"

namespace rcc {

// Generates Z8000 (Z8002, non-segmented 64KB address space) assembly, in this project's own
// dialect (see Z8000Assembler.h), for the IR built by IRBuilder. Closely mirrors
// Pdp11Codegen.cpp's own design (see that file's header comment) - the Z8000 is architecturally
// similar (16-bit words, 16 general registers, byte/word/indexed addressing, hardware
// multiply/divide) - with mnemonics and register numbers swapped for the Z8000's own real
// instruction set. `generate()` returns a complete flat binary loaded at a fixed address
// (`kBaseAddress`) with no OS/boot loader; it halts via `HALT`.
//
// This language's `int` is 16 bits (the Z8000's native word size), so no byte-sized (`B`-suffix)
// instruction is ever emitted for an `int`. Live values use three general registers: `r0`
// (genExpr()'s result), `r1` (the other operand of a binary op), `r2` (a computed address when
// dereferencing or indexing). `r13` is the dedicated frame-pointer register (see addr()). `r15`
// is the real hardware stack pointer: calls use real `call`/`ret`; every prologue/epilogue
// pushes/pops the caller's `r13` on it; the caller pushes arguments before `call` and pops them
// after return. Every call gets its own frame, so recursion is safe.
//
// `*`/`/` use the real `mult`/`div`: `mult r1,r0` leaves a 32-bit product in the register pair
// `r0:r1` (`r1`, the low word, is the truncated answer); `div r1,r0` divides the 32-bit dividend
// in `r0:r1` by a 16-bit divisor, so the dividend is first sign-extended into that pair with
// `exts` (the Z8000's EXTS, word-to-long sign extend).
//
// `<<`/`>>` loop one bit at a time via `rlc`/`rrc` (rotate-through-carry), exactly like
// Pdp11Codegen.cpp's own `genShift()` - the Z8000 does have a native variable-count dynamic
// shift (`sdl`/`sda`), but this backend doesn't use it (see docs/known-bugs.md's own entry on
// this target, matching the scope this backend was verified to).
//
// Comparisons use `cp` + a signed branch directly (`cp dst,src` sets flags from `dst - src`, so
// `cp r1,r0` with r1=lhs, r0=rhs computes `lhs - rhs` with no operand-order inversion needed -
// the real Z8000 condition codes already encode signed/unsigned comparisons correctly).
//
// Unlike the PDP-11, every branch/call/jump here targets a full 16-bit absolute address (`jp
// cc,addr`/`call addr`), so there's no short-branch range to work around and no `safeBranch()`
// equivalent is needed.
class Z8000Codegen : public Backend {
   public:
    std::string generate(const ir::Program &prog, bool objectMode = false) override;
    std::string name() const override { return "z8000"; }
    std::string description() const override { return "Zilog Z8000 (Z8002, non-segmented)"; }
    TypeWidths typeWidths() const override { return {1, 2, 2, 4, 8}; }
    std::string sourceExtension() const override { return ".s8k"; }
    std::vector<std::string> assembledFormats() const override { return {"raw", "coff", "elf", "aout"}; }
    bool isObjectFormat(const std::string &format) const override { return format != "raw" && format != "asm"; }
    std::map<std::string, std::string> perFileAsm() const override;
    std::optional<std::string> assembleInProcess(const std::string &asmText,
                                                  const std::string &format) const override;

   private:
    void genFunction(const ir::FunctionBody &fn);
    void genStmt(const ir::Stmt &s);
    void genExpr(const ir::Expr &e);  // leaves the result in r0

    void genBinary(const ir::Expr &e);
    void genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned);
    void emitCompare(const ir::Expr &lhs, const ir::Expr &rhs, const std::string &jpCondIfTrue);
    void genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs);
    void genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs);
    void genLogicalNot(const ir::Expr &rhs);
    void testTruthy(const ir::Expr &e);
    void genMul(const ir::Expr &lhs, const ir::Expr &rhs);
    void emitUnsignedDiv();
    void genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned);
    void genShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned);
    void genIndex(const ir::Expr &e);
    void genAddrOf(const ir::Expr &operand);
    void genLocalArrayInit(const ir::Slot *slot);
    void genConvert(const ir::Expr &e);
    void genCompoundOp(const std::string &op, Type type);
    void genCompoundThroughR2(const ir::Expr &e);
    void narrowR0To(Type to);

    int widthOfType(Type t) const { return rcc::widthOf(t, typeWidths()); }
    bool isWide(Type t) const { return widthOfType(t) > widthOfType(Type{BaseType::Int, false}); }
    int wordCountOf(Type t) const { return widthOfType(t) / 2; }
    int elemShift(Type t) const {
        switch (widthOfType(t)) {
            case 1: return 0;
            case 2: return 1;
            case 4: return 2;
            case 8: return 3;
            default: return -1;
        }
    }

    void loadOperand(const std::string &mem, Type type);
    void storeOperand(const std::string &mem, Type type);
    void loadIndirect(const std::string &addrReg, Type type);
    void storeIndirect(Type type);

    void genWideAdd(const ir::Expr &lhs, const ir::Expr &rhs, bool isAdd, int words);
    void genWideMul(const ir::Expr &lhs, const ir::Expr &rhs, int words);
    void genWideDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned, int words);
    void genWideShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned, int words);
    void genWideCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned, int words);
    void genWideBitwise(const ir::Expr &lhs, const ir::Expr &rhs, const std::string &op, int words);
    void genWideNegate(int words);
    void genWideLoad(const ir::Slot *slot, int words);
    void genWideLoadLiteral(long value, int words);
    void genWideStore(const ir::Slot *slot, int words);
    std::string wideWordOperand(const ir::Slot *slot, int wordIndex) const;

    int localArrayBase(const ir::Slot *slot);
    void genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr);

    static std::string label(const ir::Slot *slot) { return "g_" + slot->sourceName; }
    static std::string label(const ir::Function *fn) { return "fn_" + fn->sourceName; }

    // A Local/Param's frame-pointer-relative address text, e.g. "[r13-4]"/"[r13+6]" - encoded by
    // Z8000Assembler.cpp as the Z8000's real base+displacement indexed addressing mode, with
    // `r13` as the base register (see this file's own header comment on frame layout).
    std::string addr(const ir::Slot *slot) const {
        if (slot->kind == ir::SlotKind::Param) {
            return "[r13+" + std::to_string(slot->frameOffset) + "]";
        }
        return "[r13-" + std::to_string(ir::localOffsetDescendingWide(*slot, typeWidths())) + "]";
    }

    std::string operandOf(const ir::Slot *slot) const {
        return slot->kind == ir::SlotKind::Global ? label(slot) : addr(slot);
    }

    std::string allocTemp() { return "t" + std::to_string(tempCounter_++); }
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
    std::vector<std::string> trailer_;
    std::map<std::string, std::string> perFileAsm_;

    int tempCounter_ = 0;
    int labelCounter_ = 0;
    std::vector<std::pair<std::string, std::string>> loopLabels_;  // {continue label, break label}
    std::map<int, std::string> irLabels_;
};

}  // namespace rcc
