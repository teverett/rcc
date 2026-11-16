// EXPECT: 99
// Exercises variable-count shifts (CP1610Codegen.cpp's own single-bit `SLL`/`SLR`/`SAR` loop -
// this CPU's own shift/rotate instructions only ever shift by 1 or 2 bits in one instruction,
// never a runtime count), a deep local-variable frame (several locals below `r4`, each
// addressed by computing `r4 +/- offset` into `r2` by hand - CP1610Codegen.h's own `addr()`),
// recursion (a fresh `r4`/`r6` frame per call, with the real return address riding in `r5` -
// `JSR`'s own real convention), and a computed call through a function pointer (built from a
// direct write to `r7`, the real PC register - this CPU has no register-indirect call
// instruction at all).
int shift_left(int v, int n) {
    int i;
    int r;
    r = v;
    for (i = 0; i < n; i = i + 1) r = r << 1;
    return r;
}

int shift_right_unsigned(unsigned int v, int n) {
    unsigned int r;
    int i;
    r = v;
    for (i = 0; i < n; i = i + 1) r = r >> 1;
    return (int)r;
}

int deep_frame(int a, int b, int c, int d, int e) {
    int s1;
    int s2;
    int s3;
    int s4;
    int s5;
    s1 = a + 1;
    s2 = b + 2;
    s3 = c + 3;
    s4 = d + 4;
    s5 = e + 5;
    return s1 + s2 + s3 + s4 + s5;
}

int fib(int n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}

int get_seven() { return 7; }

int main() {
    int r;
    int fp;
    r = shift_left(1, 5);                       // 32
    r = r + shift_right_unsigned(0x8000, 15);    // 32 + 1 = 33
    r = r + deep_frame(1, 2, 3, 4, 5);           // 33 + 30 = 63
    r = r + fib(7);                               // 63 + 13 = 76
    fp = &get_seven;
    r = r + (fp)();                                // 76 + 7 = 83
    r = r + 16;                                     // 99
    return r;
}
