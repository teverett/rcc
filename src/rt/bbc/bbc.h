#ifndef RCC_RT_BBC_H
#define RCC_RT_BBC_H

// BBC Micro (Acorn) runtime library for rcc's 6502 backend (--target=6502), built on the
// machine's MOS (Machine Operating System) call interface rather than any particular ROM
// revision's own internal routines - the same "stable published entry points, not raw addresses
// into a dump" approach src/rt/msdos/'s INT 21h calls take, and unlike src/rt/c64/'s/
// src/rt/atari400/'s KERNAL/CIO jump tables only because MOS's own equivalent of a jump table
// (OSWRCH/OSRDCH/OSBYTE/OSWORD/OSFILE/OSFIND/OSBGET/OSBPUT/OSGBPB/OSCLI, each a fixed JSR target
// in the top page) already needs no indirection.
//
// Every value staged between C and an `asm { }` block goes through a global, never a local/
// parameter - a local/parameter has no fixed address an asm block's literal text could name
// directly, only a global does (see M6502Codegen.h's own `label()`; same reasoning as c64.c's/
// atari400.c's own header comments). None of these wrappers are reentrant (they all share the
// same staging globals) - fine for a single-threaded bare MOS program, the only kind this target
// runs.
//
// This dialect's own 6502 assembler (M6502Assembler.cpp) only ever encodes the exact mnemonic/
// operand shapes M6502Codegen.cpp itself emits, plus a handful of additions each previous 6502
// machine's own runtime library needed (`ldxi`, for atari400.c's IOCB-number selection). MOS's
// own calling convention needs two more: a block/string address split across X (low byte) and Y
// (high byte) - `ldxi label` already gives the low byte, and `ldyi_hi label` (added for this
// library) gives the high byte - and, for OSBGET/OSBPUT/OSFIND's own dynamic file-handle
// argument (not a compile-time constant or a label's address, so immediate addressing can't
// reach it), absolute-addressing `ldx`/`ldy` (also added for this library) to load X/Y from an
// ordinary staging global instead. There is still no STX/STY/TAX/TXA/TAY/TYA or indexed
// addressing of any kind, so a call whose own result comes back in X or Y rather than A (most
// "read a system variable" OSBYTE calls, and OSWORD's block-address-return conventions) can't be
// read back into C-visible state with this dialect - see this file's own per-area notes below for
// exactly which calls that rules out.
//
// MOS entry points used here (always a plain, fixed `JSR`, so no vector-indirection wrapper is
// needed the way a ROM patch might reroute them):
//   OSWRCH $FFEE, OSASCI $FFE3, OSNEWL $FFE7 - character output
//   OSRDCH $FFE0                             - character input
//   OSBYTE $FFF4, OSWORD $FFF1               - system/hardware service calls
//   OSCLI  $FFF7                             - `*` command invocation
//   OSFILE $FFDD, OSFIND $FFCE, OSBGET $FFD7, OSBPUT $FFD4, OSGBPB $FFD1 - file I/O
//
// Include this file directly (`#include "bbc.c"`) for a single-file `--format=raw`/`ssd` build,
// or compile bbc.c on its own and `--link` it against a separately-compiled program
// (`--format=coff`/`elf`/`aout`/`ssd` - see ../../fileformat/dfs/DfsFormat.h's own header
// comment).
//
// Known risk, not yet verified against real hardware (same caveat as src/rt/c64/'s own
// "frame-storage vs. firmware memory map" note): rcc's 6502 codegen hardcodes
// `kFrameStackBase = $0200-$05FF` as its frame bump allocator (M6502Codegen.cpp), which overlaps
// MOS's own page 3 ($0300-$03FF, "OS workspace" - graphics cursor coordinates, screen-mode
// variables, and other VDU driver state) - a program with a deep enough call stack could in
// principle have one overwrite the other. None of this library's own functions build a deep
// enough frame to trigger it in practice, but a caller doing heavy recursion while also drawing
// graphics (bbc_plot()/bbc_move()/bbc_draw()) should be aware of it.

// --- Console/screen output ------------------------------------------------

