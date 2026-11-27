#include "I8008Codegen.h"

#include <algorithm>
#include <tuple>
#include <cstdint>
#include <stdexcept>

#include "I8008Assembler.h"
#include "fileformat/FileFormat.h"

namespace rcc {

namespace {
[[noreturn]] void err(int line, const std::string &msg) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + msg);
}

// The 8008's address space tops out at 16KB (14-bit addresses).
constexpr uint32_t kMemTop = 0x4000;
// Fixed memory cells (`ac`/`bc`/.../`fpsp`) are pinned to known addresses via the assembler's `name = expr`
// equates, so the test harness's Python 8008 emulator can read results by fixed address without parsing symbols.
// Anchored below `kMemTop` (not above 0) since everything from here up stays a small fixed size regardless of
// program size, leaving the rest of the address space free for generated code and globals.
constexpr uint32_t kPseudoBase = kMemTop - 0x400;
// `wac`/`wbc`/`wtmp`/`wtmp2`: the wide (`long`/`long long`) value buffers, 8 bytes each, after the other fixed
// pseudo-registers (18 bytes total).
constexpr uint32_t kWideBase = kPseudoBase + 18;
// Capacity of the subexpression-save stack (`sp`): a fixed limit with no overflow check.
[[maybe_unused]] constexpr int kStackSize = 32;
constexpr uint32_t kStackBase = kWideBase + 32;
// Capacity of the frame-pointer chain (`fpsp`): bounded in practice by the 8008's 7-level hardware call-nesting
// limit, with no overflow check of its own.
constexpr int kFpStackCapacity = 16;
constexpr uint32_t kFpStackBase = kStackBase + kStackSize * 2;
// Where the per-call Local/Param frame bump allocator (`fnext`/`fp`) starts claiming words from.
constexpr uint32_t kFrameBase = kFpStackBase + kFpStackCapacity * 2;
}  // namespace

// Pushes `srcCell`'s value onto the stack pointed to by `spCell`: reads `srcCell`'s bytes into B/C first (since
// loading `spCell` into H,L would otherwise clobber `srcCell` mid-read if they're the same cell), then writes them
// at the top of stack and advances `spCell` by one word.
void I8008Codegen::pushCell(const std::string &srcCell, const std::string &spCell) {
    pointAt(srcCell);
    emit("mov", "a, m");
    emit("mov", "b, a");
    pointAt(srcCell + "+1");
    emit("mov", "a, m");
    emit("mov", "c, a");
    loadHLFromCell(spCell);
    emit("mov", "a, b");
    emit("mov", "m, a");
    incHL();
    emit("mov", "a, c");
    emit("mov", "m, a");
    incHL();
    storeHLToCell(spCell);
}

// Retreats `spCell` by one word, then reads the two bytes now at the top of that stack into `dstCell`. Doesn't use
// storeHLToCell() - writing the decremented address back to `spCell` needs pointAt(spCell), which would repoint H,L
// away before the popped word is read - so the decremented address is stashed in D/E and written back afterward.
void I8008Codegen::popCell(const std::string &dstCell, const std::string &spCell) {
    loadHLFromCell(spCell);  // HL = current top-of-stack pointer (one past the last pushed word)
    decHL();
    decHL();  // HL = the address of the word being popped
    emit("mov", "a, l");
    emit("mov", "d, a");  // stash the decremented address in D/E
    emit("mov", "a, h");
    emit("mov", "e, a");
    emit("mov", "a, m");
    emit("mov", "b, a");  // read the popped word while HL still points at it
    incHL();
    emit("mov", "a, m");
    emit("mov", "c, a");
    pointAt(spCell);
    emit("mov", "a, d");
    emit("mov", "m, a");
    pointAt(spCell + "+1");
    emit("mov", "a, e");
    emit("mov", "m, a");
    pointAt(dstCell);
    emit("mov", "a, b");
    emit("mov", "m, a");
    pointAt(dstCell + "+1");
    emit("mov", "a, c");
    emit("mov", "m, a");
}

namespace {
std::string byteAt(const char *base, int i) { return i == 0 ? base : (std::string(base) + "+" + std::to_string(i)); }
}  // namespace

// Byte-granular version of pushCell()/popCell() against the same `sp` stack, for wide values. `sp` stays resident
// in H,L for the whole loop rather than being reloaded per byte; reading `base`'s byte `i` still needs pointAt(),
// which repoints H,L away from the stack-top address, so that address is stashed in D,E across the read.
void I8008Codegen::pushWide(const char *base, int nbytes) {
    loadHLFromCell("sp");
    for (int i = 0; i < nbytes; i++) {
        emit("mov", "a, l");
        emit("mov", "d, a");
        emit("mov", "a, h");
        emit("mov", "e, a");  // d,e = this byte's own stack slot
        loadByte(byteAt(base, i));                                           // a = source byte (clobbers H,L)
        emit("mov", "l, d");
        emit("mov", "h, e");                             // h,l restored to that same slot
        emit("mov", "m, a");
        incHL();
    }
    storeHLToCell("sp");
}

// Mirror of pushWide(): `sp` is dropped by `nbytes` in one addConstToHL() (not `nbytes` decHL() calls) to land on
// this value's first byte, written back as the new top-of-stack, then walked forward one byte at a time.
void I8008Codegen::popWide(const char *base, int nbytes) {
    loadHLFromCell("sp");
    addConstToHL(-nbytes);
    storeHLToCell("sp");   // storeHLToCell() leaves H,L pointing at `sp+1`, not the stored address, so reload it
    loadHLFromCell("sp");
    for (int i = 0; i < nbytes; i++) {
        emit("mov", "a, l");
        emit("mov", "d, a");
        emit("mov", "a, h");
        emit("mov", "e, a");  // d,e = this byte's own stack slot
        emit("mov", "a, m");                                           // a = the byte at that slot
        storeByte(byteAt(base, i));                                         // clobbers H,L
        emit("mov", "l, d");
        emit("mov", "h, e");                             // h,l restored to that same slot
        incHL();
    }
}

void I8008Codegen::copyWide(const char *dst, const char *src, int nbytes) {
    for (int i = 0; i < nbytes; i++) {
        loadByte(byteAt(src, i));
        storeByte(byteAt(dst, i));
    }
}

void I8008Codegen::negateWideBuffer(const char *buf, int nbytes) {
    for (int i = 0; i < nbytes; i++) {
        loadByte(byteAt(buf, i));
        emit("xri", "0xff");
        storeByte(byteAt(buf, i));
    }
    loadByte(byteAt(buf, 0));
    emit("adi", "1");
    storeByte(byteAt(buf, 0));
    for (int i = 1; i < nbytes; i++) {
        loadByte(byteAt(buf, i));
        emit("aci", "0");
        storeByte(byteAt(buf, i));
    }
}

void I8008Codegen::genWideLoad(const ir::Slot *slot, int words) {
    int n = 2 * words;
    for (int i = 0; i < n; i++) {
        pointAtSlot(slot, i);
        emit("mov", "a, m");
        storeByte(byteAt("wac", i));
    }
}

void I8008Codegen::genWideLoadLiteral(long value, int words) {
    auto uv = static_cast<unsigned long>(value);
    int n = 2 * words;
    for (int i = 0; i < n; i++) {
        emit("mvi", cat("a, ", ((uv >> (8 * i)) & 0xFF)));
        storeByte(byteAt("wac", i));
    }
}

void I8008Codegen::genWideStore(const ir::Slot *slot, int words) {
    int n = 2 * words;
    for (int i = 0; i < n; i++) {
        // pointAtSlot() clobbers A internally for a Local/Param, so the value must be loaded before it's called.
        loadByte(byteAt("wac", i));
        emit("mov", "b, a");
        pointAtSlot(slot, i);
        emit("mov", "a, b");
        emit("mov", "m, a");
    }
}

// Each iteration re-derives H,L from the `ptr` cell rather than walking a live H,L across the whole loop, since
// storeByte()/loadByte() on the `wac` side repoint H,L to a fixed address in between; `ptr`'s stored value is
// advanced in memory instead (addConstToCell()), which survives that repointing.
void I8008Codegen::genWideLoadAtPtr(int words) {
    int n = 2 * words;
    for (int i = 0; i < n; i++) {
        loadHLFromCell("ptr");
        emit("mov", "a, m");
        storeByte(byteAt("wac", i));
        if (i + 1 < n) addConstToCell("ptr", 1);
    }
}

void I8008Codegen::genWideStoreAtPtr(int words) {
    int n = 2 * words;
    for (int i = 0; i < n; i++) {
        loadByte(byteAt("wac", i));
        emit("mov", "b, a");
        loadHLFromCell("ptr");
        emit("mov", "a, b");
        emit("mov", "m, a");
        if (i + 1 < n) addConstToCell("ptr", 1);
    }
}

void I8008Codegen::storeHLToCell(const std::string &cell) {
    emit("mov", "a, l");
    emit("mov", "d, a");
    emit("mov", "a, h");
    emit("mov", "e, a");
    pointAt(cell);
    emit("mov", "a, d");
    emit("mov", "m, a");
    pointAt(cell + "+1");
    emit("mov", "a, e");
    emit("mov", "m, a");
}

void I8008Codegen::decHL() {
    std::string Lskip = newLabel();
    emit("mov", "a, l");
    emit("and", "a");  // Z set iff l==0 (about to borrow into h)
    emit("jfz", Lskip);  // l != 0 - no borrow needed
    emit("dcr", "h");
    emitLabel(Lskip);
    emit("dcr", "l");
}

// A literal element's decimal text, or a symbol element's label text (the referenced Slot's/Function's address);
// the assembler's "dw" directive resolves either into a full 16-bit value.
std::string I8008Codegen::arrayElemText(const ir::ArrayInitElem &e) {
    if (!e.isSymbol) return std::to_string(e.value);
    return e.slot ? label(e.slot) : label(e.func);
}

// A `char`-width element is one plain `db` byte; a wide (`long`/`long long`) one is split into plain bytes
// directly (little-endian), since the shared "dw" directive only splits a 16-bit value.
void I8008Codegen::declareArray(const std::string &lbl, const std::vector<ir::ArrayInitElem> &values,
                                 Type elemType) {
    if (values.empty()) return;
    int w = widthOfType(elemType);
    if (w == 2) {
        std::ostringstream line;
        line << lbl << ": dw ";
        for (size_t i = 0; i < values.size(); i++) {
            if (i > 0) line << ", ";
            line << arrayElemText(values[i]);
        }
        trailer_.push_back(line.str());
        return;
    }
    bool first = true;
    auto appendTrailer = [&](const std::string &text) {
        trailer_.push_back((first ? lbl + ": " : "") + text);
        first = false;
    };
    for (const auto &e : values) {
        if (w == 1) {
            appendTrailer("db " + std::to_string(static_cast<int>(static_cast<uint8_t>(e.value))));
        } else {
            auto uv = static_cast<unsigned long>(e.value);
            for (int b = 0; b < w; b++) appendTrailer("db " + std::to_string(static_cast<int>((uv >> (8 * b)) & 0xFF)));
        }
    }
}

