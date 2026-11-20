"""Verifies main.raw actually pokes GTIA's COLBK and POKEY's AUDF1/AUDC1 registers to the
expected values before falling into its own halt loop, run in py65's bare 6502 MPU core - no
GTIA/POKEY hardware is simulated, so these addresses are read back as plain RAM, not real video/
sound registers; this only confirms the compiler's own codegen/linker/runtime-library
correctness, not what the color/tone looks or sounds like on real/emulated hardware.

Usage: test.py MAINRAW
"""
import sys

from py65.devices.mpu6502 import MPU

with open(sys.argv[1], "rb") as f:
    data = f.read()

base = 0x8000
mpu = MPU()
mpu.memory[base:base + len(data)] = data
mpu.pc = base

for _ in range(200000):
    pc_before = mpu.pc
    mpu.step()
    if mpu.pc == pc_before:
        break
else:
    raise SystemExit("simulator never reached the halt loop (step limit exceeded)")

colbk = mpu.memory[0xC01A]
assert colbk == 0x3A, f"expected COLBK ($C01A) to be $3A, got ${colbk:02X}"
audf1 = mpu.memory[0xE800]
audc1 = mpu.memory[0xE801]
assert audf1 == 60, f"expected AUDF1 ($E800) to be 60, got {audf1}"
assert audc1 == 0xAF, f"expected AUDC1 ($E801) to be $AF, got ${audc1:02X}"
print("PASS")
