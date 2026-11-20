"""Verifies api_test.raw's own behavior (see that file's header comment) by reading the Oric's
memory-mapped text screen RAM back out of py65's 6502 MPU core after it runs: oric_putc()/
oric_puts()/oric_gotoxy()/oric_putchar()/oric_print() placed their output where expected, and
oric_newline()'s scroll-at-the-bottom-row case moved everything up by one line rather than
running the cursor off the screen.

Usage: api_test.py APITESTRAW
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


def row(n):
    return screen[n * ORIC_COLS:(n + 1) * ORIC_COLS]


# Row 0 originally held oric_putc(0, 0, 'A'); the bottom-row newline() scrolled it off entirely.
assert row(0)[:7] == b"ROW ONE", row(0)
# Row 1 originally held oric_gotoxy(2, 0); oric_putchar('X'); oric_print("REST OF LINE") - now
# shifted up by the same scroll.
assert row(1)[:1] == b"X", row(1)
assert row(1)[1:13] == b"REST OF LINE", row(1)
# Second-to-last row: oric_putchar('Z') written just before the scroll-triggering newline(), so
# it also shifted up by one.
assert row(ORIC_ROWS - 2)[:1] == b"Z", row(ORIC_ROWS - 2)
# Last row: the scroll left it blank, then oric_putchar('S') wrote into the freshly-scrolled
# cursor position (column 0, since newline() resets the column even when it scrolls instead of
# advancing the row).
assert row(ORIC_ROWS - 1)[:1] == b"S", row(ORIC_ROWS - 1)
assert row(ORIC_ROWS - 1)[1:] == b" " * (ORIC_COLS - 1), row(ORIC_ROWS - 1)

print("PASS")
