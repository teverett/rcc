#include "pcbios.h"

// Staging cells for a BIOS call's argument/result words - see pcbios.h's own header
// comment for why these can't just be locals/parameters, and why none of this is
// reentrant. Named with a pcbios_ prefix (unlike msdos.c's own rt_w/rt_w2/rt_w3) so a
// program linking both libraries together never gets two conflicting definitions of the
// same global.
int pcbios_rt_w;
int pcbios_rt_w2;
int pcbios_rt_w3;
int pcbios_rt_w4;
int pcbios_rt_w5;

// INT 13h's own 512-byte sector size.
unsigned char pcbios_rt_disk_buf[512];
// pcbios_write_string()'s own staging for a caller-supplied runtime pointer - a
// local/parameter has no fixed address an asm block's literal text could name directly
// (see pcbios.h's own header comment), so the pointer value itself is staged through this
// global and read back with `[g_pcbios_rt_ptr]`, the same way every other argument is.
char *pcbios_rt_ptr;

void pcbios_set_mode(unsigned char mode) {
    pcbios_rt_w = mode;  // ah = 0 (set video mode), al = mode
    asm {
        mov ax, [g_pcbios_rt_w]
        int 0x10
    }
}

void pcbios_set_cursor_shape(unsigned char start, unsigned char end) {
    pcbios_rt_w = start * 256 + end;  // ch = start scan line, cl = end scan line
    asm {
        mov cx, [g_pcbios_rt_w]
        mov ax, 256       ; ah = 1 (set cursor shape)
        int 0x10
    }
}

void pcbios_putchar(unsigned char c) {
    pcbios_rt_w = 3584 + c;  // ah = 0x0E (teletype output), al = c
    asm {
        mov ax, [g_pcbios_rt_w]
        mov bx, 0        ; bh = page 0 (bl ignored in text modes)
        int 0x10
    }
}

void pcbios_puts(char *s) {
    int i;
    i = 0;
    while (s[i] != 0) {
        pcbios_putchar(s[i]);
        i = i + 1;
    }
}

void pcbios_set_cursor(unsigned char row, unsigned char col) {
    pcbios_rt_w = row * 256 + col;  // dh = row, dl = col
    asm {
        mov dx, [g_pcbios_rt_w]
        mov ax, 512      ; ah = 2 (set cursor position)
        mov bx, 0        ; bh = page 0
        int 0x10
    }
}

unsigned char pcbios_cursor_row() {
    asm {
        mov ax, 768       ; ah = 3 (get cursor position/size)
        mov bx, 0         ; bh = page 0
        int 0x10
        mov [g_pcbios_rt_w], dx  ; dh = row, dl = col
    }
    return pcbios_rt_w / 256;
}

unsigned char pcbios_cursor_col() {
    asm {
        mov ax, 768
        mov bx, 0
        int 0x10
        mov [g_pcbios_rt_w], dx
    }
    return pcbios_rt_w;  // truncates to dl
}

void pcbios_write_attr(unsigned char c, unsigned char attr, unsigned char count) {
    pcbios_rt_w = 2304 + c;  // ah = 9 (write char+attribute), al = c
    pcbios_rt_w2 = attr;      // bh = page 0, bl = attr
    pcbios_rt_w3 = count;     // cx = repeat count
    asm {
        mov ax, [g_pcbios_rt_w]
        mov bx, [g_pcbios_rt_w2]
        mov cx, [g_pcbios_rt_w3]
        int 0x10
    }
}

void pcbios_clear_screen() {
    pcbios_rt_w = 1536;   // ah = 6 (scroll up), al = 0 (clear entire window)
    pcbios_rt_w2 = 1792;  // bh = 7 (fill attribute: white on black)
    pcbios_rt_w3 = 6224;  // dh = 24, dl = 79 (lower-right corner of an 80x25 screen)
    asm {
        mov ax, [g_pcbios_rt_w]
        mov bx, [g_pcbios_rt_w2]
        mov cx, 0        ; ch = 0, cl = 0 (upper-left corner)
        mov dx, [g_pcbios_rt_w3]
        int 0x10
    }
}

unsigned char pcbios_peek_char() {
    asm {
        mov ax, 2048      ; ah = 8 (read char and attribute at cursor)
        mov bx, 0         ; bh = page 0
        int 0x10
        mov [g_pcbios_rt_w], ax  ; al = char, ah = attribute
    }
    return pcbios_rt_w;  // truncates to al
}

