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
// Sets the text-mode cursor's shape via `INT 10h`/`01h` - `start`/`end` are scan lines
// within the character cell (0-based from the top; a normal underline cursor on a CGA/EGA
// 8-scan-line cell is `pcbios_set_cursor_shape(6, 7)`). Bit 5 of `start` hides the cursor
// entirely on most BIOSes (`pcbios_set_cursor_shape(32, 0)`).
void pcbios_set_cursor_shape(unsigned char start, unsigned char end);
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
// Selects display page `page` via `INT 10h`/`05h` (page 0 unless the card has extra video
// RAM for more - most of this library's own calls assume page 0, so this is only useful
// alongside a program that also writes page 1+ directly).
void pcbios_select_active_page(unsigned char page);
// Scrolls the rectangle (`top`,`left`)-(`bottom`,`right`) up/down by `lines` rows via
// `INT 10h`/`06h`/`07h`, filling the rows this exposes with attribute byte `attr`.
// `lines` = 0 blanks the whole rectangle instead of scrolling it. Unlike
// pcbios_clear_screen() (which is always the full 80x25 screen, attribute 7), these cover
// an arbitrary sub-window and attribute.
void pcbios_scroll_up(unsigned char lines, unsigned char attr, unsigned char top, unsigned char left,
                       unsigned char bottom, unsigned char right);
void pcbios_scroll_down(unsigned char lines, unsigned char attr, unsigned char top, unsigned char left,
                         unsigned char bottom, unsigned char right);
// Writes `c` at the cursor `count` times via `INT 10h`/`0Ah` (page 0) - like
// pcbios_write_attr(), doesn't move the cursor, but leaves whatever attribute byte is
// already on screen alone instead of overwriting it.
void pcbios_write_char(unsigned char c, unsigned char count);
// The current video mode/column count/active page via `INT 10h`/`0Fh`. Each is a separate
// call (this dialect has no way to stage three results from one call into three different
// return values at once).
unsigned char pcbios_get_video_mode();
unsigned char pcbios_get_video_columns();
unsigned char pcbios_get_video_page();
// Plots/reads one pixel at (`row`,`col`) via `INT 10h`/`0Ch`/`0Dh` (page 0) - only
// meaningful in a graphics mode (e.g. mode 4/5/6 on CGA); `color` is a palette index,
// masked by the mode's own bits-per-pixel. `row`/`col` are `int`, not `unsigned char`,
// since CGA's 640-wide mode 6 exceeds a byte's range.
void pcbios_write_pixel(int row, int col, unsigned char color);
unsigned char pcbios_read_pixel(int row, int col);
// Writes `len` characters of `s` at (`row`,`col`) in one `INT 10h`/`13h` call, all in
// attribute `attr`, without moving the cursor (write mode 0: BIOS reads only characters
// from `s`, not interleaved attribute bytes). `s` doesn't need to be NUL-terminated -
// exactly `len` bytes are read, so embedded NULs are written like any other character.
void pcbios_write_string(unsigned char row, unsigned char col, unsigned char attr, char *s, unsigned char len);

// Reads one key via `INT 16h`/`00h`, blocking until one is available. Returns the ASCII
// character; the scan code `INT 16h` also returns is discarded.
unsigned char pcbios_getchar();
// True (nonzero) if a key is waiting, via `INT 16h`/`01h` - doesn't block and doesn't
// consume the key.
unsigned char pcbios_haskey();
// The current shift/toggle-key state via `INT 16h`/`02h`: bit 0 = right shift, bit 1 =
// left shift, bit 2 = ctrl, bit 3 = alt, bit 4 = scroll lock, bit 5 = num lock, bit 6 =
// caps lock, bit 7 = insert.
unsigned char pcbios_key_shift_status();

