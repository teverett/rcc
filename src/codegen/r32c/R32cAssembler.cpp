#include "R32cAssembler.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "ANTLRInputStream.h"
#include "BaseErrorListener.h"
#include "CommonTokenStream.h"
#include "RccAsmLexer.h"
#include "RccAsmParser.h"

// This file's mnemonic vocabulary covers the following real R32C
// instruction groups, each with its own invented opcode byte (see
// M16cAssembler.cpp for why this project encodes its own dialect rather than
// real R32C silicon object code):
//   - byte/word/dword move, add, sub, and, or, xor, cmp, not, neg, shl/sha,
//     rol/ror, adc/sbb (one opcode per mnemonic per operand size:
//     `mov`/`movb`/`movl`, `add`/`addb`/`addl`, etc.)
//   - byte/word multiply and unsigned divide (`mul`/`divu` word only, same
//     widening convention M32cAssembler.cpp documents for its own `mul.b`
//     omission); dword `mull`/`divul`/`divxl` (signed divide)
//   - stack (`push`/`pop`, dword `pushl`/`popl`), register-indirect
//     load/store through `a0`/`a1` (`ld`/`st`, byte `ldb`/`stb`, dword
//     `ldl`/`stl`), address load (`lea`, dword `leal`)
//   - control flow: `jmp`/`jsr`/`jsri`/`rts`, the signed and unsigned
//     conditional jump family, `clc`/`stc`, `fset`/`fclr`
//   - bit manipulation on a byte operand: `bset`/`bclr`/`btst`/`bnot`
//   - multiple-register stack save/restore: `pushm`/`popm`
//   - stack-frame and subroutine-return helpers: `enter`/`exitd`/`reit`
//   - sign/zero-extend of `r0` into the `r2r0` pair: `exts`/`extz`
//   - `nop`/`brk`/`und`
//
// Deliberately NOT implemented (noted here rather than silently dropped -
// see the project's final report for the R32C port): the extended-multiply
// family that produces a result wider than its operands without reusing an
// existing accumulator pair (`EMUL`/`EMULU`), the hardware string/block-move
// family (`SSTR`/`SMOVF`/`SMOVB`/`SCMPU`/`SIN`/`SOUT`), index-register
// addressing modes, and privileged/system-register access (`LDC`/`STC` to
// `FLG`/`ISP`/`INTB`/etc., as opposed to this dialect's own `clc`/`stc`
// carry-flag shorthand) and `JMPS`/`JSRS` special-page calls - same
// exclusion list M32cAssembler.cpp documents for M32C, applied here for the
// same reason: R32cCodegen.cpp never needs any of these, and this project
// has no R32C simulator to validate a guessed encoding/semantics against.

