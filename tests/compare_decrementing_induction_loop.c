// EXPECT: 55
// The decrementing counterpart of tests/compare_constant_induction_loop.c's
// `for(i=0; i<N; i++)` idiom: `for(i=N; i>0; i=i-1)`. src/optimizers/
// induction_nonneg/InductionNonNegative.cpp also proves this one stays
// non-negative (the `i > 0` guard forces `i` high enough to absorb the
// decrement before it ever reaches zero) - see
// tests/run_pdp7_induction_nonneg_tests.sh for a check that the lowering
// itself actually changed; this only checks the result is still correct,
// here and on every other target.
int main() {
    int i;
    int sum;
    sum = 0;
    for (i = 10; i > 0; i = i - 1) {
        sum = sum + i;
    }
    return sum;  // 10+9+...+1 = 55
}
