// A scalar `long` global, a `long` local `static` that persists across
// calls, a pointer to `long` (read/write through it), and a plain `int`
// array (scalar-`long`/pointer-to-`long` storage all go through the same
// word-count-generic path M16cCodegen.cpp's Assign/DerefAssign use - only
// an array *of* `long`/`long long` elements has the known one-word-per-
// element limitation noted in R32cCodegen.cpp's genIndex(), so this
// deliberately avoids that case).
// EXPECT: 4

// A global initializer can't carry a 'u'/'l' integer-literal suffix (a
// parser restriction, not specific to this target), so this one is plain.
long g_long = 123456789;

long *add_to(long *p) {
    *p = *p + 1000L;
    return p;
}

long static_counter() {
    static long count = 0;
    count = count + 1L;
    return count;
}

int array_sum(int *arr, int n) {
    int s = 0;
    for (int i = 0; i < n; i = i + 1) s += arr[i];
    return s;
}

int main() {
    long L = g_long;

    long buf = 500000L;
    long *p = add_to(&buf);
    long after = *p;

    long c1 = static_counter();
    long c2 = static_counter();

    int arr[4];
    arr[0] = 1;
    arr[1] = 2;
    arr[2] = 3;
    arr[3] = 4;
    int s = array_sum(&arr[0], 4);

    int ok = 0;
    if (L == 123456789L) ok++;
    if (after == 501000L) ok++;
    if (c1 == 1L && c2 == 2L) ok++;
    if (s == 10) ok++;
    return ok;
}
