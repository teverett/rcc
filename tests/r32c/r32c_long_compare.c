// `long` relational/equality operators, including the signed-vs-unsigned
// divergence a negative value exposes (`-1L` compares less than `1L`
// signed, but greater as an unsigned 32-bit magnitude) - exercises both
// `cmpl` jump families R32cCodegen.cpp's genWideCompare() dword fast path
// picks between.
// EXPECT: 7

int cmp_lt(long a, long b) { return a < b; }
int cmp_le(long a, long b) { return a <= b; }
int cmp_gt(long a, long b) { return a > b; }
int cmp_ge(long a, long b) { return a >= b; }
int cmp_eq(long a, long b) { return a == b; }
int cmp_ne(long a, long b) { return a != b; }
int cmp_ltu(unsigned long a, unsigned long b) { return a < b; }

int main() {
    long a = -1L;
    long b = 1L;
    int ok = 0;

    if (cmp_lt(a, b) == 1) ok++;                        // -1 < 1 signed
    if (cmp_ltu((unsigned long)a, (unsigned long)b) == 0) ok++;  // 0xFFFFFFFF < 1 unsigned is false
    if (cmp_le(a, a) == 1) ok++;
    if (cmp_gt(b, a) == 1) ok++;
    if (cmp_ge(b, a) == 1) ok++;
    if (cmp_eq(a, a) == 1) ok++;
    if (cmp_ne(a, b) == 1) ok++;

    return ok;
}
