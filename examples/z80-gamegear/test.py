"""Verifies main.raw writes bytes 1,2,3,4 into VRAM $0000-$0003 and one byte (159) to the
SN76489's own port ($7F), run on a real Z80 core (z80-python) - see ../z80-sms/test.py's own
header comment; the Game Gear's VDP/sound ports are identical to the SMS's own.

Usage: test.py MAINRAW
"""
import sys

from z80_python import Z80CPU

with open(sys.argv[1], "rb") as f:
    data = f.read()

base = 0
memory = bytearray(0x10000)
memory[base:base + len(data)] = data

vram = bytearray(0x4000)
state = {"addr": 0, "latch": None}
sound_writes = []


def read_port(port):
    port &= 0xFF
    if port == 0xBE:
        val = vram[state["addr"] & 0x3FFF]
        state["addr"] = (state["addr"] + 1) & 0x3FFF
        return val
    return 0xFF


def write_port(port, value):
    port &= 0xFF
    if port == 0xBE:
        vram[state["addr"] & 0x3FFF] = value
        state["addr"] = (state["addr"] + 1) & 0x3FFF
    elif port == 0xBF:
        if state["latch"] is None:
            state["latch"] = value
        else:
            lo, hi = state["latch"], value
            state["latch"] = None
            if not (hi & 0x80):
                state["addr"] = (lo | ((hi & 0x3F) << 8)) & 0x3FFF
    elif port == 0x7F:
        sound_writes.append(value)


cpu = Z80CPU(memory.__getitem__, memory.__setitem__, read_port=read_port, write_port=write_port)
cpu.pc = base
for _ in range(2_000_000):
    pc_before = cpu.pc
    cpu.step()
    if cpu.pc == pc_before:  # "halt: jp halt"
        break
else:
    raise SystemExit("simulator never reached the halt loop (step limit exceeded)")

assert vram[0:4] == bytes([1, 2, 3, 4]), vram[0:4]
assert sound_writes == [159], sound_writes
print("PASS")
