#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "codegen/Codegen.h"
#include "ir/IR.h"

namespace rcc {

// Generates vex assembly (see VexAssembler.h) for the IR built by IRBuilder. Vex
// (/Users/tom/projects/vex) is v1.1: an 8-register (R0-R7), 32-bit register machine - no
// data stack, no stack-relative addressing mode at all; the return stack holds only
// return addresses (CAL pushes, RET pops). This backend therefore looks like every
// other small-register-file target here (see M6809Codegen.h/MipsCodegen.h-style
// backends), not like a stack-machine port: a register holds one live value, the
// software stack (synthesized from LD/ST through a register this backend maintains as
// a stack pointer, since no PUSH/POP opcode exists any more) is the real spill area for
// anything beyond what a handful of named registers can hold live at once.
//
// --- Register convention -------------------------------------------------
//
// R0  - genExpr()'s result register: every expression leaves its value here. Also the
//       function return-value register (the caller reads it right after CAL returns).
// R1  - second-operand scratch: a binary op's right operand lands here (after the left
//       operand, in R0, is protected across evaluating it - see genBinary()'s own
//       comment), and the matching register for a software division/shift routine's
//       first argument (see "Runtime routines" below).
// R2  - a third scratch register: address-of-slot/branch-target/call-target computation
//       (every JMP/JCN/CAL needs its operand already IN a register, so a branch to a
//       label is always `LDI R2, label` then `JMP/JCN/CAL R2`), and whichever operand a
//       given sequence needs a third live register for.
// R3  - a fourth scratch register, free for whichever sequence below needs one more
//       live value at once than R0-R2 cover (building a constant, a save-across-one-
//       call spot, etc).
// R4  - frame pointer (FP): fixed for the lifetime of one function activation, restored
//       by every RET path. See "Stack frame" below.
// R5  - stack pointer (SP): synthesized push()/pop() (below) are the only way anything
//       reaches it; grows downward from kVexStackTop (VexLinkerFactory.cpp's entry
//       sequence sets it once, before the first call to `main`).
// R6, R7 - never touched by ordinary codegen; the four runtime division/shift routines
//       (ensureRuntimeRoutines()) borrow R6 as their own extra branch-target scratch,
//       safely, since they're leaves (no CAL of their own, no stack frame) that nothing
//       else expects R6 to survive across - see that section's own comment.
//
// No register is ever assumed to survive a CAL except via push()/pop() around it -
// every call (a user function or a runtime routine) is free to clobber R0-R3/R6; only
// R4/R5 are callee-saved (every function's own prologue/epilogue preserves them - see
// genFunction()).
//
// --- Storage model ---------------------------------------------------------
//
// `char`/`short`/`int`/`long` are all exactly one 32-bit cell (TypeWidths{4,4,4,4,8,8,4}: 4
// storage *bytes* each, `unitBits=8`) - deliberate: vex's `LD`/`ST` only ever move a full
// 4-byte word (there is no narrower load/store at all), so giving `char`/`short` their
// C-standard narrower width would cost a mask-and-sign-extend sequence on every single load and
// store just to emulate a width the hardware doesn't have. This backend instead keeps every
// scalar a uniform cell. This is visible to a C program in two ways, both documented backend-
// shaped limitations (not bugs) rather than something a fix is pending for:
//   - Overflow: assigning 300 to a `char` does NOT wrap to 44 the way it would on a target with
//     real 8-bit storage - the value stays a full int until something explicitly narrows it
//     (genConvert() below DOES mask+sign-extend on an explicit/implicit narrowing conversion, so
//     a deliberate `(char)x` cast or a declared-narrower assignment still truncates correctly;
//     only a char/short *variable's own storage* never silently wraps on overflow the way the C
//     standard's own `unsigned char` semantics would guarantee).
//   - `sizeof(char)` is 4, not 1: the one C-standard guarantee ("sizeof(char) == 1") this
//     backend can't make, since there genuinely is no sub-cell storage to measure it against.
//     `tests/run_tests.sh`'s own `is_known_unsupported()` excludes the handful of shared tests
//     that check either of these exactly (`types_sizeof.c`, `string_literal.c`).
//
// `long long` (2 cells) is NOT supported - see genWideUnsupported(): a correct 64-bit
// add/sub/compare needs a carry chain, and this machine's ALU reports no carry/overflow out at
// all (`ADD`/`SUB` just wrap silently), so there is no way to detect one; same documented gap as
// Pdp1Codegen.h's own `long`/`long long` cut, drawn one step narrower here since plain `long`
// costs this backend nothing extra. `struct` is NOT supported yet either (rejected with a clear
// compile error) - unrelated to the register redesign, out of scope for this pass.
//
// A Global (scalar or array) gets a label and that many `.word` cells (always 4 bytes each, even
// for a `char`, per the storage model above) - see declareGlobal().
//
// --- Stack frame -------------------------------------------------------------
//
// Every function, recursive or not, gets a uniform FP(R4)/SP(R5)-relative stack frame -
// unlike the old stack-machine backend, there is no leaf/framed split any more. That
// split existed only because the old return stack (the only place to put per-call
// storage) wasn't addressable memory, so a recursive function couldn't have an
// array/struct Local/Param or take `&` of one. The new machine has real registers plus
// ordinary LD/ST-addressable memory, so a conventional stack frame works uniformly, the
// same way M6809Codegen.cpp's own `u-(frameOffset+extraBytes)`/`u+frameOffset+extraBytes`
// already does - this backend reuses IRBuilder's generic `Slot::frameOffset` directly,
// no vex-specific offset math needed.
//
// Prologue:  push FP twice (the first push is a deliberate filler - see below), set FP
//            = SP, then SP -= frameSize to reserve locals.
// Epilogue:  SP = FP (deallocate locals), pop FP's real saved value, pop (discard) the
//            filler, RET.
//
// The filler exists because IRBuilder's `frameOffset` convention (see IR.h's own
// comment on it) assumes a param0 starts 2 native ints below the call - built for a
// machine where CALL pushes the return address onto the *same* stack FP/SP walk, so the
// prologue's one "save old FP" push lands right next to it. Vex's CAL pushes the return
// address onto its own separate return-address stack instead, leaving only 1 int of gap
// (not 2) before this backend's own SP-based stack even starts - so one throwaway
// filler push is simplest, matching the generic formula byte-for-byte with zero
// vex-specific correction, at the cost of one wasted stack word per call.
//
// Calling convention: the caller pushes every argument (right-to-left: the rightmost
// argument first), then loads the callee's address and `CAL`s it; the callee reads them
// back at positive FP-relative offsets, exactly as IRBuilder's own frameOffset already
// expects. Not the register-argument convention docs/SPEC.md section 4 merely suggests
// - that's explicitly optional, and passing everything through memory sidesteps "how
// many argument registers" while reusing the exact same frame machinery every other
// FP-based backend here already has. The return value comes back in R0.
//
// --- Division, modulo, shifts --------------------------------------------
//
// `DIV` is UNSIGNED ONLY (docs/SPEC.md section 3) and there is no shift instruction at all
// ("Not provided: shifts" - section 7), same as before. Unsigned `/`/`%`/`>>` and any
// `<</`>>` by a compile-time constant map directly onto `DIV`/`MUL` by the matching power of
// two. Signed `/`/`%` and a variable-count `<<`/`>>` go through tiny shared runtime routines
// (ensureRuntimeRoutines()) - software long division's usual sign-then-magnitude trick for the
// former, repeated doubling/halving for the latter - emitted once per whole-program compile,
// only if actually used. See VexCodegen.cpp's "runtime routines" section for their register
// convention (R1=a, R0=b, result in R0 - the natural landing spot of this backend's own
// push-left/evaluate-right/pop pattern, needing no register shuffling at any call site).
//
// --- Entry sequence -------------------------------------------------------
//
// A whole-program compile (objectMode == false) emits `_start: LDI R2, main; CAL R2; RET` -
// `main`'s own RET already popped the one return address CAL pushed, so the return stack is
// empty again by the time control reaches that final RET, which is docs/SPEC.md section 6's own
// *normal halt* condition, leaving `main`'s return value sitting untouched in R0. That is
// deliberate, not an oversight: vex has no OS/exit-code culture of its own stronger than "R0
// holds the answer" (see docs/SPEC.md's own calling-convention idiom), and tests/run_tests.sh's
// own check_vex() reads exactly that register back out via the interpreter's own `-d` dump
// flag. A program that wants a specific process exit status still can - DEO to port 0x00
// explicitly, same as any other reference-device write (see src/rt/vex/vex.h).
class VexCodegen : public Backend {
   public:
    std::string generate(const ir::Program &prog, bool objectMode = false) override;
    std::string name() const override { return "vex"; }
    std::string description() const override { return "Vex virtual machine"; }
    // Byte-addressed, `unitBits=8` (matching vex's own real address space), with every scalar
    // 4 storage units (one 32-bit cell) wide regardless of C type - see the class comment's
    // storage model.
    TypeWidths typeWidths() const override { return {4, 4, 4, 4, 8, 8, 4}; }
    std::string sourceExtension() const override { return ".s"; }
    std::vector<std::string> assembledFormats() const override { return {"raw", "coff"}; }
    bool isObjectFormat(const std::string &format) const override { return format == "coff"; }
    std::map<std::string, std::string> perFileAsm() const override;
    std::optional<std::string> assembleInProcess(const std::string &asmText,
                                                  const std::string &format) const override;

