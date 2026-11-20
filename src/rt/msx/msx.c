#include "msx.h"

// Staging cells for a BIOS call's own register operands - see msx.h's own header comment for why
// these can't just be locals/parameters, and why b/c/e each need the `ld hl, g_x` / `ld r, m`
// two-step instead of a direct load.
unsigned char rt_a;
unsigned char rt_b;
unsigned char rt_c;
unsigned char rt_e;
unsigned int rt_hl;

void msx_putc(char c) {
    rt_a = c;
    asm {
        ld a, [g_rt_a]
        call 162  ; CHPUT, $00A2
    }
}

void msx_puts(char *s) {
    int i;
    i = 0;
    while (s[i] != 0) {
        msx_putc(s[i]);
        i = i + 1;
    }
}

char msx_getc() {
    asm {
        call 159  ; CHGET, $009F
        ld [g_rt_a], a
    }
    return rt_a;
}

unsigned char msx_joystick(unsigned char stick) {
    rt_a = stick;
    asm {
        ld a, [g_rt_a]
        call 213  ; GTSTCK, $00D5
        ld [g_rt_a], a
    }
    return rt_a;
}

void msx_vdp_write_reg(unsigned char reg, unsigned char value) {
    rt_c = reg;
    rt_b = value;
    asm {
        ld hl, g_rt_c
        ld c, m
        ld hl, g_rt_b
        ld b, m
        call 71  ; WRTVDP, $0047
    }
}

void msx_vram_write(unsigned int addr, unsigned char value) {
    rt_hl = addr;
    rt_a = value;
    asm {
        ld hl, [g_rt_hl]
        ld a, [g_rt_a]
        call 77  ; WRTVRM, $004D
    }
}

unsigned char msx_vram_read(unsigned int addr) {
    rt_hl = addr;
    asm {
        ld hl, [g_rt_hl]
        call 74  ; RDVRM, $004A
        ld [g_rt_a], a
    }
    return rt_a;
}

void msx_init_text_mode() {
    asm {
        call 108  ; INITXT, $006C
    }
}

void msx_psg_write(unsigned char reg, unsigned char value) {
    rt_a = reg;
    rt_e = value;
    asm {
        ld hl, g_rt_e
        ld e, m
        ld a, [g_rt_a]
        call 147  ; WRTPSG, $0093
    }
}
