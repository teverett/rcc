// `long` bitwise operators, unary `~`/`-`, and shift by a variable (not
// compile-time-constant) count - both the left-shift/unsigned-right-shift
// (`shll`) and the sign-preserving right-shift (`shal`) paths through
// R32cCodegen.cpp's `r2r0` fast path.
// EXPECT: 8

long bitand_l(long a, long b) { return a & b; }
long bitor_l(long a, long b) { return a | b; }
long bitxor_l(long a, long b) { return a ^ b; }
long bitnot_l(long a) { return ~a; }
long neg_l(long a) { return -a; }
long shl_var(long a, int n) { return a << n; }
long shr_var(long a, int n) { return a >> n; }

int main() {
    long x = 0x12345678L;
    long y = 0x0F0F0F0FL;
    int ok = 0;

    if (bitand_l(x, y) == 0x02040608L) ok++;
    if (bitor_l(x, y) == 0x1F3F5F7FL) ok++;
    if (bitxor_l(x, y) == 0x1D3B5977L) ok++;
    if (bitnot_l(x) == -305419897L) ok++;
    if (neg_l(x) == -305419896L) ok++;

    int shiftCount = 4;
    if (shl_var(x, shiftCount) == 0x23456780L) ok++;
    if (shr_var(x, shiftCount) == 19088743L) ok++;

    long negVal = -16L;
    if (shr_var(negVal, 2) == -4L) ok++;  // arithmetic (sign-preserving) right shift

    return ok;
}
