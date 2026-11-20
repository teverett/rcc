#pragma once

#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "../Type.h"
#include "ir/IR.h"

namespace rcc {

// Turns target-independent IR (names already resolved, semantics already checked by
// IRBuilder) into a target's assembly text, and drives that target's assembler to produce
// a binary. A backend only ever does instruction selection, never symbol-table work.
class Backend {
   public:
    virtual ~Backend() = default;

    // Compiles the whole program to this target's assembly text. When `objectMode` is true
    // (real separate compilation), no entry sequence is emitted; the linker synthesizes one
    // combined entry sequence once every object is merged and `main` is resolvable.
    virtual std::string generate(const ir::Program &prog, bool objectMode = false) = 0;

    // Backend name, selected via --target=<name>.
    virtual std::string name() const = 0;

    // Human-readable description of the processor/machine this backend targets (e.g.
    // "Intel 8086/8088", "Parrot VM"), printed by --list-platforms after each platform's
    // own triple.
    virtual std::string description() const = 0;

    // This target's storage width for char/short/int/long/long long; usually bytes, but a
    // target's native word on a non-byte-addressable backend (e.g. pdp7).
    virtual TypeWidths typeWidths() const = 0;

    // Extension for the temp file holding generated text before assembling; some external
    // assemblers key off it.
    virtual std::string sourceExtension() const = 0;

    // Valid --format values other than "asm" (handled uniformly: print/save the text as-is).
    virtual std::vector<std::string> assembledFormats() const = 0;

    // Whether `format` means real separate compilation (object mode) for this backend; the
    // same format name can mean different things per backend (e.g. elf is a complete
    // program for 8086 but a relocatable object for z80). Default: only "coff".
    virtual bool isObjectFormat(const std::string &format) const { return format == "coff"; }

    // Valid only after generate() was called on this instance: splits the combined generated
    // text per input source file (for --dump-asm). Text not tied to one input file (entry
    // sequence, internal helper routines) is attributed to the file defining `main`.
    virtual std::map<std::string, std::string> perFileAsm() const = 0;

    // Assembles `asmText` for `format` entirely in-process. Every format a backend lists in
    // assembledFormats() must return a real value here - there's no external assembler
    // fallback - so the default (nullopt) is only reachable for an internal error.
    virtual std::optional<std::string> assembleInProcess(const std::string &asmText,
                                                           const std::string &format) const {
        (void)asmText;
        (void)format;
        return std::nullopt;
    }
};

// Shared text-emission scaffolding for a line-oriented assembly-text
// backend. Owns the generated-text buffer and the handful of line shapes
// every such dialect needs (instruction, label, raw directive, equate,
// verbatim `asm { }` passthrough); centralizes the indentation/newline
// bookkeeping that would otherwise be hand-typed at every call site
// (`"    " + mnemonic + " " + operand + "\n"`). Real per-target content -
// instruction selection and operand syntax (registers, addressing-mode
// text, how multiple operands are joined) - stays entirely in each
// backend's own Codegen.cpp; this only standardizes how a finished line
// gets appended, so a backend builds its own already-formatted operand
// text and hands it to `emit`.
//
// Opt-in, not part of `Backend` itself: a backend whose dialect isn't
// line-oriented mnemonic+operand text (e.g. ParrotVM/MoarVM's own
// assignment-shaped register-VM IR) has no reason to inherit this.
class TextCodegenBase {
   protected:
    // Override for a dialect that wants something other than 4 spaces
    // (e.g. a tab).
    virtual std::string indent() const { return "    "; }

    void emit(const std::string &mnemonic) { code_ << indent() << mnemonic << "\n"; }
    // `operands` arrives already formatted in this dialect's own
    // convention (comma-joined, bracketed, whatever) - only the
    // indent/separating-space/newline around it are standardized here.
    void emit(const std::string &mnemonic, const std::string &operands) {
        code_ << indent() << mnemonic << " " << operands << "\n";
    }
    template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
    void emit(const std::string &mnemonic, T operand) {
        emit(mnemonic, std::to_string(operand));
    }
    // A standalone label line (no instruction on it): "name:\n".
    void emitLabel(const std::string &name) { code_ << name << ":\n"; }
    // A directive line that isn't an indented mnemonic+operand
    // instruction (e.g. "org 0").
    void emitRaw(const std::string &text) { code_ << text << "\n"; }
    // "name = value\n" (a compile-time equate, e.g. "ptr = 4").
    void emitEquate(const std::string &name, long value) { code_ << name << " = " << value << "\n"; }
    // Passes already-assembled text through verbatim (an `asm { ... }`
    // block's own text) - only ensures it ends with exactly one newline,
    // same as every other emit above.
    void emitVerbatim(const std::string &text) {
        code_ << text;
        if (!text.empty() && text.back() != '\n') code_ << "\n";
    }
    // Concatenates mixed-type pieces into one operand string the same way a
    // backend's own `code_ <<` chain used to - each argument streamed via
    // whatever operator<< overload fits it (so a bare int/long/char* piece
    // never needs its own explicit std::to_string()/std::string() cast at
    // the call site, the way plain operator+ concatenation would require).
    template <typename... Args>
    static std::string cat(Args &&...args) {
        std::ostringstream o;
        (o << ... << args);
        return o.str();
    }

    std::ostringstream code_;
};

}  // namespace rcc
