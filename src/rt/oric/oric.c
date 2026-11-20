#include "oric.h"

// oric_putchar()/oric_print()'s own cursor - not touched by the lower-level oric_putc()/
// oric_puts() (same split as coco.c's/dragon32.c's own putchar-vs-putc layering).
int oric_cursor_row;
int oric_cursor_col;

void oric_cls() {
    unsigned char *v;
    int i;
    v = (unsigned char *)ORIC_VIDEO_BASE;
    i = 0;
    while (i < ORIC_COLS * ORIC_ROWS) {
        v[i] = 32;
        i = i + 1;
    }
    oric_cursor_row = 0;
    oric_cursor_col = 0;
}

void oric_putc(int row, int col, unsigned char c) {
    unsigned char *v;
    v = (unsigned char *)ORIC_VIDEO_BASE;
    v[row * ORIC_COLS + col] = c;
}

void oric_puts(int row, int col, char *s) {
    int i;
    i = 0;
    while (s[i] != 0) {
        oric_putc(row, col + i, s[i]);
        i = i + 1;
    }
}

void oric_gotoxy(int row, int col) {
    oric_cursor_row = row;
    oric_cursor_col = col;
}

void oric_scroll() {
    unsigned char *v;
    int i;
    v = (unsigned char *)ORIC_VIDEO_BASE;
    i = 0;
    while (i < ORIC_COLS * (ORIC_ROWS - 1)) {
        v[i] = v[i + ORIC_COLS];
        i = i + 1;
    }
    while (i < ORIC_COLS * ORIC_ROWS) {
        v[i] = 32;
        i = i + 1;
    }
}

void oric_newline() {
    oric_cursor_col = 0;
    if (oric_cursor_row == ORIC_ROWS - 1) {
        oric_scroll();
    } else {
        oric_cursor_row = oric_cursor_row + 1;
    }
}

void oric_putchar(unsigned char c) {
    if (c == 10) {
        oric_newline();
        return;
    }
    oric_putc(oric_cursor_row, oric_cursor_col, c);
    oric_cursor_col = oric_cursor_col + 1;
    if (oric_cursor_col >= ORIC_COLS) {
        oric_newline();
    }
}

void oric_print(char *s) {
    int i;
    i = 0;
    while (s[i] != 0) {
        oric_putchar(s[i]);
        i = i + 1;
    }
}
