#include "CP1610Assembler.h"

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

namespace rcc {

namespace {

[[noreturn]] void fail(const std::string &msg) { throw std::runtime_error("CP1610Assembler: " + msg); }

// --- Parsing, via the shared grammar/RccAsm.g4 --------------------------------------------
enum class SylKind { Number, Symbol, Group };
struct Syllable {
    char sign = 0;
    SylKind kind;
    std::string text;
    std::vector<Syllable> group;
};

// `Reg`: a bare `r0`-`r7`. `Imm`: a bare (optionally signed) number. `Mem`: a label - this
// backend's own direct-address mode (a plain, non-PC-relative word address). `Indirect`: the
// `[Rm]` bracket form, with no displacement at all (real CP1610 hardware has no indexed
// addressing mode - see CP1610Assembler.h's own header comment).
struct Operand {
    enum Kind { Reg, Imm, Mem, Indirect } kind = Imm;
    int reg = -1;
    std::vector<Syllable> expr;
};

struct Line {
    std::vector<std::string> labels;
    enum Kind { None, Equate, Org, Dw, Instr } kind = None;
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

// `r0`-`r7` (case-insensitive); -1 if not a register name.
int regNumber(const std::string &lower) {
    if (lower.size() != 2 || lower[0] != 'r') return -1;
    if (lower[1] < '0' || lower[1] > '7') return -1;
    return lower[1] - '0';
}

Operand buildOperand(RccAsmParser::OperandContext *ctx) {
    if (ctx->LBRACKET()) {
        std::vector<Syllable> syls = exprToSyllables(ctx->expr());
        if (syls.size() != 1 || syls[0].sign != 0 || syls[0].kind != SylKind::Symbol) {
            fail("'[...]' operand must be a bare register, e.g. '[r2]' - this target has no "
                 "indexed/displacement addressing mode");
        }
        int r = regNumber(toLower(syls[0].text));
        if (r < 0) fail("'[...]' operand must be a register");
        Operand op;
        op.kind = Operand::Indirect;
        op.reg = r;
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
    if (mnemonic == "dw") {
        line.kind = Line::Dw;
        for (auto *op : instr->operand()) {
            if (op->LBRACKET()) fail("'dw' operand can't be bracketed");
            line.exprList.push_back(exprToSyllables(op->expr()));
        }
        return line;
    }

    line.kind = Line::Instr;
    line.mnemonic = mnemonic;
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
    uint32_t wordOffset;
    std::string symbolName;
    uint16_t kind;
};

// --- Two-pass assembly, entirely in WORD units (see CP1610Assembler.h's own header comment) -
class Assembler {
   public:
    explicit Assembler(bool allowExternal) : allowExternal_(allowExternal) {}

    void run(const std::vector<Line> &lines) {
        stage_ = 1;
        pass(lines);
        stage_ = 2;
        pass(lines);
    }

    const std::vector<uint16_t> &words() const { return words_; }
    uint32_t orgBase() const { return orgBase_; }
    const std::unordered_map<std::string, uint32_t> &labels() const { return labels_; }
    const std::vector<Relocation> &relocations() const { return relocations_; }

   private:
    void pass(const std::vector<Line> &lines) {
        words_.clear();
        relocations_.clear();
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
                        relocations_.push_back({offset_, externalName, kCP1610RelocAbs16});
                    }
                    emitWord(static_cast<uint16_t>(v));
                }
                return;
            case Line::Instr:
                encodeInstr(line.mnemonic, line.operands);
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

    void emitWord(uint16_t w) {
        words_.push_back(w);
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

    void emitExtension(const Operand &op) {
        std::string externalName;
        uint32_t v = evalExpr(op.expr, &externalName);
        if (stage_ == 2 && allowExternal_ && !externalName.empty()) {
            relocations_.push_back({offset_, externalName, kCP1610RelocAbs16});
        }
        emitWord(static_cast<uint16_t>(v));
    }

    // reg,reg family (MOVR/ADDR/SUBR/CMPR/ANDR/XORR): opcode = base | (src<<3) | dst.
    void encodeRegReg(uint16_t base, const Operand &src, const Operand &dst, const std::string &mnemonic) {
        uint8_t s = requireReg(src, mnemonic);
        uint8_t d = requireReg(dst, mnemonic);
        emitWord(static_cast<uint16_t>(base | (s << 3) | d));
    }
    // One-register family (INCR/DECR/COMR/NEGR/ADCR/RSWD/CLRR/TSTR): opcode = base | reg, or
    // (for CLRR/TSTR, where the real instruction's own src and dst fields are the same
    // register) base | (reg<<3) | reg.
    void encodeOneReg(uint16_t base, const Operand &reg, const std::string &mnemonic, bool dup = false) {
        uint8_t r = requireReg(reg, mnemonic);
        emitWord(static_cast<uint16_t>(base | (dup ? ((r << 3) | r) : r)));
    }
    // The shift/rotate family (R0-R3 only, count 1 or 2): opcode = base1or2 | reg.
    void encodeShift(uint16_t base1, uint16_t base2, const Operand &reg, const Operand &count,
                      const std::string &mnemonic) {
        uint8_t r = requireReg(reg, mnemonic);
        if (r > 3) fail("'" + mnemonic + "': only r0-r3 support a shift/rotate");
        if (count.kind != Operand::Imm) fail("'" + mnemonic + "': count must be 1 or 2");
        uint32_t c = evalExpr(count.expr);
        if (c == 1) {
            emitWord(static_cast<uint16_t>(base1 | r));
        } else if (c == 2) {
            emitWord(static_cast<uint16_t>(base2 | r));
        } else {
            fail("'" + mnemonic + "': count must be 1 or 2");
        }
    }

    // The addressed two-operand family shared by MVI/ADD/SUB/CMP/AND/XOR: `src` is Mem (direct,
    // mode 0, +extension word), Indirect (mode = the register itself, 1-6, no extension word) or
    // Imm (mode 7, +extension word); `dst` is always a plain register.
    void encodeAddressed(uint16_t base, const Operand &src, const Operand &dst, const std::string &mnemonic) {
        uint8_t d = requireReg(dst, mnemonic);
        if (src.kind == Operand::Mem) {
            emitWord(static_cast<uint16_t>(base | d));
            emitExtension(src);
        } else if (src.kind == Operand::Imm) {
            emitWord(static_cast<uint16_t>(base | (7 << 3) | d));
            emitExtension(src);
        } else if (src.kind == Operand::Indirect) {
            if (src.reg < 1 || src.reg > 6) fail("'" + mnemonic + "': '[...]' register must be r1-r6");
            emitWord(static_cast<uint16_t>(base | (src.reg << 3) | d));
        } else {
            fail("'" + mnemonic + "': unsupported source operand");
        }
    }
    // `mvo`'s own reversed operand order (register first, addressable destination second).
    void encodeMvo(uint16_t base, const Operand &src, const Operand &dst, const std::string &mnemonic) {
        uint8_t s = requireReg(src, mnemonic);
        if (dst.kind == Operand::Mem) {
            emitWord(static_cast<uint16_t>(base | s));
            emitExtension(dst);
        } else if (dst.kind == Operand::Imm) {
            emitWord(static_cast<uint16_t>(base | (7 << 3) | s));
            emitExtension(dst);
        } else if (dst.kind == Operand::Indirect) {
            if (dst.reg < 1 || dst.reg > 6) fail("'" + mnemonic + "': '[...]' register must be r1-r6");
            emitWord(static_cast<uint16_t>(base | (dst.reg << 3) | s));
        } else {
            fail("'" + mnemonic + "': unsupported destination operand");
        }
    }

    // Conditional-branch family: opcode (fixed per mnemonic) + one extension word, the target's
    // word address minus the address right after that extension word, taken mod 65536 (plain
    // wraparound - see CP1610Codegen.h's own header comment on why this needs no separate
    // "reverse-direction" opcode or range check).
    void encodeBranch(uint16_t opcode, const Operand &target, const std::string &mnemonic) {
        if (target.kind != Operand::Mem) fail("'" + mnemonic + "': target must be a label");
        emitWord(opcode);
        uint32_t targetAddr = evalExpr(target.expr);
        uint32_t nextAddr = orgBase_ + offset_ + 1;
        emitWord(static_cast<uint16_t>((targetAddr - nextAddr) & 0xFFFF));
    }

    void encodeInstr(const std::string &mnemonic, const std::vector<Operand> &ops) {
        // reg,reg family
        static const std::unordered_map<std::string, uint16_t> regReg = {
            {"movr", 0x080}, {"addr", 0x0C0}, {"subr", 0x100}, {"cmpr", 0x140}, {"andr", 0x180}, {"xorr", 0x1C0},
        };
        if (auto it = regReg.find(mnemonic); it != regReg.end()) {
            encodeRegReg(it->second, requireOperand(ops, 0, mnemonic), requireOperand(ops, 1, mnemonic), mnemonic);
            return;
        }
        if (mnemonic == "tstr") {
            encodeOneReg(0x080, requireOperand(ops, 0, mnemonic), mnemonic, /*dup=*/true);
            return;
        }
        if (mnemonic == "clrr") {
            encodeOneReg(0x1C0, requireOperand(ops, 0, mnemonic), mnemonic, /*dup=*/true);
            return;
        }
        static const std::unordered_map<std::string, uint16_t> oneReg = {
            {"incr", 0x008}, {"decr", 0x010}, {"comr", 0x018}, {"negr", 0x020}, {"adcr", 0x028}, {"rswd", 0x038},
        };
        if (auto it = oneReg.find(mnemonic); it != oneReg.end()) {
            encodeOneReg(it->second, requireOperand(ops, 0, mnemonic), mnemonic);
            return;
        }
        if (mnemonic == "gswd") {
            uint8_t r = requireReg(requireOperand(ops, 0, mnemonic), mnemonic);
            if (r > 3) fail("'gswd': only r0-r3 can receive the status word");
            emitWord(static_cast<uint16_t>(0x030 | r));
            return;
        }
        if (mnemonic == "hlt") { emitWord(0x000); return; }
        if (mnemonic == "eis") { emitWord(0x002); return; }
        if (mnemonic == "dis") { emitWord(0x003); return; }
        if (mnemonic == "tci") { emitWord(0x005); return; }
        if (mnemonic == "clrc") { emitWord(0x006); return; }
        if (mnemonic == "setc") { emitWord(0x007); return; }
        if (mnemonic == "nop") { emitWord(0x034); return; }
        if (mnemonic == "sin") { emitWord(0x036); return; }

        // `dswap r` is this dialect's own name for the real "SWAP Rn,2" encoding (the real
        // CP1610's DSWAP mnemonic for it) - a fixed one-register form, not a count-selected
        // shiftOps entry like the rest of this family.
        if (mnemonic == "dswap") {
            encodeOneReg(0x044, requireOperand(ops, 0, mnemonic), mnemonic);
            return;
        }
        static const std::unordered_map<std::string, std::pair<uint16_t, uint16_t>> shiftOps = {
            {"swap", {0x040, 0x044}}, {"sll", {0x048, 0x04C}}, {"rlc", {0x050, 0x054}},
            {"sllc", {0x058, 0x05C}}, {"slr", {0x060, 0x064}}, {"sar", {0x068, 0x06C}},
            {"rrc", {0x070, 0x074}},  {"sarc", {0x078, 0x07C}},
        };
        if (auto it = shiftOps.find(mnemonic); it != shiftOps.end()) {
            encodeShift(it->second.first, it->second.second, requireOperand(ops, 0, mnemonic),
                        requireOperand(ops, 1, mnemonic), mnemonic);
            return;
        }

        static const std::unordered_map<std::string, uint16_t> branchCond = {
            {"b", 0x200}, {"bc", 0x201}, {"bov", 0x202}, {"bpl", 0x203}, {"bze", 0x204}, {"blt", 0x205},
            {"ble", 0x206}, {"busc", 0x207}, {"nopp", 0x208}, {"bnc", 0x209}, {"bnov", 0x20A}, {"bmi", 0x20B},
            {"bnze", 0x20C}, {"bge", 0x20D}, {"bgt", 0x20E}, {"besc", 0x20F},
        };
        if (auto it = branchCond.find(mnemonic); it != branchCond.end()) {
            encodeBranch(it->second, requireOperand(ops, 0, mnemonic), mnemonic);
            return;
        }
        if (mnemonic.rfind("bext", 0) == 0 && mnemonic.size() > 4) {
            try {
                size_t consumed = 0;
                int n = std::stoi(mnemonic.substr(4), &consumed);
                if (consumed == mnemonic.size() - 4 && n >= 0 && n <= 15) {
                    encodeBranch(static_cast<uint16_t>(0x210 | n), requireOperand(ops, 0, mnemonic), mnemonic);
                    return;
                }
            } catch (...) {
            }
        }

        if (mnemonic == "mvo") {
            encodeMvo(0x240, requireOperand(ops, 0, mnemonic), requireOperand(ops, 1, mnemonic), mnemonic);
            return;
        }
        if (mnemonic == "pshr") {
            uint8_t s = requireReg(requireOperand(ops, 0, mnemonic), mnemonic);
            emitWord(static_cast<uint16_t>(0x240 | (6 << 3) | s));
            return;
        }
        if (mnemonic == "pulr") {
            uint8_t d = requireReg(requireOperand(ops, 0, mnemonic), mnemonic);
            emitWord(static_cast<uint16_t>(0x280 | (6 << 3) | d));
            return;
        }
        static const std::unordered_map<std::string, uint16_t> addressed = {
            {"mvi", 0x280}, {"add", 0x2C0}, {"sub", 0x300}, {"cmp", 0x340}, {"and", 0x380}, {"xor", 0x3C0},
        };
        if (auto it = addressed.find(mnemonic); it != addressed.end()) {
            encodeAddressed(it->second, requireOperand(ops, 0, mnemonic), requireOperand(ops, 1, mnemonic),
                             mnemonic);
            return;
        }
        if (mnemonic == "mvii") {
            uint8_t d = requireReg(requireOperand(ops, 1, mnemonic), mnemonic);
            emitWord(static_cast<uint16_t>(0x280 | (7 << 3) | d));
            emitExtension(requireOperand(ops, 0, mnemonic));
            return;
        }

        if (mnemonic == "j") {
            emitWord(0x004);
            uint32_t addr = evalExpr(requireOperand(ops, 0, mnemonic).expr);
            emitWord(static_cast<uint16_t>(0x300 | (((addr >> 10) & 0x3F) << 2)));
            emitWord(static_cast<uint16_t>(addr & 0x3FF));
            return;
        }
        if (mnemonic == "jsr") {
            uint8_t r = requireReg(requireOperand(ops, 0, mnemonic), mnemonic);
            if (r < 4 || r > 6) fail("'jsr': link register must be r4, r5 or r6");
            uint32_t addr = evalExpr(requireOperand(ops, 1, mnemonic).expr);
            uint16_t regSel = static_cast<uint16_t>((r - 4) << 8);
            emitWord(0x004);
            emitWord(static_cast<uint16_t>(regSel | (((addr >> 10) & 0x3F) << 2)));
            emitWord(static_cast<uint16_t>(addr & 0x3FF));
            return;
        }

        fail("unsupported instruction '" + mnemonic + "'");
    }

    std::vector<uint16_t> words_;
    std::unordered_map<std::string, uint32_t> labels_;
    std::vector<Relocation> relocations_;
    uint32_t offset_ = 0;
    uint32_t orgBase_ = 0;
    int stage_ = 1;
    bool allowExternal_;
};

// Serializes `words_` into a plain byte image for the `raw` format: 2 bytes per word, this
// dialect's own choice of byte order (little-endian - an arbitrary choice, since nothing outside
// this project's own disposable-interpreter-verified codegen ever reads it back).
std::string wordsToBytes(const std::vector<uint16_t> &words) {
    std::string out;
    out.reserve(words.size() * 2);
    for (uint16_t w : words) {
        out.push_back(static_cast<char>(w & 0xFF));
        out.push_back(static_cast<char>((w >> 8) & 0xFF));
    }
    return out;
}

ObjectFile buildObjectFile(const Assembler &a) {
    ObjectFile obj;
    obj.machineType = kCP1610CoffMachine;
    std::string bytes = wordsToBytes(a.words());
    obj.textBytes.assign(bytes.begin(), bytes.end());

    for (const auto &kv : a.labels()) {
        obj.symbols.push_back({kv.first, (kv.second - a.orgBase()) * 2, /*isDefined=*/true});
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
        obj.relocations.push_back({r.wordOffset * 2, r.symbolName, r.kind});
    }
    return obj;
}

}  // namespace

std::string assembleCP1610(const std::string &asmText) {
    std::vector<Line> lines = parseProgram(asmText);
    Assembler assembler(/*allowExternal=*/false);
    assembler.run(lines);
    return wordsToBytes(assembler.words());
}

ObjectFile assembleCP1610Object(const std::string &asmText) {
    std::vector<Line> lines = parseProgram(asmText);
    Assembler assembler(/*allowExternal=*/true);
    assembler.run(lines);
    return buildObjectFile(assembler);
}

}  // namespace rcc
