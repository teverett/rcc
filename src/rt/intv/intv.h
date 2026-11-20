#ifndef RCC_RT_INTV_H
#define RCC_RT_INTV_H

// Mattel Intellivision runtime for rcc's CP1610 backend (--target=cp1610-intv).
//
// Every function here pokes the STIC (the console's video chip) or System RAM directly through a
// fixed, memory-mapped word address - the same "no ROM/BIOS call" reasoning src/rt/coco/coco.h's
// own header comment gives for the CoCo, and the only option here anyway: a real Intellivision
// needs its EXEC/GROM ROMs to run at all, and this project can't bundle or call into copyrighted
// ROM images it doesn't have (see docs/known-bugs.md).
//
// This is deliberately a small, PRIMARY-SOURCE-VERIFIED core, not full STIC/GROM/PSG coverage.
// Every address below is confirmed against the Intellivision community wiki
// (wiki.intellivision.us) and, for the CP1610 word byte order, against MAME's own cartridge
// loader (see IntvBinFormat.h) - see each function's own comment for exactly what's confirmed.
// Several real STIC facts this project could NOT verify from any primary source are deliberately
// left out rather than guessed - most importantly the exact bit layout of a BACKTAB word (which
// bits select GROM vs. GRAM, hold the card number, the foreground color, or the color-stack
// "Advance" bit) - see intv_backtab_poke()'s own comment and docs/known-bugs.md. Sprites (MOBs),
// FGBG/Colored-Squares display modes, and the PSG sound chip aren't covered at all for the same
// reason.

// The STIC's "Color Stack" - 4 registers, each holding one of the 16 STIC palette colors
// (0-15); `index` must be 0-3. Confirmed at $0028-$002B on the Intellivision community wiki's own
// STIC register table.
void intv_set_color_stack(unsigned int index, unsigned int color);

// The STIC's border color (0-15), confirmed at $002C on the same register table.
void intv_set_border_color(unsigned int color);

// Requests an active display for the next frame by writing to the STIC's "Active Display
// Strobe" at $0020 - confirmed on the wiki's own VBlank_Period_1 page ("the program did not
// write to location $0020 ... during this period" keeps the display off until it does). The
// value written doesn't matter; only the write itself matters.
void intv_enable_display();

// BACKTAB: the 20-column by 12-row background card table, confirmed at words $0200-$02EF (240
// words, row-major, address = $0200 + row*20 + col) on the wiki's own BACKTAB page.
#define INTV_BACKTAB_COLS 20
#define INTV_BACKTAB_ROWS 12

// Writes a raw 16-bit BACKTAB word at (row, col), both 0-based; no bounds checking. This is
// deliberately a raw poke, not a "print character" call: this project could not verify the real
// bit layout of a BACKTAB word (GROM/GRAM select, card number, foreground color, and color-stack
// "Advance" bits) from any primary source - the wiki's own diagrams for it are images, not text -
// so building the actual word is left to the caller, who should check a verified reference (the
// wiki's "Stic_color_stack_word.png" diagram, or the SDK-1600 manual) rather than trust a guess
// baked into this library. See docs/known-bugs.md.
void intv_backtab_poke(unsigned int row, unsigned int col, unsigned int value);

// Fills every BACKTAB cell with `value` via intv_backtab_poke().
void intv_backtab_fill(unsigned int value);

// The GROM font's own card number for an ASCII character 32 ("space") through 126 ("~") -
// confirmed on the wiki's own GROM page ("GROM card #0 through #94 correspond directly to ASCII
// character codes #32 through #126 ... subtract 32"). This is only the card-number half of a
// BACKTAB word - see intv_backtab_poke()'s own comment for why this library can't build the
// whole word for you. Undefined for any `c` outside 32-126.
#define INTV_GROM_CARD(c) ((c) - 32u)

#endif
