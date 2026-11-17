#include "vex.h"

// Every asm{} block here talks to the C level through this one global cell, the same convention
// src/rt/c64/c64.c's own `rt_a` uses: a plain C assignment sets it up, the block itself names it
// directly (`G_vex_rt_a`, this backend's own Global label prefix - see VexCodegen.h). Deliberately
// not `static`: a `static` global's label is mangled per translation unit (see IR.h's own
// comment), which this hand-written asm text can't follow.
int vex_rt_a;

void vex_putc(unsigned char c) {
    vex_rt_a = c;
    asm {
        ldi r0, G_vex_rt_a
        ld r0, r0
        ldi r1, 0x18
        deo r1, r0
    }
}

void vex_eputc(unsigned char c) {
    vex_rt_a = c;
    asm {
        ldi r0, G_vex_rt_a
        ld r0, r0
        ldi r1, 0x19
        deo r1, r0
    }
}

void vex_puts(char *s) {
    while (*s) {
        vex_putc(*s);
        s = s + 1;
    }
}

void vex_eputs(char *s) {
    while (*s) {
        vex_eputc(*s);
        s = s + 1;
    }
}

unsigned char vex_getc() {
    asm {
        ldi r0, 0x10
        dei r1, r0
        ldi r0, G_vex_rt_a
        st r0, r1
    }
    return vex_rt_a;
}

unsigned char vex_eof() {
    asm {
        ldi r0, 0x11
        dei r1, r0
        ldi r0, G_vex_rt_a
        st r0, r1
    }
    return vex_rt_a;
}

void vex_exit(int code) {
    vex_rt_a = code;
    asm {
        ldi r0, G_vex_rt_a
        ld r0, r0
        ldi r1, 0
        deo r1, r0
    }
}