   private:
    // --- top-level -----------------------------------------------------
    void genFunction(const ir::FunctionBody &fn);
    void declareGlobal(const ir::Slot &slot);

    void rejectStruct(Type t, int line, const std::string &what);
    [[noreturn]] void genWideUnsupported(int line, const std::string &what);
    bool isWide(Type t) const { return !isPointer(t) && t.base == BaseType::LongLong; }
    int widthOfType(Type t) const { return rcc::widthOf(t, typeWidths()); }

    // --- register helpers -------------------------------------------------
    // `R0`.."R7" text for register `n` - every emitter below goes through this rather than
    // spelling "r" + digit inline, so the register-naming convention lives in one place.
    static std::string reg(int n) { return "r" + std::to_string(n); }
    void ldi(int rd, const std::string &imm) { code_ << "    ldi " << reg(rd) << ", " << imm << "\n"; }
    void ldiNum(int rd, uint32_t v);
    void mov(int rd, int ra) { code_ << "    mov " << reg(rd) << ", " << reg(ra) << "\n"; }
    void ld(int rd, int ra) { code_ << "    ld " << reg(rd) << ", " << reg(ra) << "\n"; }
    void st(int ra, int rb) { code_ << "    st " << reg(ra) << ", " << reg(rb) << "\n"; }
    void alu3(const std::string &op, int rd, int ra, int rb) {
        code_ << "    " << op << " " << reg(rd) << ", " << reg(ra) << ", " << reg(rb) << "\n";
    }
    // Branches/calls always need their target already in a register - every one of these loads
    // `label` into `rtarget` first, so call sites never forget the LDI half.
    void jmpTo(const std::string &label, int rtarget = 2);
    void jcnTo(const std::string &label, int rcond, int rtarget = 2);
    void calTo(const std::string &label, int rtarget = 2);
    // Synthesized push/pop (no PUSH/POP opcode exists any more - see the class comment):
    // `rscratch` (default R3) holds the immediate 4 each needs; never pass the same register as
    // both `rx` and `rscratch` (push(3)/pop(3) use R1 as the scratch instead - the only two
    // call sites that ever save/restore R3 itself).
    void push(int rx, int rscratch = 3);
    void pop(int rx, int rscratch = 3);