void I8008Codegen::pointAt(const std::string &lbl) {
    emit("mvihi", cat("h, ", lbl));
    emit("mvi", cat("l, ", lbl));
}

void I8008Codegen::loadHLFromCell(const std::string &cell) {
    pointAt(cell);
    emit("mov", "a, m");
    emit("mov", "e, a");  // e = low byte of the stored value
    pointAt(cell + "+1");
    emit("mov", "a, m");
    emit("mov", "d, a");  // d = high byte of the stored value
    emit("mov", "l, e");
    emit("mov", "h, d");  // HL = (d,e) - the value itself, not cell's own address
}

void I8008Codegen::storeImmediate16(const std::string &dst, const std::string &exprText) {
    pointAt(dst);
    emit("mvi", cat("a, ", exprText));
    emit("mov", "m, a");
    pointAt(dst + "+1");
    emit("mvihi", cat("a, ", exprText));
    emit("mov", "m, a");
}

void I8008Codegen::storeAcIndirect(const std::string &addrCell) {
    pointAt("ac");
    emit("mov", "a, m");
    emit("mov", "b, a");  // b = ac's low byte
    pointAt("ac+1");
    emit("mov", "a, m");
    emit("mov", "c, a");  // c = ac's high byte
    loadHLFromCell(addrCell);                  // HL = the real target address
    emit("mov", "a, b");
    emit("mov", "m, a");
    incHL();
    emit("mov", "a, c");
    emit("mov", "m, a");
}

// The 8008 has no 16-bit increment instruction, so this chains two 8-bit INRs through a carry test.
void I8008Codegen::incHL() {
    std::string Lskip = newLabel();
    emit("inr", "l");
    emit("mov", "a, l");
    emit("and", "a");  // Z set iff l wrapped to 0
    emit("jfz", Lskip);  // didn't wrap - skip the high-byte carry
    emit("inr", "h");
    emitLabel(Lskip);
}

// Copies one 16-bit memory cell to another via four real memory accesses through B/C, since H,L can't address
// both src and dst at once.
void I8008Codegen::copy16(const std::string &dst, const std::string &src) {
    pointAt(src);
    emit("mov", "a, m");
    emit("mov", "b, a");
    pointAt(src + "+1");
    emit("mov", "a, m");
    emit("mov", "c, a");
    pointAt(dst);
    emit("mov", "a, b");
    emit("mov", "m, a");
    pointAt(dst + "+1");
    emit("mov", "a, c");
    emit("mov", "m, a");
}

int I8008Codegen::frameByteOffset(const ir::Slot *slot) const {
    if (slot->kind == ir::SlotKind::Param) {
        return slot->frameOffset - 2 * widthOfType(Type{BaseType::Int, false});
    }
    return currentParamBytes_ + slot->frameOffset - widthOfType(Type{BaseType::Int, false});
}

void I8008Codegen::loadByte(const std::string &cell) {
    pointAt(cell);
    emit("mov", "a, m");
}

void I8008Codegen::storeByte(const std::string &cell) {
    pointAt(cell);
    emit("mov", "m, a");
}

void I8008Codegen::emitSignExtendByte(const std::string &valueCell, const std::string &highDestCell,
                                       bool isUnsigned) {
    if (isUnsigned) {
        emit("mvi", "a, 0");
        storeByte(highDestCell);
        return;
    }
    loadByte(valueCell);
    emit("and", "a");
    emit("ral");  // carry = valueCell's own top (sign) bit
    std::string Lneg = newLabel(), Ldone = newLabel();
    emit("jtc", Lneg);
    emit("mvi", "a, 0");
    storeByte(highDestCell);
    emit("jmp", Ldone);
    emitLabel(Lneg);
    emit("mvi", "a, 0xff");
    storeByte(highDestCell);
    emitLabel(Ldone);
}

// Adds `byteOffset` to H,L: low byte via ADI (the carry source), high byte via ACI to absorb it.
void I8008Codegen::addConstToHL(int byteOffset) {
    int lo = byteOffset & 0xFF;
    int hi = (byteOffset >> 8) & 0xFF;
    emit("mov", "a, l");
    emit("adi", lo);
    emit("mov", "l, a");
    emit("mov", "a, h");
    emit("aci", hi);
    emit("mov", "h, a");
}

// Same ADI/ACI carry chain as addConstToHL(), against a named cell instead of H,L (no ALU instruction can add an
// immediate directly to a memory destination here).
void I8008Codegen::addConstToCell(const std::string &cell, int amount) {
    int lo = amount & 0xFF;
    int hi = (amount >> 8) & 0xFF;
    pointAt(cell);
    emit("mov", "a, m");
    emit("adi", lo);
    emit("mov", "m, a");
    pointAt(cell + "+1");
    emit("mov", "a, m");
    emit("aci", hi);
    emit("mov", "m, a");
}

void I8008Codegen::pointAtSlot(const ir::Slot *slot, int extraByteOffset) {
    if (slot->kind == ir::SlotKind::Global) {
        pointAt(extraByteOffset == 0 ? label(slot) : (label(slot) + "+" + std::to_string(extraByteOffset)));
        return;
    }
    loadHLFromCell("fp");
    addConstToHL(frameByteOffset(slot) + extraByteOffset);
}

void I8008Codegen::pointAtSlotNextByte(const ir::Slot *slot, int justPointedOffset) {
    if (slot->kind == ir::SlotKind::Global) {
        pointAtSlot(slot, justPointedOffset + 1);
        return;
    }
    incHL();
}

void I8008Codegen::copy16FromSlot(const std::string &dst, const ir::Slot *slot) {
    pointAtSlot(slot, 0);
    emit("mov", "a, m");
    emit("mov", "b, a");
    pointAtSlotNextByte(slot, 0);
    emit("mov", "a, m");
    emit("mov", "c, a");
    pointAt(dst);
    emit("mov", "a, b");
    emit("mov", "m, a");
    pointAt(dst + "+1");
    emit("mov", "a, c");
    emit("mov", "m, a");
}

void I8008Codegen::copy16ToSlot(const ir::Slot *slot, const std::string &src) {
    pointAt(src);
    emit("mov", "a, m");
    emit("mov", "b, a");
    pointAt(src + "+1");
    emit("mov", "a, m");
    emit("mov", "c, a");
    pointAtSlot(slot, 0);
    emit("mov", "a, b");
    emit("mov", "m, a");
    pointAtSlotNextByte(slot, 0);
    emit("mov", "a, c");
    emit("mov", "m, a");
}

void I8008Codegen::storeImmediate16ToSlot(const ir::Slot *slot, int elemOffset, const std::string &exprText) {
    pointAtSlot(slot, elemOffset);
    emit("mvi", cat("a, ", exprText));
    emit("mov", "m, a");
    pointAtSlotNextByte(slot, elemOffset);
    emit("mvihi", cat("a, ", exprText));
    emit("mov", "m, a");
}

// Same body as popCell(), with the final write-back aimed at pointAtSlot() instead of a plain cell's pointAt().
void I8008Codegen::popIntoSlot(const ir::Slot *slot) {
    int w = std::max(widthOfType(slot->type), 2);
    if (w <= 2) {
        loadHLFromCell("sp");
        decHL();
        decHL();
        emit("mov", "a, l");
        emit("mov", "d, a");
        emit("mov", "a, h");
        emit("mov", "e, a");
        emit("mov", "a, m");
        emit("mov", "b, a");
        incHL();
        emit("mov", "a, m");
        emit("mov", "c, a");
        pointAt("sp");
        emit("mov", "a, d");
        emit("mov", "m, a");
        pointAt("sp+1");
        emit("mov", "a, e");
        emit("mov", "m, a");
        pointAtSlot(slot, 0);
        emit("mov", "a, b");
        emit("mov", "m, a");
        pointAtSlotNextByte(slot, 0);
        emit("mov", "a, c");
        emit("mov", "m, a");
        return;
    }
    // A wide param: popped one byte at a time, highest byte first, matching the `sp` stack's LIFO order against
    // how the caller's Call case pushed it.
    for (int i = w - 1; i >= 0; i--) {
        loadHLFromCell("sp");
        decHL();
        emit("mov", "a, l");
        emit("mov", "d, a");
        emit("mov", "a, h");
        emit("mov", "e, a");
        emit("mov", "a, m");
        emit("mov", "b, a");
        pointAt("sp");
        emit("mov", "a, d");
        emit("mov", "m, a");
        pointAt("sp+1");
        emit("mov", "a, e");
        emit("mov", "m, a");
        pointAtSlot(slot, i);
        emit("mov", "a, b");
        emit("mov", "m, a");
    }
}

// Gives this call's frame back to the bump allocator and restores the caller's `fp`.
void I8008Codegen::emitEpilogue() {
    copy16("fnext", "fp");
    popCell("fp", "fpsp");
}

// One in-place 1-bit rotate of 16-bit cell `lbl` through the accumulator (RLC/RRC/RAL/RAR work on A only).
// `and a` clears carry without disturbing A (every logical instruction clears CY as a side effect).
void I8008Codegen::shiftOneBit(const std::string &lbl, bool isLeft) {
    if (isLeft) {
        pointAt(lbl);
        emit("mov", "a, m");
        emit("and", "a");
        emit("ral");
        emit("mov", "m, a");
        pointAt(lbl + "+1");
        emit("mov", "a, m");
        emit("ral");
        emit("mov", "m, a");
    } else {
        // High byte first, so its vacated-bit carry-out feeds the low byte's rotate.
        pointAt(lbl + "+1");
        emit("mov", "a, m");
        emit("and", "a");
        emit("rar");
        emit("mov", "m, a");
        pointAt(lbl);
        emit("mov", "a, m");
        emit("rar");
        emit("mov", "m, a");
    }
}

