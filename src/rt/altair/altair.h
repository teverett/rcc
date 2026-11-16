#ifndef RCC_RT_ALTAIR_H
#define RCC_RT_ALTAIR_H

// MITS Altair 8800 runtime for rcc's 8080 backend (--target=8080), reaching the 88-2SIO serial
// board through the 8080's own IN/OUT instructions (see I8080Assembler.h's own header comment
// for why those needed adding to this backend's in-process assembler at all).
//
// The Altair's bus is S-100: beyond the 2SIO almost every board (disk controller, memory-mapped
// video, front-panel sense switches) is an optional add-in with no single fixed address across
// real machines, so this runtime stays to the one convention nearly every surviving Altair setup
// actually used for text I/O - the 88-2SIO at status port $10/data port $11, the same base
// address Altair BASIC's and CP/M's own BIOS assume.
//
// A C expression can't name a port directly, so every primitive here stages its one byte
// through a global (`rt_a`) immediately before/after an `asm { in ... }`/`asm { out ... }`
// block, the same reason src/rt/coleco/coleco.c's own header comment gives for the
// ColecoVision's port-mapped VDP/controllers.

#define ALTAIR_SIO_STATUS 0x10u
#define ALTAIR_SIO_DATA 0x11u
#define ALTAIR_SIO_RDA 0x01u   // status bit 0: a byte is waiting at the data port
#define ALTAIR_SIO_TBMT 0x02u  // status bit 1: the data port is ready for the next byte out

// --- Low-level port primitives --------------------------------------

// Reads the 2SIO status port ($10) and returns it unchanged - bit 0 (ALTAIR_SIO_RDA)/bit 1
// (ALTAIR_SIO_TBMT) are the only two bits this runtime ever checks.
unsigned char altair_sio_status();
// Writes one byte straight to the 2SIO data port ($11), with no wait - the caller is
// responsible for checking ALTAIR_SIO_TBMT first (see altair_putc()).
void altair_sio_write_data(unsigned char value);
// Reads one byte straight from the 2SIO data port ($11), with no wait - the caller is
// responsible for checking ALTAIR_SIO_RDA first (see altair_getc()).
unsigned char altair_sio_read_data();

// --- Console I/O --------------------------------------------

// Busy-waits on ALTAIR_SIO_TBMT, then writes `c` to the 2SIO data port.
void altair_putc(unsigned char c);
// Writes a NUL-terminated string via altair_putc(), one character at a time; no newline
// translation - include "\r\n" in `s` for a real terminal to start the next line at column 0.
void altair_puts(char *s);
// Busy-waits on ALTAIR_SIO_RDA, then returns the byte read from the 2SIO data port.
unsigned char altair_getc();

#endif
