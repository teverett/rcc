#pragma once

#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "codegen/Codegen.h"
#include "ir/IR.h"

namespace rcc {

// Generates vex assembly (see VexAssembler.h) for the IR built by IRBuilder. Vex
// (/Users/tom/projects/vex) is a byte-addressed 32-bit stack machine with NO general-purpose
// registers at all - every one of its 22 instructions operates on an implicit data stack (and,
// for a handful, a separate return stack) - so this backend's own shape is unlike every other
// target here: there is no register allocator, because there is nothing to allocate. Compiling an
// expression just means walking it and emitting the stack ops that leave its value on top of the
// data stack; a C statement's control flow maps onto `JMP`/`JCN` directly.
//
// --- Storage model -------------------------------------------------------
//
// `char`/`short`/`int`/`long` are all exactly one 32-bit cell (TypeWidths{4,4,4,4,8,8,4}: 4
// storage *bytes* each, `unitBits=8` - see typeWidths()'s own comment on why the unit is bytes,
// not cells, despite every scalar being cell-wide) - deliberate: vex's `LD`/`ST` only ever move
// a full 4-byte cell (there is no narrower load/store at all), so giving `char`/`short` their
// C-standard narrower width would cost a mask-and-sign-extend sequence on every single load and
// store just to emulate a width the hardware doesn't have. This backend instead keeps every
// scalar a uniform cell, the same choice docs/SPEC.md itself makes for the machine's own stacks.
// This is visible to a C program in two ways, both documented backend-shaped limitations (not
// bugs) rather than something a fix is pending for:
//   - Overflow: assigning 300 to a `char` does NOT wrap to 44 the way it would on a target with
//     real 8-bit storage - the value stays a full int until something explicitly narrows it
//     (genConvert() below DOES mask+sign-extend on an explicit/implicit narrowing conversion, so
//     a deliberate `(char)x` cast or a declared-narrower assignment still truncates correctly;
//     only a char/short *variable's own storage* never silently wraps on overflow the way the C
//     standard's own `unsigned char` semantics would guarantee).
//   - `sizeof(char)` is 4, not 1: the one C-standard guarantee ("sizeof(char) == 1") this
//     backend can't make, since there genuinely is no sub-cell storage to measure it against.
//     A knock-on effect: code that walks a `char*` (or an untyped `int` holding a byte address,
//     as `tests/string_literal.c` does) one unit at a time assuming consecutive bytes - e.g. a
//     string literal's own characters - finds them one cell apart instead, not one byte apart.
//     `tests/run_tests.sh`'s own `is_known_unsupported()` excludes the handful of shared tests
//     that check either of these exactly (`types_sizeof.c`, `string_literal.c`).
//
// `long long` (2 cells) is NOT supported - see
// genWideUnsupported(): a correct 64-bit add/sub/compare needs a carry chain, and this machine's
// ALU reports no carry/overflow out at all (`ADD`/`SUB` just wrap silently), so there is no way to
// detect one; same documented gap as Pdp1Codegen.h's own `long`/`long long` cut, drawn one step
// narrower here since plain `long` costs this backend nothing extra.
//
// `struct` is NOT supported yet (rejected with a clear compile error) - out of scope for this
// pass; see the final porting report.
//
// A Global (scalar or array) gets a label and that many `.word` cells (always 4 bytes each, even
// for a `char`, per the storage model above) - see declareGlobal(). Reading/writing one is always
// `LIT <label[+index*4]>` then `LD`/`ST`; there is no addressing mode narrower than that.
//
// --- Locals/params: two storage classes ----------------------------------
//
// A function that makes no call anywhere in its body (computeIsLeaf()) - or one the programmer
// marked `@nonrecursive` - can never be re-entered while running, so every Local/Param gets its
// own fixed static memory cell (or, for an array, a fixed static block), read/written exactly
// like a Global. This is the only storage class that can ever hold an array or a value whose
// address is taken (`&x`), because it's the only one backed by real, addressable memory.
//
// A function that DOES call something (and isn't `@nonrecursive`) needs independent storage per
// activation, so its Locals/Params instead live on vex's own return stack: the caller pushes
// arguments on the DATA stack in declaration order (the uniform ABI every call site uses,
// regardless of the callee's own storage choice - see genCallArgs()); the callee's prologue moves
// them onto the RETURN stack with `PUSH`, reserving its own locals the same way, so every
// Local/Param ends up at a fixed `LDS.r`/`STS.r` cell offset from the top for the rest of the
// call (see emitPrologue()'s own comment for the exact depth arithmetic, built directly from
// IRBuilder's already-computed Slot::frameOffset). The epilogue walks it all back off with `POP`
// before `RET` - exactly docs/SPEC.md section 4's own suggested convention ("PUSH them, reach
// them with LDS.r/STS.r, and POP them again before RET").
//
// The return stack is NOT part of vex's addressable 64 KiB memory at all (see docs/SPEC.md
// section 1's own table - it's a separate fixed buffer, like the data stack), so a value stored
// there has no address `&` could ever name. A function that recurses therefore can't have an
// array/struct Local/Param, and can't take the address of a scalar one either -
// checkNoAddressOfFrameSlot() rejects this with a clear compile error (name the exact construct;
// suggest `@nonrecursive` or restructuring) rather than silently miscompiling it. This is this
// backend's one real hardware-shaped limitation, in the same spirit as Pdp1Codegen.h's own
// documented cuts.
//
// --- Division, modulo, shifts --------------------------------------------
//
// `DIV` is UNSIGNED ONLY (docs/SPEC.md section 3) and there is no shift instruction at all
// ("Not provided: shifts" - section 7). Unsigned `/`/`%`/`>>` and any `<</`>>` by a compile-time
// constant map directly onto `DIV`/`MUL` by the matching power of two. Signed `/`/`%` and a
// variable-count `<<`/`>>` go through tiny shared runtime routines (ensureRuntimeRoutines()) -
// software long division's usual sign-then-magnitude trick for the former, repeated doubling/
// halving for the latter - emitted once per whole-program compile, only if actually used.
//
// --- Entry sequence -------------------------------------------------------
//
// A whole-program compile (objectMode == false) emits `_start: LIT <main> CAL` then a bare `RET`:
// `main`'s own `RET` already popped the one return address `CAL` pushed, so the return stack is
// empty again by the time control reaches that final `RET`, which is docs/SPEC.md section 3's own
// *normal halt* condition, leaving `main`'s return value sitting untouched on top of the data
// stack. That is deliberate, not an oversight: vex has no OS/exit-code culture of its own stronger
// than "the data stack holds the answer" (see docs/SPEC.md's own calling-convention idiom), and
// tests/run_tests.sh's own check_vex() reads exactly that cell back out via the interpreter's own
// `-d` dump flag. A program that wants a specific process exit status still can - DEO to port
// 0x00 explicitly, same as any other reference-device write (see src/rt/vex/vex.h).
class VexCodegen : public Backend {
   public:
    std::string generate(const ir::Program &prog, bool objectMode = false) override;
    std::string name() const override { return "vex"; }
    // Byte-addressed, `unitBits=8` (matching vex's own real address space), with every scalar
    // 4 storage units (one 32-bit cell) wide regardless of C type - see the class comment's
    // storage model. `widthOfType()` therefore always returns a byte count directly, matching
    // what the shared IRBuilder's own pointer-arithmetic scaling (buildPointerBinary()) expects
    // one storage unit to mean: one address-space increment. Frame-offset/LDS.r/STS.r cell
    // arithmetic divides back down by 4 wherever it needs a *cell* count instead of a byte
    // count - see frameDepth()/paramBytesTotal()/emitPrologue()/emitEpilogueAndReturn().
    TypeWidths typeWidths() const override { return {4, 4, 4, 4, 8, 8, 4}; }
    std::string sourceExtension() const override { return ".s"; }
    std::vector<std::string> assembledFormats() const override { return {"raw", "coff"}; }
    bool isObjectFormat(const std::string &format) const override { return format == "coff"; }
    std::map<std::string, std::string> perFileAsm() const override;
    std::optional<std::string> assembleInProcess(const std::string &asmText,
                                                  const std::string &format) const override;