namespace rcc {

namespace {

[[noreturn]] void fail(const std::string &msg) { throw std::runtime_error("R32cAssembler: " + msg); }

// --- Parsing, via the one shared grammar/RccAsm.g4 -----------------------
enum class SylKind { Number, Symbol, Group };
struct Syllable {
    char sign = 0;
    SylKind kind;
    std::string text;
    std::vector<Syllable> group;
};

// One operand, classified from a bare expression. `Reg` is a bare
// `r0`-`r3`/`a0`/`a1`/`sp`/`fb`/`r2r0`/`r3r1` identifier; `Imm` a bare
// signed number; anything else unbracketed (a label, or `label+N`) is `Mem`.
// `Disp` is the one bracketed shape this dialect supports -
// `[fb+N]`/`[fb-N]` - with `reg` the base register and `expr` the (possibly
// negative) word-offset.
struct Operand {
    enum Kind { Reg, Imm, Mem, Disp } kind = Imm;
    int reg = -1;
    std::vector<Syllable> expr;  // Imm/Mem/Disp
};

struct Line {
    std::vector<std::string> labels;
    enum Kind { None, Org, Dw, Db, Instr } kind = None;
    std::vector<std::vector<Syllable>> exprList;  // Org (1)/Dw/Db (N)
    std::string mnemonic;                           // Instr
    std::vector<Operand> operands;                  // Instr
};

std::string toLower(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<Syllable> exprToSyllables(RccAsmParser::ExprContext *ctx) {
    std::vector<Syllable> result;
    for (auto *syl : ctx->syllable()) {
        char sign = 0;
        if (auto *s = syl->sign()) sign = s->PLUS() ? '+' : '-';
        auto *atom = syl->atom();
        if (atom->expr() != nullptr) {
            Syllable g;
            g.sign = sign;
            g.kind = SylKind::Group;
            g.group = exprToSyllables(atom->expr());
            result.push_back(std::move(g));
        } else if (atom->NUMBER()) {
            result.push_back({sign, SylKind::Number, atom->NUMBER()->getText(), {}});
        } else {
            result.push_back({sign, SylKind::Symbol, atom->IDENT()->getText(), {}});
        }
    }
    if (result.empty()) fail("empty expression");
    return result;
}

// Register codes: 0-3 = R0-R3, 4-5 = A0-A1, 6 = SP, 7 = FB, 8 = R2R0
// (32-bit pair: R0 low word, R2 high word), 9 = R3R1 (32-bit pair: R1 low
// word, R3 high word) - this dialect's own numbering, unrelated to
// Renesas's real register-field encoding (same convention M32cAssembler.cpp
// uses, kept identical across the family - see R32cAssembler.h's header
// comment). Returns -1 if `name` is none of those.
int regNumber(const std::string &lower) {
    if (lower == "r2r0") return 8;
    if (lower == "r3r1") return 9;
    if (lower.size() == 2 && lower[0] == 'r' && lower[1] >= '0' && lower[1] <= '3') return lower[1] - '0';
    if (lower == "a0") return 4;
    if (lower == "a1") return 5;
    if (lower == "sp") return 6;
    if (lower == "fb") return 7;
    return -1;
}

Operand buildOperand(RccAsmParser::OperandContext *ctx) {
    if (ctx->LBRACKET()) {
        std::vector<Syllable> syls = exprToSyllables(ctx->expr());
        int baseReg = -1;
        size_t baseIdx = 0;
        for (size_t i = 0; i < syls.size(); i++) {
            if (syls[i].kind != SylKind::Symbol) continue;
            if (int r = regNumber(toLower(syls[i].text)); r >= 0) {
                baseReg = r;
                baseIdx = i;
                break;
            }
        }
        if (baseReg < 0) {
            fail("bracketed memory operand needs a base register, e.g. '[fb-4]' - this target's own dialect "
                 "has no other bracketed addressing mode (see R32cCodegen.h)");
        }
        Operand op;
        op.kind = Operand::Disp;
        op.reg = baseReg;
        for (size_t i = 0; i < syls.size(); i++) {
            if (i != baseIdx) op.expr.push_back(syls[i]);
        }
        if (op.expr.empty()) op.expr.push_back({0, SylKind::Number, "0", {}});
        return op;
    }
    std::vector<Syllable> syls = exprToSyllables(ctx->expr());
    Operand op;
    if (syls.size() == 1 && syls[0].sign == 0 && syls[0].kind == SylKind::Symbol) {
        if (int r = regNumber(toLower(syls[0].text)); r >= 0) {
            op.kind = Operand::Reg;
            op.reg = r;
            return op;
        }
    }
    bool allNumbers = true;
    for (const auto &s : syls) {
        if (s.kind != SylKind::Number) {
            allNumbers = false;
            break;
        }
    }
    op.kind = allNumbers ? Operand::Imm : Operand::Mem;
    op.expr = std::move(syls);
    return op;
}

class ThrowingErrorListener : public antlr4::BaseErrorListener {
   public:
    void syntaxError(antlr4::Recognizer *, antlr4::Token *, size_t line, size_t col, const std::string &msg,
                      std::exception_ptr) override {
        fail(std::to_string(line) + ":" + std::to_string(col) + ": " + msg);
    }
};

// A mnemonic's size suffix (`.w`/`.b`/`.l`) is cosmetic here (the mnemonic's
// own spelling, not a dotted suffix, picks the operand size - `mov`/
// `movb`/`movl`), so it's stripped before table lookup.
std::string stripSizeSuffix(const std::string &mnemonic) {
    auto dot = mnemonic.rfind('.');
    if (dot == std::string::npos) return mnemonic;
    return mnemonic.substr(0, dot);
}

Line buildLine(RccAsmParser::LineContext *ctx) {
    Line line;
    for (auto *lbl : ctx->label()) line.labels.push_back(lbl->IDENT()->getText());

    auto *stmt = ctx->statement();
    if (stmt == nullptr) {
        line.kind = Line::None;
        return line;
    }
    if (dynamic_cast<RccAsmParser::AssignStmtContext *>(stmt)) {
        fail("'='-style equates aren't part of this target's dialect");
    }
    if (dynamic_cast<RccAsmParser::TimesStmtContext *>(stmt)) fail("'times' isn't part of this target's dialect");
    auto *instr = dynamic_cast<RccAsmParser::InstrStmtContext *>(stmt);
    if (!instr) fail("internal: unhandled statement alternative");

    std::string mnemonic = toLower(instr->mnemonic->getText());
    if (mnemonic == "org") {
        line.kind = Line::Org;
        if (instr->operand().empty() || instr->operand(0)->LBRACKET()) fail("'org' needs a plain address operand");
        line.exprList.push_back(exprToSyllables(instr->operand(0)->expr()));
        return line;
    }
    if (mnemonic == "dw") {
        line.kind = Line::Dw;
        for (auto *op : instr->operand()) {
            if (op->LBRACKET()) fail("'dw' operand can't be bracketed");
            line.exprList.push_back(exprToSyllables(op->expr()));
        }
        return line;
    }
    if (mnemonic == "db") {
        line.kind = Line::Db;
        for (auto *op : instr->operand()) {
            if (op->LBRACKET()) fail("'db' operand can't be bracketed");
            line.exprList.push_back(exprToSyllables(op->expr()));
        }
        return line;
    }

    line.kind = Line::Instr;
    line.mnemonic = stripSizeSuffix(mnemonic);
    for (auto *op : instr->operand()) line.operands.push_back(buildOperand(op));
    return line;
}

std::vector<Line> parseProgram(const std::string &text) {
    antlr4::ANTLRInputStream input(text);
    RccAsmLexer lexer(&input);
    ThrowingErrorListener errors;
    lexer.removeErrorListeners();
    lexer.addErrorListener(&errors);

    antlr4::CommonTokenStream tokens(&lexer);
    RccAsmParser parser(&tokens);
    parser.removeErrorListeners();
    parser.addErrorListener(&errors);

    RccAsmParser::ProgramContext *prog = parser.program();

    std::vector<Line> lines;
    for (auto *lineCtx : prog->line()) lines.push_back(buildLine(lineCtx));
    return lines;
}

struct Relocation {
    uint32_t byteOffset;
    std::string symbolName;
};

enum class OpWidth { Byte, Word, Long };

// --- Two-pass assembly ----------------------------------------------------
class Assembler {
   public:
    explicit Assembler(bool allowExternal) : allowExternal_(allowExternal) {}

    void run(const std::vector<Line> &lines) {
        stage_ = 1;
        pass(lines);
        stage_ = 2;
        pass(lines);
    }

    const std::vector<uint8_t> &bytes() const { return bytes_; }
    uint32_t orgBase() const { return orgBase_; }
    const std::unordered_map<std::string, uint32_t> &labels() const { return labels_; }
    const std::vector<Relocation> &relocations() const { return relocations_; }

   private:
    void pass(const std::vector<Line> &lines) {
        bytes_.clear();
        relocations_.clear();
        offset_ = 0;
        orgBase_ = 0;
        for (const auto &line : lines) processLine(line);
    }

    void processLine(const Line &line) {
        for (const auto &lbl : line.labels) labels_[lbl] = orgBase_ + offset_;
        switch (line.kind) {
            case Line::None:
                return;
            case Line::Org:
                orgBase_ = evalExpr(line.exprList.at(0));
                return;
            case Line::Dw:
                for (const auto &e : line.exprList) emit16(static_cast<uint16_t>(evalExpr(e)));
                return;
            case Line::Db:
                for (const auto &e : line.exprList) emit8(static_cast<uint8_t>(evalExpr(e)));
                return;
            case Line::Instr:
                encodeInstr(line.mnemonic, line.operands);
                return;
        }
    }

    uint32_t evalExpr(const std::vector<Syllable> &syls, std::string *externalName = nullptr,
                       bool *needsBaseShift = nullptr) {
        uint32_t total = 0;
        for (const auto &syl : syls) {
            bool wasExternal = false, wasLocalAddress = false;
            uint32_t v = resolveSyllable(syl, &wasExternal, &wasLocalAddress);
            if (wasExternal && externalName) *externalName = syl.text;
            if (wasLocalAddress && needsBaseShift) *needsBaseShift = true;
            if (syl.sign == '-') {
                total -= v;
            } else {
                total += v;
            }
        }
        return total;
    }

    uint32_t resolveSyllable(const Syllable &syl, bool *wasExternal = nullptr, bool *wasLocalAddress = nullptr) {
        if (syl.kind == SylKind::Group) return evalExpr(syl.group);
        if (syl.kind == SylKind::Number) {
            const std::string &t = syl.text;
            if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
                return static_cast<uint32_t>(std::stoul(t.substr(2), nullptr, 16));
            }
            return static_cast<uint32_t>(std::stoul(t));
        }
        auto it = labels_.find(syl.text);
        if (it != labels_.end()) {
            if (allowExternal_ && wasLocalAddress) *wasLocalAddress = true;
            return it->second;
        }
        if (stage_ == 2) {
            if (allowExternal_) {
                if (wasExternal) *wasExternal = true;
                return 0;
            }
            fail("undefined symbol '" + syl.text + "'");
        }
        return 0;
    }

    void emit8(uint8_t b) {
        bytes_.push_back(b);
        offset_ += 1;
    }

    void emit16(uint16_t w) {
        emit8(static_cast<uint8_t>(w & 0xFF));
        emit8(static_cast<uint8_t>((w >> 8) & 0xFF));
    }

    void emit32(uint32_t v) {
        emit16(static_cast<uint16_t>(v & 0xFFFF));
        emit16(static_cast<uint16_t>((v >> 16) & 0xFFFF));
    }

    // Evaluates and emits an absolute-address field, recording a relocation
    // against ".." when the expression touched a local address and/or a
    // real relocation when it referenced an external symbol. Addresses in
    // this dialect always stay a 32-bit field, regardless of the
    // instruction's own operand-size class - see R32cAssembler.h's header
    // comment on why this differs from M32cAssembler.cpp's 16-bit field.
    void emitAddr32(const std::vector<Syllable> &expr) {
        std::string externalName;
        bool needsBaseShift = false;
        uint32_t v = evalExpr(expr, &externalName, &needsBaseShift);
        if (stage_ == 2 && allowExternal_) {
            if (needsBaseShift) relocations_.push_back({offset_, ".."});
            if (!externalName.empty()) relocations_.push_back({offset_, externalName});
        }
        emit32(v);
    }

    // --- operand encoding --------------------------------------------
    // This dialect's own invented operand encoding: a one-byte tag
    // (0=register, 1=immediate, 2=absolute address, 3=frame-base-relative)
    // followed by that mode's value bytes. An immediate's own value field is
    // 1/2/4 bytes depending on `width` (an address field, for Mem/Disp's own
    // base displacement, stays the fixed width each uses - 32 bits for an
    // absolute `Mem` address, 16 bits for a `Disp` frame offset, since the
    // latter is a small signed displacement, not a pointer into this
    // dialect's own flat label space).
    static constexpr uint8_t kTagReg = 0, kTagImm = 1, kTagMem = 2, kTagDisp = 3;

    void encodeOperand(const Operand &op, OpWidth width) {
        switch (op.kind) {
            case Operand::Reg:
                emit8(kTagReg);
                emit8(static_cast<uint8_t>(op.reg));
                return;
            case Operand::Imm: {
                emit8(kTagImm);
                uint32_t v = evalExpr(op.expr);
                if (width == OpWidth::Byte) {
                    emit8(static_cast<uint8_t>(v));
                } else if (width == OpWidth::Word) {
                    emit16(static_cast<uint16_t>(v));
                } else {
                    emit32(v);
                }
                return;
            }
            case Operand::Mem:
                emit8(kTagMem);
                emitAddr32(op.expr);
                return;
            case Operand::Disp:
                emit8(kTagDisp);
                emit8(static_cast<uint8_t>(op.reg));
                emit16(static_cast<uint16_t>(evalExpr(op.expr)));
                return;
        }
    }

    static const Operand &requireOperand(const std::vector<Operand> &ops, size_t i, const std::string &mnemonic) {
        if (ops.size() <= i) fail("'" + mnemonic + "': missing operand");
        return ops[i];
    }
    static void requireOperandCount(const std::vector<Operand> &ops, size_t want, const std::string &mnemonic) {
        if (ops.size() != want) {
            fail("'" + mnemonic + "' needs exactly " + std::to_string(want) + " operand(s)");
        }
    }
    static uint8_t requireReg(const Operand &op, const std::string &mnemonic) {
        if (op.kind != Operand::Reg) fail("'" + mnemonic + "' needs a register operand");
        return static_cast<uint8_t>(op.reg);
    }
    // A register operand in a byte/word instruction must name one of the
    // eight 16-bit registers (0-7); a register operand in a dword (`*l`)
    // instruction must name one of the two 32-bit pairs (8/9, `r2r0`/
    // `r3r1`) - R32C has no way to address a bare `r0`-`r3`/`a0`/`a1` as a
    // dword operand, and no way to address `r2r0`/`r3r1` as a byte/word one
    // (same restriction M32cAssembler.cpp documents for M32C).
    static void checkRegWidth(const Operand &op, OpWidth width, const std::string &mnemonic) {
        if (op.kind != Operand::Reg) return;
        bool isPair = op.reg == 8 || op.reg == 9;
        if (width == OpWidth::Long && !isPair) {
            fail("'" + mnemonic + "': a dword operand needs a register pair ('r2r0'/'r3r1'), not a plain register");
        }
        if (width != OpWidth::Long && isPair) {
            fail("'" + mnemonic + "': a byte/word operand can't be a register pair ('r2r0'/'r3r1')");
        }
    }
    // `mul`/`divu`'s destination is always r0; `mull`/`divul`/`divxl`'s is
    // always the `r2r0` pair.
    static void requireFixedAccumulator(const Operand &op, OpWidth width, const std::string &mnemonic) {
        int want = width == OpWidth::Long ? 8 : 0;
        if (op.kind != Operand::Reg || op.reg != want) {
            fail("'" + mnemonic + "': destination must be " + (width == OpWidth::Long ? "r2r0" : "r0"));
        }
    }

    // The two-operand "dst = dst OP src" family (`mov`/`movb`/`movl` are the
    // exception, "dst = src"). `writesDst` rejects an immediate destination;
    // `cmp`/`cmpb`/`cmpl` are the exception (read-only).
    void encodeTwoOp(uint8_t opcode, const std::string &mnemonic, const std::vector<Operand> &ops, bool writesDst,
                      OpWidth width) {
        requireOperandCount(ops, 2, mnemonic);
        const Operand &src = ops[0];
        const Operand &dst = ops[1];
        if (writesDst && dst.kind == Operand::Imm) fail("'" + mnemonic + "': an immediate can't be a destination");
        checkRegWidth(src, width, mnemonic);
        checkRegWidth(dst, width, mnemonic);
        emit8(opcode);
        encodeOperand(src, width);
        encodeOperand(dst, width);
    }

    // The one-operand family (not/neg/rol/ror and their byte/dword
    // variants - all write their own operand).
    void encodeOneOp(uint8_t opcode, const std::string &mnemonic, const std::vector<Operand> &ops, OpWidth width) {
        requireOperandCount(ops, 1, mnemonic);
        if (ops[0].kind == Operand::Imm) fail("'" + mnemonic + "': an immediate can't be a destination");
        checkRegWidth(ops[0], width, mnemonic);
        emit8(opcode);
        encodeOperand(ops[0], width);
    }

    // The full R32C instruction vocabulary this dialect supports - see this
    // file's own top-of-file comment for exactly which real R32C
    // instruction groups are in scope and which are deliberately left out.
    // This opcode table is this dialect's own invented numbering - distinct
    // from M32cAssembler.cpp's own table - not a real R32C opcode map (see
    // this file's own header comment).
    void encodeInstr(const std::string &mnemonic, const std::vector<Operand> &ops) {
        static const std::unordered_map<std::string, uint8_t> wordTwoOpWrite = {
            {"mov", 0x10}, {"add", 0x11}, {"sub", 0x12}, {"and", 0x13}, {"or", 0x14}, {"xor", 0x15},
            {"shl", 0x16}, {"sha", 0x17}, {"adc", 0x18}, {"sbb", 0x19},
        };
        static const std::unordered_map<std::string, uint8_t> byteTwoOpWrite = {
            {"movb", 0x40}, {"addb", 0x41}, {"subb", 0x42}, {"andb", 0x43}, {"orb", 0x44}, {"xorb", 0x45},
            {"shlb", 0x46}, {"shab", 0x47}, {"adcb", 0x48}, {"sbbb", 0x49},
        };
        // `shll`/`shal` aren't in this table: their count operand is always
        // plain word-width even though their destination is a dword pair -
        // see their own bespoke handling below.
        static const std::unordered_map<std::string, uint8_t> longTwoOpWrite = {
            {"movl", 0x60}, {"addl", 0x61}, {"subl", 0x62}, {"andl", 0x63}, {"orl", 0x64}, {"xorl", 0x65},
            {"adcl", 0x66}, {"sbbl", 0x67},
        };
        if (auto it = wordTwoOpWrite.find(mnemonic); it != wordTwoOpWrite.end()) {
            encodeTwoOp(it->second, mnemonic, ops, /*writesDst=*/true, OpWidth::Word);
            return;
        }
        if (auto it = byteTwoOpWrite.find(mnemonic); it != byteTwoOpWrite.end()) {
            encodeTwoOp(it->second, mnemonic, ops, /*writesDst=*/true, OpWidth::Byte);
            return;
        }
        if (auto it = longTwoOpWrite.find(mnemonic); it != longTwoOpWrite.end()) {
            encodeTwoOp(it->second, mnemonic, ops, /*writesDst=*/true, OpWidth::Long);
            return;
        }
        if (mnemonic == "cmp") {
            encodeTwoOp(0x1A, mnemonic, ops, /*writesDst=*/false, OpWidth::Word);
            return;
        }
        if (mnemonic == "cmpb") {
            encodeTwoOp(0x4A, mnemonic, ops, /*writesDst=*/false, OpWidth::Byte);
            return;
        }
        if (mnemonic == "cmpl") {
            encodeTwoOp(0x68, mnemonic, ops, /*writesDst=*/false, OpWidth::Long);
            return;
        }
        static const std::unordered_map<std::string, uint8_t> wordOneOp = {
            {"not", 0x1B}, {"neg", 0x1C}, {"rol", 0x1D}, {"ror", 0x1E},
        };
        static const std::unordered_map<std::string, uint8_t> byteOneOp = {
            {"notb", 0x4B}, {"negb", 0x4C}, {"rolb", 0x4D}, {"rorb", 0x4E},
        };
        static const std::unordered_map<std::string, uint8_t> longOneOp = {
            {"notl", 0x69}, {"negl", 0x6A}, {"roll", 0x6B}, {"rorl", 0x6C},
        };
        if (auto it = wordOneOp.find(mnemonic); it != wordOneOp.end()) {
            encodeOneOp(it->second, mnemonic, ops, OpWidth::Word);
            return;
        }
        if (auto it = byteOneOp.find(mnemonic); it != byteOneOp.end()) {
            encodeOneOp(it->second, mnemonic, ops, OpWidth::Byte);
            return;
        }
        if (auto it = longOneOp.find(mnemonic); it != longOneOp.end()) {
            encodeOneOp(it->second, mnemonic, ops, OpWidth::Long);
            return;
        }
        if (mnemonic == "mul" || mnemonic == "divu") {
            requireOperandCount(ops, 2, mnemonic);
            requireFixedAccumulator(ops[1], OpWidth::Word, mnemonic);
            emit8(mnemonic == "mul" ? 0x1F : 0x20);
            encodeOperand(ops[0], OpWidth::Word);
            return;
        }
        if (mnemonic == "mull" || mnemonic == "divul" || mnemonic == "divxl") {
            requireOperandCount(ops, 2, mnemonic);
            requireFixedAccumulator(ops[1], OpWidth::Long, mnemonic);
            checkRegWidth(ops[0], OpWidth::Long, mnemonic);
            emit8(mnemonic == "mull" ? 0x6D : mnemonic == "divul" ? 0x6E : 0x6F);
            encodeOperand(ops[0], OpWidth::Long);
            return;
        }
        // `shll`/`shal` - dword shift by a variable count. Unlike every
        // other `*l` instruction, the count operand (`ops[0]`) is always
        // plain word-width (a small number, never itself a dword value)
        // even though the shifted value (`ops[1]`) is the dword pair.
        if (mnemonic == "shll" || mnemonic == "shal") {
            requireOperandCount(ops, 2, mnemonic);
            const Operand &count = ops[0];
            const Operand &dst = ops[1];
            if (dst.kind == Operand::Imm) fail("'" + mnemonic + "': an immediate can't be a destination");
            checkRegWidth(count, OpWidth::Word, mnemonic);
            checkRegWidth(dst, OpWidth::Long, mnemonic);
            emit8(mnemonic == "shll" ? 0x70 : 0x71);
            encodeOperand(count, OpWidth::Word);
            encodeOperand(dst, OpWidth::Long);
            return;
        }
        if (mnemonic == "push" || mnemonic == "pushl") {
            requireOperandCount(ops, 1, mnemonic);
            OpWidth width = mnemonic == "push" ? OpWidth::Word : OpWidth::Long;
            checkRegWidth(ops[0], width, mnemonic);
            uint8_t rn = requireReg(ops[0], mnemonic);
            emit8(mnemonic == "push" ? 0x21 : 0x72);
            emit8(rn);
            return;
        }
        if (mnemonic == "pop" || mnemonic == "popl") {
            requireOperandCount(ops, 1, mnemonic);
            OpWidth width = mnemonic == "pop" ? OpWidth::Word : OpWidth::Long;
            checkRegWidth(ops[0], width, mnemonic);
            uint8_t rn = requireReg(ops[0], mnemonic);
            emit8(mnemonic == "pop" ? 0x22 : 0x73);
            emit8(rn);
            return;
        }
        // `ld addrReg,dst` / `st src,addrReg` - register-indirect access
        // through an address register; byte (`ldb`/`stb`) and dword
        // (`ldl`/`stl`) variants only differ in which register class `dst`/
        // `src` may name.
        if (mnemonic == "ld" || mnemonic == "ldb" || mnemonic == "ldl") {
            requireOperandCount(ops, 2, mnemonic);
            OpWidth width = mnemonic == "ld" ? OpWidth::Word : mnemonic == "ldb" ? OpWidth::Byte : OpWidth::Long;
            uint8_t addr = requireReg(ops[0], mnemonic);
            checkRegWidth(ops[1], width, mnemonic);
            uint8_t dst = requireReg(ops[1], mnemonic);
            emit8(mnemonic == "ld" ? 0x23 : mnemonic == "ldb" ? 0x4F : 0x74);
            emit8(addr);
            emit8(dst);
            return;
        }
        if (mnemonic == "st" || mnemonic == "stb" || mnemonic == "stl") {
            requireOperandCount(ops, 2, mnemonic);
            OpWidth width = mnemonic == "st" ? OpWidth::Word : mnemonic == "stb" ? OpWidth::Byte : OpWidth::Long;
            checkRegWidth(ops[0], width, mnemonic);
            uint8_t src = requireReg(ops[0], mnemonic);
            uint8_t addr = requireReg(ops[1], mnemonic);
            emit8(mnemonic == "st" ? 0x24 : mnemonic == "stb" ? 0x50 : 0x75);
            emit8(src);
            emit8(addr);
            return;
        }
        if (mnemonic == "lea" || mnemonic == "leal") {
            requireOperandCount(ops, 2, mnemonic);
            if (ops[0].kind != Operand::Mem && ops[0].kind != Operand::Disp) {
                fail("'" + mnemonic + "': source must be a label or a frame-relative '[fb+N]'/'[fb-N]' address");
            }
            OpWidth width = mnemonic == "lea" ? OpWidth::Word : OpWidth::Long;
            checkRegWidth(ops[1], width, mnemonic);
            uint8_t dst = requireReg(ops[1], mnemonic);
            emit8(mnemonic == "lea" ? 0x25 : 0x76);
            encodeOperand(ops[0], OpWidth::Word);
            emit8(dst);
            return;
        }
        if (mnemonic == "jsr") {
            requireOperandCount(ops, 1, mnemonic);
            if (ops[0].kind != Operand::Mem) fail("'jsr': target must be a label");
            emit8(0x26);
            emitAddr32(ops[0].expr);
            return;
        }
        if (mnemonic == "jsri") {
            requireOperandCount(ops, 1, mnemonic);
            if (ops[0].kind != Operand::Mem) fail("'jsri': target must be a label");
            emit8(0x27);
            emitAddr32(ops[0].expr);
            return;
        }
        if (mnemonic == "rts") {
            requireOperandCount(ops, 0, mnemonic);
            emit8(0x28);
            return;
        }
        if (mnemonic == "jmp") {
            requireOperandCount(ops, 1, mnemonic);
            if (ops[0].kind != Operand::Mem) fail("'jmp': target must be a label");
            emit8(0x29);
            emitAddr32(ops[0].expr);
            return;
        }
        static const std::unordered_map<std::string, uint8_t> condJump = {
            {"jeq", 0x2A}, {"jne", 0x2B}, {"jlt", 0x2C}, {"jle", 0x2D}, {"jgt", 0x2E}, {"jge", 0x2F},
            {"jltu", 0x30}, {"jleu", 0x31}, {"jgtu", 0x32}, {"jgeu", 0x33},
        };
        if (auto it = condJump.find(mnemonic); it != condJump.end()) {
            requireOperandCount(ops, 1, mnemonic);
            if (ops[0].kind != Operand::Mem) fail("'" + mnemonic + "': target must be a label");
            emit8(it->second);
            emitAddr32(ops[0].expr);
            return;
        }
        if (mnemonic == "clc" || mnemonic == "stc") {
            requireOperandCount(ops, 0, mnemonic);
            emit8(mnemonic == "clc" ? 0x34 : 0x35);
            return;
        }
        if (mnemonic == "fset" || mnemonic == "fclr") {
            requireOperandCount(ops, 1, mnemonic);
            if (toLower(ops[0].expr.empty() ? "" : ops[0].expr[0].text) != "i" || ops[0].kind != Operand::Mem) {
                fail("'" + mnemonic + "' only supports the 'i' (interrupt enable) flag");
            }
            emit8(mnemonic == "fset" ? 0x36 : 0x37);
            return;
        }
        // `bset bit,dst` / `bclr bit,dst` / `bnot bit,dst` - set/clear/
        // invert one bit (0-7) of a byte register/memory operand; `btst
        // bit,src` only reads it (sets flags, like `cmp`).
        if (mnemonic == "bset" || mnemonic == "bclr" || mnemonic == "btst" || mnemonic == "bnot") {
            requireOperandCount(ops, 2, mnemonic);
            static const std::unordered_map<std::string, uint8_t> bitOps = {
                {"bset", 0x80}, {"bclr", 0x81}, {"btst", 0x82}, {"bnot", 0x83}};
            if (ops[0].kind != Operand::Imm) fail("'" + mnemonic + "': bit number must be an immediate 0-7");
            if (mnemonic != "btst" && ops[1].kind == Operand::Imm) {
                fail("'" + mnemonic + "': an immediate can't be a destination");
            }
            emit8(bitOps.at(mnemonic));
            encodeOperand(ops[0], OpWidth::Byte);
            encodeOperand(ops[1], OpWidth::Byte);
            return;
        }
        // `pushm mask` / `popm mask` - save/restore every register named by
        // a bit set in `mask` (bit N = register code N, 0-7), in one
        // instruction.
        if (mnemonic == "pushm" || mnemonic == "popm") {
            requireOperandCount(ops, 1, mnemonic);
            if (ops[0].kind != Operand::Imm) fail("'" + mnemonic + "': operand must be an immediate register mask");
            emit8(mnemonic == "pushm" ? 0x84 : 0x85);
            encodeOperand(ops[0], OpWidth::Byte);
            return;
        }
        // `enter N` - the compound "push fb; mov sp,fb; sub N,sp" prologue
        // in one instruction; `exitd` is its matching epilogue+`rts`.
        if (mnemonic == "enter") {
            requireOperandCount(ops, 1, mnemonic);
            if (ops[0].kind != Operand::Imm) fail("'enter': operand must be an immediate frame size");
            emit8(0x86);
            encodeOperand(ops[0], OpWidth::Word);
            return;
        }
        if (mnemonic == "exitd" || mnemonic == "reit" || mnemonic == "nop" || mnemonic == "brk" ||
            mnemonic == "und") {
            requireOperandCount(ops, 0, mnemonic);
            static const std::unordered_map<std::string, uint8_t> zeroOp = {
                {"exitd", 0x87}, {"reit", 0x88}, {"nop", 0x89}, {"brk", 0x8A}, {"und", 0x8B}};
            emit8(zeroOp.at(mnemonic));
            return;
        }
        // `exts` - sign-extend `r0` into the `r2r0` pair (`r2` becomes
        // `r0`'s sign-fill word); `extz` - zero-extend the same way. Both
        // operate on the fixed `r0`/`r2r0` pair, so neither takes an
        // operand.
        if (mnemonic == "exts" || mnemonic == "extz") {
            requireOperandCount(ops, 0, mnemonic);
            emit8(mnemonic == "exts" ? 0x8C : 0x8D);
            return;
        }
        fail("unsupported instruction '" + mnemonic + "'");
    }

    std::vector<uint8_t> bytes_;
    std::unordered_map<std::string, uint32_t> labels_;
    std::vector<Relocation> relocations_;
    uint32_t offset_ = 0;
    uint32_t orgBase_ = 0;
    int stage_ = 1;
    bool allowExternal_;
};

ObjectFile buildObjectFile(const Assembler &a) {
    ObjectFile obj;
    obj.machineType = kR32cCoffMachine;
    const auto &bytesVec = a.bytes();
    obj.textBytes.assign(bytesVec.begin(), bytesVec.end());

    for (const auto &kv : a.labels()) {
        obj.symbols.push_back({kv.first, kv.second - a.orgBase(), /*isDefined=*/true});
    }
    std::sort(obj.symbols.begin(), obj.symbols.end(),
              [](const ObjSymbol &x, const ObjSymbol &y) { return x.name < y.name; });

    std::unordered_map<std::string, bool> known;
    for (const auto &s : obj.symbols) known[s.name] = true;
    for (const auto &r : a.relocations()) {
        if (!known.count(r.symbolName)) {
            obj.symbols.push_back({r.symbolName, 0, /*isDefined=*/false});
            known[r.symbolName] = true;
        }
        obj.relocations.push_back({r.byteOffset, r.symbolName, kR32cRelocAbs32});
    }
    return obj;
}

}  // namespace

std::string assembleR32c(const std::string &asmText) {
    std::vector<Line> lines = parseProgram(asmText);
    Assembler assembler(/*allowExternal=*/false);
    assembler.run(lines);
    const auto &bytes = assembler.bytes();
    return std::string(bytes.begin(), bytes.end());
}

ObjectFile assembleR32cObject(const std::string &asmText) {
    std::vector<Line> lines = parseProgram(asmText);
    Assembler assembler(/*allowExternal=*/true);
    assembler.run(lines);
    return buildObjectFile(assembler);
}

}  // namespace rcc
