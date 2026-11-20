#pragma once

#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "codegen/Codegen.h"
#include "ir/IR.h"

namespace rcc {

// Generates PIR (Parrot Intermediate Representation) text for the IR built by IRBuilder.
// Parrot has no raw-memory instruction and no fixed register count - its $I/$N/$S/$P
// registers are typed, per-sub, and allocated on demand by Parrot's own compiler, not by
// this backend. Since every C type this project supports is integer-valued (src/Type.h's
// BaseType has no float/double at all), the whole language maps onto Parrot's $I registers
// plus one synthetic flat memory:
//
//  - A single global `ResizableIntegerArray` PMC (looked up under the global name "$mem")
//    is the entire flat, byte-addressable address space. EVERY Slot - Global, Local, Param,
//    scalar or array - lives in it, addressed the same way Slot::address/frameOffset already
//    describe addressing on every other backend; only expression-evaluation intermediates
//    ever live in a bare $I register. This mirrors every other backend's own philosophy (a
//    slot is a memory cell; only a handful of scratch registers are "real" registers) and
//    needs no "is this local's address ever taken" analysis - correct by construction for
//    arrays, structs, and arbitrary '&'/'*' use.
//  - Unlike every other backend, this one does NOT reuse ir::Slot::frameOffset's sign
//    convention: that convention exists to match a real hardware stack's fixed growth
//    direction, which this backend has none of (its "stack" is just a bump allocator into an
//    array it fully controls). genFunction() instead assigns each Local/Param its own
//    monotonically increasing offset within the current call's frame, in encounter order -
//    see localOffsets_.
//  - Globals are laid out the same way, once, at the low end of `mem`; the "stack" (per-call
//    frames) bump-allocates downward from the high end, tracked by a 2-element
//    ResizableIntegerArray PMC under the global name "$regs" (regs[0] = sp, regs[1] = fp).
//  - Control transfer (call/return, argument passing) uses Parrot's own native sub-call
//    mechanism - a direct call (ir::ExprKind::Call) is just `$I0 = 'fn_<name>'(args)`. A
//    function pointer value (ir::ExprKind::Func / CallPtr) is an index into a third global,
//    "$funcs" (a ResizablePMCArray of every function's Sub PMC, built once at start via
//    find_sub), since Parrot has no way to call through a raw integer address.
class ParrotVmCodegen : public Backend, protected TextCodegenBase {
   public:
    std::string generate(const ir::Program &prog, bool objectMode = false) override;
    std::string name() const override { return "parrotvm"; }
    std::string description() const override { return "Parrot VM"; }
    TypeWidths typeWidths() const override { return {1, 2, 4, 4, 8}; }
    std::string sourceExtension() const override { return ".pir"; }
    std::vector<std::string> assembledFormats() const override { return {"raw"}; }
    std::optional<std::string> assembleInProcess(const std::string &asmText,
                                                  const std::string &format) const override {
        if (format == "raw") return asmText;
        return std::nullopt;
    }
    std::map<std::string, std::string> perFileAsm() const override { return perFileAsm_; }

   private:
    // This dialect's own convention uses 3 spaces, not TextCodegenBase's 4-space default.
    std::string indent() const override { return "   "; }

    void genFunction(const ir::FunctionBody &fn);
    // Assigns every LocalDecl reachable from `s` its own offset in localOffsets_, in
    // encounter order - see the class comment on why this backend doesn't reuse
    // ir::Slot::frameOffset's own sign convention.
    void collectLocalDecls(const ir::Stmt *s);
    void emitEpilogueAndReturn(bool hasValue, const std::string &retReg);
    void genStmt(const ir::Stmt &s);
    // Evaluates `e` and returns the $I register holding its value.
    std::string genExpr(const ir::Expr &e);
    // Evaluates an addressable operand (Var/Index/Func) and returns the $I register holding
    // its mem[] address, or (Func) its $funcs index - the common path for '&'.
    std::string genAddress(const ir::Expr &e);

    std::string loadSlot(const ir::Slot *slot);
    void storeSlot(const ir::Slot *slot, const std::string &valueReg);
    std::string slotAddress(const ir::Slot *slot);
    std::string elementAddress(const ir::Slot *slot, const ir::Expr &idxExpr);

    std::string loadMem(const std::string &addrReg, Type t);
    void storeMem(const std::string &addrReg, const std::string &valueReg, Type t);
    // Masks/sign-extends a value already narrowed to `to`'s representable range - see
    // Pdp1Codegen::narrowAC for the two's-complement-free analogue this simplifies, since
    // Parrot's $I registers are ordinary two's-complement native ints.
    std::string narrow(const std::string &reg, Type to);

    std::string genBinary(const ir::Expr &e);
    std::string genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs);
    std::string genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs);
    std::string genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs);
    void genCondBranch(const ir::Expr &cond, const std::string &Lfalse);
    // `(slot (op)= rhs)`/`(*lhs (op)= rhs)`'s op, applied to already-evaluated operands.
    std::string applyCompoundOp(const ir::Expr &e, const std::string &oldVal, const std::string &rhsVal);

    int widthOfType(Type t) const { return rcc::widthOf(t, typeWidths()); }
    long slotUnits(const ir::Slot *slot) const {
        return static_cast<long>(slot->arraySize > 0 ? slot->arraySize : 1) * widthOfType(slot->type);
    }

    static std::string label(const ir::Function *fn) { return "fn_" + fn->sourceName; }
    // sigil: "I" for an integer register (every value in this backend is one), "P" for a
    // PMC register (only ever used to hold a looked-up Sub for CallPtr).
    std::string newReg(const std::string &sigil = "I") { return "$" + sigil + std::to_string(regCounter_++); }
    std::string newLabel() { return "L" + std::to_string(labelCounter_++); }
    std::string irLabel(int id) {
        auto it = irLabels_.find(id);
        if (it != irLabels_.end()) return it->second;
        std::string name = newLabel();
        irLabels_[id] = name;
        return name;
    }

    // Function-table index assigned to every ir::Function with a body, for CallPtr/'&fn'
    // (see the class comment's "$funcs" global).
    int funcIndex(const ir::Function *fn);

    std::map<std::string, std::string> perFileAsm_;
    std::string curFile_;

    // Global Slot -> fixed byte offset in mem's low (globals) region, assigned once in
    // generate(). Local/Param Slot -> offset within the CURRENT call's frame, reassigned at
    // the start of every genFunction() - see the class comment.
    std::unordered_map<const ir::Slot *, long> globalOffsets_;
    std::unordered_map<const ir::Slot *, long> localOffsets_;
    long currentFrameSize_ = 0;

    std::unordered_map<const ir::Function *, int> funcIndices_;
    std::vector<const ir::Function *> funcTable_;

    int regCounter_ = 0;
    int labelCounter_ = 0;
    std::map<int, std::string> irLabels_;
    std::vector<std::pair<std::string, std::string>> loopLabels_;  // {continueLabel, breakLabel}
};

}  // namespace rcc
