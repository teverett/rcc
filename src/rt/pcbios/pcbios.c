#include "pcbios.h"

// Staging cells for a BIOS call's argument/result words - see pcbios.h's own header
// comment for why these can't just be locals/parameters, and why none of this is
// reentrant. Named with a pcbios_ prefix (unlike msdos.c's own rt_w/rt_w2/rt_w3) so a
// program linking both libraries together never gets two conflicting definitions of the
// same global.
int pcbios_rt_w;
int pcbios_rt_w2;
int pcbios_rt_w3;

// INT 13h's own 512-byte sector size.
unsigned char pcbios_rt_disk_buf[512];

void pcbios_set_mode(unsigned char mode) {
    pcbios_rt_w = mode;  // ah = 0 (set video mode), al = mode
    asm {
        mov ax, [g_pcbios_rt_w]
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