// Writes one character via OSWRCH ($FFEE) to the current output stream (the screen, unless
// redirected via `*` command or VDU 2/3).
void bbc_oswrch(unsigned char c);
// Writes a NUL-terminated string one character at a time via bbc_oswrch().
void bbc_puts(char *s);
// Writes one character via OSASCI ($FFE3): identical to bbc_oswrch(), except a carriage return
// ($0D) is expanded to a line feed followed by a carriage return (a full new line).
void bbc_osasci(unsigned char c);
// Moves to the start of the next line via OSNEWL ($FFE7, itself two OSWRCH calls: LF then CR).
void bbc_newline();
// Clears the text/graphics window and homes the cursor, via VDU 12 (form feed) through
// bbc_oswrch().
void bbc_cls();
// Moves the cursor to the top-left of the current text window, via VDU 30 through bbc_oswrch().
void bbc_home();
// Selects screen MODE `mode` (0-7 on an unexpanded Model B), via VDU 22,mode through
// bbc_oswrch() - the same two bytes BASIC's own `MODE` statement sends.
void bbc_mode(unsigned char mode);
// Moves the text cursor to column `x`, row `y` (both 0-based), via VDU 31,x,y through
// bbc_oswrch().
void bbc_gotoxy(unsigned char x, unsigned char y);
// Sets the current text colour, via VDU 17,colour through bbc_oswrch() - the same byte BASIC's
// own `COLOUR` statement sends (0-7 foreground, 128-135 background in the usual MODE 7/text
// modes; the exact meaning of `colour` depends on the current MODE's own palette).
void bbc_textcolour(unsigned char colour);
// Sets a graphics colour/action, via VDU 18,mode,colour through bbc_oswrch() - the same two
// bytes BASIC's own `GCOL` statement sends.
void bbc_graphicscolour(unsigned char mode, unsigned char colour);
// Issues one PLOT operation at graphics coordinates (x, y), via VDU 25,code,x-lo,x-hi,y-lo,y-hi
// through bbc_oswrch() - the same six bytes BASIC's own `PLOT code,x,y` statement sends.
void bbc_plot(unsigned char code, int x, int y);
// Moves the graphics cursor to (x, y) without drawing, via bbc_plot(4, x, y) - BASIC's `MOVE`.
void bbc_move(int x, int y);
// Draws a line from the graphics cursor to (x, y), via bbc_plot(5, x, y) - BASIC's `DRAW`.
void bbc_draw(int x, int y);

// --- Keyboard input --------------------------------------------------------

// Blocks until a key is available and returns it, via OSRDCH ($FFE0). Doesn't check OSRDCH's own
// carry-set-on-error exit (raised on a read error or Escape, depending on filing system/redirect
// state) - this dialect has no way to read the carry flag back into C-visible state other than a
// branch, and no staging global this wrapper writes one into; a caller needing that should use
// `asm { }` directly.
unsigned char bbc_osrdch();
// Alias for bbc_osrdch(), matching every sibling runtime's own `_getch()` naming
// (c64_getch()/v20_getch()).
unsigned char bbc_getch();

// --- OSBYTE system/hardware service calls -----------------------------------

// Issues one OSBYTE call with A = `a`, X = `x`, Y = `y`, and returns the accumulator's own exit
// value. Most OSBYTE calls' real result comes back in X and/or Y instead (every "read a system
// variable" call, and the INKEY-with-time-limit convention), which this dialect has no way to
// read back (no TAX/TAY/STX/STY exist - see this file's own header comment); only a call whose
// documented result is in A, or whose effect is the call itself (every wrapper below), is
// usable through this escape hatch.
unsigned char bbc_osbyte(unsigned char a, unsigned char x, unsigned char y);
// Acknowledges the last Escape condition and clears it, via OSBYTE 126 (`&7E`).
void bbc_acknowledge_escape();
// Enables (`on` nonzero) or disables (`on` zero) the Escape key's own effect (clearing the
// screen/stopping the current program), via OSBYTE 125/124 (`&7D`/`&7C`) - the key still
// generates the condition bbc_acknowledge_escape() clears, it just stops acting on it.
void bbc_escape_enable(unsigned char on);
// Sets the keyboard's auto-repeat delay (in centiseconds before a held key starts repeating),
// via OSBYTE 11 (`&0B`).
void bbc_set_key_repeat_delay(unsigned char n);
// Sets the keyboard's auto-repeat rate (in centiseconds between repeats once started), via
// OSBYTE 12 (`&0C`).
void bbc_set_key_repeat_rate(unsigned char n);
// Flushes every buffer in class `cls` (0 = every class) via OSBYTE 15 (`&0F`).
void bbc_flush_buffer_class(unsigned char cls);
// Flushes one specific buffer (0 = keyboard buffer) via OSBYTE 21 (`&15`).
void bbc_flush_buffer(unsigned char buf);
// Waits for the next vertical sync pulse, via OSBYTE 19 (`&13`).
void bbc_wait_vsync();

// --- `*` command invocation --------------------------------------------------

// Runs `cmd` as a `*` command via OSCLI ($FFF7) - the same mechanism BASIC's own `*command` line
// and `OSCLI` keyword use. `cmd` is staged through an internal buffer and CR-terminated (MOS's
// own command-string convention, not NUL) - the first 79 characters if longer. OSCLI doesn't
// preserve A/X/Y, so nothing about its own completion is reported back beyond "it returned".
void bbc_oscli(char *cmd);

// --- Sound ------------------------------------------------------------------

