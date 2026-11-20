"""Verifies main.raw prints "HELLO, WORLD!" via py65's 6502 MPU core. It
makes real OSWRCH/OSNEWL calls (asm { jsr ... } in src/rt/bbc/bbc.c's
bbc_oswrch()/bbc_newline()) that would otherwise JSR into empty memory.
OSWRCH ($FFEE) and OSNEWL ($FFE7) are patched with tiny hand-written
6502 stubs before running. Both real vectors sit only 7 bytes apart, too
close together for either stub's own full logic to fit without spilling
into the other, so each vector instead holds just a 3-byte `JMP` into
free zero-page space ($0040/$0060) where the real work happens: a shared
helper at $0040 appends the accumulator verbatim into a buffer at
$0400, tracked via a zero-page position counter at $F0 (ldx $F0; sta
$0400,x; inx; stx $F0; rts) - the same stub shape examples/6502-c64/
test.py's own CHROUT patch uses, just relocated out from under OSWRCH's
own vector - and a second helper at $0060 calls it twice (with 10 and
13, OSNEWL's own LF-then-CR convention) for OSNEWL.

Usage: test.py MAINRAW
"""
import sys

from py65.devices.mpu6502 import MPU

with open(sys.argv[1], "rb") as f:
    data = f.read()

base = 0x1900
mpu = MPU()
mpu.memory[base:base + len(data)] = data

APPEND_BYTE = 0x0040
WRITE_NEWLINE = 0x0060
mpu.memory[APPEND_BYTE:APPEND_BYTE + 9] = [0xA6, 0xF0, 0x9D, 0x00, 0x04, 0xE8, 0x86, 0xF0, 0x60]
mpu.memory[WRITE_NEWLINE:WRITE_NEWLINE + 10] = [
    0xA9, 0x0A, 0x20, APPEND_BYTE & 0xFF, APPEND_BYTE >> 8,  # lda #10 : jsr APPEND_BYTE
    0xA9, 0x0D, 0x4C, APPEND_BYTE & 0xFF, APPEND_BYTE >> 8,  # lda #13 : jmp APPEND_BYTE
]
mpu.memory[0xFFEE:0xFFEE + 3] = [0x4C, APPEND_BYTE & 0xFF, APPEND_BYTE >> 8]    # OSWRCH: jmp APPEND_BYTE
mpu.memory[0xFFE7:0xFFE7 + 3] = [0x4C, WRITE_NEWLINE & 0xFF, WRITE_NEWLINE >> 8]  # OSNEWL: jmp WRITE_NEWLINE
mpu.memory[0xF0] = 0

mpu.pc = base
for _ in range(2_000_000):
    pc_before = mpu.pc
    mpu.step()
    if mpu.pc == pc_before:
        break
else:
    raise SystemExit("simulator never reached the halt loop (step limit exceeded)")

n = mpu.memory[0xF0]
out = bytes(mpu.memory[0x0400 + i] for i in range(n))
# bbc_cls() sends VDU 12 (clear screen) before the text, and bbc_newline()
# sends LF then CR (OSNEWL's own convention, not a single CR/LF) after it.
assert out == bytes([12]) + b"HELLO, WORLD!" + bytes([10, 13]), out
print("PASS")
