#pragma once

#include <map>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

#include "codegen/Codegen.h"
#include "ir/IR.h"

namespace rcc {

// Generates CP1610 assembly (this project's own dialect - see CP1610Assembler.h) for the IR
// built by IRBuilder, targeting the General Instrument CP1610 (the Intellivision's CPU) as a
// bare processor - a word-addressed machine with no byte addressing at all (every memory
// location holds one full 16-bit word; a `char` therefore occupies a whole word, the same
// convention Pdp7Codegen.h/Pdp10Codegen.h use for their own word-addressed targets - see
// typeWidths()). `generate()` returns a complete flat program loaded at a fixed word address
// (`kBaseAddress`) with no OS/boot ROM, halting via `HLT`.
//
// Register convention (CP1610 has eight 16-bit registers, R0-R7; R7 is the hardware PC):
//   R0  genExpr()'s result ("the value in flight")
//   R1  the other operand of a binary op
//   R2  a computed address (pointer dereference, or a Local/Param's effective address - see
//       addr()) - also the one extra general-purpose address register, since R1/R3 can be
//       needed as value registers at the same time addr() is computing a new one
//   R3  a second scratch register (wide-value temp, divisor, loop counter, as needed)
//   R4  the software frame pointer (persists across this call, like `r13`/`r5` elsewhere)
//   R5  the call-site link register (`JSR R5,label` leaves the return address here; every
//       prologue's first instruction pushes it - see genFunction()) - free as ordinary scratch
//       everywhere else, since nothing ever needs it back
//   R6  the real hardware stack pointer (`PSHR`/`PULR` - CP1610's own indirect-with-auto-
//       increment/decrement addressing modes, the only ones that exist on this CPU)
//   R7  PC - a `PULR R7` is this backend's own "return" (CP1610 has no dedicated return
//       instruction; this is the real, documented idiom)
//
// The CP1610 has no indexed/displacement addressing mode at all (only Direct, Indirect `@Rm`,
// Indirect-with-increment `@Rm+`, Indirect-with-decrement `@Rm-`, and Immediate) - unlike every
// other backend's `addr()`, a Local/Param's effective address has to be computed into R2 with
// real arithmetic every time (`MOVR R4,R2` then `ADDI`/`SUBI` the offset) rather than folded
// into one instruction's own operand - see addr()/genIndexAddr().
//
// The CP1610 has no multiply or divide instruction at all, and no OR (only AND/XOR/COM exist of
// the bitwise family) - `*`/`/`/`%` are software shift-and-add/shift-and-subtract loops (see
// genMul()/genDivMod()), and `|` is synthesized via De Morgan's law (`a|b = ~(~a & ~b)`, see
// genBinary()). There's also no variable-count shift instruction - `SLL`/`SLR`/`SAR`/`RLC`/`RRC`
// only shift by 1 or 2 bits in one instruction, and only on R0-R3, so `<<`/`>>` loop one bit at a
// time the same way Pdp11Codegen.cpp's own genShift() does (conveniently, R0/R1 - the two
// registers a binary op's operands already live in - are always shift-capable).
//
// Every branch targets a full 16-bit word address with no range limit (the branch opcode's own
// displacement word is added to the post-instruction PC using plain 16-bit wraparound, which
// reaches a backward target exactly as well as a forward one - see CP1610Assembler.h's own
// header comment on why this backend never needs the real CP1610's alternate "reverse-direction"
// branch opcodes).
class CP1610Codegen : public Backend {
   public:
    std::string generate(const ir::Program &prog, bool objectMode = false) override;
    std::string name() const override { return "cp1610"; }
    std::string description() const override { return "General Instrument CP1610"; }
    // Word-addressed, no byte addressing: char/short/int are all one native 16-bit word.
    TypeWidths typeWidths() const override { return {1, 1, 1, 2, 4, 16}; }
    std::string sourceExtension() const override { return ".s16"; }
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
    void emitCompare(const ir::Expr &lhs, const ir::Expr &rhs, const std::string &branchIfTrue,
                      bool swapOperands);
    void genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs);
    void genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs);
    void genLogicalNot(const ir::Expr &rhs);
    void testTruthy(const ir::Expr &e);
    // `*`: software shift-and-add (16 iterations, r0=accumulator/multiplicand, r1=multiplier
    // consumed one bit at a time, r3=loop counter). Truncates to the low 16 bits, same
    // convention as every other backend's narrow `*`.
    void genMul(const ir::Expr &lhs, const ir::Expr &rhs);
    // `/`/`%`: software shift-and-subtract (restoring division, 16 iterations).
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
    // `char`/`short`/`int` are all width 1 on this word-addressed target (see typeWidths()), so
    // `widthOfType(t) == 1` alone can't tell a real `char` apart from a plain `int` the way it
    // does on a byte-addressed backend - every load/store/narrow that needs to single out a
    // `char` (to re-derive its own 8-bit range - see narrowR0To()) checks this instead.
    bool isCharType(Type t) const { return t.ptrDepth == 0 && t.base == BaseType::Char; }
    int wordCountOf(Type t) const { return widthOfType(t); }  // already in native words on this target
    // log2 of the element width, for the common power-of-two widths (1/2/4/8 native words) -
    // genIndexAddr() scales an index by this many self-doublings when it applies. Returns -1 for
    // any other width (e.g. a packed struct's), telling genIndexAddr() to fall back to a real
    // (software) multiply instead.
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
    void genWideRippleAdd(int words);
    void genWideLoad(const ir::Slot *slot, int words);
    void genWideLoadLiteral(long value, int words);
    void genWideStore(const ir::Slot *slot, int words);
    // Computes word `wordIndex` (0 = least significant) of `slot`'s multi-word storage into r2.
    void genWideSlotWordAddr(const ir::Slot *slot, int wordIndex);

    // Computes slot[idxExpr]'s effective address into r2 (see this file's own header comment on
    // why the CP1610 needs real arithmetic for this, unlike a displacement-addressing backend).
    int localArrayBase(const ir::Slot *slot);
    void genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr);
    // Computes `slot`'s own effective address into r2 (r4 plus/minus a compile-time-constant
    // offset for a Local/Param, or the slot's own label for a Global - a Global still needs this
    // computed, rather than used as a direct-mode address operand directly, whenever the result
    // must ride in a register, e.g. as `&slot`).
    void genSlotAddr(const ir::Slot *slot);

    static std::string label(const ir::Slot *slot) { return "g_" + slot->sourceName; }
    static std::string label(const ir::Function *fn) { return "fn_" + fn->sourceName; }

    // A Local/Param has no label or direct-addressable storage at all - every reference to one
    // computes its address into r2 first (see genSlotAddr()); `addr()` exists only for the
    // handful of callers that immediately load/store through the freshly-computed r2 themselves.
    std::string operandOf(const ir::Slot *slot) const {
        return slot->kind == ir::SlotKind::Global ? label(slot) : "[r2]";
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