unsigned char pcbios_peek_attr() {
    asm {
        mov ax, 2048
        mov bx, 0
        int 0x10
        mov [g_pcbios_rt_w], ax
    }
    return pcbios_rt_w / 256;
}

void pcbios_select_active_page(unsigned char page) {
    pcbios_rt_w = 1280 + page;  // ah = 5 (select active display page), al = page
    asm {
        mov ax, [g_pcbios_rt_w]
        int 0x10
    }
}

void pcbios_scroll_up(unsigned char lines, unsigned char attr, unsigned char top, unsigned char left,
                       unsigned char bottom, unsigned char right) {
    pcbios_rt_w = 1536 + lines;        // ah = 6 (scroll up), al = lines (0 = blank the whole window)
    pcbios_rt_w2 = attr * 256;         // bh = fill attribute, bl = 0 (unused)
    pcbios_rt_w3 = top * 256 + left;   // ch = top row, cl = left column
    pcbios_rt_w4 = bottom * 256 + right;  // dh = bottom row, dl = right column
    asm {
        mov ax, [g_pcbios_rt_w]
        mov bx, [g_pcbios_rt_w2]
        mov cx, [g_pcbios_rt_w3]
        mov dx, [g_pcbios_rt_w4]
        int 0x10
    }
}

void pcbios_scroll_down(unsigned char lines, unsigned char attr, unsigned char top, unsigned char left,
                         unsigned char bottom, unsigned char right) {
    pcbios_rt_w = 1792 + lines;        // ah = 7 (scroll down), al = lines
    pcbios_rt_w2 = attr * 256;         // bh = fill attribute, bl = 0 (unused)
    pcbios_rt_w3 = top * 256 + left;   // ch = top row, cl = left column
    pcbios_rt_w4 = bottom * 256 + right;  // dh = bottom row, dl = right column
    asm {
        mov ax, [g_pcbios_rt_w]
        mov bx, [g_pcbios_rt_w2]
        mov cx, [g_pcbios_rt_w3]
        mov dx, [g_pcbios_rt_w4]
        int 0x10
    }
}

void pcbios_write_char(unsigned char c, unsigned char count) {
    pcbios_rt_w = 2560 + c;  // ah = 0x0A (write character only), al = c
    pcbios_rt_w2 = count;    // cx = repeat count
    asm {
        mov ax, [g_pcbios_rt_w]
        mov bx, 0        ; bh = page 0
        mov cx, [g_pcbios_rt_w2]
        int 0x10
    }
}

unsigned char pcbios_get_video_mode() {
    asm {
        mov ax, 3840      ; ah = 0x0F (get current video state)
        int 0x10
        mov [g_pcbios_rt_w], ax  ; al = mode, ah = column count
    }
    return pcbios_rt_w;  // truncates to al
}

unsigned char pcbios_get_video_columns() {
    asm {
        mov ax, 3840
        int 0x10
        mov [g_pcbios_rt_w], ax
    }
    return pcbios_rt_w / 256;  // ah = column count
}

unsigned char pcbios_get_video_page() {
    asm {
        mov ax, 3840
        int 0x10
        mov [g_pcbios_rt_w], bx  ; bh = active display page
    }
    return pcbios_rt_w / 256;
}

void pcbios_write_pixel(int row, int col, unsigned char color) {
    pcbios_rt_w = 3072 + color;  // ah = 0x0C (write graphics pixel), al = color
    pcbios_rt_w2 = col;           // cx = column
    pcbios_rt_w3 = row;           // dx = row
    asm {
        mov ax, [g_pcbios_rt_w]
        mov bx, 0        ; bh = page 0
        mov cx, [g_pcbios_rt_w2]
        mov dx, [g_pcbios_rt_w3]
        int 0x10
    }
}

unsigned char pcbios_read_pixel(int row, int col) {
    pcbios_rt_w2 = col;
    pcbios_rt_w3 = row;
    asm {
        mov ax, 3328      ; ah = 0x0D (read graphics pixel)
        mov bx, 0         ; bh = page 0
        mov cx, [g_pcbios_rt_w2]
        mov dx, [g_pcbios_rt_w3]
        int 0x10
        mov [g_pcbios_rt_w], ax  ; al = color
    }
    return pcbios_rt_w;
}