std::string I8008Codegen::generate(const ir::Program &prog, bool objectMode) {
    const ir::Function *mainFn = nullptr;
    for (const auto &fn : prog.functions) {
        if (fn->sourceName == "main") mainFn = fn.get();
    }
    // In object mode, `prog` may not define `main`; no object ever emits the entry sequence itself -
    // I8008LinkerFactory.cpp synthesizes it once every object is merged and `main`'s linked address is known.
    emitRaw("org 0");
    emitRaw("");
    // Fixed memory pseudo-registers, declared as compile-time equates - the same constant in every object, so no
    // `objectMode` special-casing is needed.
    emitEquate("ac", kPseudoBase);
    emitEquate("bc", (kPseudoBase + 2));
    emitEquate("cx", (kPseudoBase + 4));
    emitEquate("dx", (kPseudoBase + 6));
    emitEquate("ptr", (kPseudoBase + 8));
    emitEquate("sp", (kPseudoBase + 10));
    // `fp` needs no initial value here - the first function entered (`main`) overwrites it from `fnext` before
    // reading it.
    emitEquate("fp", (kPseudoBase + 12));
    emitEquate("fnext", (kPseudoBase + 14));
    emitEquate("fpsp", (kPseudoBase + 16));
    emitEquate("wac", kWideBase);
    emitEquate("wbc", (kWideBase + 8));
    emitEquate("wtmp", (kWideBase + 16));
    emitEquate("wtmp2", (kWideBase + 24));
    if (!objectMode) {
        emitRaw("");
        emitLabel("_start");
        storeImmediate16("sp", std::to_string(kStackBase));
        storeImmediate16("fpsp", std::to_string(kFpStackBase));
        storeImmediate16("fnext", std::to_string(kFrameBase));
        emit("cal", label(mainFn));
        // The program's final result sits in `ac` once execution reaches `halt`.
        emitLabel("halt");
        emit("jmp", "halt");
        emitRaw("");
    }

    // --dump-asm wants one file per input, not one combined dump - see
    // Pdp7Codegen.cpp's own generate() for the pattern this mirrors
    // (tellp()-position ranges into code_, index ranges into trailer_,
    // sliced into perFileAsm_ at the end).
    std::vector<std::tuple<std::string, size_t, size_t>> codeRanges;
    std::vector<std::tuple<std::string, size_t, size_t>> trailerRanges;

    for (const auto &fb : prog.functionBodies) {
        size_t fCodeStart = static_cast<size_t>(code_.tellp());
        genFunction(fb);
        codeRanges.push_back({fb.func->sourceFile, fCodeStart, static_cast<size_t>(code_.tellp())});
    }

    // Shared subroutines are only ever reached via `cal`, never fallen into, so it's safe to place them after
    // every real function's code.
    for (const std::string &body : wideDivModSubroutineBodies_) emitVerbatim(body);
    for (const std::string &body : wideEqualitySubroutineBodies_) emitVerbatim(body);

    for (const auto &slot : prog.slots) {
        if (slot->kind != ir::SlotKind::Global) continue;
        // An external global is only referenced by name, letting the assembler's undefined-symbol handling
        // defer it to a relocation.
        if (slot->isExternal) continue;
        if (debugMapEnabled_) debugSymbols_.push_back({label(slot.get()), /*isFunc=*/false});
        size_t gTrailerStart = trailer_.size();
        const std::string lbl = label(slot.get());
        if (slot->arraySize > 0) {
            declareArray(lbl, slot->arrayInit, slot->type);
        } else {
            long v = slot->hasInit ? slot->initValue : 0;
            auto uv = static_cast<unsigned long>(v);
            int w = widthOfType(slot->type);
            trailer_.push_back(lbl + ": db " + std::to_string(uv & 0xFF) + "  ; " + slot->sourceName);
            for (int i = 1; i < w; i++) trailer_.push_back("db " + std::to_string((uv >> (8 * i)) & 0xFF));
        }
        trailerRanges.push_back({slot->sourceFile, gTrailerStart, trailer_.size()});
    }

    std::string codeStr = code_.str();
    perFileAsm_.clear();
    for (const auto &[file, s, e] : codeRanges) perFileAsm_[file] += codeStr.substr(s, e - s);
    for (const auto &[file, s, e] : trailerRanges) {
        for (size_t i = s; i < e; i++) perFileAsm_[file] += trailer_[i] + "\n";
    }

    std::ostringstream out;
    out << code_.str();
    for (const auto &line : trailer_) out << line << "\n";
    return out.str();
}

std::map<std::string, std::string> I8008Codegen::perFileAsm() const { return perFileAsm_; }

// Prologue: saves the caller's `fp` onto `fpsp`, takes `fnext` as this call's frame base, bumps `fnext` past
// `params.size() + frameSize`, then pops each argument off `sp` (last pushed, first popped - params walked back
// to front) into the fresh frame.
void I8008Codegen::genFunction(const ir::FunctionBody &fn) {
    lastDebugLine_ = 0;
    if (debugMapEnabled_) debugSymbols_.push_back({label(fn.func), /*isFunc=*/true});
    currentParamCount_ = static_cast<int>(fn.func->params.size());
    currentParamBytes_ = 0;
    for (const ir::Slot *p : fn.func->params) currentParamBytes_ += std::max(widthOfType(p->type), 2);
    const int totalBytes = currentParamBytes_ + fn.func->frameSize;
    emitLabel(label(fn.func));
    pushCell("fp", "fpsp");
    copy16("fp", "fnext");
    if (totalBytes > 0) addConstToCell("fnext", totalBytes);
    for (int i = currentParamCount_ - 1; i >= 0; i--) popIntoSlot(fn.func->params[static_cast<size_t>(i)]);
    currentEpilogueLabel_ = newLabel();
    genStmt(*fn.body);
    emitLabel(currentEpilogueLabel_);
    emitEpilogue();
    emit("ret");
    emitRaw("");
}

void I8008Codegen::genStmt(const ir::Stmt &s) {
    emitSourceLine(s.line);
    switch (s.kind) {
        case ir::StmtKind::Block: {
            for (const auto &child : s.body) genStmt(*child);
            return;
        }
        case ir::StmtKind::If: {
            genExpr(*s.cond);
            testTruthy(*s.cond);
            std::string Lelse = s.elseStmt ? newLabel() : "";
            std::string Ldone = newLabel();
            emit("jtz", (s.elseStmt ? Lelse : Ldone));
            genStmt(*s.thenStmt);
            if (s.elseStmt) {
                emit("jmp", Ldone);
                emitLabel(Lelse);
                genStmt(*s.elseStmt);
            }
            emitLabel(Ldone);
            return;
        }
        case ir::StmtKind::While: {
            std::string Lstart = newLabel(), Lend = newLabel();
            std::string Lcont = s.postStmt ? newLabel() : Lstart;
            emitLabel(Lstart);
            genExpr(*s.cond);
            testTruthy(*s.cond);
            emit("jtz", Lend);
            loopLabels_.push_back({Lcont, Lend});
            genStmt(*s.loopBody);
            loopLabels_.pop_back();
            if (s.postStmt) {
                emitLabel(Lcont);
                genStmt(*s.postStmt);
            }
            emit("jmp", Lstart);
            emitLabel(Lend);
            return;
        }
        case ir::StmtKind::Break: {
            emit("jmp", loopLabels_.back().second);
            return;
        }
        case ir::StmtKind::Continue: {
            emit("jmp", loopLabels_.back().first);
            return;
        }
        case ir::StmtKind::Return: {
            if (s.initOrReturnOrExpr) genExpr(*s.initOrReturnOrExpr);
            emit("jmp", currentEpilogueLabel_);
            return;
        }
        case ir::StmtKind::LocalDecl: {
            // A Local array's initializer is written at runtime every time this declaration is reached (unlike a
            // Global array's, baked into the binary once), since a Local's storage is this call's own frame.
            if (s.declSlot->arraySize > 0) {
                int w = widthOfType(s.declSlot->type);
                for (size_t i = 0; i < s.declSlot->arrayInit.size(); i++) {
                    const ir::ArrayInitElem &elem = s.declSlot->arrayInit[i];
                    if (w <= 2) {
                        storeImmediate16ToSlot(s.declSlot, w * static_cast<int>(i), arrayElemText(elem));
                    } else {
                        // A wide element's literal is never a symbol - split directly into `w` plain bytes.
                        auto uv = static_cast<unsigned long>(elem.value);
                        for (int b = 0; b < w; b++) {
                            pointAtSlot(s.declSlot, w * static_cast<int>(i) + b);
                            emit("mvi", cat("a, ", ((uv >> (8 * b)) & 0xFF)));
                            emit("mov", "m, a");
                        }
                    }
                }
            } else if (s.initOrReturnOrExpr) {
                genExpr(*s.initOrReturnOrExpr);
                if (isWide(s.declSlot->type)) {
                    genWideStore(s.declSlot, wordCountOf(s.declSlot->type));
                } else if (widthOfType(s.declSlot->type) == 1) {
                    loadByte("ac");
                    emit("mov", "b, a");
                    pointAtSlot(s.declSlot, 0);
                    emit("mov", "a, b");
                    emit("mov", "m, a");
                } else {
                    copy16ToSlot(s.declSlot, "ac");
                }
            }
            return;
        }
        case ir::StmtKind::ExprStmt: {
            genExpr(*s.initOrReturnOrExpr);
            return;
        }
        case ir::StmtKind::Empty:
            return;
        case ir::StmtKind::Asm: {
            emitVerbatim(s.asmText);
            return;
        }
        case ir::StmtKind::Label: {
            emitLabel(irLabel(s.labelId));
            return;
        }
        case ir::StmtKind::Goto: {
            emit("jmp", irLabel(s.labelId));
            return;
        }
    }
}

