// Exercises every src/rt/oric/oric.h function, not just the subset main.c's hello-world needs -
// a second build/link/run check alongside main.c's (see this directory's own Makefile test
// target and README.md).
#include "../../src/rt/oric/oric.h"

int main() {
    oric_cls();
    oric_putc(0, 0, 'A');
    oric_puts(1, 0, "ROW ONE");
    oric_gotoxy(2, 0);
    oric_putchar('X');
    oric_print("REST OF LINE");
    // Bottom row: a putchar(), then a newline() that must scroll (row stays put) rather than
    // run the cursor off the screen - scrolls everything above (including row 0's 'A') up by
    // one, so the final screen has no trace of it.
    oric_gotoxy(ORIC_ROWS - 1, 0);
    oric_putchar('Z');
    oric_newline();
    oric_putchar('S');
    return 0;
}
