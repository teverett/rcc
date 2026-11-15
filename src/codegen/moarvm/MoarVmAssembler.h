#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace rcc {

// Encodes the real binary MoarVM bytecode format (header/strings/frames/callsites/
// bytecode - see docs/platforms.md's moarvm section) as MoarVmCodegen walks the IR. There is
// no official textual MoarVM assembly language to parse (unlike every real-ISA backend's
// VaxAssembler-style text assembler), so this class is driven programmatically instead of
// from text: MoarVmCodegen calls its emit*() methods directly in instruction order, one
// frame at a time, and finish() lays out and returns the complete file bytes.
//
// Every local/register is a plain 0-based index into the current frame's locals, typed
// Int64/Str/Obj at declaration (addLocal()); MoarVM has no untyped registers.
class MoarVmAssembler {
   public:
    enum class LocalType { Int64 = 4, Str = 7, Obj = 8 };

    // Interns a string in the shared string heap (entry 0 is always "", required for the
    // header's HLL-name field even though this target has no HLL). Returns its heap index.
    int internString(const std::string &s);

    // Interns a callsite descriptor (one MVM_CALLSITE_ARG_* flag byte per argument, in
    // order - see docs/platforms.md). Returns its callsite-table index.
    static constexpr uint8_t kArgObj = 1;
    static constexpr uint8_t kArgInt = 2;
    int internCallsite(const std::vector<uint8_t> &argFlags);

    // Starts a new frame and returns its 0-based frame index. `name` is purely cosmetic
    // (used for `moar --dump`'s own display; never required to be unique).
    int beginFrame(const std::string &name);
    // Adds one local to the frame currently being built and returns its index.
    int addLocal(LocalType type);
    // Ends the frame currently being built.
    void endFrame();

    // A label is a position in the CURRENT frame's bytecode, fixed up to a real byte offset
    // once every instruction before it has been emitted (placeLabel()) - exactly the
    // two-pass forward-reference scheme every other backend's own assembler uses.
    int newLabel();
    void placeLabel(int labelId);

    void emitCheckarity(int16_t minArgs, int16_t maxArgs);
    void emitConstI64(int dst, int64_t value);
    void emitConstStr(int dst, int strIdx);
    void emitSet(int dst, int src);
    void emitGoto(int labelId);
    void emitIfI(int cond, int labelId);
    void emitUnlessI(int cond, int labelId);
    // opcode is one of the w(int64) r(int64) r(int64) ops (eq_i/ne_i/lt_i/le_i/gt_i/ge_i/
    // add_i/sub_i/mul_i/div_i/mod_i/band_i/bor_i/bxor_i).
    void emitBinOp(int opcode, int dst, int a, int b);
    // opcode is one of the w(int64) r(int64) ops (neg_i/bnot_i/not_i).
    void emitUnOp(int opcode, int dst, int src);
    void emitCoerceIS(int dst, int src);
    void emitParamRpI(int dst, int16_t paramIndex);
    void emitParamRpO(int dst, int16_t paramIndex);
    // `calleeFrameIdx` is a FRAME TABLE index, not a local - see docs/platforms.md.
    void emitGetCode(int dst, int calleeFrameIdx);
    void emitReturnI(int src);
    void emitReturnO(int src);
    void emitReturnVoid();
    void emitSay(int strReg);
    void emitDispatchV(const std::string &dispatcherName, int callsiteIdx, const std::vector<int> &argRegs);
    void emitDispatchI(int dst, const std::string &dispatcherName, int callsiteIdx, const std::vector<int> &argRegs);
    void emitAtposI(int dst, int arrayReg, int indexReg);
    void emitBindposI(int arrayReg, int indexReg, int valueReg);
    void emitAtposO(int dst, int arrayReg, int indexReg);
    void emitBindposO(int arrayReg, int indexReg, int valueReg);
    // `create()`'s instance grows on demand as bindpos_i/bindpos_o writes reach past its
    // current size - MoarVM's VMArray REPR needs no explicit size/dimensions call first.
    void emitCreate(int dst, int typeReg);
    void emitBootIntArray(int dst);
    void emitBootArray(int dst);
    void emitNull(int dst);

    // The frame index that `moar` invokes first; must be set exactly once before finish().
    void setMainFrame(int frameIdx);

    // Lays out and returns the complete binary .moarvm file.
    std::string finish();

   private:
    struct Frame {
        std::string name;
        std::vector<LocalType> locals;
        std::string bytecode;  // this frame's own instruction bytes, byte-addressed from 0
        // Byte offset (within `bytecode`) of a goto/if_i/unless_i's 4-byte `ins` operand,
        // paired with the label it targets - patched once every label in this frame has a
        // known final offset (see endFrame()).
        std::vector<std::pair<size_t, int>> labelFixups;
        std::unordered_map<int, size_t> labelOffsets;
    };

    void write16(std::string &buf, uint16_t v);
    void write32(std::string &buf, uint32_t v);
    void writeOp(uint16_t opcode);  // appends to the frame currently being built
    Frame &cur();

    std::vector<Frame> frames_;
    int currentFrame_ = -1;
    int nextLabelId_ = 0;

    std::vector<std::string> strings_;
    std::unordered_map<std::string, int> stringIndex_;

    std::vector<std::vector<uint8_t>> callsites_;

    int mainFrame_ = -1;
};

}  // namespace rcc
