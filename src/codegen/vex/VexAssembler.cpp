#include "VexAssembler.h"

#include <algorithm>
#include <cctype>
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

[[noreturn]] void fail(const std::string &msg) { throw std::runtime_error("VexAssembler: " + msg); }

std::string toLower(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The opcode for every mnemonic this dialect accepts - see docs/SPEC.md section 2's own table.
const std::unordered_map<std::string, uint8_t> &opcodes() {
    static const std::unordered_map<std::string, uint8_t> table = {
        {"ret", 0x00}, {"ldi", 0x01}, {"mov", 0x02}, {"ld", 0x03}, {"st", 0x04}, {"dei", 0x05},
        {"deo", 0x06}, {"jmp", 0x07}, {"jcn", 0x08}, {"cal", 0x09}, {"add", 0x0A}, {"sub", 0x0B},
        {"mul", 0x0C}, {"div", 0x0D}, {"and", 0x0E}, {"ora", 0x0F}, {"xor", 0x10}, {"ceq", 0x11},
        {"clt", 0x12}, {"nop", 0x13}, {"psh", 0x14}, {"pop", 0x15}, {"iret", 0x16}, {"sei", 0x17},
        {"cli", 0x18}, {"hlt", 0x19}, {"gsp", 0x1A}, {"ssp", 0x1B},
    };
    return table;
}

// How many plain register operands each mnemonic takes, and whether a trailing 4-byte
// immediate (LDI only) follows them - docs/SPEC.md section 2's own per-opcode shape.
struct OpShape {
    int regCount;
    bool hasImmediate;
};
const std::unordered_map<std::string, OpShape> &opShapes() {
    static const std::unordered_map<std::string, OpShape> table = {
        {"ret", {0, false}}, {"ldi", {1, true}}, {"mov", {2, false}}, {"ld", {2, false}},
        {"st", {2, false}}, {"dei", {2, false}}, {"deo", {2, false}}, {"jmp", {1, false}},
        {"jcn", {2, false}}, {"cal", {1, false}}, {"add", {3, false}}, {"sub", {3, false}},
        {"mul", {3, false}}, {"div", {3, false}}, {"and", {3, false}}, {"ora", {3, false}},
        {"xor", {3, false}}, {"ceq", {3, false}}, {"clt", {3, false}}, {"nop", {0, false}},
        {"psh", {1, false}}, {"pop", {1, false}}, {"iret", {0, false}}, {"sei", {0, false}},
        {"cli", {0, false}}, {"hlt", {0, false}}, {"gsp", {1, false}}, {"ssp", {1, false}},
    };
    return table;
}

// --- Parsing, via the shared grammar/RccAsm.g4 ---------------------------

enum class SylKind { Number, Symbol };
struct Syllable {
    char sign = 0;
    SylKind kind;
    std::string text;
};

struct Line {
    std::vector<std::string> labels;
    enum Kind { None, Org, Byte, Short, Word, Zero, Instr } kind = None;
    std::vector<std::vector<Syllable>> exprList;  // Org (1) / Byte|Short|Word (N) / Zero (1)
    std::string mnemonic;                          // Instr - already lowercased
    std::vector<uint8_t> regOperands;               // Instr - resolved register numbers, in order
    std::vector<Syllable> immOperand;               // Instr - "ldi" only, its trailing imm32
};

class ThrowingErrorListener : public antlr4::BaseErrorListener {
   public:
    void syntaxError(antlr4::Recognizer *, antlr4::Token *, size_t line, size_t col, const std::string &msg,
                      std::exception_ptr) override {
        fail(std::to_string(line) + ":" + std::to_string(col) + ": " + msg);
    }
};

std::vector<Syllable> exprToSyllables(RccAsmParser::ExprContext *ctx) {
    std::vector<Syllable> result;
    for (auto *syl : ctx->syllable()) {
        char sign = 0;
        if (auto *s = syl->sign()) sign = s->PLUS() ? '+' : '-';
        auto *atom = syl->atom();
        if (atom->expr() != nullptr) fail("'(...)' grouping isn't part of this target's dialect");
        if (atom->NUMBER()) {
            result.push_back({sign, SylKind::Number, atom->NUMBER()->getText()});
        } else {
            result.push_back({sign, SylKind::Symbol, atom->IDENT()->getText()});
        }
    }
    if (result.empty()) fail("empty expression");
    return result;
}

std::vector<Syllable> operandExpr(RccAsmParser::OperandContext *ctx, const std::string &mnemonic) {
    if (ctx->LBRACKET()) fail("'" + mnemonic + "': this dialect has no bracketed/memory operand form");
    return exprToSyllables(ctx->expr());
}

// A register operand must spell exactly `r0`..`r7` (case-insensitive) as one bare, unsigned
// identifier syllable - never resolved against the label table the way LDI's immediate is, so
// this is checked and converted right here at parse time, not deferred to the two-pass
// label-resolution pass below.
uint8_t parseRegisterOperand(RccAsmParser::OperandContext *ctx, const std::string &mnemonic) {
    std::vector<Syllable> syls = operandExpr(ctx, mnemonic);
    if (syls.size() != 1 || syls[0].sign != 0 || syls[0].kind != SylKind::Symbol) {
        fail("'" + mnemonic + "': expected a plain register operand (r0-r7)");
    }
    std::string text = toLower(syls[0].text);
    if (text.size() == 2 && text[0] == 'r' && text[1] >= '0' && text[1] <= '7') {
        return static_cast<uint8_t>(text[1] - '0');
    }
    fail("'" + mnemonic + "': '" + syls[0].text + "' isn't a valid register (expected r0-r7)");
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
    if (dynamic_cast<RccAsmParser::TimesStmtContext *>(stmt)) {
        fail("'times' isn't part of this target's dialect");
    }
    auto *instr = dynamic_cast<RccAsmParser::InstrStmtContext *>(stmt);
    if (!instr) fail("internal: unhandled statement alternative");

    std::string word = toLower(instr->mnemonic->getText());
    if (word == ".org") {
        line.kind = Line::Org;
        if (instr->operand().size() != 1) fail("'.org' needs exactly one address operand");
        line.exprList.push_back(operandExpr(instr->operand(0), word));
        return line;
    }
    if (word == ".byte") {
        line.kind = Line::Byte;
        for (auto *op : instr->operand()) line.exprList.push_back(operandExpr(op, word));
        if (line.exprList.empty()) fail("'.byte' needs at least one value");
        return line;
    }
    if (word == ".short") {
        line.kind = Line::Short;
        for (auto *op : instr->operand()) line.exprList.push_back(operandExpr(op, word));
        if (line.exprList.empty()) fail("'.short' needs at least one value");
        return line;
    }
    if (word == ".word") {
        line.kind = Line::Word;
        for (auto *op : instr->operand()) line.exprList.push_back(operandExpr(op, word));
        if (line.exprList.empty()) fail("'.word' needs at least one value");
        return line;
    }
    if (word == ".zero") {
        line.kind = Line::Zero;
        if (instr->operand().size() != 1) fail("'.zero' needs exactly one count operand");
        line.exprList.push_back(operandExpr(instr->operand(0), word));
        return line;
    }

    auto shapeIt = opShapes().find(word);
    if (shapeIt == opShapes().end()) fail("unsupported instruction '" + instr->mnemonic->getText() + "'");
    const OpShape &shape = shapeIt->second;
    size_t wantOperands = static_cast<size_t>(shape.regCount) + (shape.hasImmediate ? 1 : 0);
    if (instr->operand().size() != wantOperands) {
        fail("'" + word + "' needs exactly " + std::to_string(wantOperands) + " operand(s), got " +
             std::to_string(instr->operand().size()));
    }

    line.kind = Line::Instr;
    line.mnemonic = word;
    for (int i = 0; i < shape.regCount; i++) {
        line.regOperands.push_back(parseRegisterOperand(instr->operand(static_cast<size_t>(i)), word));
    }
    if (shape.hasImmediate) {
        line.immOperand = operandExpr(instr->operand(static_cast<size_t>(shape.regCount)), word);
    }
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

// --- Two-pass assembly ----------------------------------------------------
class Assembler {
   public:
    // `addressBase` is added to every label's resolved value and is where `offset_` itself
    // starts counting - purely an internal numbering convention, never emitted as bytes (see
    // assembleVex()'s own comment for why a whole-program compile needs kVexBaseAddress here
    // while an object-mode compile needs 0, left to GenericLinker's own base-shift relocation
    // instead).
    explicit Assembler(bool allowExternal, uint32_t addressBase = 0)
        : allowExternal_(allowExternal), addressBase_(addressBase) {}

    void run(const std::vector<Line> &lines) {
        stage_ = 1;
        pass(lines);
        stage_ = 2;
        pass(lines);
    }

    const std::vector<uint8_t> &bytes() const { return bytes_; }
    const std::unordered_map<std::string, uint32_t> &labels() const { return labels_; }
    const std::vector<Relocation> &relocations() const { return relocations_; }

   private:
    void pass(const std::vector<Line> &lines) {
        bytes_.clear();
        relocations_.clear();
        offset_ = addressBase_;
        for (const auto &line : lines) processLine(line);
    }

    void processLine(const Line &line) {
        for (const auto &lbl : line.labels) labels_[lbl] = offset_;
        switch (line.kind) {
            case Line::None:
                return;
            case Line::Org: {
                uint32_t target = evalExpr(line.exprList.at(0));
                if (target < offset_) fail("'.org' can't move backward (at " + std::to_string(offset_) + ", asked for " +
                                            std::to_string(target) + ")");
                while (offset_ < target) emit8(0);
                return;
            }
            case Line::Byte:
                for (const auto &e : line.exprList) emitN(e, 1);
                return;
            case Line::Short:
                for (const auto &e : line.exprList) emitN(e, 2);
                return;
            case Line::Word:
                for (const auto &e : line.exprList) emitN(e, 4);
                return;
            case Line::Zero: {
                uint32_t n = evalExpr(line.exprList.at(0));
                for (uint32_t i = 0; i < n; i++) emit8(0);
                return;
            }
            case Line::Instr:
                encodeInstr(line);
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

    uint32_t resolveSyllable(const Syllable &syl, bool *wasExternal, bool *wasLocalAddress) {
        if (syl.kind == SylKind::Number) {
            const std::string &t = syl.text;
            if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
                return static_cast<uint32_t>(std::stoul(t.substr(2), nullptr, 16));
            }
            return static_cast<uint32_t>(std::stoul(t));
        }
        auto it = labels_.find(syl.text);
        if (it != labels_.end()) {
            if (allowExternal_) *wasLocalAddress = true;
            return it->second;
        }
        if (stage_ == 2) {
            if (allowExternal_) {
                *wasExternal = true;
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

    // `n` bytes, big-endian. A symbolic/external value is only meaningful in a full 4-byte
    // field (every address this backend ever stores - `.word`, or `LDI`'s own immediate - is
    // 4 bytes; see VexCodegen.h), so stage 2 rejects one anywhere narrower instead of silently
    // truncating a relocation that would only ever patch a 4-byte site.
    void emitN(const std::vector<Syllable> &e, int n) {
        std::string externalName;
        bool needsBaseShift = false;
        uint32_t v = evalExpr(e, &externalName, &needsBaseShift);
        if (stage_ == 2) {
            if (n < 4 && (needsBaseShift || !externalName.empty())) {
                fail("a label address needs a 4-byte field (.word or 'ldi'), not " + std::to_string(n) +
                     " byte(s)");
            }
            if (n < 4) {
                uint32_t limit = 1u << (8 * n);
                if (v >= limit) fail("value " + std::to_string(v) + " doesn't fit in " + std::to_string(n) + " byte(s)");
            } else if (allowExternal_) {
                if (needsBaseShift) relocations_.push_back({offset_, ".."});
                if (!externalName.empty()) relocations_.push_back({offset_, externalName});
            }
        }
        for (int i = n - 1; i >= 0; i--) emit8(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
    }

    // The 4-byte big-endian `LDI` immediate - the one relocatable field this dialect's own
    // instructions (as opposed to its data directives) ever have.
    void emitImmediate(const std::vector<Syllable> &e) { emitN(e, 4); }

    void encodeInstr(const Line &line) {
        emit8(opcodes().at(line.mnemonic));
        for (uint8_t reg : line.regOperands) emit8(reg);
        if (line.mnemonic == "ldi") emitImmediate(line.immOperand);
    }

    std::vector<uint8_t> bytes_;
    std::unordered_map<std::string, uint32_t> labels_;
    std::vector<Relocation> relocations_;
    uint32_t offset_ = 0;
    int stage_ = 1;
    bool allowExternal_;
    uint32_t addressBase_ = 0;
};

ObjectFile buildObjectFile(const Assembler &a) {
    ObjectFile obj;
    obj.machineType = kVexCoffMachine;
    const auto &bytesVec = a.bytes();
    obj.textBytes.assign(bytesVec.begin(), bytesVec.end());

    for (const auto &kv : a.labels()) obj.symbols.push_back({kv.first, kv.second, /*isDefined=*/true});
    std::sort(obj.symbols.begin(), obj.symbols.end(),
              [](const ObjSymbol &x, const ObjSymbol &y) { return x.name < y.name; });

    std::unordered_map<std::string, bool> known;
    for (const auto &s : obj.symbols) known[s.name] = true;
    for (const auto &r : a.relocations()) {
        if (!known.count(r.symbolName)) {
            obj.symbols.push_back({r.symbolName, 0, /*isDefined=*/false});
            known[r.symbolName] = true;
        }
        obj.relocations.push_back({r.byteOffset, r.symbolName, kVexRelocAbs32});
    }
    return obj;
}

}  // namespace

std::string assembleVex(const std::string &asmText) {
    std::vector<Line> lines = parseProgram(asmText);
    // Whole-program compile: every label must resolve to the true VM address LD/ST/JMP/CAL will
    // see at runtime (the interpreter maps this file's own byte 0 to kVexBaseAddress), so label
    // values are counted from there directly - see the Assembler constructor's own comment.
    Assembler assembler(/*allowExternal=*/false, kVexBaseAddress);
    assembler.run(lines);
    const auto &bytes = assembler.bytes();
    return std::string(bytes.begin(), bytes.end());
}

ObjectFile assembleVexObject(const std::string &asmText) {
    std::vector<Line> lines = parseProgram(asmText);
    // Object mode: labels resolve relative to offset 0 ("org 0" until GenericLinker adds the
    // real base in via the ".." pseudo-symbol's base-shift relocation - see VexLinkerFactory.cpp).
    Assembler assembler(/*allowExternal=*/true, /*addressBase=*/0);
    assembler.run(lines);
    return buildObjectFile(assembler);
}

}  // namespace rcc
