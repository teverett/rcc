#include "basic_apple2.h"

#include "apple2.h"

void __basic_print_int(int v) {
    char digits[6];  // 6502's int is 16-bit (see M6502Codegen::typeWidths) - "-32768" is 6 characters.
    int count;
    int n;
    if (v < 0) {
        a2_cout('-');
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
        a2_cout(digits[count]);
    }
}

void __basic_print_str(char *s) { a2_puts(s); }

// A simplified placeholder, not real column-aware zone tabbing (which
// would need reading the monitor's own cursor-column cell, CH at $24) -
// enough to visibly separate PRINT items until that's written.
void __basic_print_tab() {
    a2_cout(' ');
    a2_cout(' ');
    a2_cout(' ');
}

void __basic_print_newline() { a2_crout(); }
