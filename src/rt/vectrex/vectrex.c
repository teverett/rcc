#include "vectrex.h"

// Scratch globals the inline `asm` blocks below use to move a C value into/out of a 6809
// register - the same pattern src/rt/coleco/coleco.c uses (g_<name> is this backend's own
// mangled label for a global, see M6809Codegen.h's label()).
signed char vectrex_arg_a;
signed char vectrex_arg_b;
int vectrex_ret_d;
unsigned char vectrex_ret_a;

void vectrex_wait_recal() {
    asm {
        jsr [0xF192]
    }
}

void vectrex_reset0ref() {
    asm {
        jsr [0xF354]
    }
}

void vectrex_intensity(signed char level) {
    vectrex_arg_a = level;
    asm {
        lda [g_vectrex_arg_a]
        jsr [0xF2AB]
    }
}

void vectrex_moveto(signed char x, signed char y) {
    vectrex_arg_a = y;
    vectrex_arg_b = x;
    asm {
        lda [g_vectrex_arg_a]
        ldb [g_vectrex_arg_b]
        jsr [0xF312]
    }
}

void vectrex_dot(signed char x, signed char y) {
    vectrex_arg_a = y;
    vectrex_arg_b = x;
    asm {
        lda [g_vectrex_arg_a]
        ldb [g_vectrex_arg_b]
        jsr [0xF2C3]
    }
}

int vectrex_random() {
    asm {
        jsr [0xF517]
        std [g_vectrex_ret_d]
    }
    return vectrex_ret_d;
}

unsigned char vectrex_read_buttons() {
    asm {
        jsr [0xF1F8]
        sta [g_vectrex_ret_a]
    }
    return vectrex_ret_a;
}
