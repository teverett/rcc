// EXPECT: 77
//
// Edge cases neither test1.c nor test2.c exercise: round-toward-zero
// signed division/modulo with a negative operand, division/modulo/
// shift by a *variable* amount (not a compile-time literal - a
// distinct codegen path from the literal-amount case on most
// backends, and the exact trigger condition for the scratch-cell ROM
// bug documented in ../../docs/known-bugs.md for z80-coleco/
// 6809-vectrex, neither of which this directory's Makefile targets),
// `unsigned int` arithmetic, and the signed/unsigned comparison
// conversion rule (docs/language.md's "usual arithmetic conversions":
// on equal rank, unsigned wins, so a negative int compares as a huge
// value against a same-rank unsigned).
//
// Every value here is small and the comparisons are relative, not
// absolute-width-dependent, so the same EXPECT holds on every target
// regardless of int width - see test2.c's header comment for why
// that matters cross-platform. No `long`/`long long`, same reason as
// test1.c/test2.c.

int main() {
    int result;
    int a;
    int b;
    int n;
    int si;
    unsigned int ui;
    unsigned int ua;
    unsigned int ub;

    result = 0;

    /* round-toward-zero signed division/modulo, negative operand */
    a = -7;
    b = 2;
    result += a / b;        /* -3 -> -3 */
    result += a % b;        /* -1 -> -4 */
    result += (-a) / b;     /* 3  -> -1 */
    result += (-a) % b;     /* 1  ->  0 */

    /* division/modulo by a variable divisor */
    a = 17;
    b = 3;
    result += a / b;        /* 5 -> 5 */
    result += a % b;        /* 2 -> 7 */

    /* shift by a variable amount, driven by a loop counter */
    for (n = 0; n < 4; n++) {
        result += (1 << n); /* 1+2+4+8 -> 22 */
    }
    a = 128;
    n = 3;
    result += (a >> n);     /* 16 -> 38 */

    /* compound-assignment shift by a variable amount */
    a = 1;
    n = 5;
    a <<= n;                /* 32 */
    result += a;            /* -> 70 */

    /* unsigned division/modulo */
    ua = 17;
    ub = 5;
    result += ua / ub;      /* 3 -> 73 */
    result += ua % ub;      /* 2 -> 75 */

    /* signed/unsigned comparison: si converts to unsigned, becomes
       huge, so it's never less than, and always greater than, a
       small positive unsigned - true regardless of int's width */
    si = -1;
    ui = 1;
    result += (si < ui) ? 0 : 1;  /* 1 -> 76 */
    result += (si > ui) ? 1 : 0;  /* 1 -> 77 */

    return result;
}
