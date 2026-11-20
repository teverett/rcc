// Exercises every src/rt/dragon64/dragon64.h function, not just the subset main.c's
// hello-world needs - a second build/link check alongside main.c's (see this directory's
// own Makefile test target and README.md).
#include "../../src/rt/dragon64/dragon64.h"

int main() {
    int row;
    dragon64_cls();
    dragon64_putc(0, 0, 'A');
    dragon64_puts(1, 0, "ROW ONE");
    dragon64_gotoxy(2, 0);
    dragon64_putchar('X');
    dragon64_print("REST OF LINE");
    row = 0;
    while (row < DRAGON64_ROWS + 1) {
        dragon64_putchar(10);
        row = row + 1;
    }
    dragon64_scroll();
    dragon64_newline();
    return 0;
}