void pcbios_write_string(unsigned char row, unsigned char col, unsigned char attr, char *s, unsigned char len) {
    pcbios_rt_ptr = s;
    pcbios_rt_w = 4864;              // ah = 0x13 (write string), al = 0 (attr in bl, cursor not moved)
    pcbios_rt_w2 = attr;             // bh = page 0, bl = attribute
    pcbios_rt_w3 = row * 256 + col;  // dh = row, dl = column
    pcbios_rt_w5 = len;              // cx = string length
    asm {
        push bp           ; bp is this dialect's own frame pointer (see I8086Codegen.cpp) -
                           ; save/restore it around the one BIOS call that needs it as a
                           ; general-purpose register (es:bp -> the string; es still equals
                           ; ds - see pcbios.h)
        mov ax, [g_pcbios_rt_w]
        mov bx, [g_pcbios_rt_w2]
        mov dx, [g_pcbios_rt_w3]
        mov bp, [g_pcbios_rt_ptr]
        mov cx, [g_pcbios_rt_w5]
        int 0x10
        pop bp
    }
}

unsigned char pcbios_key_shift_status() {
    asm {
        mov ax, 512      ; ah = 2 (get shift flags)
        int 0x16
        mov [g_pcbios_rt_w], ax
    }
    return pcbios_rt_w;
}

unsigned char pcbios_getchar() {
    asm {
        mov ax, 0        ; ah = 0 (read keystroke, wait for one)
        int 0x16
        mov [g_pcbios_rt_w], ax  ; al = ascii char, ah = scan code (ignored)
    }
    return pcbios_rt_w;  // truncates to al
}

