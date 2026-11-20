#ifndef RCC_RT_MSX_H
#define RCC_RT_MSX_H

// MSX runtime for rcc's Z80 backend (--target=z80-msx, --format=msxrom - see
// MsxLinkerFactory.cpp's own header comment for the cartridge header that target links in).
//
// Unlike src/rt/coleco/ (every hardware access is a raw IN/OUT, no ROM involved at all) and like
// src/rt/apple2/ (ROM subroutine calls), every primitive here reaches hardware through a
// documented MSX BIOS entry point - a cartridge's own INIT routine runs with the BIOS still
// mapped and "live" (see MsxLinkerFactory.cpp's own header comment on why the stack is seeded
// away from the BIOS work area instead of claiming all of page 3), so a cartridge program can
// just `call` a fixed BIOS address the same way it could from BASIC's own USR() hook. Every
// address/register convention below is cross-checked against the Konamiman MSX2 Technical
// Handbook's own BIOS listing (Appendix 1) - the one source consulted that states both the exact
// entry address and the exact input/output register for each routine, not just the address alone
// (several other sources agree on addresses but phrase register conventions inconsistently).
//
// One exception: GTSTCK's own *output* encoding (how A's 0-8 maps to "centered"/8 directions) is
// not stated by that source - only that "A = the joystick direction" comes back, with no value
// table. `msx_joystick()` below returns that raw, undecoded byte rather than guess at the
// mapping; see its own comment.
//
// A C expression can't name a specific 8-bit register (B/C/E) directly, so each primitive here
// stages its own operands through globals (`rt_a`/`rt_b`/`rt_c`/`rt_e`/`rt_hl`) immediately
// before the `asm { call ... }` block - the same reason src/rt/coleco/coleco.c stages through its
// own `rt_a` before a port `IN`/`OUT`. B/C/E have no direct `LD r,(nn)` form on real Z80 (only A
// and the register pairs do), so loading one from a global goes through HL and the assembler's
// `m` pseudo-register name for `(HL)` (`ld hl, g_x` for the global's address, then `ld r, m`) -
// see Z80Assembler.cpp's own `reg8Code()` comment for `m`.

// --- Console I/O ---------------------------------------------------------

// Displays one character (CHPUT, $00A2).
void msx_putc(char c);
// Writes a NUL-terminated string, one msx_putc() call per character - no newline translation,
// scrolling, or cursor positioning beyond what CHPUT itself does.
void msx_puts(char *s);
// Blocking single-character read (CHGET, $009F); returns the ASCII code of the key pressed.
char msx_getc();

// --- Joystick --------------------------------------------------------------

// Reads joystick/cursor-key direction (GTSTCK, $00D5). `stick` is 0 for the cursor keys, 1 or 2
// for the two joystick ports. Returns the BIOS's own raw direction byte, undecoded - the
// Technical Handbook's own BIOS listing doesn't state the value table (see this file's own
// header comment), so this doesn't guess at what each value means.
unsigned char msx_joystick(unsigned char stick);

// --- VDP (TMS9918A) -------------------------------------------------------

// Writes `value` to VDP register `reg` (WRTVDP, $0047).
void msx_vdp_write_reg(unsigned char reg, unsigned char value);
// Writes one byte directly to VRAM address `addr` (WRTVRM, $004D) - unlike
// msx_vdp_write_reg()/msx_psg_write(), this isn't a register write; it's a one-shot VRAM poke,
// the VDP's own auto-incrementing address pointer (the same one BASIC's VPOKE/VPEEK use) is
// never touched by these two, so repeated calls each re-seek.
void msx_vram_write(unsigned int addr, unsigned char value);
// Reads one byte directly from VRAM address `addr` (RDVRM, $004A). Only the TMS9918A's own
// 14-bit address range (0-16383) is valid - the Handbook notes this routine doesn't reach the
// fuller VRAM range a V9938/9958 (MSX2+) can have; this project offers no NRDVRM equivalent.
unsigned char msx_vram_read(unsigned int addr);
// (Re-)establishes TEXT1 mode, 40x24 characters (INITXT, $006C). Needs no input register - the
// BIOS reads its own TXTNAM/TXTCGP/LINL40 work-area variables instead, left at their ROM
// defaults by every real MSX's own boot sequence.
void msx_init_text_mode();

// --- PSG (AY-3-8910) -------------------------------------------------------

// Writes `value` to PSG register `reg` (WRTPSG, $0093).
void msx_psg_write(unsigned char reg, unsigned char value);

#endif