void I8008Codegen::genExpr(const ir::Expr &e) {
    switch (e.kind) {
        case ir::ExprKind::FloatLit:
            err(e.line, "float/double is not supported on this target (floatSupport() == None)");
        case ir::ExprKind::IntLit: {
            if (isWide(e.type)) {
                genWideLoadLiteral(e.intValue, wordCountOf(e.type));
                return;
            }
            storeImmediate16("ac", std::to_string(static_cast<uint16_t>(e.intValue)));
            return;
        }
        case ir::ExprKind::Var: {
            if (isWide(e.slot->type)) {
                genWideLoad(e.slot, wordCountOf(e.slot->type));
                return;
            }
            if (widthOfType(e.slot->type) == 1) {
                pointAtSlot(e.slot, 0);
                emit("mov", "a, m");
                storeByte("ac");
                emitSignExtendByte("ac", "ac+1", e.slot->type.isUnsigned);
                return;
            }
            copy16FromSlot("ac", e.slot);
            return;
        }
        case ir::ExprKind::Index: {
            genIndex(e);
            return;
        }
        case ir::ExprKind::Assign: {
            if (e.lhs) {  // slot[idx] = rhs
                genIndexAddr(e.slot, *e.lhs);  // ptr = target address
                copy16("ac", "ptr");
                pushAc();  // push ptr's own value, carried via ac
                genExpr(*e.rhs);
                popInto("ptr");  // ac is untouched by popInto() - it still holds rhs's value
                storeOperandAtPtr(e.slot->type);
                return;
            }
            genExpr(*e.rhs);
            if (isWide(e.slot->type)) {
                genWideStore(e.slot, wordCountOf(e.slot->type));
            } else if (widthOfType(e.slot->type) == 1) {
                // pointAtSlot() clobbers A internally for a Local/Param (it computes `fp + offset` through it).
                loadByte("ac");
                emit("mov", "b, a");
                pointAtSlot(e.slot, 0);
                emit("mov", "a, b");
                emit("mov", "m, a");
            } else {
                copy16ToSlot(e.slot, "ac");
            }
            return;
        }
        case ir::ExprKind::DerefAssign: {
            // Stores at e.type's width (the pointee type, or plain native-word width for untyped-int-as-address);
            // `ptr` is set directly from the pointer value instead of via genIndexAddr().
            genExpr(*e.lhs);  // ac = pointer value
            copy16("ptr", "ac");
            pushAc();  // ac already equals the pointer value - push it directly
            genExpr(*e.rhs);
            popInto("ptr");  // ac/wac is untouched by popInto() - it still holds rhs's value
            storeOperandAtPtr(e.type);
            return;
        }
        case ir::ExprKind::CompoundAssign: {
            if (e.lhs) {  // slot[idx] op= rhs (including slot[idx]++/--)
                genIndexAddr(e.slot, *e.lhs);  // ptr = target address
                genCompoundThroughPtr(e);
                return;
            }
            // A plain scalar target's storage is reconstructible at zero cost after rhs evaluates (fp is restored
            // by any nested call's epilogue), so nothing needs holding across rhs's evaluation here.
            genExpr(*e.rhs);     // ac = rhs
            copy16("bc", "ac");  // bc = rhs
            if (widthOfType(e.slot->type) == 1) {
                pointAtSlot(e.slot, 0);
                emit("mov", "a, m");
                storeByte("ac");
                emitSignExtendByte("ac", "ac+1", e.slot->type.isUnsigned);
            } else {
                copy16FromSlot("ac", e.slot);  // ac = old
            }
            if (e.yieldOld) pushAc();
            genCompoundOp(e.op, e.type);
            narrowAcTo(e.type);
            if (widthOfType(e.slot->type) == 1) {
                loadByte("ac");
                emit("mov", "b, a");
                pointAtSlot(e.slot, 0);
                emit("mov", "a, b");
                emit("mov", "m, a");
            } else {
                copy16ToSlot(e.slot, "ac");
            }
            if (e.yieldOld) popInto("ac");
            return;
        }
        case ir::ExprKind::CompoundDerefAssign: {
            genExpr(*e.lhs);     // ac = pointer value
            copy16("ptr", "ac");  // ptr = address
            genCompoundThroughPtr(e);
            return;
        }
        case ir::ExprKind::Ternary: {
            genExpr(*e.lhs);
            testTruthy(*e.lhs);
            std::string Lelse = newLabel();
            std::string Ldone = newLabel();
            emit("jtz", Lelse);
            genExpr(*e.rhs);
            emit("jmp", Ldone);
            emitLabel(Lelse);
            genExpr(*e.elseExpr);
            emitLabel(Ldone);
            return;
        }
        case ir::ExprKind::Unary: {
            bool wide = isWide(e.rhs->type);
            if (e.op == "-") {
                genExpr(*e.rhs);
                if (wide) {
                    negateWideBuffer("wac", 2 * wordCountOf(e.rhs->type));
                } else {
                    pointAt("ac");
                    emit("mov", "a, m");
                    emit("xri", "0xff");
                    emit("mov", "m, a");
                    pointAt("ac+1");
                    emit("mov", "a, m");
                    emit("xri", "0xff");
                    emit("mov", "m, a");
                    pointAt("ac");
                    emit("mov", "a, m");
                    emit("adi", "1");
                    emit("mov", "m, a");
                    pointAt("ac+1");
                    emit("mov", "a, m");
                    emit("aci", "0");
                    emit("mov", "m, a");
                }
            } else if (e.op == "!") {
                genLogicalNot(*e.rhs);
            } else if (e.op == "*") {
                // Loads at e.type's width, `ptr` set directly from the pointer value instead of genIndexAddr().
                genExpr(*e.rhs);  // ac = pointer value
                copy16("ptr", "ac");
                loadOperandAtPtr(e.type);
            } else if (e.op == "~") {
                genExpr(*e.rhs);
                if (wide) {
                    int n = 2 * wordCountOf(e.rhs->type);
                    for (int i = 0; i < n; i++) {
                        loadByte(byteAt("wac", i));
                        emit("xri", "0xff");
                        storeByte(byteAt("wac", i));
                    }
                } else {
                    pointAt("ac");
                    emit("mov", "a, m");
                    emit("xri", "0xff");
                    emit("mov", "m, a");
                    pointAt("ac+1");
                    emit("mov", "a, m");
                    emit("xri", "0xff");
                    emit("mov", "m, a");
                }
            } else {  // "&"
                genAddrOf(*e.rhs);
            }
            return;
        }
        case ir::ExprKind::Call: {
            // Each argument is pushed onto `sp` in argument order rather than through a cell named after the
            // callee's param, since a shared cell would be clobbered by direct recursion before `cal` even runs.
            // The callee's prologue pops them in reverse into its fresh frame. A wide argument pushes its real
            // width in bytes; a narrow one always pushes 2 bytes via pushAc() (already promoted to `int`).
            for (size_t i = 0; i < e.args.size(); i++) {
                genExpr(*e.args[i]);
                int w = std::max(widthOfType(e.args[i]->type), 2);
                if (w > 2) {
                    pushWide("wac", w);
                } else {
                    pushAc();
                }
            }
            emit("cal", label(e.func));
            return;
        }
        case ir::ExprKind::CallPtr: {
            // The 8008 has no indirect call/jump (every CAL/JMP operand is a fixed address baked in at assemble
            // time, unlike the 8080/Z80's PCHL/JP (HL)), so a computed call is emulated with self-modifying code:
            // the placeholder `cal 0` below has its own operand bytes (Lcal+1/+2) overwritten with the callee's
            // address, computed into `ac`, immediately before the CPU falls through and executes it as an
            // ordinary CAL. Safe here since this backend has no interrupts or concurrency to race the patch.
            genExpr(*e.lhs);  // ac = the callee's own entry address
            std::string Lcal = newLabel();
            pointAt("ac");
            emit("mov", "a, m");
            pointAt(Lcal + "+1");
            emit("mov", "m, a");
            pointAt("ac+1");
            emit("mov", "a, m");
            pointAt(Lcal + "+2");
            emit("mov", "m, a");
            emitRaw(cat(Lcal, ": cal 0"));
            return;
        }
        case ir::ExprKind::Binary: {
            genBinary(e);
            return;
        }
        case ir::ExprKind::Intrinsic: {
            // The 8008 has no interrupt enable/disable instruction (interrupts work via an external signal
            // forcing an instruction onto the bus), so ion()/iof() are no-ops.
            (void)e;
            return;
        }
        case ir::ExprKind::Convert: {
            genConvert(e);
            return;
        }
        case ir::ExprKind::Func:
            err(e.line, "internal: Func reached genExpr - should only ever appear as '&'s operand");
    }
}

// Leaves slot[idxExpr]'s effective address in `ptr`. `idxExpr` is evaluated first and alone. The base is the
// array's first word for `slot->arraySize > 0` (a Global's fixed label, or `fp` plus a runtime offset for a
// Local); otherwise `slot` is a plain pointer variable and the base comes from reading its stored value. No
// hardware 16-bit add exists, so the doubled index and base are added byte-wise with carry chained low into
// high. `fp` is the start, not the middle, of each frame, so every Local/Param offset here is non-negative.
void I8008Codegen::genIndexAddr(const ir::Slot *slot, const ir::Expr &idxExpr) {
    genExpr(idxExpr);  // ac = index
    // ptr = index * widthOf(slot->type), via repeated doubling (elemShift()) when the width
    // is a power of two, or the shared software multiply routine (genMulCore(): bc * dx ->
    // ac) otherwise - a struct's width isn't always a power of two.
    int shift = elemShift(slot->type);
    if (shift < 0) {
        copy16("bc", "ac");
        storeImmediate16("dx", std::to_string(widthOfType(slot->type)));
        genMulCore();
        copy16("ptr", "ac");
    } else {
        copy16("ptr", "ac");
        for (int s = 0; s < shift; s++) {
            pointAt("ptr");
            emit("mov", "a, m");
            emit("mov", "b, a");  // b = ptr_lo
            pointAt("ptr+1");
            emit("mov", "a, m");
            emit("mov", "c, a");  // c = ptr_hi
            emit("mov", "a, b");
            emit("add", "b");
            emit("mov", "d, a");  // d = ptr_lo + ptr_lo
            emit("mov", "a, c");
            emit("adc", "c");
            emit("mov", "e, a");  // e = ptr_hi + ptr_hi + carry
            pointAt("ptr");
            emit("mov", "a, d");
            emit("mov", "m, a");
            pointAt("ptr+1");
            emit("mov", "a, e");
            emit("mov", "m, a");
        }
    }

    if (slot->arraySize > 0 && slot->kind == ir::SlotKind::Global) {
        // A Global array's address is a compile-time constant, loaded directly into B,C.
        std::string lbl = label(slot);
        emit("mvi", cat("b, ", lbl));     // b = low byte of the array's own address
        emit("mvihi", cat("c, ", lbl));    // c = high byte
    } else if (slot->arraySize > 0) {
        // A Local array's address is computed into H,L via pointAtSlot(), then copied into B,C.
        pointAtSlot(slot, 0);
        emit("mov", "a, l");
        emit("mov", "b, a");
        emit("mov", "a, h");
        emit("mov", "c, a");
    } else {
        // `slot` is a plain pointer variable - the base comes from reading its stored value.
        pointAtSlot(slot, 0);
        emit("mov", "a, m");
        emit("mov", "b, a");
        pointAtSlotNextByte(slot, 0);
        emit("mov", "a, m");
        emit("mov", "c, a");
    }
    pointAt("ptr");
    emit("mov", "a, m");
    emit("add", "b");
    emit("mov", "d, a");
    pointAt("ptr+1");
    emit("mov", "a, m");
    emit("adc", "c");
    emit("mov", "e, a");
    pointAt("ptr");
    emit("mov", "a, d");
    emit("mov", "m, a");
    pointAt("ptr+1");
    emit("mov", "a, e");
    emit("mov", "m, a");
}

