#include "imsai.h"

unsigned char rt_i;

unsigned char imsai_sio_status() {
    asm {
        in 3  ; IMSAI_SIO_STATUS ($03)
        sta g_rt_i
    }
    return rt_i;
}

void imsai_sio_write_data(unsigned char value) {
    rt_i = value;
    asm {
        lda g_rt_i
        out 2  ; IMSAI_SIO_DATA ($02)
    }
}

unsigned char imsai_sio_read_data() {
    asm {
        in 2  ; IMSAI_SIO_DATA ($02)
        sta g_rt_i
    }
    return rt_i;
}

void imsai_putc(unsigned char c) {
    while (!(imsai_sio_status() & IMSAI_SIO_TXRDY)) {
    }
    imsai_sio_write_data(c);
}

void imsai_puts(char *s) {
    while (*s) {
        imsai_putc(*s);
        s = s + 1;
    }
}

unsigned char imsai_getc() {
    while (!(imsai_sio_status() & IMSAI_SIO_RXRDY)) {
    }
    return imsai_sio_read_data();
}
