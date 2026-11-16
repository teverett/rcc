// EXPECT: 141
// Exercises the Z8000 backend's `mult`/`div` register-pair convention (Z8000Codegen.h's own
// header comment on why both use the `r0:r1` pair, and why the multiplier/divisor always lands
// in a third register rather than `r1` itself) across signed and unsigned, positive and negative
// operands, including the hand-rolled unsigned-divide path (emitUnsignedDiv()) for a divisor
// with its top bit set.
int main() {
    int a;
    int b;
    unsigned int ua;
    unsigned int ub;
    int r;

    a = 7;
    b = 6;
    r = a * b;  // 42

    a = -8;
    b = 5;
    r = r + a * b;  // 42 - 40 = 2

    a = 17;
    b = 5;
    r = r + a / b;  // 2 + 3 = 5
    r = r + a % b;  // 5 + 2 = 7

    a = -17;
    r = r + a / b;  // 7 + (-3) = 4
    r = r + a % b;  // 4 + (-2) = 2

    ua = 60000;
    ub = 7;
    r = r + (int)(ua / ub);  // 2 + 8571 = 8573
    r = r + (int)(ua % ub);  // 8573 + 3 = 8576

    ua = 40000;
    ub = 50000;  // divisor's own top bit is set - exercises emitUnsignedDiv()'s Lbig path
    r = r + (int)(ua / ub);  // 8576 + 0 = 8576
    r = r - 8435;  // land on a small, easy-to-check final value

    return r;
}
