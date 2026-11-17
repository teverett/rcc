// EXPECT: 1
//
// float/double compound assignment (+=, -=, *=, /=, and pre/post ++/--, including the
// pre-vs-post "yieldOld" distinction) and a float condition driving a while loop and a
// ternary - both routed through genTruthToEax() rather than a plain `cmp eax,0`. Also a
// pointer to a double (*p read/write) and a float->double->float round trip.
double acc;
float fx;

int main() {
    acc = 0.0;
    int i = 0;
    while (i < 5) {
        acc += 1.5;
        i = i + 1;
    }
    if (acc != 7.5) return 0;  // 5 * 1.5

    acc -= 2.5;   // 5.0
    if (acc != 5.0) return 0;
    acc *= 2.0;   // 10.0
    if (acc != 10.0) return 0;
    acc /= 4.0;   // 2.5
    if (acc != 2.5) return 0;

    fx = 1.0f;
    fx++;              // post-increment: statement result unused, fx becomes 2.0
    if (fx != 2.0f) return 0;
    float pre = ++fx;  // pre-increment: fx becomes 3.0, pre is also 3.0
    if (fx != 3.0f || pre != 3.0f) return 0;
    float postVal = fx--; // post-decrement: postVal is 3.0 (old value), fx becomes 2.0
    if (fx != 2.0f || postVal != 3.0f) return 0;

    double *p = &acc;
    *p = *p + 0.5;  // 3.0
    if (acc != 3.0) return 0;

    double cond = 1.0;
    int ternResult = cond ? 10 : 20;  // a float condition, int-typed result
    if (ternResult != 10) return 0;

    float roundTrip = (float)(double)fx;  // float -> double -> float, no change
    if (roundTrip != fx) return 0;

    return 1;
}
