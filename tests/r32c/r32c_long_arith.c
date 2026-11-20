// `long` add/sub/mul/div/mod, each through a real function call so the
// values round-trip through R32cCodegen.cpp's `r2r0`/`r3r1` pair-register
// fast path (see its own header comment) rather than staying as compile-time
// constants the optimizer could fold away.
// EXPECT: 3

long add_l(long a, long b) { return a + b; }
long sub_l(long a, long b) { return a - b; }
long mul_l(long a, long b) { return a * b; }
long div_l(long a, long b) { return a / b; }
long mod_l(long a, long b) { return a % b; }

int main() {
    long a = add_l(1000000L, 2345678L);  // 3345678
    long b = sub_l(a, 345678L);          // 3000000
    long c = mul_l(1234L, -5678L);       // -7006652
    long d = div_l(-1000000L, 7L);       // -142857 (truncates toward zero)
    long e = mod_l(-1000000L, 7L);       // -1 (same sign as the dividend)

    long result = (a - 3345678L) + (b - 3000000L) + (c + 7006652L) + (d + 142857L) + (e + 4L);
    return (int)result;
}