    // Used by checkFrameableBody()'s own body-scanning helper in VexCodegen.cpp's anonymous
    // namespace - public only so that free function can reach it.
    static bool exprTakesAddressOfFrameSlot(const ir::Expr *e, const std::unordered_set<const ir::Slot *> &frameSlots,
                                             std::string *badName);

   private:
    // --- top-level -----------------------------------------------------
    void genFunction(const ir::FunctionBody &fn);
    void declareGlobal(const ir::Slot &slot);

    // --- leaf/framed classification ------------------------------------
    // Every function that can reach itself through one or more direct calls - see this
    // method's own comment in the .cpp for exactly what "reach" means here. Computed once, in
    // generate(), from the whole program's call graph.
    static std::unordered_set<const ir::Function *> computeRecursiveFunctions(const ir::Program &prog);
    std::unordered_set<const ir::Function *> recursiveFunctions_;
    bool currentFunctionIsLeaf_ = false;
    // Throws if `fn` isn't leaf/`@nonrecursive` but declares an array/struct Local/Param, or
    // takes the address of one anywhere in its own body - see the class comment's "two storage
    // classes" section. Struct is rejected unconditionally (see rejectStruct()).
    void checkFrameableBody(const ir::Function *fn, const ir::Stmt *body);
    void rejectStruct(Type t, int line, const std::string &what);
    [[noreturn]] void genWideUnsupported(int line, const std::string &what);
    bool isWide(Type t) const { return !isPointer(t) && t.base == BaseType::LongLong; }
    int widthOfType(Type t) const { return rcc::widthOf(t, typeWidths()); }

