"""Verifies main.raw writes "HELLO, WORLD!\r\n" out through the IMSAI's SIO-2 serial board, run
on a real, independent Z80 core (z80-python) standing in for the 8080 - valid since the Z80
is object-code compatible with the 8080 (see src/codegen/i8080/I8080Assembler.h's own header
comment, and tests/run_tests.sh's own check_8080() for the same substitution).

This virtual SIO-2 always reports TXRDY (transmit ready) set and RXRDY (receive data available)
clear - imsai.c's imsai_putc() busy-waits on the first before every write, so the loop never
blocks; nothing here ever calls imsai_getc(), so RXRDY is never checked. Every byte main.raw
writes to the data port ($02) is captured in order and compared against the expected string -
no ROM/BIOS to fake, since imsai.c only ever reaches the SIO-2 through plain IN/OUT.

z80-python models real 8080/Z80 hardware's own `IN A,(n)`/`OUT (n),A` bus behavior: the port it
hands `read_port`/`write_port` has the immediate `n` in its low byte and whatever's in the
accumulator in its high byte (the real chip puts A on the address bus's own upper half for
exactly these two instructions), not just the bare 8-bit port number - so both callbacks below
mask with `& 0xFF` before comparing against $02/$03.

Usage: test.py MAINRAW
"""
import sys

from z80_python import Z80CPU

with open(sys.argv[1], "rb") as f:
    data = f.read()

base = 0x0100
memory = bytearray(0x10000)
memory[base:base + len(data)] = data

output = bytearray()


def read_port(port):
    if port & 0xFF == 0x03:  # IMSAI_SIO_STATUS: TXRDY set, RXRDY clear
        return 0x01
    return 0x00


def write_port(port, value):
    if port & 0xFF == 0x02:  # IMSAI_SIO_DATA
        output.append(value)


cpu = Z80CPU(memory.__getitem__, memory.__setitem__, read_port=read_port, write_port=write_port)
cpu.pc = base
for _ in range(2_000_000):
    pc_before = cpu.pc
    cpu.step()
    if cpu.pc == pc_before:  # "halt: jmp halt"
        break
else:
    raise SystemExit("simulator never reached the halt loop (step limit exceeded)")

assert bytes(output) == b"HELLO, WORLD!\r\n", output
print("PASS")
