// Exercises every src/rt/dragon32/dragon32.h function, not just the subset main.c's
// hello-world needs - a second build/link check alongside main.c's (see this directory's
// own Makefile test target and README.md).
#include "../../src/rt/dragon32/dragon32.h"

int main() {
    int row;
    dragon32_cls();
    dragon32_putc(0, 0, 'A');
    dragon32_puts(1, 0, "ROW ONE");
    dragon32_gotoxy(2, 0);
    dragon32_putchar('X');
    dragon32_print("REST OF LINE");
    row = 0;
    while (row < DRAGON32_ROWS + 1) {
        dragon32_putchar(10);
        row = row + 1;
    }
    dragon32_scroll();
    dragon32_newline();
    return 0;
}
