#ifndef RCC_RT_IMSAI_H
#define RCC_RT_IMSAI_H

// IMSAI 8080 runtime for rcc's 8080 backend (--target=8080), reaching the IMSAI SIO-2 serial
// board through the 8080's own IN/OUT instructions (see I8080Assembler.h's own header comment
// for why those needed adding to this backend's in-process assembler at all).
//
// The IMSAI 8080 is an S-100 machine like the Altair 8800 (see src/rt/altair/altair.h's own
// header comment), with the same "no single fixed address for most boards" caveat - but its own
// SIO-2 board (a pair of Intel 8251 USARTs, not the Altair 2SIO's 6850 ACIA) is standard enough
// across real IMSAI setups, at its default jumper address, to give this runtime one fixed
// convention: port A's data register at $02 and its status register at $03, the same base
// Lifeboat CP/M's own BIOS and the IMSAI SIO-2 emulators (IMSAI8080esp, z80pack's imsaisim)
// assume. This is a different status-bit layout from the Altair's 2SIO, since the 8251 numbers
// its ready bits the other way round: bit 0 is transmit-ready, bit 1 is receive-ready.
//
// A C expression can't name a port directly, so every primitive here stages its one byte
// through a global (`rt_i`) immediately before/after an `asm { in ... }`/`asm { out ... }`
// block, the same reason src/rt/altair/altair.c's own header comment gives for the Altair's
// 2SIO.

#define IMSAI_SIO_DATA 0x02u
#define IMSAI_SIO_STATUS 0x03u
#define IMSAI_SIO_TXRDY 0x01u  // status bit 0: the data port is ready for the next byte out
#define IMSAI_SIO_RXRDY 0x02u  // status bit 1: a byte is waiting at the data port

// --- Low-level port primitives --------------------------------------

// Reads the SIO-2 status port ($03) and returns it unchanged - bit 0 (IMSAI_SIO_TXRDY)/bit 1
// (IMSAI_SIO_RXRDY) are the only two bits this runtime ever checks.
unsigned char imsai_sio_status();
// Writes one byte straight to the SIO-2 data port ($02), with no wait - the caller is
// responsible for checking IMSAI_SIO_TXRDY first (see imsai_putc()).
void imsai_sio_write_data(unsigned char value);
// Reads one byte straight from the SIO-2 data port ($02), with no wait - the caller is
// responsible for checking IMSAI_SIO_RXRDY first (see imsai_getc()).
unsigned char imsai_sio_read_data();

// --- Console I/O --------------------------------------------

// Busy-waits on IMSAI_SIO_TXRDY, then writes `c` to the SIO-2 data port.
void imsai_putc(unsigned char c);
// Writes a NUL-terminated string via imsai_putc(), one character at a time; no newline
// translation - include "\r\n" in `s` for a real terminal to start the next line at column 0.
void imsai_puts(char *s);
// Busy-waits on IMSAI_SIO_RXRDY, then returns the byte read from the SIO-2 data port.
unsigned char imsai_getc();

#endif