// The installed-equipment word via `INT 11h` (bit layout: bit 0 = at least one floppy
// drive present, bits 4-5 = initial video mode, bits 6-7 = number of floppy drives minus
// one when bit 0 is set, bit 9-11 = number of serial ports, bit 14-15 = number of
// parallel/printer ports - see Ralf Brown's Interrupt List for every other bit).
int pcbios_equipment();
// The amount of contiguous RAM below 640KB, in KB, via `INT 12h` - doesn't see any memory
// above that (extended/XMS memory needs `INT 15h`, not provided by this BIOS-only
// library - see this header's own closing comment).
int pcbios_memory_size_kb();

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
// Verifies (re-reads and checks the sector's own CRC, without transferring data anywhere)
// the same (cylinder, head, sector) one sector via `INT 13h`/`04h`. Same parameters and
// return convention as pcbios_disk_read().
unsigned char pcbios_disk_verify(unsigned char drive, unsigned char cyl, unsigned char head, unsigned char sector);
// Whether removable media in `drive` has changed since the last access, via
// `INT 13h`/`16h` (floppy drives only - always reports "no change" on a fixed disk).
// Returns 0 (no change), 1 (change-line not supported by this drive/controller), or 6
// (media changed - the caller should pcbios_disk_reset() before touching the drive
// again).
unsigned char pcbios_disk_changed(unsigned char drive);
// The status of the last `INT 13h` operation on `drive`, via `INT 13h`/`01h` - the same
// status codes pcbios_disk_read()/pcbios_disk_write() themselves already return, provided
// separately for a caller that wants to recheck without redoing the operation.
unsigned char pcbios_disk_last_status(unsigned char drive);
// `drive`'s own geometry via `INT 13h`/`08h` - call this first; the four accessors below
// then decode its result (no asm involved in any of them - see pcbios_cursor_row()'s own
// comment for the same "one call, several decode-only accessors" shape). Returns 0 on
// success, or `INT 13h`'s own status code on failure (in which case the accessors' values
// are whatever garbage the BIOS happened to leave in CX/DX).
unsigned char pcbios_disk_get_params(unsigned char drive);
int pcbios_disk_cylinders();          // 1-1024 (the 10-bit CH+top-2-bits-of-CL field, plus one)
unsigned char pcbios_disk_heads();     // 1-255 (DH, plus one)
unsigned char pcbios_disk_sectors_per_track();  // 1-63 (CL's own low 6 bits)
unsigned char pcbios_disk_count();     // number of drives of drive's own type (floppy/fixed) BIOS knows about

// Terminates the program via `INT 21h`/`4Ch` (the one DOS call this library makes - a
// `.com` process still needs it to hand control back to DOS, same as `src/rt/msdos/`'s own
// dos_exit()), with `code` as the real DOS exit code. Never returns.
void pcbios_exit(unsigned char code);

// Initializes serial port `port` (0 = COM1, 1 = COM2, ...) via `INT 14h`/`00h`. `params`
// packs baud rate (bits 5-7: 0=110,1=150,2=300,3=600,4=1200,5=2400,6=4800,7=9600), parity
// (bits 3-4: 0/2=none,1=odd,3=even), stop bits (bit 2: 0=1,1=2), and data bits (bits 0-1:
// 2=7,3=8) - see Ralf Brown's Interrupt List for the exact bit layout.
void pcbios_serial_init(unsigned char port, unsigned char params);
// Sends `c` out `port` via `INT 14h`/`01h`. Returns the line-status byte on return - bit 7
// set means the send timed out (no error otherwise; a successful send's other bits are
// the same UART status bits pcbios_serial_line_status() reports).
unsigned char pcbios_serial_send(unsigned char port, unsigned char c);
// Receives one character from `port` via `INT 14h`/`02h`, blocking until one arrives or
// the UART's own timeout elapses. Returns the character; pcbios_serial_receive_status()
// reports whether this call actually timed out instead.
unsigned char pcbios_serial_receive(unsigned char port);
// The line-status byte from the most recent pcbios_serial_receive() call - bit 7 set means
// that call timed out rather than receiving a real character.
unsigned char pcbios_serial_receive_status();
// `port`'s own UART line-status byte via `INT 14h`/`03h` (bit 0 = data ready, bit 1 =
// overrun error, bit 2 = parity error, bit 3 = framing error, bit 4 = break detected,
// bit 5 = transmit holding register empty, bit 6 = transmitter empty, bit 7 = timeout).
unsigned char pcbios_serial_line_status(unsigned char port);
// `port`'s own UART modem-status byte via the same `INT 14h`/`03h` call (bit 4 = clear to
// send, bit 5 = data set ready, bit 6 = ring indicator, bit 7 = carrier detect).
unsigned char pcbios_serial_modem_status(unsigned char port);

