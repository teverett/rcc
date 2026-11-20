"""Verifies main.raw runs the BASIC program in main.bas on py65's 6502 MPU
core: PRINTs a banner, sums 1..10 via FOR/NEXT, doubles it via GOSUB/RETURN,
and PRINTs both results. Same COUT/CROUT-stub approach as
../6502-dos33/test.py (its own header comment explains the stub bytes) -
only COUT/CROUT are patched here since main.bas never calls HOME/SETTXT/
VTAB/SETINV/SETNORM.

Usage: test.py MAINRAW
"""
import sys

from py65.devices.mpu6502 import MPU

with open(sys.argv[1], "rb") as f:
    data = f.read()

base = 0x2000
mpu = MPU()
mpu.memory[base:base + len(data)] = data

mpu.memory[0xFDED:0xFDED + 9] = [0xA6, 0xF0, 0x9D, 0x00, 0x04, 0xE8, 0x86, 0xF0, 0x60]
mpu.memory[0xF0] = 0
mpu.memory[0xFD8E:0xFD8E + 5] = [0xA9, 0x8A, 0x4C, 0xED, 0xFD]

mpu.pc = base
for _ in range(2_000_000):
    pc_before = mpu.pc
    mpu.step()
    if mpu.pc == pc_before:
        break
else:
    raise SystemExit("simulator never reached the halt loop (step limit exceeded)")

n = mpu.memory[0xF0]
out = bytes(mpu.memory[0x0400 + i] & 0x7F for i in range(n))
expected = (
    b"RCC BASIC ON APPLE ][!\n"
    b"SUM 1 TO 10 =\n"
    b"55\n"
    b"DOUBLED =\n"
    b"110\n"
)
assert out == expected, out
print("PASS")
