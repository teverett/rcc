#ifndef RCC_RT_ORIC_H
#define RCC_RT_ORIC_H

// Tangerine Oric-1 / Oric Atmos runtime for rcc's 6502 backend (--target=6502).
//
// Both models share the same 6502A CPU, the same memory map, and the same text screen - the
// Atmos is a 1985 revision of the 1983 Oric-1 (better keyboard, a fixed/renumbered ROM), not a
// different machine - so this is one runtime library for one platform, not two.
//
// Every function here reaches real hardware through a fixed, memory-mapped address - video RAM
// for output - rather than a ROM call: the Oric-1's ROM (version 1.0) and the Atmos's ROM
// (version 1.1) relocate most of their own routines between versions, so there's no single
// stable call address this library could target across both models the way src/rt/vic20/'s
// KERNAL jump table is stable across the whole Commodore 8-bit line - see that file's own header
// comment for the contrast. This matches src/rt/coco/'s and src/rt/dragon32/'s own reasoning for
// the same kind of machine (a 6502/6809 home computer with memory-mapped text video and no
// single stable ROM entry point worth depending on).
//
// Text screen RAM is a fixed 40-column by 28-row grid at $BB80-$BFDF (1120 bytes, one byte per
// cell), present identically on both models regardless of ROM version - this is RAM, not a ROM
// call, so there's no version-dependent address to get wrong. Character codes 32-126 display as
// plain ASCII (unlike the Commodore PETSCII order src/rt/vic20/'s own header comment describes,
// or the CoCo/Dragon 32's own 6-bit-masked character set - see coco.h's own header comment); this
// runtime only ever stores that printable ASCII range, and doesn't use any of the Oric's control
// character codes (0-7/16-23 set the ink/paper color of the rest of the line when stored in
// screen RAM; this runtime leaves color alone).
//
// No ROM-call based keyboard, sound, tape, or disk support - see docs/known-bugs.md for why
// (the exact ROM-version-dependent addresses those need aren't independently verified sources
// this project has access to), matching coco.h's/dragon32.h's own output-only scope.

#define ORIC_VIDEO_BASE 0xBB80u
#define ORIC_COLS 40
#define ORIC_ROWS 28

// Fills the whole text screen with spaces and homes the cursor (row 0, column 0) that
// oric_putchar()/oric_print()/oric_newline() track.
void oric_cls();
// Writes one character at (row, col), both 0-based; no bounds checking. Doesn't touch or move
// the oric_putchar() cursor.
void oric_putc(int row, int col, unsigned char c);
// Writes a NUL-terminated string left to right starting at (row, col), via oric_putc(); does not
// wrap or clip at the screen's own edges, and doesn't touch or move the oric_putchar() cursor.
void oric_puts(int row, int col, char *s);

// Moves oric_putchar()'s own cursor to (row, col), both 0-based; no bounds checking.
void oric_gotoxy(int row, int col);
// Scrolls the whole screen up by one row (row 0's own contents are lost), via oric_putc(), and
// fills the newly-exposed bottom row with spaces. Doesn't move the cursor.
void oric_scroll();
// Moves the cursor to column 0 of the next row, scrolling via oric_scroll() first if the cursor
// was already on the bottom row.
void oric_newline();
// Writes one character at the cursor and advances it one column, via oric_putc(); a '\n' instead
// calls oric_newline() and writes nothing. Wraps to the next row (scrolling if needed, via
// oric_newline()) once the cursor would otherwise run past the last column.
void oric_putchar(unsigned char c);
// Writes a NUL-terminated string via oric_putchar(), one character at a time.
void oric_print(char *s);

#endif
