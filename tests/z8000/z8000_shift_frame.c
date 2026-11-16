// EXPECT: 99
// Exercises variable-count shifts (Z8000Codegen.cpp's own `rlc`/`rrc`-through-carry bit-serial
// loop - see genShift()'s header comment on why the Z8000's native `sdl`/`sda` dynamic shift
// isn't used) and a deep local-variable frame (several locals below `r13`, each addressed via
// the Z8000's real base+displacement indexed addressing mode - Z8000Codegen.h's own `addr()`),
// plus recursion (a fresh `r13`/`r15` frame per call).
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

int main() {
    int r;
    r = shift_left(1, 5);               // 32
    r = r + shift_right_unsigned(0x8000, 15);  // 32 + 1 = 33
    r = r + deep_frame(1, 2, 3, 4, 5);  // 33 + 30 = 63
    r = r + fib(7);                      // 63 + 13 = 76
    r = r + 23;                          // 99
    return r;
}
