#ifndef RCC_RT_APPLE2_H
#define RCC_RT_APPLE2_H

// Apple ][ runtime library for rcc's 6502 backend (--target=6502).
//
// Calls into the Apple ][ monitor ROM and the ProDOS MLI. Both use a
// register-based calling convention (arguments/results in the 6502's A
// register, or - for the MLI - inline bytes immediately after the call
// instruction) that has nothing to do with rcc's own C calling convention,
// and rcc's in-process 6502 assembler only ever encodes the exact
// mnemonic/operand shapes M6502Codegen.cpp itself emits (no LDX/LDY/STX/STY/
// TAX/TXA/TAY/TYA at all - see M6502Assembler.cpp's own header comment), so
// every wrapper here only ever uses the accumulator. Each one stages its
// argument/result through a global (`rt_a`) immediately before/after an
// `asm { ... }` block, since a C local/parameter has no fixed address an
// asm block's literal text could name directly - only a global does (see
// M6502Codegen.h's own `label()`).
//
// Include this file directly (`#include "apple2.c"`) for a single-file
// `--format=raw` build, or compile apple2.c on its own and `--link` it
// against a separately-compiled program (`--format=coff`/`elf`/`aout`).

// Prints one character via the monitor ROM's COUT ($FDED).
void a2_cout(unsigned char c);
// Prints a NUL-terminated string one character at a time via a2_cout().
void a2_puts(char *s);
// Moves the cursor to the start of the next line, via CROUT ($FD8E).
void a2_crout();
// Waits for and returns one keystroke (high bit set), via RDKEY ($FD0C).
unsigned char a2_rdkey();
// Clears the text screen and homes the cursor, via HOME ($FC58).
void a2_home();
// Prints a byte as two hex digits, via PRBYTE ($FDDA).
void a2_prbyte(unsigned char b);
// Prints a nibble (low 4 bits of `n`) as one hex digit, via PRHEX ($FDE3).
void a2_prhex(unsigned char n);
// Rings the bell, via BELL ($FF3A).
void a2_bell();
// Busy-waits roughly `n` * the monitor's own WAIT ($FCA8) delay unit.
void a2_wait(unsigned char n);
// Prints one character directly to the screen, bypassing the $36/$37
// output hook, via COUT1 ($FDF0). Same high-bit convention as a2_cout().
void a2_cout1(unsigned char c);
// Waits for and returns one keystroke, with RDKEY's own escape-sequence
// handling, via RDCHAR ($FD35).
unsigned char a2_rdchar();
// Reads one key directly from the keyboard hardware, via KEYIN ($FD1B).
unsigned char a2_keyin();
// Moves the cursor to text row `row` (0-based), via VTAB ($FC22).
void a2_vtab(unsigned char row);
// Clears from the cursor to the end of the current line, via CLREOL ($FC9C).
void a2_clreol();
// Clears from the cursor to the end of the text page, via CLREOP ($FC42).
void a2_clreop();
// Sets the soft switches for 40-column text mode, via SETTXT ($FB39).
void a2_settxt();
// Sets the soft switches for lo-res graphics mode, via SETGR ($FB40).
void a2_setgr();
// Sets inverse video text, via SETINV ($FE80).
void a2_setinv();
// Sets normal video text, via SETNORM ($FE84).
void a2_setnorm();
// Resets the character-input hook ($38/$39) to the ROM keyboard routine,
// via SETKBD ($FE89).
void a2_setkbd();
// Resets the character-output hook ($36/$37) to the ROM screen routine,
// via SETVID ($FE93).
void a2_setvid();
// Clears the lo-res graphics screen to black, via CLRSCR ($F832).
void a2_grclrscr();
// Clears the top (non-text-window) part of the lo-res graphics screen to
// black, via CLRTOP ($F836).
void a2_clrtop();

// Reads one line of keyboard input via GETLN ($FD6A): shows the current
// PROMPT ($33) character, echoes keystrokes, and supports the usual
// line-editing keys. The monitor itself (not this wrapper, and not rcc -
// see docs/known-bugs.md's "'@' addr pinning isn't implemented for the
// 6502/65c02 backends' global arrays") always writes the finished line
// into its own fixed buffer at $200-$2FF, one byte per character with bit
// 7 set (the same normal-text convention as a2_cout()'s own input),
// terminated by a single $8D (a carriage return, not 0) rather than NUL.
void a2_getln();

