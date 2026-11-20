#include "intv.h"

#define INTV_COLOR_STACK0 0x0028u
#define INTV_BORDER_COLOR 0x002Cu
#define INTV_DISPLAY_ENABLE 0x0020u
#define INTV_BACKTAB_BASE 0x0200u

void intv_set_color_stack(unsigned int index, unsigned int color) {
    *(unsigned int *)(INTV_COLOR_STACK0 + index) = color;
}

void intv_set_border_color(unsigned int color) { *(unsigned int *)INTV_BORDER_COLOR = color; }

void intv_enable_display() { *(unsigned int *)INTV_DISPLAY_ENABLE = 1; }

void intv_backtab_poke(unsigned int row, unsigned int col, unsigned int value) {
    *(unsigned int *)(INTV_BACKTAB_BASE + row * INTV_BACKTAB_COLS + col) = value;
}

void intv_backtab_fill(unsigned int value) {
    unsigned int row;
    unsigned int col;
    for (row = 0; row < INTV_BACKTAB_ROWS; row++) {
        for (col = 0; col < INTV_BACKTAB_COLS; col++) intv_backtab_poke(row, col, value);
    }
}
