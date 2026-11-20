"""Verifies main.raw calls CHPUT ($00A2) once per character of "HELLO, WORLD!", in order, with
the character code in A each time - run on a real Z80 core (z80-python). There's no real MSX
BIOS ROM dump bundled with this project (see src/rt/msx/msx.h's own header comment), so CHPUT is
patched with a bare RET: this can't verify what a real BIOS does with the character, only that
msx_putc()/msx_puts() load A and CALL the documented address in the documented order - the same
scope `tests/run_msx_rt_tests.sh` checks for each primitive individually.

Usage: test.py MAINRAW
"""
import sys

from z80_python import Z80CPU

with open(sys.argv[1], "rb") as f:
    data = f.read()

CHPUT = 0x00A2

base = 0x4000
memory = bytearray(0x10000)
memory[base:base + len(data)] = data
memory[CHPUT] = 0xC9  # RET

seen = []
cpu = Z80CPU(memory.__getitem__, memory.__setitem__, read_port=lambda p: 0xFF, write_port=lambda p, v: None)
cpu.pc = base + 10  # skip the 10-byte cartridge header (sig+init+statement+device+text)
cpu.sp = 0xF380
for _ in range(2_000_000):
    pc_before = cpu.pc
    if cpu.pc == CHPUT:
        seen.append(cpu.a)
    cpu.step()
    if cpu.pc == pc_before:  # "halt: jp halt"
        break
else:
    raise SystemExit("simulator never reached the halt loop (step limit exceeded)")

expected = [ord(c) for c in "HELLO, WORLD!"]
assert seen == expected, (seen, expected)
print("PASS")
