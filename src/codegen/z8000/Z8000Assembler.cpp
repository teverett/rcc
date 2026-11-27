#include "Z8000Assembler.h"

#include <algorithm>
#include <sstream>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ANTLRInputStream.h"
#include "BaseErrorListener.h"
#include "CommonTokenStream.h"
#include "RccAsmLexer.h"
#include "RccAsmParser.h"

namespace rcc {

namespace {

[[noreturn]] void fail(const std::string &msg) { throw std::runtime_error("Z8000Assembler: " + msg); }

// --- Parsing, via the shared grammar/rccasm/RccAsm.g4 --------------------------------------------
enum class SylKind { Number, Symbol, Group };
struct Syllable {
    char sign = 0;
    SylKind kind;
    std::string text;
    std::vector<Syllable> group;
};

// `Reg`: a bare `r0`-`r15` (word) or `rl0`-`rl7`/`rh0`-`rh7` (byte) identifier - `reg` holds the
// raw instruction-field register number (0-15 either way; see regNumberByte()). `Imm`: a bare
// (optionally signed) number. `Mem`: anything else (a label, or `label+N`) - this backend's own
// direct-address mode (never PC-relative - see Z8000Assembler.h's own header comment). `Indexed`:
// the `[Rn+N]`/`[Rn-N]` bracket form - a literal, compile-time-constant displacement added to
// `reg`, used for frame-pointer-relative Local/Param addressing and for a pointer dereference at
// a constant offset.
struct Operand {
    enum Kind { Reg, Imm, Mem, Indexed } kind = Imm;
    int reg = -1;
    std::vector<Syllable> expr;
};

struct Line {
    std::vector<std::string> labels;
    enum Kind { None, Equate, Org, Dw, Db, Instr, DebugLine } kind = None;
    int debugLine = 0;  // DebugLine
    std::string equateName;
    std::vector<std::vector<Syllable>> exprList;
    std::string mnemonic;
    std::vector<Operand> operands;
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

// `r0`-`r15` (case-insensitive); -1 if not a plain word register name.
int wordRegNumber(const std::string &lower) {
    if (lower.size() < 2 || lower.size() > 3 || lower[0] != 'r') return -1;
    if (lower[1] < '0' || lower[1] > '9') return -1;
    try {
        size_t consumed = 0;
        int n = std::stoi(lower.substr(1), &consumed);
        if (consumed != lower.size() - 1) return -1;
        return (n >= 0 && n <= 15) ? n : -1;
    } catch (...) {
        return -1;
    }
}

// `rl0`-`rl7`/`rh0`-`rh7` (case-insensitive); -1 if not a byte register name. Real Z8000 byte-
// register field encoding: `rh0`-`rh7` (the high byte of `r0`-`r7`) use field values 0-7;
// `rl0`-`rl7` (the low byte) use field values 8-15. `r8`-`r15` have no byte half.
int byteRegNumber(const std::string &lower) {
    if (lower.size() != 3 || lower[0] != 'r') return -1;
    if (lower[1] != 'l' && lower[1] != 'h') return -1;
    if (lower[2] < '0' || lower[2] > '7') return -1;
    int n = lower[2] - '0';
    return lower[1] == 'h' ? n : n + 8;
}

Operand buildOperand(RccAsmParser::OperandContext *ctx, bool byteContext) {
    if (ctx->LBRACKET()) {
        std::vector<Syllable> syls = exprToSyllables(ctx->expr());
        if (syls.empty() || syls[0].sign != 0 || syls[0].kind != SylKind::Symbol) {
            fail("'[...]' operand must start with a bare base register, e.g. '[r13+4]'");
        }
        int baseReg = wordRegNumber(toLower(syls[0].text));
        if (baseReg < 0) fail("'[...]' operand's base must be a word register");
        for (size_t i = 1; i < syls.size(); i++) {
            if (syls[i].kind != SylKind::Number) fail("'[...]' operand's displacement must be a literal number");
        }
        Operand op;
        op.kind = Operand::Indexed;
        op.reg = baseReg;
        op.expr.assign(syls.begin() + 1, syls.end());
        return op;
    }
    std::vector<Syllable> syls = exprToSyllables(ctx->expr());
    Operand op;
    if (syls.size() == 1 && syls[0].sign == 0 && syls[0].kind == SylKind::Symbol) {
        int r = byteContext ? byteRegNumber(toLower(syls[0].text)) : wordRegNumber(toLower(syls[0].text));
        if (r >= 0) {
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

// Mnemonics whose register operand(s) are byte registers (`rl0`-`rl7`/`rh0`-`rh7`), not word
// registers - needed before operands are built, since a bare register name is ambiguous between
// the two tables (`r3` is a word register; a byte register is always spelled `rl3`/`rh3`, but
// `buildOperand()` still needs to know which table to classify against before it can tell).
bool mnemonicIsByteSized(const std::string &m) {
    static const std::unordered_map<std::string, bool> byteOps = {
        {"ldb", true},  {"addb", true}, {"subb", true}, {"andb", true}, {"orb", true},  {"xorb", true},
        {"cpb", true},  {"comb", true}, {"negb", true}, {"testb", true}, {"tsetb", true}, {"clrb", true},
        {"incb", true}, {"decb", true}, {"rlcb", true}, {"rrcb", true},
        // `extsb` is NOT here: it takes one *word* register operand (e.g. `r0`), sign-
        // extending that register's own low byte into the rest of itself - unlike `exts`
        // (a long's pair-base register) it has no separate byte-register form.
    };
    auto it = byteOps.find(m);
    return it != byteOps.end();
}

Line buildLine(RccAsmParser::LineContext *ctx) {
    Line line;
    for (auto *lbl : ctx->label()) line.labels.push_back(lbl->IDENT()->getText());

    auto *stmt = ctx->statement();
    if (stmt == nullptr) {
        line.kind = Line::None;
        return line;
    }
    if (auto *assign = dynamic_cast<RccAsmParser::AssignStmtContext *>(stmt)) {
        line.kind = Line::Equate;
        line.equateName = assign->lhs->getText();
        line.exprList.push_back(exprToSyllables(assign->rhs));
        return line;
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
    if (mnemonic == "dw" || mnemonic == "db") {
        line.kind = mnemonic == "dw" ? Line::Dw : Line::Db;
        for (auto *op : instr->operand()) {
            if (op->LBRACKET()) fail("'" + mnemonic + "' operand can't be bracketed");
            line.exprList.push_back(exprToSyllables(op->expr()));
        }
        return line;
    }

    // "__line N" is a debug-map-only pseudo-op - not a real instruction, so
    // it's intercepted here the same way "org" is. Emits no byte and doesn't
    // advance `offset_`; Assembler::processLine() just records (current
    // address, N) into debugLines_ when it sees one.
    if (mnemonic == "__line") {
        if (instr->operand().empty() || instr->operand(0)->LBRACKET()) {
            fail("__line takes exactly one bare non-negative number");
        }
        std::vector<Syllable> syls = exprToSyllables(instr->operand(0)->expr());
        if (syls.size() != 1 || syls[0].kind != SylKind::Number || syls[0].sign != 0) {
            fail("__line takes exactly one bare non-negative number");
        }
        line.kind = Line::DebugLine;
        line.debugLine = std::stoi(syls[0].text);
        return line;
    }

    line.kind = Line::Instr;
    line.mnemonic = mnemonic;
    bool byteCtx = mnemonicIsByteSized(mnemonic);
    for (auto *op : instr->operand()) line.operands.push_back(buildOperand(op, byteCtx));
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
    uint16_t kind;
};

// --- Two-pass assembly ---------------------------------------------------------------------
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
    // (address, source line) pairs recorded from every "__line N"
    // pseudo-op (see Line::DebugLine), in program order - which is also
    // address order, same as Pdp7Assembler.cpp's own debugLines().
    const std::vector<std::pair<uint32_t, int>> &debugLines() const { return debugLines_; }

   private:
    void pass(const std::vector<Line> &lines) {
        bytes_.clear();
        relocations_.clear();
        debugLines_.clear();
        offset_ = 0;
        orgBase_ = 0;
        for (const auto &line : lines) processLine(line);
    }

    void processLine(const Line &line) {
        for (const auto &lbl : line.labels) labels_[lbl] = orgBase_ + offset_;
        switch (line.kind) {
            case Line::None: return;
            case Line::Equate:
                labels_[line.equateName] = evalExpr(line.exprList.at(0));
                return;
            case Line::Org:
                orgBase_ = evalExpr(line.exprList.at(0));
                return;
            case Line::Dw:
                for (const auto &e : line.exprList) {
                    std::string externalName;
                    uint32_t v = evalExpr(e, &externalName);
                    if (stage_ == 2 && allowExternal_ && !externalName.empty()) {
                        relocations_.push_back({offset_, externalName, kZ8000RelocAbs16BE});
                    }
                    emit16(static_cast<uint16_t>(v));
                }
                return;
            case Line::Db:
                for (const auto &e : line.exprList) emit8(static_cast<uint8_t>(evalExpr(e)));
                return;
            case Line::Instr:
                encodeInstr(line.mnemonic, line.operands);
                return;
            case Line::DebugLine:
                // No byte emitted, `offset_` untouched - this marker just
                // attributes whatever address the NEXT real instruction
                // lands at to a source line, same as a label would.
                if (stage_ == 2) debugLines_.push_back({orgBase_ + offset_, line.debugLine});
                return;
        }
    }

    uint32_t evalExpr(const std::vector<Syllable> &syls, std::string *externalName = nullptr) {
        uint32_t total = 0;
        for (const auto &syl : syls) {
            bool wasExternal = false;
            uint32_t v = resolveSyllable(syl, &wasExternal);
            if (wasExternal && externalName) *externalName = syl.text;
            if (syl.sign == '-') {
                total -= v;
            } else {
                total += v;
            }
        }
        return total;
    }

    uint32_t resolveSyllable(const Syllable &syl, bool *wasExternal = nullptr) {
        if (syl.kind == SylKind::Group) return evalExpr(syl.group);
        if (syl.kind == SylKind::Number) {
            const std::string &t = syl.text;
            if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
                return static_cast<uint32_t>(std::stoul(t.substr(2), nullptr, 16));
            }
            return static_cast<uint32_t>(std::stoul(t));
        }
        auto it = labels_.find(syl.text);
        if (it != labels_.end()) return it->second;
        if (stage_ == 2) {
            if (allowExternal_) {
                if (wasExternal) *wasExternal = true;
                return 0;
            }
            fail("undefined symbol '" + syl.text + "'");
        }
        return 0;
    }

    // Big-endian: the Z8000 is a big-endian machine (see Z8000Assembler.h's own header comment).
    void emit16(uint16_t w) {
        bytes_.push_back(static_cast<uint8_t>((w >> 8) & 0xFF));
        bytes_.push_back(static_cast<uint8_t>(w & 0xFF));
        offset_ += 2;
    }
    void emit8(uint8_t b) {
        bytes_.push_back(b);
        offset_ += 1;
    }

    static uint8_t requireReg(const Operand &op, const std::string &mnemonic) {
        if (op.kind != Operand::Reg) fail("'" + mnemonic + "' needs a register operand");
        return static_cast<uint8_t>(op.reg);
    }
    static const Operand &requireOperand(const std::vector<Operand> &ops, size_t i, const std::string &mnemonic) {
        if (ops.size() <= i) fail("'" + mnemonic + "': missing operand");
        return ops[i];
    }

    // Emits a label/number extension word for a direct-address (`Mem`) or `Imm` operand - both
    // are plain absolute 16-bit values on this target (never PC-relative).
    void emitAbsExtension(const Operand &op) {
        std::string externalName;
        uint32_t v = evalExpr(op.expr, &externalName);
        if (stage_ == 2 && allowExternal_ && !externalName.empty()) {
            relocations_.push_back({offset_, externalName, kZ8000RelocAbs16BE});
        }
        emit16(static_cast<uint16_t>(v));
    }

    // reg,reg: opcode = base | (srcReg<<4) | dstReg.
    void encodeRegReg(uint16_t base, const Operand &dst, const Operand &src, const std::string &mnemonic) {
        uint8_t d = requireReg(dst, mnemonic);
        uint8_t s = requireReg(src, mnemonic);
        emit16(static_cast<uint16_t>(base | (s << 4) | d));
    }

    // reg,imm: opcode = base | dstReg, followed by one extension word (the immediate - a byte
    // immediate still occupies a full word, in the low byte, high byte zero, matching the real
    // Z8000's own encoding).
    void encodeRegImm(uint16_t base, const Operand &dst, const Operand &imm, const std::string &mnemonic,
                       bool isByte) {
        uint8_t d = requireReg(dst, mnemonic);
        emit16(static_cast<uint16_t>(base | d));
        uint32_t v = evalExpr(imm.expr);
        emit16(isByte ? static_cast<uint16_t>(v & 0xFF) : static_cast<uint16_t>(v));
    }

    // One-register family at a fixed `base`, suboptype `sub` selecting the operation within it
    // (com/neg/test/tset/clr, or their byte counterparts) - see Z8000Codegen.h's own header
    // comment on the 0x8C00/0x8D00 single-operand families.
    void encodeRegSub(uint16_t base, uint8_t sub, const Operand &reg, const std::string &mnemonic) {
        uint8_t r = requireReg(reg, mnemonic);
        emit16(static_cast<uint16_t>(base | (r << 4) | sub));
    }

    // Direct-address (Global) load/store: opcode = base | reg, extension word = the label's own
    // absolute address.
    void encodeDirect(uint16_t base, const Operand &reg, const Operand &mem, const std::string &mnemonic) {
        uint8_t r = requireReg(reg, mnemonic);
        if (mem.kind != Operand::Mem) fail("'" + mnemonic + "': expected a label operand");
        emit16(static_cast<uint16_t>(base | r));
        emitAbsExtension(mem);
    }

    // Indexed (Local/Param/pointer-offset) load/store: opcode = base | (baseReg<<4) | reg,
    // extension word = the literal displacement.
    void encodeIndexed(uint16_t base, const Operand &reg, const Operand &indexed, const std::string &mnemonic) {
        uint8_t r = requireReg(reg, mnemonic);
        if (indexed.kind != Operand::Indexed) fail("'" + mnemonic + "': expected a '[Rn+N]' operand");
        // Base register 0 is reserved on real Z8000 hardware (this same opcode range's
        // `baseReg==0` case means PC-relative "program reference" addressing instead - a mode
        // this backend never emits) - reject it rather than silently miscompiling.
        if (indexed.reg == 0) fail("'" + mnemonic + "': '[r0+N]' can't be used as a base register");
        emit16(static_cast<uint16_t>(base | (indexed.reg << 4) | r));
        uint32_t disp = indexed.expr.empty() ? 0 : evalExpr(indexed.expr);
        emit16(static_cast<uint16_t>(disp));
    }

    void encodeInstr(const std::string &mnemonic, const std::vector<Operand> &ops) {
        // ld/ldb/lda: dst is always Reg; src is Reg, Imm, Mem (direct) or Indexed.
        if (mnemonic == "ld" || mnemonic == "ldb" || mnemonic == "lda") {
            const Operand &a = requireOperand(ops, 0, mnemonic);
            const Operand &b = requireOperand(ops, 1, mnemonic);
            bool isByte = mnemonic == "ldb";
            // `lda dst,src`: src must be addressable (never a register/immediate).
            if (mnemonic == "lda") {
                if (b.kind == Operand::Mem) return encodeDirect(0x7600, a, b, mnemonic);
                if (b.kind == Operand::Indexed) return encodeIndexed(0x3400, a, b, mnemonic);
                fail("'lda': source must be a label or '[Rn+N]'");
            }
            // dst,src both plain registers/immediate - or one side is memory.
            if (a.kind == Operand::Reg && b.kind == Operand::Reg) {
                return encodeRegReg(isByte ? 0xA000 : 0xA100, a, b, mnemonic);
            }
            if (a.kind == Operand::Reg && b.kind == Operand::Imm) {
                return encodeRegImm(isByte ? 0x2000 : 0x2100, a, b, mnemonic, isByte);
            }
            if (a.kind == Operand::Reg && b.kind == Operand::Mem) {
                return encodeDirect(isByte ? 0x6000 : 0x6100, a, b, mnemonic);
            }
            if (a.kind == Operand::Mem && b.kind == Operand::Reg) {
                return encodeDirect(isByte ? 0x6E00 : 0x6F00, b, a, mnemonic);
            }
            if (a.kind == Operand::Reg && b.kind == Operand::Indexed) {
                return encodeIndexed(isByte ? 0x3000 : 0x3100, a, b, mnemonic);
            }
            if (a.kind == Operand::Indexed && b.kind == Operand::Reg) {
                return encodeIndexed(isByte ? 0x3200 : 0x3300, b, a, mnemonic);
            }
            fail("'" + mnemonic + "': unsupported operand combination");
        }

        static const std::unordered_map<std::string, uint16_t> regRegWord = {
            {"add", 0x8100}, {"sub", 0x8300}, {"and", 0x8700}, {"or", 0x8500},
            {"xor", 0x8900}, {"cp", 0x8B00},   {"adc", 0xB500}, {"sbc", 0xB700},
            {"mult", 0x9900}, {"div", 0x9B00},
        };
        static const std::unordered_map<std::string, uint16_t> regRegByte = {
            {"addb", 0x8000}, {"subb", 0x8200}, {"andb", 0x8600}, {"orb", 0x8400},
            {"xorb", 0x8800}, {"cpb", 0x8A00},
        };
        static const std::unordered_map<std::string, uint16_t> regImmWord = {
            {"add", 0x0100}, {"sub", 0x0300}, {"and", 0x0700}, {"or", 0x0500}, {"xor", 0x0900}, {"cp", 0x0B00},
        };
        static const std::unordered_map<std::string, uint16_t> regImmByte = {
            {"addb", 0x0000}, {"subb", 0x0200}, {"andb", 0x0600}, {"orb", 0x0400}, {"xorb", 0x0800}, {"cpb", 0x0A00},
        };
        if (auto it = regRegWord.find(mnemonic); it != regRegWord.end()) {
            const Operand &dst = requireOperand(ops, 0, mnemonic);
            const Operand &src = requireOperand(ops, 1, mnemonic);
            if (src.kind == Operand::Imm) {
                return encodeRegImm(regImmWord.at(mnemonic), dst, src, mnemonic, /*isByte=*/false);
            }
            return encodeRegReg(it->second, dst, src, mnemonic);
        }
        if (auto it = regRegByte.find(mnemonic); it != regRegByte.end()) {
            const Operand &dst = requireOperand(ops, 0, mnemonic);
            const Operand &src = requireOperand(ops, 1, mnemonic);
            if (src.kind == Operand::Imm) {
                return encodeRegImm(regImmByte.at(mnemonic), dst, src, mnemonic, /*isByte=*/true);
            }
            return encodeRegReg(it->second, dst, src, mnemonic);
        }

        static const std::unordered_map<std::string, uint8_t> wordSub = {
            {"com", 0x0}, {"neg", 0x2}, {"test", 0x4}, {"tset", 0x6}, {"clr", 0x8},
        };
        if (auto it = wordSub.find(mnemonic); it != wordSub.end()) {
            return encodeRegSub(0x8D00, it->second, requireOperand(ops, 0, mnemonic), mnemonic);
        }
        static const std::unordered_map<std::string, uint8_t> byteSub = {
            {"comb", 0x0}, {"negb", 0x2}, {"testb", 0x4}, {"tsetb", 0x6}, {"clrb", 0x8},
        };
        if (auto it = byteSub.find(mnemonic); it != byteSub.end()) {
            return encodeRegSub(0x8C00, it->second, requireOperand(ops, 0, mnemonic), mnemonic);
        }
        if (mnemonic == "inc") return encodeRegSub(0xA900, 0x0, requireOperand(ops, 0, mnemonic), mnemonic);
        if (mnemonic == "dec") return encodeRegSub(0xAB00, 0x0, requireOperand(ops, 0, mnemonic), mnemonic);
        if (mnemonic == "incb") return encodeRegSub(0xA800, 0x0, requireOperand(ops, 0, mnemonic), mnemonic);
        if (mnemonic == "decb") return encodeRegSub(0xAA00, 0x0, requireOperand(ops, 0, mnemonic), mnemonic);
        if (mnemonic == "exts") return encodeRegSub(0xB100, 0xA, requireOperand(ops, 0, mnemonic), mnemonic);
        if (mnemonic == "extsb") return encodeRegSub(0xB100, 0x0, requireOperand(ops, 0, mnemonic), mnemonic);
        if (mnemonic == "rlc") return encodeRegSub(0xB300, 0x8, requireOperand(ops, 0, mnemonic), mnemonic);
        if (mnemonic == "rrc") return encodeRegSub(0xB300, 0xC, requireOperand(ops, 0, mnemonic), mnemonic);
        if (mnemonic == "rlcb") return encodeRegSub(0xB200, 0x8, requireOperand(ops, 0, mnemonic), mnemonic);
        if (mnemonic == "rrcb") return encodeRegSub(0xB200, 0xC, requireOperand(ops, 0, mnemonic), mnemonic);

        if (mnemonic == "clc") {
            if (!ops.empty()) fail("'clc' takes no operand");
            emit16(0x8D81);  // resflg c - clears the carry flag only
            return;
        }
        if (mnemonic == "nop") {
            if (!ops.empty()) fail("'nop' takes no operand");
            emit16(0x8D07);
            return;
        }
        if (mnemonic == "halt") {
            if (!ops.empty()) fail("'halt' takes no operand");
            emit16(0x7A00);
            return;
        }
        if (mnemonic == "ret") {
            if (!ops.empty()) fail("'ret' takes no operand");
            emit16(0x9E08);  // ret always
            return;
        }
        if (mnemonic == "push") {
            uint8_t r = requireReg(requireOperand(ops, 0, mnemonic), mnemonic);
            emit16(static_cast<uint16_t>(0x9300 | (15 << 4) | r));  // push @r15,r
            return;
        }
        if (mnemonic == "pop") {
            uint8_t r = requireReg(requireOperand(ops, 0, mnemonic), mnemonic);
            emit16(static_cast<uint16_t>(0x9700 | (15 << 4) | r));  // pop r,@r15
            return;
        }
        if (mnemonic == "call") {
            const Operand &target = requireOperand(ops, 0, mnemonic);
            if (target.kind == Operand::Mem) {
                emit16(0x5F00);
                emitAbsExtension(target);
                return;
            }
            fail("'call': target must be a label (use 'callind' for a register)");
        }
        if (mnemonic == "callind") {
            uint8_t r = requireReg(requireOperand(ops, 0, mnemonic), mnemonic);
            if (r == 0) fail("'callind': register 0 is reserved - use any of r1-r15");
            emit16(static_cast<uint16_t>(0x1F00 | (r << 4)));
            return;
        }

        // Conditional direct-address jump family (always targets a label - see
        // Z8000Codegen.h's own header comment on why this backend never uses the real Z8000's
        // short PC-relative `jr`).
        static const std::unordered_map<std::string, uint8_t> branchCond = {
            {"beq", 0x6}, {"bne", 0xE}, {"blt", 0x1}, {"ble", 0x2}, {"bgt", 0xA}, {"bge", 0x9},
            {"blo", 0x7}, {"blos", 0x3}, {"bhi", 0xB}, {"bhis", 0xF}, {"br", 0x8},
        };
        if (auto it = branchCond.find(mnemonic); it != branchCond.end()) {
            const Operand &target = requireOperand(ops, 0, mnemonic);
            if (target.kind != Operand::Mem) fail("'" + mnemonic + "': target must be a label");
            emit16(static_cast<uint16_t>(0x5E00 | it->second));
            emitAbsExtension(target);
            return;
        }

        fail("unsupported instruction '" + mnemonic + "'");
    }

    std::vector<uint8_t> bytes_;
    std::unordered_map<std::string, uint32_t> labels_;
    std::vector<Relocation> relocations_;
    std::vector<std::pair<uint32_t, int>> debugLines_;
    uint32_t offset_ = 0;
    uint32_t orgBase_ = 0;
    int stage_ = 1;
    bool allowExternal_;
};

ObjectFile buildObjectFile(const Assembler &a) {
    ObjectFile obj;
    obj.machineType = kZ8000CoffMachine;
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
        obj.relocations.push_back({r.byteOffset, r.symbolName, r.kind});
    }
    return obj;
}

// Plain-text FILE/SYM/LINE debug map (see Pdp7Assembler.cpp's own
// writeDebugMap(), which this mirrors - minus any RELATIVE-flag/base-shift
// handling, since labels() already holds plain final addresses for a
// whole-program org <realBase> compile). `symbols` is the exact
// (label, isFunc) list the codegen's own debugSymbols_ collects, not a
// prefix guess over every label this assembler ever sees.
std::string writeDebugMap(const Assembler &a, const std::string &sourcePath,
                           const std::vector<std::pair<std::string, bool>> &symbols) {
    std::ostringstream out;
    out << "FILE 1 " << sourcePath << "\n";
    std::vector<std::pair<std::string, bool>> sorted = symbols;
    std::sort(sorted.begin(), sorted.end());
    for (const auto &[name, isFunc] : sorted) {
        uint32_t addr = a.labels().at(name);
        std::string bare = name.substr(isFunc ? 3 : 2);  // strip "fn_"/"g_" - see label()
        out << "SYM " << addr << " " << bare << " " << (isFunc ? "FUNC" : "DATA") << "\n";
    }
    for (const auto &[addr, line] : a.debugLines()) {
        out << "LINE " << addr << " 1 " << line << "\n";
    }
    return out.str();
}

}  // namespace

std::string assembleZ8000(const std::string &asmText) {
    std::vector<Line> lines = parseProgram(asmText);
    Assembler assembler(/*allowExternal=*/false);
    assembler.run(lines);
    const auto &bytes = assembler.bytes();
    return std::string(bytes.begin(), bytes.end());
}

std::string assembleZ8000DebugMap(const std::string &asmText, const std::string &sourcePath,
                       const std::vector<std::pair<std::string, bool>> &symbols,
                                     bool allowExternal) {
    std::vector<Line> lines = parseProgram(asmText);
    Assembler assembler(/*allowExternal=*/allowExternal);
    assembler.run(lines);
    return writeDebugMap(assembler, sourcePath, symbols);
}

ObjectFile assembleZ8000Object(const std::string &asmText) {
    std::vector<Line> lines = parseProgram(asmText);
    Assembler assembler(/*allowExternal=*/true);
    assembler.run(lines);
    return buildObjectFile(assembler);
}

}  // namespace rcc
