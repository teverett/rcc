#pragma once

#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "MoarVmAssembler.h"
#include "codegen/Codegen.h"
#include "ir/IR.h"

namespace rcc {

// Targets the MoarVM bytecode interpreter (the VM behind Rakudo/Raku), the same way
// ParrotVmCodegen targets Parrot - see that class's own comment for the shared design this
// one follows (a single flat memory array backs every Slot, so arbitrary '&'/'*' use is
// correct by construction with no "address taken" analysis). MoarVM differs from Parrot in
// two ways that shape this backend:
//
//  - MoarVM has no official textual assembly language at all, only the binary bytecode
//    format itself (see MoarVmAssembler.h), so generate() returns the final binary bytes
//    directly rather than text for a separate assembler to parse - the same shortcut
//    ParrotVmCodegen/VexCodegen take for their own textual output, just binary instead of
//    text. perFileAsm() instead slices a separate human-readable mnemonic log kept purely
//    for --dump-asm's benefit; it is not re-assemblable and was never meant to be.
//  - A MoarVM frame (its unit of code - roughly a function activation) has no storage that
//    automatically persists across separate invocations the way Parrot's named globals do;
//    reaching that would need real lexical-scope nesting, which is NQP-compiler machinery
//    this backend does not have. So instead of a Parrot-style named global, the flat memory
//    array (`mem`) and the function-pointer table (`funcs`, one entry per ir::Function with
//    a body, for `&fn`/CallPtr) are built once by the entry frame and passed as the first
//    two actual parameters of every single call this backend ever emits - every generated
//    frame's real parameter list is `(mem, funcs, <the function's own params>)`. A call
//    site, whether to a statically named function or through a function pointer, always
//    invokes the callee via the MoarVM-builtin `lang-call` dispatcher, which needs no HLL
//    config or serialization context setup.
//  - The bump-allocated per-call stack (ParrotVM's `$regs[0]` "$sp") has nowhere persistent
//    to live either, so slot 0 of `mem` itself is reserved for it; every Global's offset
//    starts at 1, not 0.
class MoarVmCodegen : public Backend {
   public:
    std::string generate(const ir::Program &prog, bool objectMode = false) override;
    std::string name() const override { return "moarvm"; }
    std::string description() const override { return "MoarVM"; }
    TypeWidths typeWidths() const override { return {1, 2, 4, 4, 8}; }
    std::string sourceExtension() const override { return ".moarvm"; }
    std::vector<std::string> assembledFormats() const override { return {"raw"}; }
    std::optional<std::string> assembleInProcess(const std::string &asmText,
                                                  const std::string &format) const override {
        if (format == "raw") return asmText;
        return std::nullopt;
    }
    std::map<std::string, std::string> perFileAsm() const override { return perFileAsm_; }

   private:
    // Allocates (but does not yet fill in) a MoarVmAssembler frame for every ir::Function
    // with a body plus the synthetic entry frame, so getcode's frame-index operand - needed
    // for both a direct call and building the `funcs` table - is always already known.
    int frameIndexOf(const ir::Function *fn);
    void genFunction(const ir::FunctionBody &fn);
    void collectLocalDecls(const ir::Stmt *s);
    void emitEpilogueAndReturn(bool hasValue, int retReg);
    void genStmt(const ir::Stmt &s);
    int genExpr(const ir::Expr &e);
    int genAddress(const ir::Expr &e);

    int loadSlot(const ir::Slot *slot);
    void storeSlot(const ir::Slot *slot, int valueReg);
    int slotAddress(const ir::Slot *slot);
    int elementAddress(const ir::Slot *slot, const ir::Expr &idxExpr);

    int loadMem(int addrReg, Type t);
    void storeMem(int addrReg, int valueReg, Type t);
    int narrow(int reg, Type to);

    int genBinary(const ir::Expr &e);
    int genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs);
    int genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs);
    int genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs);
    void genCondBranch(const ir::Expr &cond, int Lfalse);
    int applyCompoundOp(const ir::Expr &e, int oldVal, int rhsVal);
    int applyBinOp(const std::string &op, int a, int b, int line);

    // `moar` has no own-process way to call back into a host C library, so there is no
    // runtime library the way a hardware target's src/rt/<machine>/ provides one (see
    // docs/known-bugs.md) - only one output primitive exists at all, recognized here by
    // name instead of a real call: `extern void moarvm_say(char *s)` walks a NUL-terminated
    // byte string out of `mem` one cell at a time and prints it via MoarVM's own `say` op.
    void genSayBuiltin(int addrReg);

    int irLabel(int id);

    // Loads a compile-time-known constant into a fresh local - MoarVM's arithmetic ops take
    // only registers, never an immediate operand (unlike the PIR text ParrotVmCodegen
    // emits), so every literal operand has to be materialized this way first.
    int loadConst(long value);
    int newLocal(MoarVmAssembler::LocalType type = MoarVmAssembler::LocalType::Int64);

    int widthOfType(Type t) const { return rcc::widthOf(t, typeWidths()); }
    long slotUnits(const ir::Slot *slot) const {
        return static_cast<long>(slot->arraySize > 0 ? slot->arraySize : 1) * widthOfType(slot->type);
    }

    void log(const std::string &line) { log_ << line << '\n'; }

    MoarVmAssembler asm_;
    std::ostringstream log_;
    std::map<std::string, std::string> perFileAsm_;

    std::unordered_map<const ir::Function *, int> frameIndices_;

    std::unordered_map<const ir::Function *, int> funcIndices_;
    std::vector<const ir::Function *> funcTable_;

    // Registers (MoarVmAssembler locals) of the frame currently being generated.
    int memLocal_ = -1;
    int funcsLocal_ = -1;
    int fpLocal_ = -1;

    std::unordered_map<const ir::Slot *, long> globalOffsets_;
    std::unordered_map<const ir::Slot *, long> localOffsets_;
    long currentFrameSize_ = 0;

    std::map<int, int> irLabels_;  // ir::Stmt::labelId -> MoarVmAssembler label id
    std::vector<std::pair<int, int>> loopLabels_;  // {continueLabel, breakLabel}
};

}  // namespace rcc
