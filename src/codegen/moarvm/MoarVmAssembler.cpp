#include "MoarVmAssembler.h"

#include <stdexcept>

namespace rcc {

// Opcode numbers below are MoarVM 2026.11 / bytecode version 7's real numeric op codes, read
// from that release's own code generator (share/nqp/lib/MAST/Ops.nqp's %codes table) - see
// docs/platforms.md's moarvm section for how these were obtained and verified against a real
// `moar` interpreter. They are not derivable from MoarVM's own public docs/bytecode.md, which
// only describes operand shapes, not op numbers (those come from src/core/oplist's file order,
// which this port does not re-derive at build time).
namespace {
constexpr uint16_t kOpConstI64 = 4;
constexpr uint16_t kOpConstI6416 = 597;
constexpr uint16_t kOpConstI6432 = 598;
constexpr uint16_t kOpConstS = 7;
constexpr uint16_t kOpSet = 8;
constexpr uint16_t kOpGoto = 23;
constexpr uint16_t kOpIfI = 24;
constexpr uint16_t kOpUnlessI = 25;
constexpr uint16_t kOpCoerceIs = 121;
constexpr uint16_t kOpCheckarity = 140;
constexpr uint16_t kOpParamRpI = 141;
constexpr uint16_t kOpParamRpO = 144;
constexpr uint16_t kOpGetcode = 159;
constexpr uint16_t kOpNull = 247;
constexpr uint16_t kOpCreate = 254;
constexpr uint16_t kOpBootarray = 342;
constexpr uint16_t kOpBootintarray = 343;
constexpr uint16_t kOpAtposI = 290;
constexpr uint16_t kOpBindposI = 294;
constexpr uint16_t kOpAtposO = 293;
constexpr uint16_t kOpBindposO = 297;
constexpr uint16_t kOpReturnI = 51;
constexpr uint16_t kOpReturnO = 54;
constexpr uint16_t kOpReturn = 55;
constexpr uint16_t kOpSay = 495;
constexpr uint16_t kOpDispatchV = 826;
constexpr uint16_t kOpDispatchI = 827;

constexpr uint32_t kHeaderSize = 96;
constexpr uint32_t kBytecodeVersion = 7;
}  // namespace

void MoarVmAssembler::write16(std::string &buf, uint16_t v) {
    buf.push_back(static_cast<char>(v & 0xFF));
    buf.push_back(static_cast<char>((v >> 8) & 0xFF));
}

void MoarVmAssembler::write32(std::string &buf, uint32_t v) {
    buf.push_back(static_cast<char>(v & 0xFF));
    buf.push_back(static_cast<char>((v >> 8) & 0xFF));
    buf.push_back(static_cast<char>((v >> 16) & 0xFF));
    buf.push_back(static_cast<char>((v >> 24) & 0xFF));
}

MoarVmAssembler::Frame &MoarVmAssembler::cur() {
    if (currentFrame_ < 0) throw std::runtime_error("MoarVmAssembler: no frame is open");
    return frames_[static_cast<size_t>(currentFrame_)];
}

void MoarVmAssembler::writeOp(uint16_t opcode) { write16(cur().bytecode, opcode); }

int MoarVmAssembler::internString(const std::string &s) {
    auto it = stringIndex_.find(s);
    if (it != stringIndex_.end()) return it->second;
    int idx = static_cast<int>(strings_.size());
    strings_.push_back(s);
    stringIndex_[s] = idx;
    return idx;
}

int MoarVmAssembler::internCallsite(const std::vector<uint8_t> &argFlags) {
    for (size_t i = 0; i < callsites_.size(); i++) {
        if (callsites_[i] == argFlags) return static_cast<int>(i);
    }
    callsites_.push_back(argFlags);
    return static_cast<int>(callsites_.size() - 1);
}

int MoarVmAssembler::beginFrame(const std::string &name) {
    currentFrame_ = static_cast<int>(frames_.size());
    frames_.push_back(Frame{name, {}, {}, {}, {}});
    return currentFrame_;
}

int MoarVmAssembler::addLocal(LocalType type) {
    cur().locals.push_back(type);
    return static_cast<int>(cur().locals.size() - 1);
}

int MoarVmAssembler::newLabel() { return nextLabelId_++; }

void MoarVmAssembler::placeLabel(int labelId) { cur().labelOffsets[labelId] = cur().bytecode.size(); }

void MoarVmAssembler::endFrame() {
    Frame &f = cur();
    for (const auto &[byteOffset, labelId] : f.labelFixups) {
        auto it = f.labelOffsets.find(labelId);
        if (it == f.labelOffsets.end()) throw std::runtime_error("MoarVmAssembler: label never placed");
        uint32_t target = static_cast<uint32_t>(it->second);
        f.bytecode[byteOffset] = static_cast<char>(target & 0xFF);
        f.bytecode[byteOffset + 1] = static_cast<char>((target >> 8) & 0xFF);
        f.bytecode[byteOffset + 2] = static_cast<char>((target >> 16) & 0xFF);
        f.bytecode[byteOffset + 3] = static_cast<char>((target >> 24) & 0xFF);
    }
    currentFrame_ = -1;
}

void MoarVmAssembler::emitCheckarity(int16_t minArgs, int16_t maxArgs) {
    writeOp(kOpCheckarity);
    write16(cur().bytecode, static_cast<uint16_t>(minArgs));
    write16(cur().bytecode, static_cast<uint16_t>(maxArgs));
}

void MoarVmAssembler::emitConstI64(int dst, int64_t value) {
    if (value > -32767 && value < 32768) {
        writeOp(kOpConstI6416);
        write16(cur().bytecode, static_cast<uint16_t>(dst));
        write16(cur().bytecode, static_cast<uint16_t>(static_cast<int16_t>(value)));
    } else if (value > -2147483647LL && value < 2147483647LL) {
        writeOp(kOpConstI6432);
        write16(cur().bytecode, static_cast<uint16_t>(dst));
        write32(cur().bytecode, static_cast<uint32_t>(static_cast<int32_t>(value)));
    } else {
        writeOp(kOpConstI64);
        write16(cur().bytecode, static_cast<uint16_t>(dst));
        write32(cur().bytecode, static_cast<uint32_t>(value & 0xFFFFFFFFu));
        write32(cur().bytecode, static_cast<uint32_t>((static_cast<uint64_t>(value) >> 32) & 0xFFFFFFFFu));
    }
}

void MoarVmAssembler::emitConstStr(int dst, int strIdx) {
    writeOp(kOpConstS);
    write16(cur().bytecode, static_cast<uint16_t>(dst));
    write32(cur().bytecode, static_cast<uint32_t>(strIdx));
}

void MoarVmAssembler::emitSet(int dst, int src) {
    writeOp(kOpSet);
    write16(cur().bytecode, static_cast<uint16_t>(dst));
    write16(cur().bytecode, static_cast<uint16_t>(src));
}

void MoarVmAssembler::emitGoto(int labelId) {
    writeOp(kOpGoto);
    cur().labelFixups.push_back({cur().bytecode.size(), labelId});
    write32(cur().bytecode, 0);
}

void MoarVmAssembler::emitIfI(int cond, int labelId) {
    writeOp(kOpIfI);
    write16(cur().bytecode, static_cast<uint16_t>(cond));
    cur().labelFixups.push_back({cur().bytecode.size(), labelId});
    write32(cur().bytecode, 0);
}

void MoarVmAssembler::emitUnlessI(int cond, int labelId) {
    writeOp(kOpUnlessI);
    write16(cur().bytecode, static_cast<uint16_t>(cond));
    cur().labelFixups.push_back({cur().bytecode.size(), labelId});
    write32(cur().bytecode, 0);
}

void MoarVmAssembler::emitBinOp(int opcode, int dst, int a, int b) {
    writeOp(static_cast<uint16_t>(opcode));
    write16(cur().bytecode, static_cast<uint16_t>(dst));
    write16(cur().bytecode, static_cast<uint16_t>(a));
    write16(cur().bytecode, static_cast<uint16_t>(b));
}

void MoarVmAssembler::emitUnOp(int opcode, int dst, int src) {
    writeOp(static_cast<uint16_t>(opcode));
    write16(cur().bytecode, static_cast<uint16_t>(dst));
    write16(cur().bytecode, static_cast<uint16_t>(src));
}

void MoarVmAssembler::emitCoerceIS(int dst, int src) { emitUnOp(kOpCoerceIs, dst, src); }

void MoarVmAssembler::emitParamRpI(int dst, int16_t paramIndex) {
    writeOp(kOpParamRpI);
    write16(cur().bytecode, static_cast<uint16_t>(dst));
    write16(cur().bytecode, static_cast<uint16_t>(paramIndex));
}

void MoarVmAssembler::emitParamRpO(int dst, int16_t paramIndex) {
    writeOp(kOpParamRpO);
    write16(cur().bytecode, static_cast<uint16_t>(dst));
    write16(cur().bytecode, static_cast<uint16_t>(paramIndex));
}

void MoarVmAssembler::emitGetCode(int dst, int calleeFrameIdx) {
    writeOp(kOpGetcode);
    write16(cur().bytecode, static_cast<uint16_t>(dst));
    write16(cur().bytecode, static_cast<uint16_t>(calleeFrameIdx));
}

void MoarVmAssembler::emitReturnI(int src) {
    writeOp(kOpReturnI);
    write16(cur().bytecode, static_cast<uint16_t>(src));
}

void MoarVmAssembler::emitReturnO(int src) {
    writeOp(kOpReturnO);
    write16(cur().bytecode, static_cast<uint16_t>(src));
}

void MoarVmAssembler::emitReturnVoid() { writeOp(kOpReturn); }

void MoarVmAssembler::emitSay(int strReg) {
    writeOp(kOpSay);
    write16(cur().bytecode, static_cast<uint16_t>(strReg));
}

void MoarVmAssembler::emitDispatchV(const std::string &dispatcherName, int callsiteIdx,
                                     const std::vector<int> &argRegs) {
    writeOp(kOpDispatchV);
    write32(cur().bytecode, static_cast<uint32_t>(internString(dispatcherName)));
    write16(cur().bytecode, static_cast<uint16_t>(callsiteIdx));
    for (int r : argRegs) write16(cur().bytecode, static_cast<uint16_t>(r));
}

void MoarVmAssembler::emitDispatchI(int dst, const std::string &dispatcherName, int callsiteIdx,
                                     const std::vector<int> &argRegs) {
    writeOp(kOpDispatchI);
    write16(cur().bytecode, static_cast<uint16_t>(dst));
    write32(cur().bytecode, static_cast<uint32_t>(internString(dispatcherName)));
    write16(cur().bytecode, static_cast<uint16_t>(callsiteIdx));
    for (int r : argRegs) write16(cur().bytecode, static_cast<uint16_t>(r));
}

void MoarVmAssembler::emitAtposI(int dst, int arrayReg, int indexReg) {
    writeOp(kOpAtposI);
    write16(cur().bytecode, static_cast<uint16_t>(dst));
    write16(cur().bytecode, static_cast<uint16_t>(arrayReg));
    write16(cur().bytecode, static_cast<uint16_t>(indexReg));
}

void MoarVmAssembler::emitBindposI(int arrayReg, int indexReg, int valueReg) {
    writeOp(kOpBindposI);
    write16(cur().bytecode, static_cast<uint16_t>(arrayReg));
    write16(cur().bytecode, static_cast<uint16_t>(indexReg));
    write16(cur().bytecode, static_cast<uint16_t>(valueReg));
}

void MoarVmAssembler::emitAtposO(int dst, int arrayReg, int indexReg) {
    writeOp(kOpAtposO);
    write16(cur().bytecode, static_cast<uint16_t>(dst));
    write16(cur().bytecode, static_cast<uint16_t>(arrayReg));
    write16(cur().bytecode, static_cast<uint16_t>(indexReg));
}

void MoarVmAssembler::emitBindposO(int arrayReg, int indexReg, int valueReg) {
    writeOp(kOpBindposO);
    write16(cur().bytecode, static_cast<uint16_t>(arrayReg));
    write16(cur().bytecode, static_cast<uint16_t>(indexReg));
    write16(cur().bytecode, static_cast<uint16_t>(valueReg));
}

void MoarVmAssembler::emitCreate(int dst, int typeReg) {
    writeOp(kOpCreate);
    write16(cur().bytecode, static_cast<uint16_t>(dst));
    write16(cur().bytecode, static_cast<uint16_t>(typeReg));
}

void MoarVmAssembler::emitBootIntArray(int dst) {
    writeOp(kOpBootintarray);
    write16(cur().bytecode, static_cast<uint16_t>(dst));
}

void MoarVmAssembler::emitBootArray(int dst) {
    writeOp(kOpBootarray);
    write16(cur().bytecode, static_cast<uint16_t>(dst));
}

void MoarVmAssembler::emitNull(int dst) {
    writeOp(kOpNull);
    write16(cur().bytecode, static_cast<uint16_t>(dst));
}

void MoarVmAssembler::setMainFrame(int frameIdx) { mainFrame_ = frameIdx; }

std::string MoarVmAssembler::finish() {
    if (currentFrame_ != -1) throw std::runtime_error("MoarVmAssembler: a frame was never ended");
    if (mainFrame_ < 0) throw std::runtime_error("MoarVmAssembler: no main frame set");
    if (strings_.empty() || strings_[0] != "") {
        throw std::runtime_error("MoarVmAssembler: string heap entry 0 must be the empty string");
    }

    std::string stringsHeap;
    for (const auto &s : strings_) {
        write32(stringsHeap, (static_cast<uint32_t>(s.size()) << 1) | 1u);
        stringsHeap += s;
        while (stringsHeap.size() % 4 != 0) stringsHeap.push_back('\0');
    }

    std::string framesData;
    std::string bytecodeSeg;
    for (const auto &f : frames_) {
        uint32_t bcOffset = static_cast<uint32_t>(bytecodeSeg.size());
        bytecodeSeg += f.bytecode;

        write32(framesData, bcOffset);
        write32(framesData, static_cast<uint32_t>(f.bytecode.size()));
        write32(framesData, static_cast<uint32_t>(f.locals.size()));
        write32(framesData, 0);  // num lexicals
        write32(framesData, 0);  // cuuid str idx
        write32(framesData, 0);  // name str idx
        size_t selfIdx = &f - &frames_[0];
        write16(framesData, static_cast<uint16_t>(selfIdx));  // outer = self (no closures)
        write32(framesData, 0);                               // annotation offset
        write32(framesData, 0);                               // num annotations
        write32(framesData, 0);                               // num handlers
        write16(framesData, 0);                               // frame flag bits
        write16(framesData, 0);                               // num static lex entries
        write32(framesData, 0);                               // code obj SC dep idx+1
        write32(framesData, 0);                               // SC object idx
        write32(framesData, 0);                               // num local debug name mappings
        for (LocalType t : f.locals) write16(framesData, static_cast<uint16_t>(t));
    }

    std::string callsitesData;
    for (const auto &flags : callsites_) {
        write16(callsitesData, static_cast<uint16_t>(flags.size()));
        for (uint8_t b : flags) callsitesData.push_back(static_cast<char>(b));
        if (flags.size() % 2 != 0) callsitesData.push_back('\0');
    }

    uint32_t stringsOff = kHeaderSize;
    uint32_t framesOff = stringsOff + static_cast<uint32_t>(stringsHeap.size());
    uint32_t callsitesOff = framesOff + static_cast<uint32_t>(framesData.size());
    uint32_t bytecodeOff = callsitesOff + static_cast<uint32_t>(callsitesData.size());
    uint32_t annotOff = bytecodeOff + static_cast<uint32_t>(bytecodeSeg.size());

    std::string header;
    header += "MOARVM\r\n";
    write32(header, kBytecodeVersion);
    write32(header, 0);
    write32(header, 0);  // sc deps
    write32(header, 0);
    write32(header, 0);  // extops
    write32(header, framesOff);
    write32(header, static_cast<uint32_t>(frames_.size()));
    write32(header, callsitesOff);
    write32(header, static_cast<uint32_t>(callsites_.size()));
    write32(header, stringsOff);
    write32(header, static_cast<uint32_t>(strings_.size()));
    write32(header, 0);
    write32(header, 0);  // sc data
    write32(header, bytecodeOff);
    write32(header, static_cast<uint32_t>(bytecodeSeg.size()));
    write32(header, annotOff);
    write32(header, 0);  // annotation
    write32(header, 0);  // HLL name -> string 0, the required empty string
    write32(header, static_cast<uint32_t>(mainFrame_ + 1));  // mainline
    write32(header, static_cast<uint32_t>(mainFrame_ + 1));  // main
    write32(header, 0);                                      // load
    write32(header, 0);                                      // deserialize

    return header + stringsHeap + framesData + callsitesData + bytecodeSeg;
}

}  // namespace rcc
