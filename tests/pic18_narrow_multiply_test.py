from pathlib import Path
import re
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
RCC = Path(sys.argv.pop(1) if len(sys.argv) > 1 else ROOT / "build/bin/rcc").resolve()
BUILD = ROOT / "build/pic18-narrow-multiply-tests"


def narrowMulSource(a, b):
    return """
unsigned char a;
unsigned char b;
unsigned int r;
int main() {
    a = %d;
    b = %d;
    r = a * b;
    return 0;
}
""" % (a, b)


WIDE_MUL_SOURCE = """
int a;
int b;
int r;
int main() {
    a = 300;
    b = 7;
    r = a * b;
    return 0;
}
"""


def compileToAsm(source, path):
    path.write_text(source)
    out_path = path.with_suffix(".asm")
    result = subprocess.run(
        [str(RCC), "--target=pic18", "--format=asm", f"--out={out_path}", str(path)],
        capture_output=True, text=True,
    )
    return result, out_path


def compileToHex(source, path, out_path):
    path.write_text(source)
    result = subprocess.run(
        [str(RCC), "--target=pic18", "--format=hex", f"--out={out_path}", str(path)],
        capture_output=True, text=True,
    )
    return result


# A minimal interpreter for exactly the straight-line PIC18 instruction subset
# PIC18Codegen.cpp's genMul()/genFunction() prologue/epilogue emit: enough to
# execute this test's whole-program output for real and read back the global
# result, rather than only inspecting the assembly text.
class TinyPIC18Interpreter:
    def __init__(self, asmText):
        self.equates = {}
        self.mem = bytearray(256)
        self.w = 0
        lines = [ln.split(";", 1)[0].strip() for ln in asmText.splitlines()]
        lines = [ln for ln in lines if ln]

        ramStart = lines.index("orgram 0x00")
        codeLines = lines[:ramStart]
        ramLines = lines[ramStart + 1:]

        for ln in codeLines:
            m = re.match(r"^(\w+)\s*=\s*(.+)$", ln)
            if m:
                self.equates[m.group(1)] = int(m.group(2), 0)

        addr = 0
        for ln in ramLines:
            m = re.match(r"^(\w+):\s*res\s+(\d+)$", ln)
            if not m:
                continue
            self.equates[m.group(1)] = addr
            addr += int(m.group(2))

        self.labels = {}
        self.instrs = []
        for ln in codeLines:
            if ln in ("org 0",):
                continue
            m = re.match(r"^(\w+):$", ln)
            if m:
                self.labels[m.group(1)] = len(self.instrs)
                continue
            if re.match(r"^\w+\s*=\s*.+$", ln):
                continue
            self.instrs.append(ln)

    def resolveOperand(self, tok):
        tok = tok.strip()
        m = re.match(r"^(\w+)\+(\d+)$", tok)
        if m:
            return self.equates[m.group(1)] + int(m.group(2))
        if tok in self.equates:
            return self.equates[tok]
        if tok in ("w", "f"):
            return tok
        return int(tok, 0)

    # FSR0L/INDF0/POSTINC0 are hardware indirect-addressing registers, not plain memory cells:
    # a read/write of INDF0 or POSTINC0 actually targets whatever byte FSR0L currently holds the
    # address of (POSTINC0 then bumps FSR0L), exactly as PIC18Codegen.cpp's pushCell2()/
    # popCell2()/loadAcIndirect() rely on in hardware.
    def readMem(self, addr):
        if addr == self.equates["INDF0"]:
            return self.mem[self.mem[self.equates["FSR0L"]]]
        if addr == self.equates["POSTINC0"]:
            real = self.mem[self.equates["FSR0L"]]
            self.mem[self.equates["FSR0L"]] = (real + 1) & 0xFF
            return self.mem[real]
        return self.mem[addr]

    def writeMem(self, addr, val):
        if addr == self.equates["INDF0"]:
            self.mem[self.mem[self.equates["FSR0L"]]] = val
            return
        if addr == self.equates["POSTINC0"]:
            real = self.mem[self.equates["FSR0L"]]
            self.mem[self.equates["FSR0L"]] = (real + 1) & 0xFF
            self.mem[real] = val
            return
        self.mem[addr] = val

    def run(self, maxSteps=20000):
        haltPc = self.labels["HALT"]
        pc = 0
        callStack = []
        steps = 0
        while True:
            steps += 1
            if steps > maxSteps:
                raise AssertionError("simulated program did not halt")
            if pc == haltPc:
                return
            line = self.instrs[pc]
            parts = line.split(None, 1)
            mnemonic = parts[0]
            rawOps = [o.strip() for o in parts[1].split(",")] if len(parts) > 1 else []
            nextPc = pc + 1

            if mnemonic == "goto":
                nextPc = self.labels[rawOps[0]]
            elif mnemonic == "call":
                callStack.append(pc + 1)
                nextPc = self.labels[rawOps[0]]
            elif mnemonic == "return":
                nextPc = callStack.pop()
            elif mnemonic == "movlw":
                self.w = self.resolveOperand(rawOps[0]) & 0xFF
            elif mnemonic == "addlw":
                self.w = (self.w + self.resolveOperand(rawOps[0])) & 0xFF
            elif mnemonic == "movwf":
                self.writeMem(self.resolveOperand(rawOps[0]), self.w)
            elif mnemonic == "clrf":
                self.writeMem(self.resolveOperand(rawOps[0]), 0)
            elif mnemonic == "decf":
                addr = self.resolveOperand(rawOps[0])
                val = (self.readMem(addr) - 1) & 0xFF
                if rawOps[1] == "f":
                    self.writeMem(addr, val)
                else:
                    self.w = val
            elif mnemonic == "movf":
                addr = self.resolveOperand(rawOps[0])
                val = self.readMem(addr)
                if rawOps[1] == "f":
                    self.writeMem(addr, val)
                else:
                    self.w = val
            elif mnemonic == "movff":
                self.writeMem(self.resolveOperand(rawOps[1]), self.readMem(self.resolveOperand(rawOps[0])))
            elif mnemonic == "mulwf":
                f = self.readMem(self.resolveOperand(rawOps[0]))
                product = (self.w * f) & 0xFFFF
                self.mem[self.equates["PRODL"]] = product & 0xFF
                self.mem[self.equates["PRODH"]] = (product >> 8) & 0xFF
            else:
                raise AssertionError("unsupported instruction in this minimal interpreter: " + line)
            pc = nextPc

    def globalWord(self, label):
        lo = self.mem[self.equates[label]]
        hi = self.mem[self.equates[label] + 1]
        return lo | (hi << 8)


