#ifndef RCC_RT_DRAGON32_H
#define RCC_RT_DRAGON32_H

// Dragon 32 (Dragon Data Ltd) runtime for rcc's 6809 backend (--target=6809).
//
// The Dragon 32 and the TRS-80 Color Computer (CoCo) share the same CPU (6809E), the same
// Motorola 6883 SAM memory-management IC, and the same 6847 VDG video chip, wired up the same
// way: PIA-0 at $FF00-$FF03, PIA-1 at $FF20-$FF23, and - on power-up, before any program changes
// SAM's video mode register - the same 32-column by 16-row "internal alphanumeric" text screen
// at $0400-$05FF, with no row interleaving. This runtime therefore reaches the screen exactly
// the way src/rt/coco/coco.h's own header comment reaches it: a fixed, memory-mapped address,
// not a ROM call (this project has no byte-identical Dragon ROM dump to call into, and the 6809
// backend already supports plain integer-literal-as-pointer addressing for cases like this).
//
// UNLIKE the TRS-80 (whose video RAM matches its character generator's own codes directly to
// ASCII), the 6847 VDG's internal character set is NOT ASCII: a printable uppercase letter,
// digit, punctuation mark, or space (ASCII 32-95, i.e. $20-$5F) is shown correctly only after
// masking it down to 6 bits (`c & 63`); storing a raw ASCII byte above 95, or any lowercase
// letter (ASCII 97-122), displays the wrong glyph. This runtime deliberately only supports that
// 32-95 range - every dragon32_putc() store masks with `& 63` - and has no lowercase or
// extended-character support.
//
// No keyboard support yet, matching src/rt/coco/coco.h's and src/rt/trs80/trs80.h's own scope
// cut (output only) - the Dragon's keyboard matrix is wired differently from the CoCo's, and
// reading it isn't needed for this runtime's console-output scope.

#define DRAGON32_VIDEO_BASE 1024u
#define DRAGON32_COLS 32
#define DRAGON32_ROWS 16

// Fills the whole screen with spaces and homes the cursor (row 0, column 0) that
// dragon32_putchar()/dragon32_print()/dragon32_newline() track.
void dragon32_cls();
// Writes one character at (row, col), both 0-based; no bounds checking. Doesn't touch or move
// the dragon32_putchar() cursor. Masks `c` with `& 63` per this file's own header comment.
void dragon32_putc(int row, int col, unsigned char c);
// Writes a NUL-terminated string left to right starting at (row, col), via dragon32_putc();
// does not wrap or clip at the screen's own edges, and doesn't touch or move the
// dragon32_putchar() cursor.
void dragon32_puts(int row, int col, char *s);

// Moves dragon32_putchar()'s own cursor to (row, col), both 0-based; no bounds checking.
void dragon32_gotoxy(int row, int col);
// Scrolls the whole screen up by one row (row 0's own contents are lost), via dragon32_putc(),
// and fills the newly-exposed bottom row with spaces. Doesn't move the cursor.
void dragon32_scroll();
// Moves the cursor to column 0 of the next row, scrolling via dragon32_scroll() first if the
// cursor was already on the bottom row.
void dragon32_newline();
// Writes one character at the cursor and advances it one column, via dragon32_putc(); a '\n'
// instead calls dragon32_newline() and writes nothing. Wraps to the next row (scrolling if
// needed, via dragon32_newline()) once the cursor would otherwise run past the last column.
void dragon32_putchar(unsigned char c);
// Writes a NUL-terminated string via dragon32_putchar(), one character at a time.
void dragon32_print(char *s);

#endif
