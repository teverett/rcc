#include "dragon64.h"

// dragon64_putchar()/dragon64_print()'s own cursor - not touched by the lower-level
// dragon64_putc()/dragon64_puts() (same split as dragon32.c's own dragon32_putc()-vs-
// dragon32_putchar() layering).
int dragon64_cursor_row;
int dragon64_cursor_col;

void dragon64_cls() {
    unsigned char *v;
    int i;
    v = (unsigned char *)DRAGON64_VIDEO_BASE;
    i = 0;
    while (i < DRAGON64_COLS * DRAGON64_ROWS) {
        v[i] = 32;
        i = i + 1;
    }
    dragon64_cursor_row = 0;
    dragon64_cursor_col = 0;
}

void dragon64_putc(int row, int col, unsigned char c) {
    unsigned char *v;
    v = (unsigned char *)DRAGON64_VIDEO_BASE;
    v[row * DRAGON64_COLS + col] = c & 63;
}

void dragon64_puts(int row, int col, char *s) {
    int i;
    i = 0;
    while (s[i] != 0) {
        dragon64_putc(row, col + i, s[i]);
        i = i + 1;
    }
}

void dragon64_gotoxy(int row, int col) {
    dragon64_cursor_row = row;
    dragon64_cursor_col = col;
}

void dragon64_scroll() {
    unsigned char *v;
    int i;
    v = (unsigned char *)DRAGON64_VIDEO_BASE;
    i = 0;
    while (i < DRAGON64_COLS * (DRAGON64_ROWS - 1)) {
        v[i] = v[i + DRAGON64_COLS];
        i = i + 1;
    }
    while (i < DRAGON64_COLS * DRAGON64_ROWS) {
        v[i] = 32;
        i = i + 1;
    }
}

void dragon64_newline() {
    dragon64_cursor_col = 0;
    if (dragon64_cursor_row == DRAGON64_ROWS - 1) {
        dragon64_scroll();
    } else {
        dragon64_cursor_row = dragon64_cursor_row + 1;
    }
}

void dragon64_putchar(unsigned char c) {
    if (c == 10) {
        dragon64_newline();
        return;
    }
    dragon64_putc(dragon64_cursor_row, dragon64_cursor_col, c);
    dragon64_cursor_col = dragon64_cursor_col + 1;
    if (dragon64_cursor_col >= DRAGON64_COLS) {
        dragon64_newline();
    }
}

void dragon64_print(char *s) {
    int i;
    i = 0;
    while (s[i] != 0) {
        dragon64_putchar(s[i]);
        i = i + 1;
    }
}