// Sends `c` to printer port `port` (0 = LPT1, ...) via `INT 17h`/`00h`. Returns the
// printer-status byte (same bit layout as pcbios_printer_status()) - bit 0 set anywhere
// in it means the print timed out or otherwise failed.
unsigned char pcbios_printer_putchar(unsigned char port, unsigned char c);
// Initializes (resets) printer port `port` via `INT 17h`/`01h`. Returns the same
// printer-status byte pcbios_printer_status() does.
unsigned char pcbios_printer_init(unsigned char port);
// `port`'s own status byte via `INT 17h`/`02h` (bit 0 = timeout, bit 3 = I/O error,
// bit 4 = selected, bit 5 = out of paper, bit 6 = acknowledge, bit 7 = not busy).
unsigned char pcbios_printer_status(unsigned char port);

// The system tick counter (18.2 ticks/second since midnight) via `INT 1Ah`/`00h` - call
// this first; the three accessors below then decode its result (same "one call, several
// decode-only accessors" shape as pcbios_disk_get_params()). The two ticks_* halves
// combine as `ticks_high * 65536 + ticks_low` for the full 32-bit count.
void pcbios_get_system_time();
unsigned char pcbios_time_midnight_flag();  // nonzero if midnight has passed since this was last read/cleared
int pcbios_time_ticks_high();
int pcbios_time_ticks_low();
// Sets the tick counter and clears the midnight flag via `INT 1Ah`/`01h`.
void pcbios_set_system_time(int ticks_high, int ticks_low);
// The motherboard RTC's own time-of-day via `INT 1Ah`/`02h` - call this first; the four
// accessors below decode its result. Each field is packed BCD (`0x59`, not `59`), the
// RTC's own native format - see pcbios_disk_get_params()'s comment for the same call/
// accessor shape. Returns 0 on success, 1 if the RTC isn't running (its battery is dead,
// on real hardware).
unsigned char pcbios_get_rtc_time();
unsigned char pcbios_rtc_hour_bcd();
unsigned char pcbios_rtc_minute_bcd();
unsigned char pcbios_rtc_second_bcd();
unsigned char pcbios_rtc_dst_flag();  // nonzero if daylight saving time is in effect
// Sets the RTC's time-of-day via `INT 1Ah`/`03h`. Every argument is packed BCD, same
// convention as pcbios_get_rtc_time()'s own accessors.
void pcbios_set_rtc_time(unsigned char hour_bcd, unsigned char minute_bcd, unsigned char second_bcd,
                          unsigned char dst_flag);
// The motherboard RTC's own date via `INT 1Ah`/`04h` - same call/accessor shape and packed
// BCD convention as pcbios_get_rtc_time(). `century_bcd`/`year_bcd` combine as a 4-digit
// year (`0x19`,`0x99` = 1999).
unsigned char pcbios_get_rtc_date();
unsigned char pcbios_rtc_century_bcd();
unsigned char pcbios_rtc_year_bcd();
unsigned char pcbios_rtc_month_bcd();
unsigned char pcbios_rtc_day_bcd();
// Sets the RTC's date via `INT 1Ah`/`05h`. Every argument is packed BCD.
void pcbios_set_rtc_date(unsigned char century_bcd, unsigned char year_bcd, unsigned char month_bcd,
                          unsigned char day_bcd);

// Deliberately not provided:
// - `INT 10h`/`10h`-`12h` (EGA/VGA palette, character generator, alternate select) and
//   `1Ah` (VGA display combination) - CGA/MDA-era PC/XT hardware (this library's own
//   scope) has none of this; a real EGA/VGA card still boots in CGA-compatible modes
//   these calls don't touch.
// - `INT 15h` - the original PC/XT BIOS's own functions here are the cassette-motor
//   interface (obsolete even by the XT's day - few XTs shipped with the cassette port
//   wired up) and a handful of AT-and-later services (extended memory size, device busy/
//   wait, PS/2 mouse) that need hardware this library isn't scoped to.
// - `INT 10h`/`13h`'s write modes 1-3 (interleaved character+attribute pairs, or cursor
//   repositioned after writing) - pcbios_write_string() only implements mode 0; call
//   pcbios_set_cursor() yourself afterward if the cursor needs to move.

#endif
