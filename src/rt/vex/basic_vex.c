#include "basic_vex.h"

#include "vex.h"

void __basic_print_int(int v) {
    char digits[11];  // vex's int is 32-bit (see VexCodegen::typeWidths) - "-2147483648" is 11 characters.
    int count;
    int n;
    if (v < 0) {
        vex_putc('-');
        n = -v;
    } else {
        n = v;
    }
    count = 0;
    if (n == 0) {
        digits[0] = '0';
        count = 1;
    } else {
        while (n > 0) {
            digits[count] = (n % 10) + '0';
            n = n / 10;
            count = count + 1;
        }
    }
    while (count > 0) {
        count = count - 1;
        vex_putc(digits[count]);
    }
}

void __basic_print_str(char *s) { vex_puts(s); }

// A simplified placeholder, not real column-aware zone tabbing - see
// src/rt/apple2/basic_apple2.c's own comment on the same simplification.
void __basic_print_tab() {
    vex_putc(' ');
    vex_putc(' ');
    vex_putc(' ');
}

// '\n' isn't valid here - CLexer.g4's CHARLIT has no escapes at all (see
// its own header comment) - so this is the plain ASCII code instead.
void __basic_print_newline() { vex_putc(10); }
