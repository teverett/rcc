// EXPECT: 1
// Compile-only coverage for --target=i286/i286-dos: control flow (loops, switch,
// break/continue), calls with varying argument counts/widths/return widths,
// recursion, and function pointers. See arith_width_overflow.c's own header
// comment in this directory for why this is compile-only, not execution-checked.
// rcc's own C subset has no typedef/function-pointer declarator syntax (see
// docs/language.md) - a function pointer is just a plain int holding its
// address, called through `(table[i])(...)`, same as tests/test_func_ptr_array_loop.c.
int add3(int a, int b, int c) { return a + b + c; }
long addLong(long a, long b) { return a + b; }

int fact(int n) {
    if (n <= 1) return 1;
    return n * fact(n - 1);
}

// A call through a computed function address only ever supports zero
// arguments in this subset (see IR.h's own ExprKind::CallPtr comment) -
// plus()/minus() read their operands out of globals instead of taking
// parameters, same shape as tests/test_func_ptr_array_loop.c's own fa/fb/fc.
int g_opA = 3;
int g_opB = 4;
int plus(void) { return g_opA + g_opB; }
int minus(void) { return g_opA - g_opB; }

int main() {
    int sum;
    int i;
    int ops[2];
    int called;
    long calledLong;
    int recursed;
    int viaPtr;
    int total;

    sum = 0;
    for (i = 0; i < 10; i = i + 1) {
        if (i % 2 == 0) continue;
        sum += i;
        if (i > 7) break;
    }

    switch (sum) {
        case 0:
            sum = -1;
            break;
        default:
            sum += 1;
            break;
    }

    called = add3(1, 2, 3);
    calledLong = addLong(100000L, 1L);
    recursed = fact(5);

    ops[0] = &plus;
    ops[1] = &minus;
    viaPtr = (ops[0])() + (ops[1])();

    total = sum + called + (int)calledLong + recursed + viaPtr;
    return total == total;  // always 1; exercises total without an unused-variable warning
}
