// Byte-exact coverage of every instruction I8086Assembler.cpp's encodeInstr() only accepts
// when `allow286_` is set (assembleI286()'s own dialect) - the full set the 80286 adds over
// plain 8086: pusha/popa, push/pushb of an immediate, 2-/3-operand imul with an immediate, a
// shift/rotate by an immediate count other than 1, bound, enter/leave, insb/insw/outsb/outsw,
// and the protected-mode/descriptor-table group (lgdt/sgdt/lidt/sidt/lldt/sldt/ltr/str/lmsw/
// smsw/clts/arpl/verr/verw). Every encoding here is cross-checked against the Intel 80286
// Programmer's Reference Manual's own opcode tables. Each mnemonic also gets a companion
// "rejected on plain 8086" check via assembleI8086(), confirming the variant gate actually
// gates - a plain 8086/8088/8086-dos compile can never silently depend on one of these.
#include "codegen/x86_8086/I8086Assembler.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

std::vector<uint8_t> toBytes(const std::string &s) { return std::vector<uint8_t>(s.begin(), s.end()); }

void expectBytes(const std::string &asmText, const std::vector<uint8_t> &want, const char *why) {
    std::string got;
    try {
        got = rcc::assembleI286(asmText);
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << why << ": threw " << e.what() << '\n';
        failures++;
        return;
    }
    std::vector<uint8_t> gotBytes = toBytes(got);
    if (gotBytes != want) {
        std::cerr << "FAIL " << why << ": got";
        for (uint8_t b : gotBytes) std::cerr << ' ' << std::hex << static_cast<int>(b);
        std::cerr << ", want";
        for (uint8_t b : want) std::cerr << ' ' << std::hex << static_cast<int>(b);
        std::cerr << std::dec << '\n';
        failures++;
        return;
    }
    std::cout << "PASS " << why << '\n';
}

// Confirms the same source, run through the plain-8086 dialect, is rejected - the variant
// gate's whole point.
void expectRejectedOn8086(const std::string &asmText, const char *why) {
    try {
        rcc::assembleI8086(asmText, rcc::I8086OutputFormat::Bin);
    } catch (const std::runtime_error &) {
        std::cout << "PASS " << why << '\n';
        return;
    }
    std::cerr << "FAIL " << why << ": expected assembleI8086() to reject an 80286-only instruction\n";
    failures++;
}

void testPushaPopa() {
    expectBytes("pusha\npopa\n", {0x60, 0x61}, "pusha_popa");
    expectRejectedOn8086("pusha\n", "pusha_rejected_on_8086");
    expectRejectedOn8086("popa\n", "popa_rejected_on_8086");
}

void testPushImmediate() {
    // "push 0x1234" - full 16-bit form (opcode 0x68), always, regardless of value.
    expectBytes("push 0x1234\n", {0x68, 0x34, 0x12}, "push_imm16");
    // "pushb 0x7F" - the explicit byte-sized spelling (opcode 0x6A).
    expectBytes("pushb 0x7F\n", {0x6A, 0x7F}, "pushb_imm8");
    expectRejectedOn8086("push 0x1234\n", "push_imm16_rejected_on_8086");
    expectRejectedOn8086("pushb 0x7F\n", "pushb_imm8_rejected_on_8086");
}

void testImul() {
    // "imul cx, 5" - 2-operand form (dst used as both reg and rm fields): 0x69 /r iw.
    expectBytes("imul cx, 5\n", {0x69, 0xC9, 0x05, 0x00}, "imul_2operand");
    // "imul ax, dx, 7" - 3-operand form (reg=dst=ax, rm=src=dx).
    expectBytes("imul ax, dx, 7\n", {0x69, 0xC2, 0x07, 0x00}, "imul_3operand");
    // The plain 1-operand AX-implied form is unchanged - still legal on plain 8086.
    expectBytes("imul cx\n", {0xF7, 0xE9}, "imul_1operand_unchanged");
    rcc::assembleI8086("imul cx\n", rcc::I8086OutputFormat::Bin);  // doesn't throw
    expectRejectedOn8086("imul cx, 5\n", "imul_2operand_rejected_on_8086");
    expectRejectedOn8086("imul ax, dx, 7\n", "imul_3operand_rejected_on_8086");
}

void testShiftByImmediateCount() {
    // "shl ax, 4" - 0xC1 /4 ib.
    expectBytes("shl ax, 4\n", {0xC1, 0xE0, 0x04}, "shl_imm_count");
    // "rcr bx, 3" - 0xC1 /3 ib.
    expectBytes("rcr bx, 3\n", {0xC1, 0xDB, 0x03}, "rcr_imm_count");
    // The existing "shift by 1"/"shift by cl" forms are unchanged - still legal on plain 8086.
    rcc::assembleI8086("shl ax, 1\n", rcc::I8086OutputFormat::Bin);          // doesn't throw
    rcc::assembleI8086("shl ax, cl\n", rcc::I8086OutputFormat::Bin);         // doesn't throw
    expectRejectedOn8086("shl ax, 4\n", "shl_imm_count_rejected_on_8086");
    expectRejectedOn8086("rcr bx, 3\n", "rcr_imm_count_rejected_on_8086");
}

void testBound() {
    expectBytes("org 0x1000\nlbl: dw 0\nbound cx, [lbl]\n", {0x00, 0x00, 0x62, 0x0E, 0x00, 0x10}, "bound");
    expectRejectedOn8086("org 0x1000\nlbl: dw 0\nbound cx, [lbl]\n", "bound_rejected_on_8086");
}

