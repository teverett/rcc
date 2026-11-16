#include "altair.h"

unsigned char rt_a;

unsigned char altair_sio_status() {
    asm {
        in 16  ; ALTAIR_SIO_STATUS ($10)
        sta g_rt_a
    }
    return rt_a;
}

void altair_sio_write_data(unsigned char value) {
    rt_a = value;
    asm {
        lda g_rt_a
        out 17  ; ALTAIR_SIO_DATA ($11)
    }
}

unsigned char altair_sio_read_data() {
    asm {
        in 17  ; ALTAIR_SIO_DATA ($11)
        sta g_rt_a
    }
    return rt_a;
}

void altair_putc(unsigned char c) {
    while (!(altair_sio_status() & ALTAIR_SIO_TBMT)) {
    }
    altair_sio_write_data(c);
}

void altair_puts(char *s) {
    while (*s) {
        altair_putc(*s);
        s = s + 1;
    }
}

unsigned char altair_getc() {
    while (!(altair_sio_status() & ALTAIR_SIO_RDA)) {
    }
    return altair_sio_read_data();
}