    // --- statements/expressions -----------------------------------------
    void genStmt(const ir::Stmt &s);
    void genExpr(const ir::Expr &e);  // leaves the result on top of the data stack
    void genBinary(const ir::Expr &e);
    void genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned);
    void genCondBranch(const ir::Expr &cond, const std::string &Lfalse);
    void genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs);
    void genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs);
    // Both assume the operand(s) already sit on top of the data stack (`[a]` for a shift's own
    // left operand plus a freshly-pushed count, `[a,b]` for div/mod) - the shape every compound-
    // assignment call site needs (its own "old value" has no Expr to re-evaluate), and every
    // ordinary binary call site reaches the same way after its own genExpr(lhs)/genExpr(rhs).
    void genShiftOp(const std::string &op, bool isUnsigned);  // consumes `[a,n]`, leaves the result
    void genDivModFromStack(bool wantRemainder, bool isUnsigned);  // consumes `[a,b]`, leaves the result
    void genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned);
    // Combines a compound assignment's current value (already on top of the data stack) with
    // `e.rhs` per `e.op`, leaving the result on top. Covers every compound operator, including
    // `*=`/`/=`/`%=`/`<<=`/`>>=` which have no single matching opcode (`+=`/`-=`/`&=`/`|=`/`^=`
    // go through binOpInstr() directly).
    void applyCompoundOp(const ir::Expr &e);
    // `addr` is already on top of the return stack when this runs (see call sites) - shared by
    // genCompoundAssign's indexed form and genCompoundDerefAssign.
    void genCompoundThroughAddress(const ir::Expr &e);
    void genConvert(const ir::Expr &e);
    void genIndex(const ir::Expr &e);     // pushes the element's address
    void genAddrOf(const ir::Expr &operand);
    void genCall(const ir::Expr &e);
    void genCallPtr(const ir::Expr &e);

    // Slot access - dispatches on Global vs. leaf-static vs. framed storage (see class comment).
    void genLoadSlot(const ir::Slot *slot);
    void genStoreSlot(const ir::Slot *slot);   // value already on top of the data stack
    void genSlotAddress(const ir::Slot *slot);  // pushes the slot's own address (leaf/global only)
    void genAssign(const ir::Expr &e);
    void genCompoundAssign(const ir::Expr &e);
    // `addr` is already on top of the return stack when this runs - shared by genAssign's
    // indexed form and genDerefAssign.
    void genAssignThroughAddress(const ir::Expr &e);
    void genDerefAssign(const ir::Expr &e);
    void genCompoundDerefAssign(const ir::Expr &e);

    // --- framed-function frame arithmetic --------------------------------
    // Cell offset from the top of the return stack, valid for the rest of the current (framed)
    // function's body once emitPrologue() has run - see the class comment's own derivation.
    int frameDepth(const ir::Slot *slot) const;
    int paramBytesTotal(const ir::Function *fn) const;
    void emitPrologue(const ir::Function *fn);
    void emitEpilogueAndReturn(bool hasValue);
    std::string epilogueLabel() const { return curFunctionLabel_ + "_epi"; }

    // --- labels/temporaries ----------------------------------------------
    static std::string label(const ir::Slot *slot) { return "G_" + slot->sourceName; }
    static std::string label(const ir::Function *fn) { return "FN_" + fn->sourceName; }
    // A leaf function's own static cell for one Local/Param, keyed by id and prefixed with the
    // owning function's own already-globally-unique label - see Pdp1Codegen.h's own `leafLabel()`
    // for why the prefix matters across separately-compiled files.
    std::string staticLabel(const ir::Slot *slot) const { return curFunctionLabel_ + "_S" + std::to_string(slot->id); }
    std::string newLabel() { return "J" + std::to_string(labelCounter_++); }
    std::string irLabel(int id) {
        auto it = irLabels_.find(id);
        if (it != irLabels_.end()) return it->second;
        std::string name = newLabel();
        irLabels_[id] = name;
        return name;
    }

    void ensureRuntimeRoutines();
    bool needSDivMod_ = false;
    bool needVarShift_ = false;

    std::ostringstream code_;
    std::vector<std::string> trailer_;  // global data/leaf-static declarations, emitted after code
    std::map<std::string, std::string> perFileAsm_;

    std::string curFunctionLabel_;
    const ir::Function *curFunction_ = nullptr;
    int labelCounter_ = 0;
    // Every scratch `PUSH` onto the return stack (saving an intermediate value there, as the
    // many "copy via LDS.r, discard via POP" idioms throughout this file do) shifts every
    // framed Local/Param's own `LDS.r`/`STS.r` depth deeper by one cell for as long as it stays
    // outstanding - frameDepth() adds this in. Incremented/decremented in lockstep with every
    // such PUSH/POP via pushScratch()/popScratch() below; never touches the *permanent* frame
    // pushes/pops emitPrologue()/emitEpilogueAndReturn() make (those are already baked into
    // frameDepth()'s own base formula, not a transient adjustment).
    int retAdjust_ = 0;
    void pushScratch() {
        code_ << "    push\n";
        retAdjust_++;
    }
    void popScratch() {
        code_ << "    pop\n";
        retAdjust_--;
    }
    std::vector<std::pair<std::string, std::string>> loopLabels_;  // (continue, break) targets
    std::map<int, std::string> irLabels_;
};

// The vex runtime every whole-program compile shares when it needs it (ensureRuntimeRoutines(),
// selectively) and every object-mode link's entry object defines unconditionally (see
// VexCodegen.cpp's own header comment on "runtime routines", and VexLinkerFactory.cpp): the
// `G___discard`/`G___retval` scratch cells, plus the signed divide/modulo and variable-shift
// helpers.
std::string vexRuntimeAsm();

}  // namespace rcc