// Copies the inclusive byte range [from, to] to a same-size range starting
// at dest, via MOVE ($FE2C). `from`/`to`/`dest` are plain untyped addresses
// (see docs/language.md's "a plain `int` used as an untyped address"), not
// typed pointers - MOVE stages them through the monitor's own zero-page
// cells ($3C-$3F, $42-$43) rather than any 6502 register.
void a2_move(int from, int to, int dest);

// Patches mli_trampoline's command byte and parameter-list address (an
// untyped address - see docs/language.md's "a plain `int` used as an
// untyped address") without calling it - the piece that's testable without
// a real/emulated Apple ][ ROM, since nothing here jumps anywhere.
void mli_patch(unsigned char cmd, int paramsAddr);
// Issues one ProDOS MLI call (JSR $BF00, cmd byte, parameter-list address -
// see ProDOS Technical Reference Manual ch. 5) and returns its error code
// (0 = no error, matching the accumulator ProDOS itself leaves on return).
int mli_call(unsigned char cmd, int paramsAddr);

// Lo-res (40x48, GR mode) graphics, via the Monitor ROM. Call a2_setgr()
// first to switch into graphics mode. Color is a nibble (0-15); a2_color()
// sets the one COLOR cell ($30) every one of PLOT/HLINE/VLINE reads.
//
// Plots one block at column `x` (0-39), row `y` (0-47), via PLOT ($F800):
// A = y, Y = x.
void a2_plot(unsigned char x, unsigned char y);
// Draws a horizontal line from column `x1` to `x2` (inclusive) on row `y`,
// via HLINE ($F819): A = y, Y = x1, zero page $2C (H2) = x2. $2C/$2D fall
// inside M6502Codegen.h's own reserved frame-storage scratch range
// ($00-$2F, specifically division's `wtmp2` bytes 4-5) - safe in practice
// only because that scratch is always fully consumed into its own result
// before any later statement (including a call) runs; see
// M6502Codegen.h's own header comment for the expression-evaluation model
// this relies on.
void a2_hline(unsigned char x1, unsigned char x2, unsigned char y);
// Draws a vertical line from row `y1` to `y2` (inclusive) in column `x`,
// via VLINE ($F828): A = y1, Y = x, zero page $2D (V2) = y2. See
// a2_hline()'s own comment above about $2C/$2D and compiler scratch.
void a2_vline(unsigned char x, unsigned char y1, unsigned char y2);
// Sets the current lo-res color (low nibble of `c`, 0-15) via SETCOL
// ($F864): A = c. Stored into zero page $30 (COLOR), which PLOT/HLINE/
// VLINE each read.
void a2_color(unsigned char c);
// Reads back the lo-res color (0-15) already plotted at column `x`, row
// `y`, via SCRN ($F871): A = y, Y = x; result returned in A.
unsigned char a2_scrn(unsigned char x, unsigned char y);

// Hi-res (280x192, HGR mode) graphics, via the Monitor/Applesoft ROM.
// Register conventions below are taken directly from a disassembly of the
// Apple ][+ ROM (not just a secondary summary), since HPOSN/HGLIN split a
// 9-bit X-coordinate across two 6502 registers and getting the byte order
// wrong would silently plot at the wrong column.
//
// Switches to hi-res page 1 and clears it, via HGR ($F3E2). Takes no
// register input; sets zero page $E6 (HGR_PAGE) to $20 so later HPOSN
// calls target page 1.
void a2_hgr();
// Switches to hi-res page 2 and clears it, via HGR2 ($F3D8). Same as
// a2_hgr() but sets $E6 to $40 (page 2).
void a2_hgr2();
// Sets the hi-res color (0-7) that a2_hplot()/a2_hlineto() will use, by
// storing it directly into zero page $E4 (HGR_COLOR) - the cell HPOSN
// itself reads on every call to compute the even/odd-column bit pattern
// (see M6502Codegen.h's own frame layout for why this can't be a local).
void a2_hcolor(unsigned char c);
// Plots one dot at hi-res column `x` (0-279) and row `y` (0-191), via
// HPOSN+HPLOT ($F457): the accumulator takes `y`; `x`'s low byte goes in
// X, `x`'s high byte (0 or 1) goes in Y - confirmed from the ROM's own
// `stx HGR_X` / `sty HGR_X+1` right after HPOSN's entry.
void a2_hplot(int x, unsigned char y);
// Draws a line from the point last plotted by a2_hplot()/a2_hlineto() to
// hi-res column `x`, row `y`, via HGLIN ($F53A): the accumulator takes
// `x`'s low byte, X takes `x`'s high byte, Y takes `y` - confirmed from
// the ROM's own `sta HGR_X` / `stx HGR_X+1` / `sty HGR_Y` sequence.
void a2_hlineto(int x, unsigned char y);

