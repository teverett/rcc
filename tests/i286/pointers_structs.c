// EXPECT: 1
// Compile-only coverage for --target=i286/i286-dos: pointers, arrays, structs,
// globals, statics, and array initializers. See arith_width_overflow.c's own
// header comment in this directory for why this is compile-only, not
// execution-checked. Per docs/language.md's own "Structs" section, this subset
// has no struct initializer (`= {...}`) and no whole-struct assignment, so
// g_point's fields are set individually instead.
struct Point {
    int x;
    int y;
};

int g_array[5] = {1, 2, 3, 4, 5};
struct Point g_point;

static int counter(void) {
    static int n = 0;
    n++;
    return n;
}

int sumArray(int *arr, int len) {
    int total;
    int i;
    total = 0;
    for (i = 0; i < len; i = i + 1) total += arr[i];
    return total;
}

int main() {
    int localArr[3] = {7, 8, 9};
    int *p;
    int viaPtr;
    struct Point *pp;
    int arraySum;
    int c1;
    int c2;
    int total;

    p = &localArr[0];
    viaPtr = *(p + 1);

    g_point.x = 10;
    g_point.y = 20;
    pp = &g_point;
    pp->x += pp->y;

    arraySum = sumArray(&g_array[0], 5);
    c1 = counter();
    c2 = counter();

    total = viaPtr + g_point.x + arraySum + c1 + c2;
    return total == total;  // always 1; exercises total without an unused-variable warning
}
