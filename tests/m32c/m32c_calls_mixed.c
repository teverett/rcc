// Mixed int/long function arguments (stack use: a `long` argument pushes
// two words, an `int` one word - see M32cCodegen.cpp's Call case) and
// recursion accumulating into a `long`.
// EXPECT: 2

long sum_to(int n) {
    if (n <= 0) return 0L;
    return (long)n + sum_to(n - 1);
}

long combine4(int a, long b, int c, long d) { return (long)a + b + (long)c + d; }

int main() {
    long s = sum_to(100);                       // 1+2+...+100 = 5050
    long m = combine4(5, 1000000L, 7, 2000000L);  // 5+1000000+7+2000000 = 3000012

    int ok = 0;
    if (s == 5050L) ok++;
    if (m == 3000012L) ok++;
    return ok;
}