// Reads game-port paddle `n` (0-3) via PREAD ($FB1E): X = n on entry;
// the result (0-255) comes back in Y, not A, so this needs the `sty`
// opcode M6502Assembler.cpp added for exactly this call.
unsigned char a2_pread(unsigned char n);

// Toggles the speaker once by reading the $C030 ($C030 = 49200) soft
// switch - any access to it clicks the speaker, regardless of the byte
// read back.
void a2_click();
// Clicks the speaker `cycles` times, pausing roughly a2_wait(halfPeriod)
// between clicks, to approximate a square-wave tone. There's no cycle-
// exact timing available to this C subset (no inline-asm loop-counting
// contract), so this can't promise a specific frequency - only that
// lower `halfPeriod` values click faster.
void a2_tone(unsigned char cycles, unsigned char halfPeriod);

// ProDOS file convenience wrappers, built on mli_call()/mli_patch() above.
// Each one builds its call's parameter list in a fixed global array
// (little-endian 2-byte fields, per the ProDOS 8 Technical Reference
// Manual ch. 4), the same reason mli_trampoline stages its own bytes
// rather than a local's address (no local/parameter has a fixed address
// an asm block, or ProDOS itself, can write into). A pathname argument
// must be the address of its own global `unsigned char[]` whose byte 0 is
// the length, followed by the path text (no NUL) - ProDOS's own "count
// byte + up to 64 characters" convention, not a C string. A data/io
// buffer argument must likewise be the address of its own global array;
// an io_buffer must be exactly 1024 bytes, starting on a page boundary.
//
// Every call below returns the MLI error code (0 = success), same
// convention as mli_call(). Any additional result field ProDOS writes
// back into the parameter list is read back with that call's own accessor
// function, since the block is a global array the caller (and this file)
// can index into directly.

// CREATE ($C0): creates `pathname` with the given access/file-type/
// aux-type/storage-type bytes (dates left zeroed - ProDOS treats a zero
// date/time as "unknown").
int pd_create(int pathname, unsigned char access, unsigned char fileType, int auxType, unsigned char storageType);

// OPEN ($C8): opens `pathname` using `buffer` (its own 1024-byte,
// page-aligned global array) as ProDOS's required I/O buffer.
int pd_open(int pathname, int buffer);
// The ref_num OPEN wrote back into its own parameter list, valid after
// the most recent pd_open() call.
unsigned char pd_open_refnum();

// READ ($CA): reads up to `requestCount` bytes from the file opened as
// `refNum` into `dataBuffer`.
int pd_read(unsigned char refNum, int dataBuffer, int requestCount);
// The byte count READ actually transferred, valid after the most recent
// pd_read() call.
int pd_read_transcount();

// WRITE ($CB): writes `requestCount` bytes from `dataBuffer` to the file
// opened as `refNum`.
int pd_write(unsigned char refNum, int dataBuffer, int requestCount);
// The byte count WRITE actually transferred, valid after the most recent
// pd_write() call.
int pd_write_transcount();

// CLOSE ($CC): closes the file opened as `refNum` (0 closes every file
// opened at or above the current level).
int pd_close(unsigned char refNum);

// GET_FILE_INFO ($C4): reads `pathname`'s directory entry fields. The
// accessors below read back what this call wrote into its own parameter
// list, valid after the most recent pd_get_file_info() call.
int pd_get_file_info(int pathname);
unsigned char pd_info_access();
unsigned char pd_info_file_type();
int pd_info_aux_type();
unsigned char pd_info_storage_type();
int pd_info_blocks_used();

#endif