void I8008Codegen::genIndex(const ir::Expr &e) {
    genIndexAddr(e.slot, *e.lhs);
    loadOperandAtPtr(e.slot->type);
}

void I8008Codegen::loadOperandAtPtr(Type type) {
    int w = widthOfType(type);
    if (w > 2) {
        genWideLoadAtPtr(wordCountOf(type));
        return;
    }
    if (w == 1) {
        loadHLFromCell("ptr");
        emit("mov", "a, m");
        storeByte("ac");
        emitSignExtendByte("ac", "ac+1", type.isUnsigned);
        return;
    }
    loadHLFromCell("ptr");  // HL = the computed address itself
    emit("mov", "a, m");
    emit("mov", "b, a");
    incHL();
    emit("mov", "a, m");
    emit("mov", "c, a");
    pointAt("ac");
    emit("mov", "a, b");
    emit("mov", "m, a");
    pointAt("ac+1");
    emit("mov", "a, c");
    emit("mov", "m, a");
}

void I8008Codegen::storeOperandAtPtr(Type type) {
    int w = widthOfType(type);
    if (w > 2) {
        genWideStoreAtPtr(wordCountOf(type));
        return;
    }
    if (w == 1) {
        // loadHLFromCell() clobbers A internally, so the value to store must survive in B across that call.
        loadByte("ac");
        emit("mov", "b, a");
        loadHLFromCell("ptr");
        emit("mov", "a, b");
        emit("mov", "m, a");
        return;
    }
    storeAcIndirect("ptr");
}

void I8008Codegen::narrowAcTo(Type to) {
    if (widthOfType(to) != 1) return;  // a native word: ac already holds the full value
    emitSignExtendByte("ac", "ac+1", to.isUnsigned);
}

// `op` applied to ac=old, bc=rhs, leaving the raw new value in ac; `old` plays genBinary()'s lhs role, `rhs` its
// rhs role, for non-commutative operators.
void I8008Codegen::genCompoundOp(const std::string &op, Type type) {
    bool isUnsigned = type.isUnsigned;
    if (op == "+" || op == "-") {
        const char *lo = op == "+" ? "add" : "sub";
        const char *hi = op == "+" ? "adc" : "sbb";
        pointAt("ac");
        emit("mov", "a, m");  // a = old_lo
        pointAt("bc");
        emit(lo, "m");  // a = old_lo (+/-) rhs_lo, CY set accordingly
        emit("mov", "b, a");
        pointAt("ac+1");
        emit("mov", "a, m");  // a = old_hi
        pointAt("bc+1");
        emit(hi, "m");  // a = old_hi (+/-) rhs_hi (+/-) carry
        emit("mov", "c, a");
        pointAt("ac");
        emit("mov", "a, b");
        emit("mov", "m, a");
        pointAt("ac+1");
        emit("mov", "a, c");
        emit("mov", "m, a");
        return;
    }
    if (op == "*") {
        copy16("dx", "ac");  // dx = multiplier = old (bc already = rhs = multiplicand)
        genMulCore();
        return;
    }
    if (op == "/" || op == "%") {
        copy16("dx", "ac");  // dx = dividend = old (bc already = rhs = divisor)
        genDivModCore(op == "%", isUnsigned);
        return;
    }
    if (op == "&" || op == "|" || op == "^") {
        const char *insn = (op == "&") ? "and" : (op == "|") ? "or" : "xor";
        pointAt("ac");
        emit("mov", "a, m");
        pointAt("bc");
        emit(insn, "m");
        emit("mov", "b, a");
        pointAt("ac+1");
        emit("mov", "a, m");
        pointAt("bc+1");
        emit(insn, "m");
        emit("mov", "c, a");
        pointAt("ac");
        emit("mov", "a, b");
        emit("mov", "m, a");
        pointAt("ac+1");
        emit("mov", "a, c");
        emit("mov", "m, a");
        return;
    }
    // "<<" or ">>" - genShiftCore() expects bc=value/cx=count, the opposite of ac=old/bc=rhs here.
    copy16("cx", "bc");  // cx = shift count (rhs)
    copy16("bc", "ac");  // bc = value to shift (old)
    genShiftCore(op == "<<", isUnsigned);
}

// `ptr` must already hold the resolved address on entry. `e.rhs`'s evaluation can clobber ptr/ac/bc/cx/dx, so the
// address is stashed on the `sp` stack across it; genCompoundOp()'s "/"/"%" case also reaches into `ptr`, so it's
// re-stashed a second time around that call.
void I8008Codegen::genCompoundThroughPtr(const ir::Expr &e) {
    pushCell("ptr", "sp");  // stash address across rhs's own evaluation
    genExpr(*e.rhs);        // ac = rhs
    copy16("bc", "ac");     // bc = rhs
    popCell("ptr", "sp");   // ptr restored
    loadOperandAtPtr(e.type);  // ac = old
    if (e.yieldOld) pushAc();  // stash old - this expr's own result
    pushCell("ptr", "sp");     // re-stash address around genCompoundOp()
    genCompoundOp(e.op, e.type);  // ac = new (raw), using ac=old, bc=rhs
    narrowAcTo(e.type);
    popCell("ptr", "sp");   // ptr restored (ac, holding `new`, is untouched)
    storeOperandAtPtr(e.type);
    if (e.yieldOld) popInto("ac");  // ac = old (final result)
}

void I8008Codegen::genAddrOf(const ir::Expr &operand) {
    if (operand.kind == ir::ExprKind::Var) {
        if (operand.slot->kind == ir::SlotKind::Global) {
            storeImmediate16("ac", label(operand.slot));
        } else {
            // A Local/Param's address is only known at runtime (`fp` plus a word offset), never a compile-time
            // immediate the way a Global's is.
            pointAtSlot(operand.slot, 0);  // H,L = this call's own copy of operand.slot
            storeHLToCell("ac");
        }
        return;
    }
    if (operand.kind == ir::ExprKind::Index) {
        genIndexAddr(operand.slot, *operand.lhs);
        copy16("ac", "ptr");
        return;
    }
    if (operand.kind == ir::ExprKind::Func) {
        storeImmediate16("ac", label(operand.func));
        return;
    }
    err(operand.line, "internal: unhandled addressable-operand kind");
}

// An implicit or explicit type conversion.
void I8008Codegen::genConvert(const ir::Expr &e) {
    Type from = e.lhs->type;
    Type to = e.type;
    bool fromWide = isWide(from);
    bool toWide = isWide(to);
    auto narrowCharFixup = [&]() { emitSignExtendByte("ac", "ac+1", to.isUnsigned); };
    if (!fromWide && !toWide) {
        genExpr(*e.lhs);
        if (widthOfType(to) == 1) narrowCharFixup();
        return;
    }
    if (fromWide && !toWide) {
        genExpr(*e.lhs);
        loadByte(byteAt("wac", 0));
        storeByte("ac");
        loadByte(byteAt("wac", 1));
        storeByte("ac+1");
        if (widthOfType(to) == 1) narrowCharFixup();
        return;
    }
    if (!fromWide && toWide) {
        int toWords = wordCountOf(to);
        genExpr(*e.lhs);
        loadByte("ac");
        storeByte(byteAt("wac", 0));
        loadByte("ac+1");
        storeByte(byteAt("wac", 1));
        if (toWords > 1) {
            emitSignExtendByte("ac+1", "dx", from.isUnsigned);
            for (int i = 2; i < 2 * toWords; i++) {
                loadByte("dx");
                storeByte(byteAt("wac", i));
            }
        }
        return;
    }
    int fromN = 2 * wordCountOf(from);
    int toN = 2 * wordCountOf(to);
    genExpr(*e.lhs);
    if (toN > fromN) {
        emitSignExtendByte(byteAt("wac", fromN - 1), "dx", from.isUnsigned);
        for (int i = fromN; i < toN; i++) {
            loadByte("dx");
            storeByte(byteAt("wac", i));
        }
    }
}

void I8008Codegen::genWideAdd(const ir::Expr &lhs, const ir::Expr &rhs, bool isAdd, int words) {
    int n = 2 * words;
    genExpr(lhs);
    pushWide("wac", n);
    genExpr(rhs);
    popWide("wbc", n);  // wbc = lhs, wac = rhs
    const char *op0 = isAdd ? "add" : "sub";
    const char *opc = isAdd ? "adc" : "sbb";
    for (int i = 0; i < n; i++) {
        pointAt(byteAt("wbc", i));
        emit("mov", "a, m");
        pointAt(byteAt("wac", i));
        emit((i == 0 ? op0 : opc), "m");
        storeByte(byteAt("wac", i));
    }
}

void I8008Codegen::genWideBitwise(const ir::Expr &lhs, const ir::Expr &rhs, const std::string &op, int words) {
    int n = 2 * words;
    genExpr(lhs);
    pushWide("wac", n);
    genExpr(rhs);
    popWide("wbc", n);  // wbc = lhs, wac = rhs
    const char *insn = (op == "&") ? "and" : (op == "|") ? "or" : "xor";
    for (int i = 0; i < n; i++) {
        pointAt(byteAt("wbc", i));
        emit("mov", "a, m");
        pointAt(byteAt("wac", i));
        emit(insn, "m");
        storeByte(byteAt("wac", i));
    }
}

std::string I8008Codegen::getWideEqualitySubroutine(int words) {
    if (auto it = wideEqualitySubroutineLabel_.find(words); it != wideEqualitySubroutineLabel_.end()) {
        return it->second;
    }
    std::string label = newLabel();
    wideEqualitySubroutineLabel_.emplace(words, label);

    std::ostringstream callSiteCode;
    std::swap(callSiteCode, code_);
    emitLabel(label);

    int n = 2 * words;
    pointAt(byteAt("wbc", 0));
    emit("mov", "a, m");
    pointAt(byteAt("wac", 0));
    emit("sub", "m");
    storeByte("dx");
    for (int i = 1; i < n; i++) {
        pointAt(byteAt("wbc", i));
        emit("mov", "a, m");
        pointAt(byteAt("wac", i));
        emit("sub", "m");
        pointAt("dx");
        emit("or", "m");
        storeByte("dx");
    }
    emit("ret");
    emitRaw("");
    wideEqualitySubroutineBodies_.push_back(code_.str());
    std::swap(callSiteCode, code_);
    return label;
}