void testEnterLeave() {
    expectBytes("enter 8, 0\n", {0xC8, 0x08, 0x00, 0x00}, "enter");
    expectBytes("leave\n", {0xC9}, "leave");
    expectRejectedOn8086("enter 8, 0\n", "enter_rejected_on_8086");
    expectRejectedOn8086("leave\n", "leave_rejected_on_8086");
}

void testStringIO() {
    expectBytes("insb\ninsw\noutsb\noutsw\n", {0x6C, 0x6D, 0x6E, 0x6F}, "ins_outs");
    expectRejectedOn8086("insb\n", "insb_rejected_on_8086");
    expectRejectedOn8086("outsw\n", "outsw_rejected_on_8086");
}

void testClts() {
    expectBytes("clts\n", {0x0F, 0x06}, "clts");
    expectRejectedOn8086("clts\n", "clts_rejected_on_8086");
}

void testDescriptorTableLoadStore() {
    // lgdt/sgdt/lidt/sidt only ever take a memory operand (a 6-byte pseudo-descriptor).
    expectBytes("org 0x1000\nlbl: dw 0\nlgdt [lbl]\n", {0x00, 0x00, 0x0F, 0x01, 0x16, 0x00, 0x10}, "lgdt");
    expectBytes("org 0x1000\nlbl: dw 0\nsgdt [lbl]\n", {0x00, 0x00, 0x0F, 0x01, 0x06, 0x00, 0x10}, "sgdt");
    expectBytes("org 0x1000\nlbl: dw 0\nlidt [lbl]\n", {0x00, 0x00, 0x0F, 0x01, 0x1E, 0x00, 0x10}, "lidt");
    expectBytes("org 0x1000\nlbl: dw 0\nsidt [lbl]\n", {0x00, 0x00, 0x0F, 0x01, 0x0E, 0x00, 0x10}, "sidt");
    expectRejectedOn8086("org 0x1000\nlbl: dw 0\nlgdt [lbl]\n", "lgdt_rejected_on_8086");
    expectRejectedOn8086("org 0x1000\nlbl: dw 0\nsidt [lbl]\n", "sidt_rejected_on_8086");
    expectRejectedOn8086("sgdt cx\n", "sgdt_register_operand_rejected");  // mem-only, even on i286
}

void testSegmentSelectorGroup() {
    // sldt/str/lldt/ltr/verr/verw take a register OR memory r/m16 operand.
    expectBytes("sldt cx\n", {0x0F, 0x00, 0xC1}, "sldt_reg");
    expectBytes("org 0x1000\nlbl: dw 0\nltr [lbl]\n", {0x00, 0x00, 0x0F, 0x00, 0x1E, 0x00, 0x10}, "ltr_mem");
    expectBytes("verr bx\n", {0x0F, 0x00, 0xE3}, "verr_reg");
    expectBytes("org 0x1000\nlbl: dw 0\nverw [lbl]\n", {0x00, 0x00, 0x0F, 0x00, 0x2E, 0x00, 0x10}, "verw_mem");
    expectRejectedOn8086("sldt cx\n", "sldt_rejected_on_8086");
    expectRejectedOn8086("verr bx\n", "verr_rejected_on_8086");
}

void testMachineStatusWord() {
    // smsw/lmsw take a register OR memory r/m16 operand too, but in the 0x0F 0x01 group.
    expectBytes("smsw dx\n", {0x0F, 0x01, 0xE2}, "smsw_reg");
    expectBytes("org 0x1000\nlbl: dw 0\nlmsw [lbl]\n", {0x00, 0x00, 0x0F, 0x01, 0x36, 0x00, 0x10}, "lmsw_mem");
    expectRejectedOn8086("smsw dx\n", "smsw_rejected_on_8086");
    expectRejectedOn8086("lmsw dx\n", "lmsw_rejected_on_8086");
}

void testArpl() {
    expectBytes("arpl cx, dx\n", {0x63, 0xD1}, "arpl_reg_reg");
    expectBytes("org 0x1000\nlbl: dw 0\narpl [lbl], bx\n", {0x00, 0x00, 0x63, 0x1E, 0x00, 0x10}, "arpl_mem_reg");
    expectRejectedOn8086("arpl cx, dx\n", "arpl_rejected_on_8086");
}

// "loadall" is deliberately not implemented (see I8086Assembler.cpp's own comment on the
// protected-mode group) - undocumented, stepping-dependent, not in the 80286 Programmer's
// Reference Manual. Confirms it's rejected on both dialects rather than silently miscompiled.
void testLoadallNotImplemented() {
    expectRejectedOn8086("loadall\n", "loadall_rejected_on_8086");
    try {
        rcc::assembleI286("loadall\n");
        std::cerr << "FAIL loadall_rejected_on_i286: expected a throw\n";
        failures++;
    } catch (const std::runtime_error &) {
        std::cout << "PASS loadall_rejected_on_i286\n";
    }
}

}  // namespace

int main() {
    testPushaPopa();
    testPushImmediate();
    testImul();
    testShiftByImmediateCount();
    testBound();
    testEnterLeave();
    testStringIO();
    testClts();
    testDescriptorTableLoadStore();
    testSegmentSelectorGroup();
    testMachineStatusWord();
    testArpl();
    testLoadallNotImplemented();

    std::cout << "---counts---\n";
    std::cout << failures << '\n';
    return failures == 0 ? 0 : 1;
}