    // --- statements/expressions -----------------------------------------
    void genStmt(const ir::Stmt &s);
    void genExpr(const ir::Expr &e);  // leaves the result in R0
    void genBinary(const ir::Expr &e);
    void genCompare(const ir::Expr &e, bool isUnsigned);
    void genCondBranch(const ir::Expr &cond, const std::string &Lfalse);
    void genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs);
    void genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs);
    // Both assume R1=a, R0=b already (the natural landing spot of this file's own
    // push-left/evaluate-right/pop pattern - see genBinary()'s own comment), and leave the
    // result in R0. Shared by every ordinary binary call site and by applyCompoundOp().
    void genShiftOp(const std::string &op, bool isUnsigned);
    void genDivModFromRegs(bool wantRemainder, bool isUnsigned);
    void genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned);
    // Precondition: the compound assignment's current value is in R0. Combines it with
    // `e.rhs` per `e.op`, leaving the new value in R0. Covers every compound operator,
    // including `*=`/`/=`/`%=`/`<<=`/`>>=` which have no single matching opcode (`+=`/`-=`/
    // `&=`/`|=`/`^=` go through binOpInstr() directly).
    void applyCompoundOp(const ir::Expr &e);
    // Precondition: the target address is in R0. Shared by genCompoundAssign's indexed form
    // and genCompoundDerefAssign - see its own comment for the full sequence.
    void genCompoundThroughAddress(const ir::Expr &e);
    void genConvert(const ir::Expr &e);
    void genIndex(const ir::Expr &e);     // leaves the element's address in R0
    void genAddrOf(const ir::Expr &operand);
    void genCall(const ir::Expr &e);
    void genCallPtr(const ir::Expr &e);

    // Slot access.
    void genLoadSlot(const ir::Slot *slot);   // leaves the value in R0
    void genStoreSlot(const ir::Slot *slot);  // value already in R0; R0 left unchanged
    void genSlotAddress(const ir::Slot *slot);  // leaves the slot's own address in R0
    void genAssign(const ir::Expr &e);
    void genCompoundAssign(const ir::Expr &e);
    // Precondition: the target address is in R0. Shared by genAssign's indexed form and
    // genDerefAssign.
    void genAssignThroughAddress(const ir::Expr &e);
    void genDerefAssign(const ir::Expr &e);
    void genCompoundDerefAssign(const ir::Expr &e);

    // --- frame addressing -------------------------------------------------
    // Leaves the address of `slot` (a Local or Param) in `rd` - FP+frameOffset for a Param,
    // FP-frameOffset for a Local, exactly M6809Codegen.cpp's own convention (see the class
    // comment's "Stack frame" section for why no vex-specific correction is needed).
    void genFrameAddress(const ir::Slot *slot, int rd);
    void emitPrologue(const ir::Function *fn);
    void emitEpilogueAndReturn();
    std::string epilogueLabel() const { return curFunctionLabel_ + "_epi"; }

    // --- labels -----------------------------------------------------------
    static std::string label(const ir::Slot *slot) { return "G_" + slot->sourceName; }
    static std::string label(const ir::Function *fn) { return "FN_" + fn->sourceName; }
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
    std::vector<std::string> trailer_;  // global data declarations, emitted after code
    std::map<std::string, std::string> perFileAsm_;

    std::string curFunctionLabel_;
    const ir::Function *curFunction_ = nullptr;
    int labelCounter_ = 0;
    std::vector<std::pair<std::string, std::string>> loopLabels_;  // (continue, break) targets
    std::map<int, std::string> irLabels_;
};

// Every byte-addressable address this backend ever hands SP (R5) at program start -
// VexLinkerFactory.cpp's entry sequence sets it once, before the first CAL to `main`.
// Picked with generous headroom below the 4 GiB address space; vex's own interpreter
// backs the whole space unconditionally, so any value works - this one is simply a
// documented, arbitrary, round choice, the same kind every other flat-memory backend's
// own stack-top constant is.
constexpr uint32_t kVexStackTop = 0x01000000;  // 16 MiB

// The vex runtime every whole-program compile shares when it needs it (ensureRuntimeRoutines(),
// selectively) and every object-mode link's entry object defines unconditionally (see
// VexCodegen.cpp's own header comment on "runtime routines", and VexLinkerFactory.cpp): the
// signed divide/modulo and variable-shift helpers.
std::string vexRuntimeAsm();

}  // namespace rcc