void I8008Codegen::emitWideEqualityCompare(const ir::Expr &lhs, const ir::Expr &rhs, int words, bool wantEqual) {
    int n = 2 * words;
    genExpr(lhs);
    pushWide("wac", n);
    genExpr(rhs);
    popWide("wbc", n);  // wbc = lhs, wac = rhs
    emit("cal", getWideEqualitySubroutine(words));
    std::string Ltrue = newLabel(), Ldone = newLabel();
    pointAt("dx");
    emit("mov", "a, m");
    emit("and", "a");
    emit(cat("j", (wantEqual ? "tz" : "fz")), Ltrue);
    storeImmediate16("ac", "0");
    emit("jmp", Ldone);
    emitLabel(Ltrue);
    storeImmediate16("ac", "1");
    emitLabel(Ldone);
}

// Scans byte-wise least-significant first via a chained subtract (one `sub`, then `sbb` per later byte, borrow
// propagating automatically). Flipping the sign bit of only the most significant byte before subtracting maps
// the signed range onto the unsigned one, the wide generalization of the 16-bit sign-bit-bias trick.
void I8008Codegen::emitWideLessCompare(const ir::Expr &a, const ir::Expr &b, bool isUnsigned, int words,
                                        bool invert) {
    int n = 2 * words;
    genExpr(a);
    pushWide("wac", n);
    genExpr(b);
    popWide("wbc", n);  // wbc = a, wac = b
    if (!isUnsigned) {
        pointAt(byteAt("wbc", n - 1));
        emit("mov", "a, m");
        emit("xri", "0x80");
        emit("mov", "m, a");
        pointAt(byteAt("wac", n - 1));
        emit("mov", "a, m");
        emit("xri", "0x80");
        emit("mov", "m, a");
    }
    pointAt(byteAt("wbc", 0));
    emit("mov", "a, m");
    pointAt(byteAt("wac", 0));
    emit("sub", "m");
    for (int i = 1; i < n; i++) {
        pointAt(byteAt("wbc", i));
        emit("mov", "a, m");
        pointAt(byteAt("wac", i));
        emit("sbb", "m");
    }
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit(cat("j", (invert ? "fc" : "tc")), Ltrue);
    storeImmediate16("ac", "0");
    emit("jmp", Ldone);
    emitLabel(Ltrue);
    storeImmediate16("ac", "1");
    emitLabel(Ldone);
}

void I8008Codegen::genWideCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned,
                                   int words) {
    if (op == "==") return emitWideEqualityCompare(lhs, rhs, words, /*wantEqual=*/true);
    if (op == "!=") return emitWideEqualityCompare(lhs, rhs, words, /*wantEqual=*/false);
    if (op == "<") return emitWideLessCompare(lhs, rhs, isUnsigned, words, /*invert=*/false);
    if (op == ">") return emitWideLessCompare(rhs, lhs, isUnsigned, words, /*invert=*/false);
    if (op == "<=") return emitWideLessCompare(rhs, lhs, isUnsigned, words, /*invert=*/true);
    if (op == ">=") return emitWideLessCompare(lhs, rhs, isUnsigned, words, /*invert=*/true);
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

// A wide value shifted by a runtime count: loops one bit at a time across all `n` bytes, chained through the
// accumulator (RAL/RAR work on A only). A signed right shift re-derives the current sign bit into carry before
// the top byte's rotate, instead of a fixed 0-fill.
void I8008Codegen::genWideShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned, int words) {
    int n = 2 * words;
    genExpr(lhs);
    pushWide("wac", n);
    genExpr(rhs);  // ac = shift count (already promoted to a plain int)
    copy16("cx", "ac");
    popWide("wac", n);
    pointAt("cx");
    emit("mov", "a, m");
    emit("mov", "b, a");  // b = shift count
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("mov", "a, b");
    emit("and", "a");
    emit("jtz", Ldone);
    if (isLeft) {
        pointAt(byteAt("wac", 0));
        emit("mov", "a, m");
        emit("and", "a");
        emit("ral");
        emit("mov", "m, a");
        for (int i = 1; i < n; i++) {
            pointAt(byteAt("wac", i));
            emit("mov", "a, m");
            emit("ral");
            emit("mov", "m, a");
        }
    } else if (isUnsigned) {
        pointAt(byteAt("wac", n - 1));
        emit("mov", "a, m");
        emit("and", "a");
        emit("rar");
        emit("mov", "m, a");
        for (int i = n - 2; i >= 0; i--) {
            pointAt(byteAt("wac", i));
            emit("mov", "a, m");
            emit("rar");
            emit("mov", "m, a");
        }
    } else {
        pointAt(byteAt("wac", n - 1));
        emit("mov", "a, m");
        emit("mov", "c, a");
        emit("and", "a");
        emit("ral");  // carry = current sign bit; c = saved value
        emit("mov", "a, c");
        emit("rar");
        emit("mov", "m, a");
        for (int i = n - 2; i >= 0; i--) {
            pointAt(byteAt("wac", i));
            emit("mov", "a, m");
            emit("rar");
            emit("mov", "m, a");
        }
    }
    emit("dcr", "b");
    emit("jmp", Lloop);
    emitLabel(Ldone);
}

// Textbook shift-and-add multiply, `n*8` bits serially - the wide generalization of genMul(). Needs no
// signedness, since a truncated multiply's bit pattern never depends on it.
void I8008Codegen::genWideMul(const ir::Expr &lhs, const ir::Expr &rhs, int words) {
    int n = 2 * words;
    genExpr(lhs);
    pushWide("wac", n);
    genExpr(rhs);
    popWide("wbc", n);  // wbc = multiplicand, wac = multiplier
    for (int i = 0; i < n; i++) {
        emit("mvi", "a, 0");
        storeByte(byteAt("wtmp", i));
    }
    emit("mvi", cat("d, ", (8 * n)));
    std::string Lloop = newLabel(), Ldone = newLabel(), Lskipadd = newLabel();
    emitLabel(Lloop);
    emit("mov", "a, d");
    emit("and", "a");
    emit("jtz", Ldone);
    pointAt(byteAt("wac", 0));
    emit("mov", "a, m");
    emit("ani", "1");
    emit("jtz", Lskipadd);
    emit("mvi", "e, 0");  // e = carry-in for the first (least significant) byte's own add
    for (int i = 0; i < n; i++) {
        pointAt(byteAt("wtmp", i));
        emit("mov", "a, m");
        pointAt(byteAt("wbc", i));
        emit((i == 0 ? "add" : "adc"), "m");
        storeByte(byteAt("wtmp", i));
    }
    emitLabel(Lskipadd);
    pointAt(byteAt("wbc", 0));
    emit("mov", "a, m");
    emit("and", "a");
    emit("ral");
    emit("mov", "m, a");
    for (int i = 1; i < n; i++) {
        pointAt(byteAt("wbc", i));
        emit("mov", "a, m");
        emit("ral");
        emit("mov", "m, a");
    }
    pointAt(byteAt("wac", n - 1));
    emit("mov", "a, m");
    emit("and", "a");
    emit("rar");
    emit("mov", "m, a");
    for (int i = n - 2; i >= 0; i--) {
        pointAt(byteAt("wac", i));
        emit("mov", "a, m");
        emit("rar");
        emit("mov", "m, a");
    }
    emit("dcr", "d");
    emit("jmp", Lloop);
    emitLabel(Ldone);
    copyWide("wac", "wtmp", n);
}

std::string I8008Codegen::getWideDivModSubroutine(bool isUnsigned, int words) {
    auto key = std::make_pair(isUnsigned, words);
    if (auto it = wideDivModSubroutineLabel_.find(key); it != wideDivModSubroutineLabel_.end()) {
        return it->second;
    }
    std::string label = newLabel();
    wideDivModSubroutineLabel_.emplace(key, label);

    // Builds this subroutine's body into a fresh stream so it can be queued separately rather than landing at the
    // call site; `code_` is simply swapped for the duration, so every helper still writes through it as usual.
    std::ostringstream callSiteCode;
    std::swap(callSiteCode, code_);
    emitLabel(label);

    int n = 2 * words;
    auto isNegByte = [&](const std::string &byteCell, const std::string &dstCell) {
        pointAt(byteCell);
        emit("mov", "a, m");
        emit("and", "a");
        emit("ral");
        std::string Lneg = newLabel(), Ldone = newLabel();
        emit("mvi", "a, 0");
        storeByte(dstCell);
        emit("jtc", Lneg);
        emit("jmp", Ldone);
        emitLabel(Lneg);
        emit("mvi", "a, 1");
        storeByte(dstCell);
        emitLabel(Ldone);
    };
    auto negateIfFlag = [&](const std::string &flagCell, const char *buf) {
        pointAt(flagCell);
        emit("mov", "a, m");
        emit("and", "a");
        std::string Lskip = newLabel();
        emit("jtz", Lskip);
        negateWideBuffer(buf, n);
        emitLabel(Lskip);
    };
    // On entry, `wbc` holds the dividend and `wac` the divisor - placed there by the call site before `cal`-ing
    // this label.
    if (!isUnsigned) {
        isNegByte(byteAt("wbc", n - 1), "dx");
        isNegByte(byteAt("wac", n - 1), "dx+1");
        negateIfFlag("dx", "wbc");
        negateIfFlag("dx+1", "wac");
    }
    for (int i = 0; i < n; i++) {
        emit("mvi", "a, 0");
        storeByte(byteAt("wtmp", i));
        storeByte(byteAt("wtmp2", i));
    }
    emit("mvi", cat("d, ", (8 * n)));
    std::string Lloop = newLabel(), Ldone = newLabel();
    std::string Lrestore = newLabel(), Lsetbit = newLabel(), Lshiftq = newLabel();
    emitLabel(Lloop);
    emit("mov", "a, d");
    emit("and", "a");
    emit("jtz", Ldone);
    // Combined 1-bit left shift of the (wtmp:wbc) pair.
    pointAt(byteAt("wbc", 0));
    emit("mov", "a, m");
    emit("and", "a");
    emit("ral");
    emit("mov", "m, a");
    for (int i = 1; i < n; i++) {
        pointAt(byteAt("wbc", i));
        emit("mov", "a, m");
        emit("ral");
        emit("mov", "m, a");
    }
    for (int i = 0; i < n; i++) {
        pointAt(byteAt("wtmp", i));
        emit("mov", "a, m");
        emit("ral");
        emit("mov", "m, a");
    }
    // Trial subtraction: wtmp -= wac, always committed.
    pointAt(byteAt("wtmp", 0));
    emit("mov", "a, m");
    pointAt(byteAt("wac", 0));
    emit("sub", "m");
    storeByte(byteAt("wtmp", 0));
    for (int i = 1; i < n; i++) {
        pointAt(byteAt("wtmp", i));
        emit("mov", "a, m");
        pointAt(byteAt("wac", i));
        emit("sbb", "m");
        storeByte(byteAt("wtmp", i));
    }
    emit("jtc", Lrestore);
    emit("jmp", Lsetbit);
    emitLabel(Lrestore);
    pointAt(byteAt("wtmp", 0));
    emit("mov", "a, m");
    pointAt(byteAt("wac", 0));
    emit("add", "m");
    storeByte(byteAt("wtmp", 0));
    for (int i = 1; i < n; i++) {
        pointAt(byteAt("wtmp", i));
        emit("mov", "a, m");
        pointAt(byteAt("wac", i));
        emit("adc", "m");
        storeByte(byteAt("wtmp", i));
    }
    emit("mvi", "a, 0");  // new quotient bit = 0; carry must end up clear for the roll below
    emit("and", "a");
    emit("jmp", Lshiftq);
    emitLabel(Lsetbit);
    emit("mvi", "a, 0xff");
    emit("and", "a");
    emit("ral");  // carry = 1 (bit7 of 0xff), A discarded
    emitLabel(Lshiftq);
    for (int i = 0; i < n; i++) {
        pointAt(byteAt("wtmp2", i));
        emit("mov", "a, m");
        emit("ral");
        emit("mov", "m, a");
    }
    emit("dcr", "d");
    emit("jmp", Lloop);
    emitLabel(Ldone);
    if (!isUnsigned) {
        pointAt("dx");
        emit("mov", "a, m");
        pointAt("dx+1");
        emit("xor", "m");
        std::string Lskip = newLabel();
        emit("jtz", Lskip);
        negateWideBuffer("wtmp2", n);
        emitLabel(Lskip);
        negateIfFlag("dx", "wtmp");
    }
    emit("ret");
    emitRaw("");
    wideDivModSubroutineBodies_.push_back(code_.str());
    std::swap(callSiteCode, code_);
    return label;
}

