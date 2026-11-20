#include "dragon32.h"

// dragon32_putchar()/dragon32_print()'s own cursor - not touched by the lower-level
// dragon32_putc()/dragon32_puts() (same split as coco.c's own coco_putc()-vs-coco_putchar()
// layering).
int dragon32_cursor_row;
int dragon32_cursor_col;

void dragon32_cls() {
    unsigned char *v;
    int i;
    v = (unsigned char *)DRAGON32_VIDEO_BASE;
    i = 0;
    while (i < DRAGON32_COLS * DRAGON32_ROWS) {
        v[i] = 32;
        i = i + 1;
    }
    dragon32_cursor_row = 0;
    dragon32_cursor_col = 0;
}

void dragon32_putc(int row, int col, unsigned char c) {
    unsigned char *v;
    v = (unsigned char *)DRAGON32_VIDEO_BASE;
    v[row * DRAGON32_COLS + col] = c & 63;
}

void dragon32_puts(int row, int col, char *s) {
    int i;
    i = 0;
    while (s[i] != 0) {
        dragon32_putc(row, col + i, s[i]);
        i = i + 1;
    }
}

void dragon32_gotoxy(int row, int col) {
    dragon32_cursor_row = row;
    dragon32_cursor_col = col;
}

void dragon32_scroll() {
    unsigned char *v;
    int i;
    v = (unsigned char *)DRAGON32_VIDEO_BASE;
    i = 0;
    while (i < DRAGON32_COLS * (DRAGON32_ROWS - 1)) {
        v[i] = v[i + DRAGON32_COLS];
        i = i + 1;
    }
    while (i < DRAGON32_COLS * DRAGON32_ROWS) {
        v[i] = 32;
        i = i + 1;
    }
}

void dragon32_newline() {
    dragon32_cursor_col = 0;
    if (dragon32_cursor_row == DRAGON32_ROWS - 1) {
        dragon32_scroll();
    } else {
        dragon32_cursor_row = dragon32_cursor_row + 1;
    }
}

void dragon32_putchar(unsigned char c) {
    if (c == 10) {
        dragon32_newline();
        return;
    }
    dragon32_putc(dragon32_cursor_row, dragon32_cursor_col, c);
    dragon32_cursor_col = dragon32_cursor_col + 1;
    if (dragon32_cursor_col >= DRAGON32_COLS) {
        dragon32_newline();
    }
}

void dragon32_print(char *s) {
    int i;
    i = 0;
    while (s[i] != 0) {
        dragon32_putchar(s[i]);
        i = i + 1;
    }
}