class PIC18NarrowMultiplyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        BUILD.mkdir(parents=True, exist_ok=True)

    def runAndGetResult(self, a, b, name):
        src_path = BUILD / f"{name}.c"
        result, asm_path = compileToAsm(narrowMulSource(a, b), src_path)
        self.assertEqual(result.returncode, 0, result.stderr)
        interp = TinyPIC18Interpreter(asm_path.read_text())
        interp.run()
        return interp.globalWord("g_r")

    def test_unsigned_char_multiply_runs_and_produces_correct_result(self):
        for a, b in [(12, 13), (0, 200), (200, 0), (1, 1), (16, 16)]:
            with self.subTest(a=a, b=b):
                self.assertEqual(self.runAndGetResult(a, b, f"mul_{a}_{b}"), a * b)

    def test_unsigned_char_multiply_overflow_boundary_runs_and_produces_correct_result(self):
        # 255*255 = 65025, the largest value MULWF's 16-bit PRODH:PRODL result can ever
        # hold - exercises the boundary the generic 16x16 loop also has to get right.
        self.assertEqual(self.runAndGetResult(255, 255, "mul_255_255"), 255 * 255)

    def test_unsigned_char_multiply_uses_hardware_mulwf(self):
        _, asm_path = compileToAsm(narrowMulSource(12, 13), BUILD / "mul_asm_check.c")
        text = asm_path.read_text()
        self.assertIn("mulwf bc", text)
        self.assertIn("PRODL", text)
        self.assertIn("PRODH", text)

    def test_wide_multiply_does_not_use_mulwf(self):
        # A plain `int*int` multiply doesn't fit MULWF's 8x8->16 case, so the generic
        # shift-add loop (unaffected by this optimization) must still be used.
        result, asm_path = compileToAsm(WIDE_MUL_SOURCE, BUILD / "wide_mul.c")
        self.assertEqual(result.returncode, 0, result.stderr)
        text = asm_path.read_text()
        self.assertNotIn("mulwf", text)

    def test_signed_char_multiply_does_not_use_mulwf(self):
        # MULWF is an unsigned-only 8x8 multiply; a signed-char operand must keep using the
        # generic path since its negative-value bit pattern isn't what MULWF computes.
        source = """
char a;
char b;
int r;
int main() {
    a = -2;
    b = 3;
    r = a * b;
    return 0;
}
"""
        result, asm_path = compileToAsm(source, BUILD / "signed_mul.c")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("mulwf", asm_path.read_text())

    def test_wide_multiply_assembles_successfully(self):
        out_path = BUILD / "wide_mul.hex"
        result = compileToHex(WIDE_MUL_SOURCE, BUILD / "wide_mul2.c", out_path)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
