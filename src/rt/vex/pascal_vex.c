#include "pascal_vex.h"

#include "vex.h"

void __pascal_write_int(int v) {
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

// `c` is a Pascal char value (an int here - see
// src/rt/apple2/pascal_apple2.c's own comment on why, including why
// this is passed on as-is rather than cast explicitly).
void __pascal_write_char(int c) { vex_putc(c); }

void __pascal_write_str(char *s) { vex_puts(s); }

// '\n' isn't valid here - CLexer.g4's CHARLIT has no escapes at all
// (see its own header comment) - so this is the plain ASCII code
// instead.
void __pascal_write_newline() { vex_putc(10); }
