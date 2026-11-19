#include "Riscv32Assembler.h"

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

[[noreturn]] void fail(const std::string &msg) { throw std::runtime_error("Riscv32Assembler: " + msg); }

// --- Parsing, via the shared grammar/RccAsm.g4 ---------------------------
enum class SylKind { Number, Symbol, Group };
struct Syllable {
    char sign = 0;
    SylKind kind;
    std::string text;
    std::vector<Syllable> group;
};

// A bare register name is Reg, a bare signed number is Imm, anything
// else (a label or `label+N`) is Mem, resolved as an address.
struct Operand {
    enum Kind { Reg, Imm, Mem } kind = Imm;
    int reg = -1;
    std::vector<Syllable> expr;  // Imm/Mem
};

struct Line {
    std::vector<std::string> labels;
    enum Kind { None, Equate, Org, Db, Dh, Dw, Instr } kind = None;
    std::string equateName;
    std::vector<std::vector<Syllable>> exprList;  // Equate (1)/Org (1)/Dw (N)
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

// Every one of RV32I's 32 integer registers is reachable either by its
// bare number (`x0`-`x31`) or its standard ABI name; this dialect's own
// codegen only ever uses a handful of them (zero/ra/sp/fp/t0-t6, plus `s1`
// for its own internal scratch, matching MipsCodegen.h's/SparcCodegen.h's
// own "at" register), but every name is accepted so hand-written assembly
// (and this backend's own assembler tests) can use any of the 32.
int regNumber(const std::string &lower) {
    static const std::unordered_map<std::string, int> names = {
        {"zero", 0}, {"ra", 1},  {"sp", 2},   {"gp", 3},   {"tp", 4},   {"t0", 5},   {"t1", 6},
        {"t2", 7},   {"fp", 8},  {"s0", 8},   {"s1", 9},   {"a0", 10},  {"a1", 11},  {"a2", 12},
        {"a3", 13},  {"a4", 14}, {"a5", 15},  {"a6", 16},  {"a7", 17},  {"s2", 18},  {"s3", 19},
        {"s4", 20},  {"s5", 21}, {"s6", 22},  {"s7", 23},  {"s8", 24},  {"s9", 25},  {"s10", 26},
        {"s11", 27}, {"t3", 28}, {"t4", 29},  {"t5", 30},  {"t6", 31},
        {"x0", 0},   {"x1", 1},  {"x2", 2},   {"x3", 3},   {"x4", 4},   {"x5", 5},   {"x6", 6},
        {"x7", 7},   {"x8", 8},  {"x9", 9},   {"x10", 10}, {"x11", 11}, {"x12", 12}, {"x13", 13},
        {"x14", 14}, {"x15", 15}, {"x16", 16}, {"x17", 17}, {"x18", 18}, {"x19", 19}, {"x20", 20},
        {"x21", 21}, {"x22", 22}, {"x23", 23}, {"x24", 24}, {"x25", 25}, {"x26", 26}, {"x27", 27},
        {"x28", 28}, {"x29", 29}, {"x30", 30}, {"x31", 31},
    };
    auto it = names.find(lower);
    return it == names.end() ? -1 : it->second;
}

Operand buildOperand(RccAsmParser::OperandContext *ctx) {
    if (ctx->LBRACKET()) {
        fail(
            "bracketed memory operands aren't part of this target's dialect - RISC-V "
            "loads/stores always take their base register and displacement as two plain, "
            "comma-separated operands instead (see Riscv32Codegen.h)");
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
    if (mnemonic == "dw" || mnemonic == "db" || mnemonic == "dh") {
        line.kind = mnemonic == "dw" ? Line::Dw : mnemonic == "dh" ? Line::Dh : Line::Db;
        for (auto *op : instr->operand()) {
            if (op->LBRACKET()) fail("'" + mnemonic + "' operand can't be bracketed");
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

// A pending fixup, turned into an ObjRelocation once assembly is done.
struct Relocation {
    uint32_t offset;
    std::string symbolName;
    uint16_t kind;
};

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
            case Line::Equate:
                labels_[line.equateName] = evalExpr(line.exprList.at(0));
                return;
            case Line::Org:
                orgBase_ = evalExpr(line.exprList.at(0));
                return;
            case Line::Dw:
                for (const auto &e : line.exprList) {
                    std::string externalName;
                    bool needsBaseShift = false;
                    uint32_t v = evalExpr(e, &externalName, &needsBaseShift);
                    if (stage_ == 2 && allowExternal_) {
                        if (needsBaseShift) relocations_.push_back({offset_, "..", kRiscv32RelocAbs32});
                        if (!externalName.empty()) relocations_.push_back({offset_, externalName, kRiscv32RelocAbs32});
                    }
                    emit32(v);
                }
                return;
            case Line::Db:
                for (const auto &e : line.exprList) emit8(static_cast<uint8_t>(evalExpr(e)));
                return;
            case Line::Dh:
                for (const auto &e : line.exprList) emit16(static_cast<uint16_t>(evalExpr(e)));
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

    // --- word emission -------------------------------------------------
    void emit8(uint8_t v) {
        bytes_.push_back(v);
        offset_ += 1;
    }
    void emit16(uint16_t v) {
        // RISC-V is little-endian.
        bytes_.push_back(static_cast<uint8_t>(v & 0xFF));
        bytes_.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        offset_ += 2;
    }
    void emit32(uint32_t v) {
        bytes_.push_back(static_cast<uint8_t>(v & 0xFF));
        bytes_.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        bytes_.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        bytes_.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
        offset_ += 4;
    }

    static uint8_t requireReg(const Operand &op, const std::string &mnemonic) {
        if (op.kind != Operand::Reg) fail("'" + mnemonic + "' needs a register operand");
        return static_cast<uint8_t>(op.reg);
    }
    static const Operand &requireOperand(const std::vector<Operand> &ops, size_t i, const std::string &mnemonic) {
        if (ops.size() <= i) fail("'" + mnemonic + "': missing operand");
        return ops[i];
    }

    static uint32_t imm12Of(int32_t v) {
        if (v < -2048 || v > 2047) fail("immediate out of signed 12-bit range");
        return static_cast<uint32_t>(v) & 0xFFFu;
    }
    uint32_t evalImm12(const Operand &op) {
        if (op.kind == Operand::Reg) fail("expected an immediate or label operand, not a register");
        return imm12Of(static_cast<int32_t>(evalExpr(op.expr)));
    }
    // An optional trailing displacement operand (loads/stores' third
    // operand), defaulting to 0 when omitted.
    uint32_t dispOf(const std::vector<Operand> &ops, size_t i) {
        if (ops.size() <= i) return 0;
        return evalImm12(ops[i]);
    }

    // --- RV32I instruction formats ---------------------------------------
    // R-type: funct7(7) rs2(5) rs1(5) funct3(3) rd(5) opcode(7).
    void emitR(uint8_t funct7, uint8_t rs2, uint8_t rs1, uint8_t funct3, uint8_t rd, uint8_t opcode) {
        emit32((static_cast<uint32_t>(funct7) << 25) | (static_cast<uint32_t>(rs2) << 20) |
               (static_cast<uint32_t>(rs1) << 15) | (static_cast<uint32_t>(funct3) << 12) |
               (static_cast<uint32_t>(rd) << 7) | opcode);
    }
    // I-type: imm[11:0](12) rs1(5) funct3(3) rd(5) opcode(7). `imm12` is the raw 12-bit field
    // value (already masked); the caller - not this helper - decides how it was derived.
    void emitI(uint32_t imm12, uint8_t rs1, uint8_t funct3, uint8_t rd, uint8_t opcode) {
        emit32(((imm12 & 0xFFFu) << 20) | (static_cast<uint32_t>(rs1) << 15) |
               (static_cast<uint32_t>(funct3) << 12) | (static_cast<uint32_t>(rd) << 7) | opcode);
    }
    // S-type: imm[11:5](7) rs2(5) rs1(5) funct3(3) imm[4:0](5) opcode(7).
    void emitS(uint32_t imm12, uint8_t rs2, uint8_t rs1, uint8_t funct3, uint8_t opcode) {
        uint32_t imm = imm12 & 0xFFFu;
        emit32(((imm >> 5) << 25) | (static_cast<uint32_t>(rs2) << 20) | (static_cast<uint32_t>(rs1) << 15) |
               (static_cast<uint32_t>(funct3) << 12) | ((imm & 0x1Fu) << 7) | opcode);
    }
    // B-type: imm[12](1) imm[10:5](6) rs2(5) rs1(5) funct3(3) imm[4:1](4) imm[11](1) opcode(7).
    // `imm13` is a signed, word-pair-aligned (bit 0 always 0) displacement.
    void emitB(int32_t imm13, uint8_t rs2, uint8_t rs1, uint8_t funct3, uint8_t opcode) {
        uint32_t imm = static_cast<uint32_t>(imm13);
        uint32_t bit12 = (imm >> 12) & 1u, bits10_5 = (imm >> 5) & 0x3Fu, bits4_1 = (imm >> 1) & 0xFu,
                 bit11 = (imm >> 11) & 1u;
        emit32((bit12 << 31) | (bits10_5 << 25) | (static_cast<uint32_t>(rs2) << 20) |
               (static_cast<uint32_t>(rs1) << 15) | (static_cast<uint32_t>(funct3) << 12) | (bits4_1 << 8) |
               (bit11 << 7) | opcode);
    }
    // U-type: imm[31:12](20) rd(5) opcode(7). `imm20` is the raw field value, placed directly
    // into the instruction's own upper 20 bits.
    void emitU(uint32_t imm20, uint8_t rd, uint8_t opcode) {
        emit32(((imm20 & 0xFFFFFu) << 12) | (static_cast<uint32_t>(rd) << 7) | opcode);
    }
    // J-type: imm[20](1) imm[10:1](10) imm[11](1) imm[19:12](8) rd(5) opcode(7). `imm21` is a
    // signed, word-pair-aligned displacement.
    void emitJ(int32_t imm21, uint8_t rd, uint8_t opcode) {
        uint32_t imm = static_cast<uint32_t>(imm21);
        uint32_t bit20 = (imm >> 20) & 1u, bits10_1 = (imm >> 1) & 0x3FFu, bit11 = (imm >> 11) & 1u,
                 bits19_12 = (imm >> 12) & 0xFFu;
        emit32((bit20 << 31) | (bits10_1 << 21) | (bit11 << 20) | (bits19_12 << 12) | (static_cast<uint32_t>(rd) << 7) |
               opcode);
    }

    void encodeRRR(uint8_t funct7, uint8_t funct3, const std::vector<Operand> &ops, const std::string &mnemonic) {
        uint8_t rd = requireReg(requireOperand(ops, 0, mnemonic), mnemonic);
        uint8_t rs1 = requireReg(requireOperand(ops, 1, mnemonic), mnemonic);
        uint8_t rs2 = requireReg(requireOperand(ops, 2, mnemonic), mnemonic);
        emitR(funct7, rs2, rs1, funct3, rd, 0x33);
    }
    void encodeImmArith(uint8_t funct3, const std::vector<Operand> &ops, const std::string &mnemonic) {
        uint8_t rd = requireReg(requireOperand(ops, 0, mnemonic), mnemonic);
        uint8_t rs1 = requireReg(requireOperand(ops, 1, mnemonic), mnemonic);
        uint32_t imm = evalImm12(requireOperand(ops, 2, mnemonic));
        emitI(imm, rs1, funct3, rd, 0x13);
    }
    // slli/srli/srai: funct7(0000000 or 0100000) in bits[11:5] of the I-type immediate field,
    // shamt(0-31) in bits[4:0].
    void encodeShiftImm(uint8_t funct7, uint8_t funct3, const std::vector<Operand> &ops, const std::string &mnemonic) {
        uint8_t rd = requireReg(requireOperand(ops, 0, mnemonic), mnemonic);
        uint8_t rs1 = requireReg(requireOperand(ops, 1, mnemonic), mnemonic);
        uint32_t shamt = evalExpr(requireOperand(ops, 2, mnemonic).expr) & 0x1Fu;
        emitI((static_cast<uint32_t>(funct7) << 5) | shamt, rs1, funct3, rd, 0x13);
    }
    void encodeLoad(uint8_t funct3, const std::vector<Operand> &ops, const std::string &mnemonic) {
        uint8_t rd = requireReg(requireOperand(ops, 0, mnemonic), mnemonic);
        uint8_t rs1 = requireReg(requireOperand(ops, 1, mnemonic), mnemonic);
        uint32_t imm = dispOf(ops, 2);
        emitI(imm, rs1, funct3, rd, 0x03);
    }
    void encodeStore(uint8_t funct3, const std::vector<Operand> &ops, const std::string &mnemonic) {
        uint8_t rs2 = requireReg(requireOperand(ops, 0, mnemonic), mnemonic);
        uint8_t rs1 = requireReg(requireOperand(ops, 1, mnemonic), mnemonic);
        uint32_t imm = dispOf(ops, 2);
        emitS(imm, rs2, rs1, funct3, 0x23);
    }
    void encodeBranch(uint8_t funct3, const std::vector<Operand> &ops, const std::string &mnemonic) {
        uint8_t rs1 = requireReg(requireOperand(ops, 0, mnemonic), mnemonic);
        uint8_t rs2 = requireReg(requireOperand(ops, 1, mnemonic), mnemonic);
        const Operand &target = requireOperand(ops, 2, mnemonic);
        if (target.kind != Operand::Mem) fail("'" + mnemonic + "': target must be a label");
        uint32_t targetAddr = evalExpr(target.expr);
        int32_t rel = static_cast<int32_t>(targetAddr) - static_cast<int32_t>(orgBase_ + offset_);
        if (stage_ == 2 && (rel < -4096 || rel > 4094 || (rel & 1) != 0)) fail("branch target out of range");
        emitB(rel, rs2, rs1, funct3, 0x63);
    }

    // `lui`/`auipc`'s own plain-immediate form, or (when the operand resolves a label) the
    // upper-20-bits half of a `lui`/`addi` absolute-address-load pair - see
    // Riscv32Assembler.h's own header comment for the Hi/Lo split's rounding convention.
    void encodeUpper(uint8_t opcode, const std::vector<Operand> &ops, const std::string &mnemonic) {
        uint8_t rd = requireReg(requireOperand(ops, 0, mnemonic), mnemonic);
        const Operand &immOp = requireOperand(ops, 1, mnemonic);
        if (immOp.kind != Operand::Mem) {
            emitU(evalExpr(immOp.expr), rd, opcode);
            return;
        }
        std::string externalName;
        bool needsBaseShift = false;
        uint32_t full = evalExpr(immOp.expr, &externalName, &needsBaseShift);
        uint32_t lo12 = full & 0xFFFu;
        uint32_t borrow = (lo12 & 0x800u) ? 1u : 0u;
        uint32_t hi20 = ((full >> 12) + borrow) & 0xFFFFFu;
        if (stage_ == 2 && allowExternal_) {
            if (needsBaseShift) relocations_.push_back({offset_, "..", kRiscv32RelocHi});
            if (!externalName.empty()) relocations_.push_back({offset_, externalName, kRiscv32RelocHi});
        }
        emitU(hi20, rd, opcode);
    }
    // `addi`'s own plain-immediate form, or (when the third operand resolves a label) the
    // lower-12-bits half of the `lui`/`addi` pair above - must immediately follow its `lui` at
    // +4 so the linker's own Hi patch can find this site.
    void encodeAddi(const std::vector<Operand> &ops) {
        uint8_t rd = requireReg(requireOperand(ops, 0, "addi"), "addi");
        uint8_t rs1 = requireReg(requireOperand(ops, 1, "addi"), "addi");
        const Operand &immOp = requireOperand(ops, 2, "addi");
        if (immOp.kind != Operand::Mem) {
            emitI(evalImm12(immOp), rs1, 0x0, rd, 0x13);
            return;
        }
        std::string externalName;
        bool needsBaseShift = false;
        uint32_t full = evalExpr(immOp.expr, &externalName, &needsBaseShift);
        uint32_t lo12 = full & 0xFFFu;
        if (stage_ == 2 && allowExternal_) {
            if (needsBaseShift) relocations_.push_back({offset_, "..", kRiscv32RelocLo});
            if (!externalName.empty()) relocations_.push_back({offset_, externalName, kRiscv32RelocLo});
        }
        emitI(lo12, rs1, 0x0, rd, 0x13);
    }

    // `jal`: real two-operand `jal rd,label`, or the one-operand pseudo `jal label` (rd=ra
    // implied, matching real RISC-V assembler convention) that this dialect's own codegen
    // always uses for a direct call. A target in this same file needs no relocation (`jal`'s
    // displacement is PC-relative, so it stays correct regardless of this object's eventual
    // link address); only a cross-object target does.
    void encodeJal(const std::vector<Operand> &ops, uint8_t rdWhenImplicit) {
        uint8_t rd = ops.size() >= 2 ? requireReg(ops[0], "jal") : rdWhenImplicit;
        const Operand &target = requireOperand(ops, ops.size() >= 2 ? 1 : 0, "jal");
        if (target.kind != Operand::Mem) fail("'jal': target must be a label");
        std::string externalName;
        uint32_t targetAddr = evalExpr(target.expr, &externalName);
        if (stage_ == 2 && allowExternal_ && !externalName.empty()) {
            relocations_.push_back({offset_, externalName, kRiscv32RelocJal});
            emitJ(0, rd, 0x6F);
            return;
        }
        int32_t rel = static_cast<int32_t>(targetAddr) - static_cast<int32_t>(orgBase_ + offset_);
        if (stage_ == 2 && (rel < -(1 << 20) || rel >= (1 << 20) || (rel & 1) != 0)) {
            fail("'jal': target out of 20-bit range");
        }
        emitJ(rel, rd, 0x6F);
    }

    void encodeJalr(const std::vector<Operand> &ops) {
        uint8_t rd, rs1;
        uint32_t imm;
        if (ops.size() >= 3) {
            rd = requireReg(ops[0], "jalr");
            rs1 = requireReg(ops[1], "jalr");
            imm = evalImm12(ops[2]);
        } else if (ops.size() == 2) {
            rd = requireReg(ops[0], "jalr");
            rs1 = requireReg(ops[1], "jalr");
            imm = 0;
        } else {  // one-operand pseudo: rd=ra implied
            rd = 1;
            rs1 = requireReg(requireOperand(ops, 0, "jalr"), "jalr");
            imm = 0;
        }
        emitI(imm, rs1, 0x0, rd, 0x67);
    }

    void encodeInstr(const std::string &mnemonic, const std::vector<Operand> &ops) {
        static const std::unordered_map<std::string, std::pair<uint8_t, uint8_t>> rtype = {
            {"add", {0x00, 0x0}}, {"sub", {0x20, 0x0}}, {"sll", {0x00, 0x1}}, {"slt", {0x00, 0x2}},
            {"sltu", {0x00, 0x3}}, {"xor", {0x00, 0x4}}, {"srl", {0x00, 0x5}}, {"sra", {0x20, 0x5}},
            {"or", {0x00, 0x6}}, {"and", {0x00, 0x7}},
            // RV32M.
            {"mul", {0x01, 0x0}}, {"mulh", {0x01, 0x1}}, {"mulhsu", {0x01, 0x2}}, {"mulhu", {0x01, 0x3}},
            {"div", {0x01, 0x4}}, {"divu", {0x01, 0x5}}, {"rem", {0x01, 0x6}}, {"remu", {0x01, 0x7}},
        };
        if (auto it = rtype.find(mnemonic); it != rtype.end()) {
            encodeRRR(it->second.first, it->second.second, ops, mnemonic);
            return;
        }
        if (mnemonic == "addi") {
            encodeAddi(ops);
            return;
        }
        static const std::unordered_map<std::string, uint8_t> itype = {
            {"slti", 0x2}, {"sltiu", 0x3}, {"xori", 0x4}, {"ori", 0x6}, {"andi", 0x7},
        };
        if (auto it = itype.find(mnemonic); it != itype.end()) {
            encodeImmArith(it->second, ops, mnemonic);
            return;
        }
        if (mnemonic == "slli") {
            encodeShiftImm(0x00, 0x1, ops, mnemonic);
            return;
        }
        if (mnemonic == "srli") {
            encodeShiftImm(0x00, 0x5, ops, mnemonic);
            return;
        }
        if (mnemonic == "srai") {
            encodeShiftImm(0x20, 0x5, ops, mnemonic);
            return;
        }
        static const std::unordered_map<std::string, uint8_t> loads = {
            {"lb", 0x0}, {"lh", 0x1}, {"lw", 0x2}, {"lbu", 0x4}, {"lhu", 0x5},
        };
        if (auto it = loads.find(mnemonic); it != loads.end()) {
            encodeLoad(it->second, ops, mnemonic);
            return;
        }
        static const std::unordered_map<std::string, uint8_t> stores = {
            {"sb", 0x0}, {"sh", 0x1}, {"sw", 0x2},
        };
        if (auto it = stores.find(mnemonic); it != stores.end()) {
            encodeStore(it->second, ops, mnemonic);
            return;
        }
        static const std::unordered_map<std::string, uint8_t> branches = {
            {"beq", 0x0}, {"bne", 0x1}, {"blt", 0x4}, {"bge", 0x5}, {"bltu", 0x6}, {"bgeu", 0x7},
        };
        if (auto it = branches.find(mnemonic); it != branches.end()) {
            encodeBranch(it->second, ops, mnemonic);
            return;
        }
        if (mnemonic == "lui") {
            encodeUpper(0x37, ops, mnemonic);
            return;
        }
        if (mnemonic == "auipc") {
            encodeUpper(0x17, ops, mnemonic);
            return;
        }
        if (mnemonic == "jal") {
            encodeJal(ops, /*rdWhenImplicit=*/1);
            return;
        }
        if (mnemonic == "call") {  // pseudo: jal ra,label
            encodeJal(ops, /*rdWhenImplicit=*/1);
            return;
        }
        if (mnemonic == "j") {  // pseudo: jal zero,label
            encodeJal(ops, /*rdWhenImplicit=*/0);
            return;
        }
        if (mnemonic == "jalr") {
            encodeJalr(ops);
            return;
        }
        if (mnemonic == "jr") {  // pseudo: jalr zero,rs1,0
            uint8_t rs1 = requireReg(requireOperand(ops, 0, mnemonic), mnemonic);
            emitI(0, rs1, 0x0, 0, 0x67);
            return;
        }
        if (mnemonic == "ret") {  // pseudo: jalr zero,ra,0
            if (!ops.empty()) fail("'ret' takes no operand");
            emitI(0, 1, 0x0, 0, 0x67);
            return;
        }
        if (mnemonic == "nop") {  // pseudo: addi zero,zero,0
            if (!ops.empty()) fail("'nop' takes no operand");
            emitI(0, 0, 0x0, 0, 0x13);
            return;
        }
        if (mnemonic == "mv") {  // pseudo: add rd,rs,zero
            uint8_t rd = requireReg(requireOperand(ops, 0, mnemonic), mnemonic);
            uint8_t rs = requireReg(requireOperand(ops, 1, mnemonic), mnemonic);
            emitR(0x00, 0, rs, 0x0, rd, 0x33);
            return;
        }
        if (mnemonic == "li") {  // pseudo: addi (fits in 12 bits) or lui+addi otherwise
            uint8_t rd = requireReg(requireOperand(ops, 0, mnemonic), mnemonic);
            const Operand &immOp = requireOperand(ops, 1, mnemonic);
            if (immOp.kind == Operand::Mem) fail("'li' takes a plain immediate, not a label - use 'lui'/'addi'");
            int32_t v = static_cast<int32_t>(evalExpr(immOp.expr));
            if (v >= -2048 && v <= 2047) {
                emitI(imm12Of(v), 0, 0x0, rd, 0x13);
                return;
            }
            uint32_t uv = static_cast<uint32_t>(v);
            uint32_t lo12 = uv & 0xFFFu;
            uint32_t borrow = (lo12 & 0x800u) ? 1u : 0u;
            uint32_t hi20 = ((uv >> 12) + borrow) & 0xFFFFFu;
            emitU(hi20, rd, 0x37);
            emitI(lo12, rd, 0x0, rd, 0x13);
            return;
        }
        if (mnemonic == "fence") {
            if (!ops.empty()) fail("'fence' takes no operand");
            emitI(0x0FF, 0, 0x0, 0, 0x0F);  // pred=succ=iorw
            return;
        }
        if (mnemonic == "fence.i") {
            if (!ops.empty()) fail("'fence.i' takes no operand");
            emitI(0, 0, 0x1, 0, 0x0F);
            return;
        }
        if (mnemonic == "ecall") {
            if (!ops.empty()) fail("'ecall' takes no operand");
            emitI(0, 0, 0x0, 0, 0x73);
            return;
        }
        if (mnemonic == "ebreak") {
            if (!ops.empty()) fail("'ebreak' takes no operand");
            emitI(1, 0, 0x0, 0, 0x73);
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
    obj.machineType = kRiscv32CoffMachine;
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
        obj.relocations.push_back({r.offset, r.symbolName, r.kind});
    }
    return obj;
}

}  // namespace

std::string assembleRiscv32(const std::string &asmText) {
    std::vector<Line> lines = parseProgram(asmText);
    Assembler assembler(/*allowExternal=*/false);
    assembler.run(lines);
    const auto &bytes = assembler.bytes();
    return std::string(bytes.begin(), bytes.end());
}

ObjectFile assembleRiscv32Object(const std::string &asmText) {
    std::vector<Line> lines = parseProgram(asmText);
    Assembler assembler(/*allowExternal=*/true);
    assembler.run(lines);
    return buildObjectFile(assembler);
}

}  // namespace rcc
