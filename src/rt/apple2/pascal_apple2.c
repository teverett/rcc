#include "pascal_apple2.h"

#include "apple2.h"

void __pascal_write_int(int v) {
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

// `c` is a Pascal char value (an int here - PascalFrontend.cpp's own
// char type lowers onto BaseType::Char, but write()'s own extern
// prototype for this call takes a plain int - see its ensureExternFn
// call). Passed on as-is, not an explicit cast: an explicit int ->
// unsigned char cast warns here (IRBuilder's own warnRiskyCast), while
// an implicit narrowing conversion like this argument-passing one
// doesn't - and every value this is ever called with already fits a
// byte.
void __pascal_write_char(int c) { a2_cout(c); }

void __pascal_write_str(char *s) { a2_puts(s); }

void __pascal_write_newline() { a2_crout(); }