// Issues one SOUND command via OSWORD 7 (`&07`) - the same four parameters BASIC's own `SOUND`
// statement takes: `channel` (1-3 a tone channel, 0 the noise channel, -1 to -4 a channel with
// its envelope's own amplitude flushed immediately); `amplitude` (0 silent to -15 loudest, or
// 1-4 to instead select envelope number `amplitude`); `pitch` (0-255, roughly quarter-semitone
// steps); `duration` (20ms units, -1 = until replaced/stopped).
void bbc_sound(int channel, int amplitude, int pitch, int duration);
// Defines sound envelope `n` (1-16) via OSWORD 8 (`&08`) - the same 14 parameters BASIC's own
// `ENVELOPE` statement takes, in order: `n`, `t` (step length, centiseconds), `ps1`/`ps2`/`ps3`
// (pitch change per step, each of 3 phases) and `ns1`/`ns2`/`ns3` (number of steps in each
// phase), `ar`/`dr`/`sr`/`rr` (amplitude attack/decay/sustain/release rate), `targetAmp`
// (sustain amplitude level), and `repeats` (number of times the whole envelope repeats, 0 =
// forever).
void bbc_envelope(unsigned char n, unsigned char t, unsigned char ps1, unsigned char ps2, unsigned char ps3,
                  unsigned char ns1, unsigned char ns2, unsigned char ns3, unsigned char ar, unsigned char dr,
                  unsigned char sr, unsigned char rr, unsigned char targetAmp, unsigned char repeats);

// --- File I/O ----------------------------------------------------------------

// OSFIND's own open-mode byte (`A` on entry) - see bbc_osfind_open()'s own comment.
#define BBC_OPEN_INPUT 0x40
#define BBC_OPEN_OUTPUT 0x80
#define BBC_OPEN_UPDATE 0xC0

// Opens `name` via OSFIND ($FFCE) with mode `BBC_OPEN_INPUT`/`BBC_OPEN_OUTPUT`/`BBC_OPEN_UPDATE`,
// and returns the file handle MOS assigns (0 if the open failed, e.g. the file doesn't exist for
// `BBC_OPEN_INPUT`). `name` is staged through an internal buffer and CR-terminated (MOS's own
// filename convention, not NUL) - the first 38 characters if longer.
unsigned char bbc_osfind_open(char *name, unsigned char mode);
// Closes `handle` via OSFIND with A = 0 (0 itself closes every open file at once, MOS's own
// convention, not just this wrapper's).
void bbc_osfind_close(unsigned char handle);
// Reads and returns one byte from open file `handle` via OSBGET ($FFD7). Check
// bbc_osbget_eof() right after the call: OSBGET's own carry flag (set at end of file, with the
// returned byte then undefined) is captured there since this dialect can't read it back any
// other way.
unsigned char bbc_osbget(unsigned char handle);
// True (nonzero) if the most recent bbc_osbget() call hit end of file.
unsigned char bbc_osbget_eof();
// Writes one byte to open file `handle` via OSBPUT ($FFD4).
void bbc_osbput(unsigned char handle, unsigned char b);
// Writes `count` bytes starting at the plain address `bufAddr` (see docs/language.md's "a plain
// `int` used as an untyped address") to open file `handle`, from its current sequential pointer,
// via OSGBPB ($FFD1) function 2.
void bbc_osgbpb_write(unsigned char handle, int bufAddr, int count);
// Reads up to `count` bytes from open file `handle`'s current sequential pointer into the plain
// address `bufAddr`, via OSGBPB function 4. Returns the number of bytes NOT transferred (0 means
// every byte requested was read; nonzero means end of file was reached first).
int bbc_osgbpb_read(unsigned char handle, int bufAddr, int count);

// Saves the byte range [start, end) (an exclusive end address, both plain untyped addresses) to
// a new file `name` via OSFILE ($FFDD) function 0, recording `loadAddr`/`execAddr` as its own
// load/execution address metadata (the same fields DfsFormat.h's own catalogue entry carries).
// Returns the object type MOS reports (normally 1, "file").
unsigned char bbc_osfile_save(char *name, int start, int end, int loadAddr, int execAddr);
// Loads `name` into memory at its own catalogued load address via OSFILE function 255 (`&FF`).
// Returns the object type (0 if `name` doesn't exist - OSFILE itself raises a real MOS error in
// that case, which this wrapper doesn't trap); bbc_osfile_load_address()/_length() read back
// where it landed and how long it was.
unsigned char bbc_osfile_load(char *name);
// Reads `name`'s own catalogue metadata (without loading its data) via OSFILE function 5.
// Returns the object type (0 if `name` doesn't exist); bbc_osfile_load_address()/_exec_address()/
// _length()/_attributes() read back the rest.
unsigned char bbc_osfile_info(char *name);
// Deletes `name` via OSFILE function 6. Returns the object type it had before deletion (0 if it
// didn't exist).
unsigned char bbc_osfile_delete(char *name);
// The load address bbc_osfile_load()/_info()/_save() left in the most recent OSFILE control
// block.
int bbc_osfile_load_address();
// The execution address bbc_osfile_load()/_info()/_save() left in the most recent OSFILE control
// block.
int bbc_osfile_exec_address();
// The file length bbc_osfile_load()/_info() left in the most recent OSFILE control block (valid
// after function 255/5, not function 0/6).
int bbc_osfile_length();
// The attribute byte bbc_osfile_load()/_info() left in the most recent OSFILE control block
// (valid after function 255/5, not function 0/6) - see OSFILE's own documented bit layout
// (readable/writable/executable/locked, owner and other).
unsigned char bbc_osfile_attributes();

#endif
