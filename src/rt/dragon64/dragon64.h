#ifndef RCC_RT_DRAGON64_H
#define RCC_RT_DRAGON64_H

// Dragon 64 (Dragon Data Ltd) runtime for rcc's 6809 backend (--target=6809).
//
// The Dragon 64 is a Dragon 32 with 64KB of RAM instead of 32KB, a second (banked) 32KB ROM,
// and an onboard 6551 ACIA serial port that the Dragon 32 lacks; none of that changes how this
// runtime reaches the screen. Text-mode video RAM, the 6847 VDG, and the 6883 SAM/PIA layout
// ($FF00-$FF03, $FF20-$FF23) are identical to the Dragon 32 and the CoCo - see
// src/rt/dragon32/dragon32.h's own header comment for the full reasoning (fixed memory-mapped
// address rather than a ROM call, 6-bit-masked VDG character codes, no keyboard support yet).
// This runtime is therefore the same code as src/rt/dragon32/'s, under the dragon64_ prefix, so
// a Dragon 64 program links against a runtime named for its own machine.

#define DRAGON64_VIDEO_BASE 1024u
#define DRAGON64_COLS 32
#define DRAGON64_ROWS 16

// Fills the whole screen with spaces and homes the cursor (row 0, column 0) that
// dragon64_putchar()/dragon64_print()/dragon64_newline() track.
void dragon64_cls();
// Writes one character at (row, col), both 0-based; no bounds checking. Doesn't touch or move
// the dragon64_putchar() cursor. Masks `c` with `& 63` per this file's own header comment.
void dragon64_putc(int row, int col, unsigned char c);
// Writes a NUL-terminated string left to right starting at (row, col), via dragon64_putc();
// does not wrap or clip at the screen's own edges, and doesn't touch or move the
// dragon64_putchar() cursor.
void dragon64_puts(int row, int col, char *s);

// Moves dragon64_putchar()'s own cursor to (row, col), both 0-based; no bounds checking.
void dragon64_gotoxy(int row, int col);
// Scrolls the whole screen up by one row (row 0's own contents are lost), via dragon64_putc(),
// and fills the newly-exposed bottom row with spaces. Doesn't move the cursor.
void dragon64_scroll();
// Moves the cursor to column 0 of the next row, scrolling via dragon64_scroll() first if the
// cursor was already on the bottom row.
void dragon64_newline();
// Writes one character at the cursor and advances it one column, via dragon64_putc(); a '\n'
// instead calls dragon64_newline() and writes nothing. Wraps to the next row (scrolling if
// needed, via dragon64_newline()) once the cursor would otherwise run past the last column.
void dragon64_putchar(unsigned char c);
// Writes a NUL-terminated string via dragon64_putchar(), one character at a time.
void dragon64_print(char *s);

#endif
