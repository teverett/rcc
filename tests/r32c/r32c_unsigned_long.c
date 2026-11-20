// `unsigned long` wraparound and unsigned divide/modulo - exercises
// R32cCodegen.cpp's `divul` (unsigned) path distinctly from
// r32c_long_arith.c's signed `divxl` path.
// EXPECT: 4

unsigned long wrap_add(unsigned long a, unsigned long b) { return a + b; }
unsigned long udiv_l(unsigned long a, unsigned long b) { return a / b; }
unsigned long umod_l(unsigned long a, unsigned long b) { return a % b; }

int main() {
    unsigned long wrapped = wrap_add(4294967295UL, 1UL);  // wraps to 0
    unsigned long q = udiv_l(4000000000UL, 7UL);          // 571428571
    unsigned long r = umod_l(4000000000UL, 7UL);          // 3

    int ok = 1;
    if (wrapped != 0UL) ok = 0;
    if (q != 571428571UL) ok = 0;
    if (r != 3UL) ok = 0;
    return ok ? 4 : 0;
}