void I8008Codegen::genWideDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned,
                                  int words) {
    int n = 2 * words;
    genExpr(lhs);
    pushWide("wac", n);
    genExpr(rhs);
    popWide("wbc", n);  // wbc = dividend, wac = divisor
    std::string label = getWideDivModSubroutine(isUnsigned, words);
    emit("cal", label);
    copyWide("wac", wantRemainder ? "wtmp" : "wtmp2", n);
}

void I8008Codegen::testTruthy(const ir::Expr &e) {
    if (!isWide(e.type)) {
        pointAt("ac");
        emit("mov", "a, m");
        pointAt("ac+1");
        emit("or", "m");
        return;
    }
    int n = 2 * wordCountOf(e.type);
    loadByte(byteAt("wac", 0));
    for (int i = 1; i < n; i++) {
        pointAt(byteAt("wac", i));
        emit("or", "m");
    }
    storeByte("ac");
    emit("mvi", "a, 0");
    storeByte("ac+1");
    pointAt("ac");
    emit("mov", "a, m");
    emit("and", "a");
}

void I8008Codegen::genBinary(const ir::Expr &e) {
    const std::string &op = e.op;
    if (op == "&&") {
        genLogicalAnd(*e.lhs, *e.rhs);
        return;
    }
    if (op == "||") {
        genLogicalOr(*e.lhs, *e.rhs);
        return;
    }
    Type operandType = e.lhs->type;
    bool wide = isWide(operandType);
    bool isUnsigned = operandType.isUnsigned;
    int words = wide ? wordCountOf(operandType) : 1;
    if (op == "+" || op == "-") {
        if (wide) {
            genWideAdd(*e.lhs, *e.rhs, op == "+", words);
            return;
        }
        genExpr(*e.lhs);
        pushAc();
        genExpr(*e.rhs);
        popInto("bc");  // bc = lhs
        const char *lo = op == "+" ? "add" : "sub";
        const char *hi = op == "+" ? "adc" : "sbb";
        pointAt("bc");
        emit("mov", "a, m");  // a = lhs_lo
        pointAt("ac");
        emit(lo, "m");  // a = lhs_lo (+/-) rhs_lo, CY set accordingly
        emit("mov", "b, a");
        pointAt("bc+1");
        emit("mov", "a, m");  // a = lhs_hi
        pointAt("ac+1");
        emit(hi, "m");  // a = lhs_hi (+/-) rhs_hi (+/-) carry
        emit("mov", "c, a");
        pointAt("ac");
        emit("mov", "a, b");
        emit("mov", "m, a");
        pointAt("ac+1");
        emit("mov", "a, c");
        emit("mov", "m, a");
        return;
    }
    if (op == "*") {
        if (wide) {
            genWideMul(*e.lhs, *e.rhs, words);
        } else {
            genMul(*e.lhs, *e.rhs);
        }
        return;
    }
    if (op == "/" || op == "%") {
        if (wide) {
            genWideDivMod(*e.lhs, *e.rhs, /*wantRemainder=*/op == "%", isUnsigned, words);
        } else {
            genDivMod(*e.lhs, *e.rhs, /*wantRemainder=*/op == "%", isUnsigned);
        }
        return;
    }
    if (op == "&" || op == "|" || op == "^") {
        if (wide) {
            genWideBitwise(*e.lhs, *e.rhs, op, words);
            return;
        }
        genExpr(*e.lhs);
        pushAc();
        genExpr(*e.rhs);
        popInto("bc");  // bc = lhs
        const char *insn = (op == "&") ? "and" : (op == "|") ? "or" : "xor";
        pointAt("bc");
        emit("mov", "a, m");
        pointAt("ac");
        emit(insn, "m");
        emit("mov", "b, a");
        pointAt("bc+1");
        emit("mov", "a, m");
        pointAt("ac+1");
        emit(insn, "m");
        emit("mov", "c, a");
        pointAt("ac");
        emit("mov", "a, b");
        emit("mov", "m, a");
        pointAt("ac+1");
        emit("mov", "a, c");
        emit("mov", "m, a");
        return;
    }
    if (op == "<<" || op == ">>") {
        if (wide) {
            genWideShift(*e.lhs, *e.rhs, op == "<<", isUnsigned, words);
            return;
        }
        genShift(*e.lhs, *e.rhs, op == "<<", isUnsigned);
        return;
    }
    if (wide) {
        genWideCompare(op, *e.lhs, *e.rhs, isUnsigned, words);
        return;
    }
    genCompare(op, *e.lhs, *e.rhs, isUnsigned);
}

// No variable-count shift instruction - loop `rhs` times, one bit at a time, via shiftOneBit(). A signed right
// shift re-derives the current sign bit into carry before each rotate, instead of a fixed 0-fill.
void I8008Codegen::genShiftCore(bool isLeft, bool isUnsigned) {
    pointAt("cx");
    emit("mov", "a, m");
    emit("mov", "b, a");  // b = shift count (always small; low byte is enough)
    std::string Lloop = newLabel(), Ldone = newLabel();
    emitLabel(Lloop);
    emit("mov", "a, b");
    emit("and", "a");
    emit("jtz", Ldone);
    if (isLeft || isUnsigned) {
        shiftOneBit("bc", isLeft);
    } else {
        pointAt("bc+1");
        emit("mov", "a, m");
        emit("mov", "c, a");
        emit("and", "a");
        emit("ral");  // carry = current sign bit
        emit("mov", "a, c");
        emit("rar");
        emit("mov", "m, a");
        pointAt("bc");
        emit("mov", "a, m");
        emit("rar");
        emit("mov", "m, a");
    }
    emit("dcr", "b");
    emit("jmp", Lloop);
    emitLabel(Ldone);
    copy16("ac", "bc");
}

void I8008Codegen::genShift(const ir::Expr &lhs, const ir::Expr &rhs, bool isLeft, bool isUnsigned) {
    genExpr(lhs);  // ac = value to shift
    pushAc();
    genExpr(rhs);         // ac = shift count
    copy16("cx", "ac");  // popInto() below uses every one of A/B/C/D/E/H/L as scratch, so stash the count in a cell
    popInto("bc");         // bc = value (restored only now)
    genShiftCore(isLeft, isUnsigned);
}

// Textbook shift-and-add multiply (no hardware multiply): `bc` is
// the multiplicand (doubled each iteration), `dx` the multiplier
// (halved each iteration), `ac` the accumulating product. Only
// verified against non-negative operands.
void I8008Codegen::genMulCore() {
    storeImmediate16("ac", "0");
    emit("mvi", "d, 16");  // loop counter, in the real register D
    std::string Lloop = newLabel(), Ldone = newLabel(), Lskipadd = newLabel();
    emitLabel(Lloop);
    emit("mov", "a, d");
    emit("and", "a");
    emit("jtz", Ldone);
    pointAt("dx");
    emit("mov", "a, m");
    emit("ani", "1");
    emit("jtz", Lskipadd);
    pointAt("bc");
    emit("mov", "a, m");
    emit("mov", "e, a");  // e = bc_lo
    pointAt("ac");
    emit("mov", "a, m");
    emit("add", "e");
    emit("mov", "m, a");
    pointAt("bc+1");
    emit("mov", "a, m");
    emit("mov", "e, a");  // e = bc_hi
    pointAt("ac+1");
    emit("mov", "a, m");
    emit("adc", "e");
    emit("mov", "m, a");
    emitLabel(Lskipadd);
    shiftOneBit("bc", /*isLeft=*/true);
    shiftOneBit("dx", /*isLeft=*/false);
    emit("dcr", "d");
    emit("jmp", Lloop);
    emitLabel(Ldone);
}

void I8008Codegen::genMul(const ir::Expr &lhs, const ir::Expr &rhs) {
    genExpr(lhs);  // ac = multiplicand
    pushAc();
    genExpr(rhs);
    popInto("bc");       // bc = multiplicand (restored only now)
    copy16("dx", "ac");    // dx = multiplier
    genMulCore();
}

