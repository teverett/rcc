"""Verifies main.raw clears the screen and prints "HELLO, WORLD!" centered at (row 13, col 13)
via py65's 6502 MPU core. Unlike examples/6502-vic20/test.py, this needs no ROM-call patch
first: oric_cls()/oric_puts() (src/rt/oric/oric.c) write straight into the Oric's own
memory-mapped text screen RAM ($BB80-$BFDF), never through a JSR into ROM.

Usage: test.py MAINRAW
"""
import sys

from py65.devices.mpu6502 import MPU

ORIC_VIDEO_BASE = 0xBB80
ORIC_COLS = 40
ORIC_ROWS = 28

with open(sys.argv[1], "rb") as f:
    data = f.read()

base = 0x0600
mpu = MPU()
mpu.memory[base:base + len(data)] = data
mpu.pc = base

for _ in range(2_000_000):
    pc_before = mpu.pc
    mpu.step()
    if mpu.pc == pc_before:  # "halt: jmp halt"
        break
else:
    raise SystemExit("simulator never reached the halt loop (step limit exceeded)")

screen = bytes(mpu.memory[ORIC_VIDEO_BASE + i] for i in range(ORIC_COLS * ORIC_ROWS))

row, col = 13, 13
msg = b"HELLO, WORLD!"
start = row * ORIC_COLS + col
assert screen[start:start + len(msg)] == msg, screen[start:start + len(msg)]

# Every other cell must still be the blank oric_cls() left behind - confirms the message
# landed in exactly one place, not smeared across the screen by a cursor/offset bug.
rest = screen[:start] + screen[start + len(msg):]
assert rest == b" " * len(rest), rest

print("PASS")