unsigned char pcbios_haskey() {
    asm {
        mov ax, 256       ; ah = 1 (check for keystroke, don't wait)
        int 0x16
        je PCBIOS_HASKEY_NO  ; ZF set -> no key waiting
        mov ax, 1
        jmp PCBIOS_HASKEY_DONE
PCBIOS_HASKEY_NO:
        mov ax, 0
PCBIOS_HASKEY_DONE:
        mov [g_pcbios_rt_w], ax
    }
    return pcbios_rt_w;
}

int pcbios_equipment() {
    asm {
        int 0x11
        mov [g_pcbios_rt_w], ax
    }
    return pcbios_rt_w;
}

int pcbios_memory_size_kb() {
    asm {
        int 0x12
        mov [g_pcbios_rt_w], ax
    }
    return pcbios_rt_w;
}

void pcbios_disk_reset(unsigned char drive) {
    pcbios_rt_w = drive;  // dl = drive, ah = 0 (reset disk system)
    asm {
        mov dx, [g_pcbios_rt_w]
        mov ax, 0
        int 0x13
    }
}

unsigned char pcbios_disk_read(unsigned char drive, unsigned char cyl, unsigned char head, unsigned char sector) {
    pcbios_rt_w = 513;                      // ah = 2 (read sectors), al = 1 (one sector)
    pcbios_rt_w2 = cyl * 256 + sector;      // ch = cyl, cl = sector (1-63)
    pcbios_rt_w3 = head * 256 + drive;      // dh = head, dl = drive
    asm {
        mov ax, [g_pcbios_rt_w]
        mov cx, [g_pcbios_rt_w2]
        mov dx, [g_pcbios_rt_w3]
        mov bx, g_pcbios_rt_disk_buf  ; es:bx -> buffer (es still equals ds - see pcbios.h)
        int 0x13
        jnc PCBIOS_DISK_READ_OK
        mov [g_pcbios_rt_w], ax         ; ah = BIOS error status on failure
        jmp PCBIOS_DISK_READ_DONE
PCBIOS_DISK_READ_OK:
        mov ax, 0
        mov [g_pcbios_rt_w], ax
PCBIOS_DISK_READ_DONE:
    }
    return pcbios_rt_w / 256;  // 0 on success, else the status code
}

unsigned char pcbios_disk_write(unsigned char drive, unsigned char cyl, unsigned char head, unsigned char sector) {
    pcbios_rt_w = 769;                      // ah = 3 (write sectors), al = 1 (one sector)
    pcbios_rt_w2 = cyl * 256 + sector;      // ch = cyl, cl = sector (1-63)
    pcbios_rt_w3 = head * 256 + drive;      // dh = head, dl = drive
    asm {
        mov ax, [g_pcbios_rt_w]
        mov cx, [g_pcbios_rt_w2]
        mov dx, [g_pcbios_rt_w3]
        mov bx, g_pcbios_rt_disk_buf
        int 0x13
        jnc PCBIOS_DISK_WRITE_OK
        mov [g_pcbios_rt_w], ax
        jmp PCBIOS_DISK_WRITE_DONE
PCBIOS_DISK_WRITE_OK:
        mov ax, 0
        mov [g_pcbios_rt_w], ax
PCBIOS_DISK_WRITE_DONE:
    }
    return pcbios_rt_w / 256;
}

unsigned char pcbios_disk_verify(unsigned char drive, unsigned char cyl, unsigned char head, unsigned char sector) {
    pcbios_rt_w = 1025;                     // ah = 4 (verify sectors), al = 1 (one sector)
    pcbios_rt_w2 = cyl * 256 + sector;      // ch = cyl, cl = sector (1-63)
    pcbios_rt_w3 = head * 256 + drive;      // dh = head, dl = drive
    asm {
        mov ax, [g_pcbios_rt_w]
        mov cx, [g_pcbios_rt_w2]
        mov dx, [g_pcbios_rt_w3]
        mov bx, g_pcbios_rt_disk_buf  ; es:bx - some BIOSes still expect a valid pointer here, though verify never reads through it
        int 0x13
        jnc PCBIOS_DISK_VERIFY_OK
        mov [g_pcbios_rt_w], ax
        jmp PCBIOS_DISK_VERIFY_DONE
PCBIOS_DISK_VERIFY_OK:
        mov ax, 0
        mov [g_pcbios_rt_w], ax
PCBIOS_DISK_VERIFY_DONE:
    }
    return pcbios_rt_w / 256;
}

unsigned char pcbios_disk_changed(unsigned char drive) {
    pcbios_rt_w = drive;  // dl = drive, ah = 0x16 (detect disk change)
    asm {
        mov dx, [g_pcbios_rt_w]
        mov ax, 5632      ; ah = 0x16, al = 0
        int 0x13
        mov [g_pcbios_rt_w], ax
    }
    return pcbios_rt_w / 256;  // ah = status (0 = no change, 1 = unsupported, 6 = changed)
}

unsigned char pcbios_disk_last_status(unsigned char drive) {
    pcbios_rt_w = drive;  // dl = drive, ah = 1 (get status of last operation)
    asm {
        mov dx, [g_pcbios_rt_w]
        mov ax, 256       ; ah = 1, al = 0
        int 0x13
        mov [g_pcbios_rt_w], ax
    }
    return pcbios_rt_w;  // al = status
}

unsigned char pcbios_disk_get_params(unsigned char drive) {
    pcbios_rt_w = drive;  // dl = drive, ah = 8 (get drive parameters)
    asm {
        mov dx, [g_pcbios_rt_w]
        mov ax, 2048      ; ah = 8, al = 0
        int 0x13
        jnc PCBIOS_DISKPARAMS_OK
        mov [g_pcbios_rt_w], ax         ; ah = status on failure
        jmp PCBIOS_DISKPARAMS_DONE
PCBIOS_DISKPARAMS_OK:
        mov ax, 0
        mov [g_pcbios_rt_w], ax
PCBIOS_DISKPARAMS_DONE:
        mov [g_pcbios_rt_w2], cx  ; ch = max cylinder (low 8 bits), cl = sectors/track | cylinder's top 2 bits
        mov [g_pcbios_rt_w3], dx  ; dh = max head, dl = number of drives
    }
    return pcbios_rt_w / 256;
}

int pcbios_disk_cylinders() {
    int ch;
    int top;
    ch = pcbios_rt_w2 / 256;         // CH: max cylinder, low 8 bits
    top = (pcbios_rt_w2 & 192) / 64;  // CL bits 7-6: max cylinder, high 2 bits
    return top * 256 + ch + 1;
}

unsigned char pcbios_disk_heads() {
    return pcbios_rt_w3 / 256 + 1;  // DH: max head index, plus one
}

unsigned char pcbios_disk_sectors_per_track() {
    return pcbios_rt_w2 & 63;  // CL's own low 6 bits
}

unsigned char pcbios_disk_count() {
    return pcbios_rt_w3;  // DL, truncates
}

int pcbios_buf_get(int i) {
    return pcbios_rt_disk_buf[i];
}

void pcbios_buf_set(int i, unsigned char value) {
    pcbios_rt_disk_buf[i] = value;
}

void pcbios_exit(unsigned char code) {
    pcbios_rt_w = 19456 + code;  // ah = 0x4C (terminate with return code), al = code
    asm {
        mov ax, [g_pcbios_rt_w]
        int 0x21
    }
}

void pcbios_serial_init(unsigned char port, unsigned char params) {
    pcbios_rt_w = params;  // ah = 0 (initialize port), al = params
    pcbios_rt_w2 = port;   // dx = port
    asm {
        mov ax, [g_pcbios_rt_w]
        mov dx, [g_pcbios_rt_w2]
        int 0x14
    }
}

unsigned char pcbios_serial_send(unsigned char port, unsigned char c) {
    pcbios_rt_w = 256 + c;  // ah = 1 (send character), al = c
    pcbios_rt_w2 = port;    // dx = port
    asm {
        mov ax, [g_pcbios_rt_w]
        mov dx, [g_pcbios_rt_w2]
        int 0x14
        mov [g_pcbios_rt_w], ax
    }
    return pcbios_rt_w / 256;  // ah = line status (bit 7 = timeout)
}

unsigned char pcbios_serial_receive(unsigned char port) {
    pcbios_rt_w2 = port;  // dx = port
    asm {
        mov ax, 512       ; ah = 2 (receive character)
        mov dx, [g_pcbios_rt_w2]
        int 0x14
        mov [g_pcbios_rt_w], ax
    }
    return pcbios_rt_w;  // al = character
}

unsigned char pcbios_serial_receive_status() {
    return pcbios_rt_w / 256;  // ah left by the last pcbios_serial_receive() call
}

unsigned char pcbios_serial_line_status(unsigned char port) {
    pcbios_rt_w2 = port;
    asm {
        mov ax, 768       ; ah = 3 (get port status)
        mov dx, [g_pcbios_rt_w2]
        int 0x14
        mov [g_pcbios_rt_w], ax
    }
    return pcbios_rt_w / 256;  // ah = line status
}

unsigned char pcbios_serial_modem_status(unsigned char port) {
    pcbios_rt_w2 = port;
    asm {
        mov ax, 768
        mov dx, [g_pcbios_rt_w2]
        int 0x14
        mov [g_pcbios_rt_w], ax
    }
    return pcbios_rt_w;  // al = modem status
}

unsigned char pcbios_printer_putchar(unsigned char port, unsigned char c) {
    pcbios_rt_w = c;      // ah = 0 (print character), al = c
    pcbios_rt_w2 = port;  // dx = port
    asm {
        mov ax, [g_pcbios_rt_w]
        mov dx, [g_pcbios_rt_w2]
        int 0x17
        mov [g_pcbios_rt_w], ax
    }
    return pcbios_rt_w / 256;  // ah = printer status
}

unsigned char pcbios_printer_init(unsigned char port) {
    pcbios_rt_w2 = port;
    asm {
        mov ax, 256       ; ah = 1 (initialize printer port)
        mov dx, [g_pcbios_rt_w2]
        int 0x17
        mov [g_pcbios_rt_w], ax
    }
    return pcbios_rt_w / 256;
}

unsigned char pcbios_printer_status(unsigned char port) {
    pcbios_rt_w2 = port;
    asm {
        mov ax, 512       ; ah = 2 (get printer status)
        mov dx, [g_pcbios_rt_w2]
        int 0x17
        mov [g_pcbios_rt_w], ax
    }
    return pcbios_rt_w / 256;
}

void pcbios_get_system_time() {
    asm {
        mov ax, 0         ; ah = 0 (get system time)
        int 0x1a
        mov [g_pcbios_rt_w], ax   ; al = midnight flag
        mov [g_pcbios_rt_w2], cx  ; high word of tick count
        mov [g_pcbios_rt_w3], dx  ; low word of tick count
    }
}

unsigned char pcbios_time_midnight_flag() {
    return pcbios_rt_w;
}

int pcbios_time_ticks_high() {
    return pcbios_rt_w2;
}

int pcbios_time_ticks_low() {
    return pcbios_rt_w3;
}

void pcbios_set_system_time(int ticks_high, int ticks_low) {
    pcbios_rt_w2 = ticks_high;
    pcbios_rt_w3 = ticks_low;
    asm {
        mov cx, [g_pcbios_rt_w2]
        mov dx, [g_pcbios_rt_w3]
        mov ax, 256       ; ah = 1 (set system time)
        int 0x1a
    }
}

unsigned char pcbios_get_rtc_time() {
    asm {
        mov ax, 512       ; ah = 2 (get RTC time)
        int 0x1a
        jnc PCBIOS_RTC_TIME_OK
        mov ax, 1
        mov [g_pcbios_rt_w], ax
        jmp PCBIOS_RTC_TIME_DONE
PCBIOS_RTC_TIME_OK:
        mov ax, 0
        mov [g_pcbios_rt_w], ax
PCBIOS_RTC_TIME_DONE:
        mov [g_pcbios_rt_w2], cx  ; ch = hour (bcd), cl = minute (bcd)
        mov [g_pcbios_rt_w3], dx  ; dh = second (bcd), dl = dst flag
    }
    return pcbios_rt_w;  // 0 = ok, 1 = RTC not running
}

unsigned char pcbios_rtc_hour_bcd() {
    return pcbios_rt_w2 / 256;  // CH
}

unsigned char pcbios_rtc_minute_bcd() {
    return pcbios_rt_w2;  // CL, truncates
}

unsigned char pcbios_rtc_second_bcd() {
    return pcbios_rt_w3 / 256;  // DH
}

unsigned char pcbios_rtc_dst_flag() {
    return pcbios_rt_w3;  // DL, truncates
}

void pcbios_set_rtc_time(unsigned char hour_bcd, unsigned char minute_bcd, unsigned char second_bcd,
                          unsigned char dst_flag) {
    pcbios_rt_w2 = hour_bcd * 256 + minute_bcd;  // ch = hour, cl = minute
    pcbios_rt_w3 = second_bcd * 256 + dst_flag;  // dh = second, dl = dst flag
    asm {
        mov cx, [g_pcbios_rt_w2]
        mov dx, [g_pcbios_rt_w3]
        mov ax, 768       ; ah = 3 (set RTC time)
        int 0x1a
    }
}

unsigned char pcbios_get_rtc_date() {
    asm {
        mov ax, 1024      ; ah = 4 (get RTC date)
        int 0x1a
        jnc PCBIOS_RTC_DATE_OK
        mov ax, 1
        mov [g_pcbios_rt_w], ax
        jmp PCBIOS_RTC_DATE_DONE
PCBIOS_RTC_DATE_OK:
        mov ax, 0
        mov [g_pcbios_rt_w], ax
PCBIOS_RTC_DATE_DONE:
        mov [g_pcbios_rt_w2], cx  ; ch = century (bcd), cl = year (bcd)
        mov [g_pcbios_rt_w3], dx  ; dh = month (bcd), dl = day (bcd)
    }
    return pcbios_rt_w;
}

unsigned char pcbios_rtc_century_bcd() {
    return pcbios_rt_w2 / 256;  // CH
}

unsigned char pcbios_rtc_year_bcd() {
    return pcbios_rt_w2;  // CL, truncates
}

unsigned char pcbios_rtc_month_bcd() {
    return pcbios_rt_w3 / 256;  // DH
}

unsigned char pcbios_rtc_day_bcd() {
    return pcbios_rt_w3;  // DL, truncates
}

void pcbios_set_rtc_date(unsigned char century_bcd, unsigned char year_bcd, unsigned char month_bcd,
                          unsigned char day_bcd) {
    pcbios_rt_w2 = century_bcd * 256 + year_bcd;  // ch = century, cl = year
    pcbios_rt_w3 = month_bcd * 256 + day_bcd;     // dh = month, dl = day
    asm {
        mov cx, [g_pcbios_rt_w2]
        mov dx, [g_pcbios_rt_w3]
        mov ax, 1280      ; ah = 5 (set RTC date)
        int 0x1a
    }
}
