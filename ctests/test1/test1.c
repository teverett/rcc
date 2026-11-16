// EXPECT: 168
//
// Exercises every math operator and every flow-control construct in
// rcc's supported C subset (see ../../docs/language.md) in one
// program. Each section below adds a specific, hand-verified amount
// to `result`; the final returned value is only 168 if every
// operator and every control-flow path actually did what it's
// supposed to.
//
// Stays within plain `int` throughout (no `long`/`long long`) so the
// same source runs unmodified on every target this directory's
// Makefile checks, including ones with no wide-integer support at
// all (see docs/known-bugs.md).

int add(int a, int b) {
    return a + b;
}

int sum_to(int n) {
    int total;
    int i;
    total = 0;
    for (i = 1; i <= n; i++) {
        total += i;
    }
    return total;
}

int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

int main() {
    int a;
    int b;
    int c;
    int result;
    int i;

    a = 7;
    b = 3;
    result = 0;

    /* binary arithmetic: + - * / % */
    result += a + b;   /* 10  -> 10  */
    result += a - b;   /* 4   -> 14  */
    result += a * b;   /* 21  -> 35  */
    result += a / b;   /* 2   -> 37  */
    result += a % b;   /* 1   -> 38  */

    /* unary arithmetic/bitwise: - ~ */
    result += -a;      /* -7  -> 31  */
    result += a & b;   /* 3   -> 34  */
    result += a | b;   /* 7   -> 41  */
    result += a ^ b;   /* 4   -> 45  */
    result += ~a;      /* -8  -> 37  */
    result += a << 1;  /* 14  -> 51  */
    result += a >> 1;  /* 3   -> 54  */

    /* compound assignment: += -= *= /= %= &= |= ^= <<= >>= */
    c = 10;
    c += 5;    /* 15 */
    c -= 3;    /* 12 */
    c *= 2;    /* 24 */
    c /= 4;    /* 6  */
    c %= 4;    /* 2  */
    c &= 3;    /* 2  */
    c |= 4;    /* 6  */
    c ^= 1;    /* 7  */
    c <<= 2;   /* 28 */
    c >>= 1;   /* 14 */
    result += c;   /* -> 68 */

    /* prefix/postfix increment and decrement */
    i = 5;
    result += i++;   /* adds 5, i=6 -> 73 */
    result += ++i;   /* i=7, adds 7 -> 80 */
    result += i--;   /* adds 7, i=6 -> 87 */
    result += --i;   /* i=5, adds 5 -> 92 */

    /* relational and logical operators: == != < > <= >= && || ! */
    result += (a > b);          /* 1 -> 93  */
    result += (a < b);          /* 0 -> 93  */
    result += (a >= 7);         /* 1 -> 94  */
    result += (b <= 3);         /* 1 -> 95  */
    result += (a == 7);         /* 1 -> 96  */
    result += (a != b);         /* 1 -> 97  */
    result += (a > 0 && b > 0); /* 1 -> 98  */
    result += (a < 0 || b > 0); /* 1 -> 99  */
    result += !(a == b);        /* 1 -> 100 */

    /* ternary operator */
    result += (a > b) ? 1 : 0;  /* 1 -> 101 */

    /* if / else */
    if (a > b) {
        result += 2;   /* -> 103 */
    } else {
        result -= 2;
    }

    /* while */
    i = 0;
    while (i < 4) {
        result += 1;   /* 4 times -> 107 */
        i++;
    }

    /* for */
    for (i = 0; i < 3; i++) {
        result += 2;   /* 3 times -> 113 */
    }

    /* switch / case / default, shared labels */
    i = 2;
    switch (i) {
        case 1:
            result += 100;
            break;
        case 2:
        case 3:
            result += 3;   /* -> 116 */
            break;
        default:
            result += 100;
    }

    /* switch fallthrough: case 1 has no break, runs into case 2 */
    i = 1;
    switch (i) {
        case 1:
            result += 1;   /* -> 117 */
        case 2:
            result += 1;   /* -> 118 */
            break;
        case 3:
            result += 100;
    }

    /* break */
    i = 0;
    while (1) {
        if (i == 5) break;
        result += 1;   /* 5 times (i=0..4) -> 123 */
        i++;
    }

    /* continue */
    i = 0;
    while (i < 6) {
        i++;
        if (i % 2 == 0) continue;
        result += 1;   /* fires for i=1,3,5 -> 126 */
    }

    /* goto / labels */
    i = 0;
loop:
    if (i >= 3) goto done;
    result += 1;   /* 3 times -> 129 */
    i++;
    goto loop;
done:

    /* function calls, including recursion */
    result += add(2, 3);     /* 5  -> 134 */
    result += sum_to(4);     /* 10 -> 144 */
    result += factorial(4);  /* 24 -> 168 */

    return result;
}