// Restoring shift-subtract (no hardware divide), 16 steps: `dx`
// holds the dividend and fills with quotient bits from the bottom as
// the dividend shifts out of the top into the remainder `cx`; `bc` is
// the divisor. A carry out of the remainder's shift means it exceeds
// any 16-bit divisor, so the subtraction is forced. Signed operands
// are divided as magnitudes; the quotient is negated when the signs
// differ and the remainder takes the dividend's sign.
void I8008Codegen::genDivModCore(bool wantRemainder, bool isUnsigned) {
    auto negate16 = [&](const std::string &cell) {
        pointAt(cell);
        emit("mov", "a, m");
        emit("xri", "0xff");
        emit("mov", "m, a");
        pointAt(cell + "+1");
        emit("mov", "a, m");
        emit("xri", "0xff");
        emit("mov", "m, a");
        pointAt(cell);
        emit("mov", "a, m");
        emit("adi", "1");
        emit("mov", "m, a");
        pointAt(cell + "+1");
        emit("mov", "a, m");
        emit("aci", "0");
        emit("mov", "m, a");
    };
    auto isNeg = [&](const std::string &cell, const std::string &dstCell) {
        pointAt(cell + "+1");
        emit("mov", "a, m");
        emit("and", "a");
        emit("ral");
        std::string Lneg = newLabel(), Ldone = newLabel();
        emit("mvi", "a, 0");
        storeByte(dstCell);
        emit("jtc", Lneg);
        emit("jmp", Ldone);
        emitLabel(Lneg);
        emit("mvi", "a, 1");
        storeByte(dstCell);
        emitLabel(Ldone);
    };
    auto negateIf = [&](const std::string &flagCell, const std::string &cell) {
        pointAt(flagCell);
        emit("mov", "a, m");
        emit("and", "a");
        std::string Lskip = newLabel();
        emit("jtz", Lskip);
        negate16(cell);
        emitLabel(Lskip);
    };
    // `ptr`'s own low/high byte double as this operation's two 0/1
    // sign flags (dividend/divisor) - `ptr` itself is otherwise never
    // touched by this routine.
    if (!isUnsigned) {
        isNeg("dx", "ptr");
        isNeg("bc", "ptr+1");
        negateIf("ptr", "dx");
        negateIf("ptr+1", "bc");
    }
    storeImmediate16("cx", "0");
    emit("mvi", "d, 16");
    std::string Lloop = newLabel(), Ldone = newLabel(), Lforce = newLabel();
    std::string Lcommit = newLabel(), Lnext = newLabel();
    auto trialSubtract = [&]() {  // e = low, a = high byte of cx - bc; CY = borrow
        pointAt("cx");
        emit("mov", "a, m");
        pointAt("bc");
        emit("sub", "m");
        emit("mov", "e, a");
        pointAt("cx+1");
        emit("mov", "a, m");
        pointAt("bc+1");
        emit("sbb", "m");
    };
    emitLabel(Lloop);
    emit("mov", "a, d");
    emit("and", "a");
    emit("jtz", Ldone);
    pointAt("dx");
    emit("mov", "a, m");
    emit("and", "a");
    emit("ral");
    emit("mov", "m, a");
    for (const char *cell : {"dx+1", "cx", "cx+1"}) {
        pointAt(cell);
        emit("mov", "a, m");
        emit("ral");
        emit("mov", "m, a");
    }
    emit("jtc", Lforce);
    trialSubtract();
    emit("jtc", Lnext);
    emit("jmp", Lcommit);
    emitLabel(Lforce);
    trialSubtract();
    emitLabel(Lcommit);
    emit("mov", "c, a");
    pointAt("cx");
    emit("mov", "a, e");
    emit("mov", "m, a");
    pointAt("cx+1");
    emit("mov", "a, c");
    emit("mov", "m, a");
    pointAt("dx");
    emit("mov", "a, m");
    emit("ori", "1");
    emit("mov", "m, a");
    emitLabel(Lnext);
    emit("dcr", "d");
    emit("jmp", Lloop);
    emitLabel(Ldone);
    if (!isUnsigned) {
        pointAt("ptr");
        emit("mov", "a, m");
        pointAt("ptr+1");
        emit("xor", "m");
        std::string Lskip = newLabel();
        emit("jtz", Lskip);
        negate16("dx");
        emitLabel(Lskip);
        negateIf("ptr", "cx");
    }
    copy16("ac", wantRemainder ? "cx" : "dx");
}

void I8008Codegen::genDivMod(const ir::Expr &lhs, const ir::Expr &rhs, bool wantRemainder, bool isUnsigned) {
    genExpr(lhs);  // ac = dividend
    pushAc();
    genExpr(rhs);
    popInto("dx");        // dx = dividend (restored only now)
    copy16("bc", "ac");     // bc = divisor
    genDivModCore(wantRemainder, isUnsigned);
}

void I8008Codegen::genCompare(const std::string &op, const ir::Expr &lhs, const ir::Expr &rhs, bool isUnsigned) {
    if (op == "==") return emitEqualityCompare(lhs, rhs, /*wantEqual=*/true);
    if (op == "!=") return emitEqualityCompare(lhs, rhs, /*wantEqual=*/false);
    if (op == "<") return emitLessCompare(lhs, rhs, isUnsigned, /*invert=*/false);
    if (op == ">") return emitLessCompare(rhs, lhs, isUnsigned, /*invert=*/false);
    if (op == "<=") return emitLessCompare(rhs, lhs, isUnsigned, /*invert=*/true);
    if (op == ">=") return emitLessCompare(lhs, rhs, isUnsigned, /*invert=*/true);
    err(lhs.line, "internal: unhandled comparison operator '" + op + "'");
}

// `==`/`!=` need no sign handling: a-b (16 bits, borrow chained low into high) is zero iff a==b, checked by
// ORing the two result bytes together.
void I8008Codegen::emitEqualityCompare(const ir::Expr &lhs, const ir::Expr &rhs, bool wantEqual) {
    genExpr(lhs);
    pushAc();
    genExpr(rhs);
    popInto("bc");  // bc = lhs
    pointAt("bc");
    emit("mov", "a, m");
    pointAt("ac");
    emit("sub", "m");
    emit("mov", "b, a");  // b = tentative low diff
    pointAt("bc+1");
    emit("mov", "a, m");
    pointAt("ac+1");
    emit("sbb", "m");
    emit("or", "b");  // Z set iff both diff bytes zero
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit(cat("j", (wantEqual ? "tz" : "fz")), Ltrue);
    storeImmediate16("ac", "0");
    emit("jmp", Ldone);
    emitLabel(Ltrue);
    storeImmediate16("ac", "1");
    emitLabel(Ldone);
}

// Signed 16-bit "a < b" via the sign-bit-bias trick (the 8008 has no hardware overflow flag): flipping the sign
// bit of each operand's high byte maps the signed range onto the unsigned one, leaving just the final borrow to
// check.
void I8008Codegen::emitLessCompare(const ir::Expr &a, const ir::Expr &b, bool isUnsigned, bool invert) {
    genExpr(a);
    pushAc();
    genExpr(b);
    popInto("bc");  // bc = a
    if (!isUnsigned) {
        pointAt("bc+1");
        emit("mov", "a, m");
        emit("xri", "0x80");
        emit("mov", "m, a");  // a's high byte, sign flipped (in place)
        pointAt("ac+1");
        emit("mov", "a, m");
        emit("xri", "0x80");
        emit("mov", "m, a");  // b's high byte, sign flipped (in place)
    }
    pointAt("bc");
    emit("mov", "a, m");
    pointAt("ac");
    emit("sub", "m");  // a'_lo - b'_lo, CY = borrow
    pointAt("bc+1");
    emit("mov", "a, m");
    pointAt("ac+1");
    emit("sbb", "m");  // a'_hi - b'_hi - borrow; CY now = (a < b) unbiased
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit(cat("j", (invert ? "fc" : "tc")), Ltrue);
    storeImmediate16("ac", "0");
    emit("jmp", Ldone);
    emitLabel(Ltrue);
    storeImmediate16("ac", "1");
    emitLabel(Ldone);
}

void I8008Codegen::genLogicalAnd(const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    genExpr(lhs);
    testTruthy(lhs);
    emit("jtz", Lfalse);
    genExpr(rhs);
    testTruthy(rhs);
    emit("jtz", Lfalse);
    emitLabel(Ltrue);
    storeImmediate16("ac", "1");
    emit("jmp", Ldone);
    emitLabel(Lfalse);
    storeImmediate16("ac", "0");
    emitLabel(Ldone);
}

void I8008Codegen::genLogicalOr(const ir::Expr &lhs, const ir::Expr &rhs) {
    std::string Lfalse = newLabel(), Ltrue = newLabel(), Ldone = newLabel();
    genExpr(lhs);
    testTruthy(lhs);
    emit("jfz", Ltrue);
    genExpr(rhs);
    testTruthy(rhs);
    emit("jfz", Ltrue);
    emitLabel(Lfalse);
    storeImmediate16("ac", "0");
    emit("jmp", Ldone);
    emitLabel(Ltrue);
    storeImmediate16("ac", "1");
    emitLabel(Ldone);
}

void I8008Codegen::genLogicalNot(const ir::Expr &rhs) {
    genExpr(rhs);
    testTruthy(rhs);
    std::string Ltrue = newLabel(), Ldone = newLabel();
    emit("jtz", Ltrue);
    storeImmediate16("ac", "0");
    emit("jmp", Ldone);
    emitLabel(Ltrue);
    storeImmediate16("ac", "1");
    emitLabel(Ldone);
}

std::optional<std::string> I8008Codegen::assembleInProcess(const std::string &asmText,
                                                             const std::string &format) const {
    if (format == "raw") return assembleI8008(asmText);
    // `coff`/`elf`/`macho`/`aout`/`objraw`: the assembler builds one format-neutral ObjectFile, and the
    // requested format name just picks that format's FileFormatWriter to serialize it.
    if (auto kind = fileFormatKindFromName(format)) {
        ObjectFile obj = assembleI8008Object(asmText);
        return makeFileFormatWriter(*kind)->write(obj);
    }
    return std::nullopt;
}

std::optional<std::string> I8008Codegen::debugMap(const std::string &asmText, const std::string &format,
        const std::string &sourcePath) const {
    (void)format;
    return assembleI8008DebugMap(asmText, sourcePath, debugSymbols_);
}

std::optional<std::string> I8008Codegen::debugMapPreLink(const std::string &asmText,
                                                    const std::string &sourcePath) const {
    return assembleI8008DebugMap(asmText, sourcePath, debugSymbols_, /*allowExternal=*/true);
}

// Only emits a marker when `debugMapEnabled_` (set via setDebugMapEnabled(),
// before generate() runs) and `line` actually differs from the last one
// emitted - see Pdp7Codegen.cpp's identical emitSourceLine(), which this
// mirrors exactly.
void I8008Codegen::emitSourceLine(int line) {
    if (!debugMapEnabled_ || line == 0 || line == lastDebugLine_) return;
    emitRaw(cat("__line ", line));
    lastDebugLine_ = line;
}

}  // namespace rcc
