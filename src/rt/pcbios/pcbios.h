#ifndef RCC_RT_PCBIOS_H
#define RCC_RT_PCBIOS_H

// PC BIOS runtime library for rcc's `--target=8086-dos` backend (IBM 5160/PC-XT and
// compatibles): video via `INT 10h`, keyboard via `INT 16h`, and raw disk sectors via
// `INT 13h` (Ralf Brown's Interrupt List has every function number used below), called
// directly rather than through MS-DOS - `src/rt/msdos/` is the `INT 21h` counterpart, and
// the two can be linked together (they share no globals or labels).
//
// This target's backend is used instead of plain `8086`/`8088` purely for its loader
// convention: DOS gets the CPU into real mode with the BIOS resident before a `.com`
// program's entry point ever runs. rcc itself never builds a boot sector (out of scope -
// see `docs/known-bugs.md`'s `8086`/`8088` entry), so a BIOS-calling program still needs
// DOS (or an emulator that stands in for it, such as DOSBox-X) to get loaded and started at
// all; it just never calls back into DOS (`INT 21h`) once running.
//
// Same staging-global and byte-extraction conventions as `src/rt/msdos/msdos.h` (see its
// own header comment): no `ah`/`al`/`dh`/`dl`-style sub-register names exist in this
// dialect, so a value that has to land in one specific byte of a register pair is either
// combined as a single 16-bit immediate at the C level (`pcbios_rt_w = 3584 + c` for
// `ah=0x0E,al=c`) or extracted with `pcbios_rt_w / 256` (high byte) / plain truncating
// `return` (low byte); every value crossing from C into an `asm { }` block goes through
// one of the `pcbios_rt_w*` globals, read back with `[g_name]`, never a local or
// parameter. These are named with a `pcbios_` prefix - unlike `msdos.c`'s own `rt_w`/
// `rt_w2`/`rt_w3` - so a program linking both libraries together never gets two
// conflicting definitions of the same global. None of these wrappers are reentrant.
//
// `pcbios_disk_read()`/`pcbios_disk_write()` point `INT 13h` at `pcbios_rt_disk_buf` via a
// bare `mov bx, g_pcbios_rt_disk_buf` with no segment override - this dialect has no
// `es:[bx]` syntax at all (same gap `msdos.h` notes for DOS's own environment-block
// access), so this only works because a `.com` program's `ES` still equals `DS` exactly as
// DOS set it at load, and nothing in this library ever changes `ES`. Both functions also
// assume the disk's cylinder count fits in one byte (fine for any floppy geometry; a hard
// disk with 256 or more cylinders needs the 2 extra bits BIOS packs into the top of `CL`,
// which isn't extracted here).
//
// Include this file directly (`#include "pcbios.c"`) for a single-file `--format=raw`
// build, or compile pcbios.c on its own and `--link` it (`--format=coff`/`elf`).

// Sets the video mode via `INT 10h`/`00h` (3 = 80x25 16-color text, the BIOS default).
void pcbios_set_mode(unsigned char mode);
// Writes one character via `INT 10h`/`0Eh` (teletype output: advances the cursor, scrolls
// at the bottom of the screen, and `\r`/`\n`/`\b`/BEL behave as a real terminal's).
void pcbios_putchar(unsigned char c);
// Writes a NUL-terminated string one character at a time via pcbios_putchar().
void pcbios_puts(char *s);
// Moves the cursor via `INT 10h`/`02h` (page 0). `row` is 0-24, `col` is 0-79 in the
// default 80x25 text mode.
void pcbios_set_cursor(unsigned char row, unsigned char col);
// The cursor's current row/column via `INT 10h`/`03h` (page 0).
unsigned char pcbios_cursor_row();
unsigned char pcbios_cursor_col();
// Writes `c` with attribute byte `attr` at the cursor `count` times via `INT 10h`/`09h`
// (page 0) - unlike pcbios_putchar(), this does not move the cursor or interpret `c` as a
// control character. `attr`'s low nibble is the foreground color, the high nibble the
// background (bit 7 blinks instead of a bright background, on most real CGA/EGA/VGA cards).
void pcbios_write_attr(unsigned char c, unsigned char attr, unsigned char count);
// Clears the whole 80x25 screen and homes the cursor via `INT 10h`/`06h` (scroll the entire
// window up by its own height), filling with attribute byte 7 (white on black).
void pcbios_clear_screen();
// The character/attribute byte already on screen at the cursor, via `INT 10h`/`08h` (page
// 0) - reads back what pcbios_putchar()/pcbios_write_attr() actually left there, without
// disturbing it.
unsigned char pcbios_peek_char();
unsigned char pcbios_peek_attr();

// Reads one key via `INT 16h`/`00h`, blocking until one is available. Returns the ASCII
// character; the scan code `INT 16h` also returns is discarded.
unsigned char pcbios_getchar();
// True (nonzero) if a key is waiting, via `INT 16h`/`01h` - doesn't block and doesn't
// consume the key.
unsigned char pcbios_haskey();

// Resets drive `drive`'s disk controller via `INT 13h`/`00h` (0 = first floppy, 0x80 = first
// hard disk) - needed once before the first read/write, and again after either one fails.
void pcbios_disk_reset(unsigned char drive);
// Reads one 512-byte sector (cylinder `cyl`, head `head`, sector `sector` - sectors are
// numbered from 1, not 0) from `drive` into `rt_disk_buf` via `INT 13h`/`02h`. Returns 0 on
// success, or `INT 13h`'s own nonzero status code on failure (see Ralf Brown's Interrupt
// List for what each code means) - including status 6 ("disk changed"), which a real
// floppy controller (and at least one BIOS emulator this was verified against) can return
// on the very next operation after a write or a media insert; the documented recovery is a
// pcbios_disk_reset() followed by retrying the read once, not treating it as a hard error.
unsigned char pcbios_disk_read(unsigned char drive, unsigned char cyl, unsigned char head, unsigned char sector);
// Writes `rt_disk_buf` as one 512-byte sector to `drive` via `INT 13h`/`03h`. Same
// parameters and return convention as pcbios_disk_read().
unsigned char pcbios_disk_write(unsigned char drive, unsigned char cyl, unsigned char head, unsigned char sector);
// Reads/writes byte `i` (0-511) of the shared sector buffer pcbios_disk_read()/
// pcbios_disk_write() transfer through - accessors rather than a plain exported array, for
// the same reason msdos.h's dos_buf_get()/dos_buf_set() aren't either.
int pcbios_buf_get(int i);
void pcbios_buf_set(int i, unsigned char value);

// Terminates the program via `INT 21h`/`4Ch` (the one DOS call this library makes - a
// `.com` process still needs it to hand control back to DOS, same as `src/rt/msdos/`'s own
// dos_exit()), with `code` as the real DOS exit code. Never returns.
void pcbios_exit(unsigned char code);

#endif
