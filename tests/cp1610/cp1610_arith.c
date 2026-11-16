// EXPECT: 141
// Exercises the CP1610 backend's software multiply/divide (CP1610Codegen.cpp's own
// genMul()/genDivMod() - this CPU has no multiply or divide instruction at all) across signed
// and unsigned, positive and negative operands, plus the real hardware's own carry convention
// (`CMPR`/`SUBR` set carry = 1 for "no borrow", the opposite sense of a borrow-flag CPU) that
// genCompare()'s unsigned `<`/`>`/`<=`/`>=` branches depend on.
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
    ub = 50000;
    r = r + (int)(ua / ub);  // 8576 + 0 = 8576
    r = r - 8435;

    return r;
}
