// `switch`/`break`/`continue` driving a `long` accumulator (plain `=`,
// never `+=` - the frontend rejects compound assignment on a `long` on
// every target, not just this one, so there's nothing to test there; see
// src/ir/IRBuilder.cpp's rejectIfWide()).
// EXPECT: 2

long classify_sum(int n) {
    long acc = 0L;
    for (int i = 1; i <= n; i = i + 1) {
        switch (i % 3) {
            case 0:
                acc = acc + (long)i * 10L;
                break;
            case 1:
                acc = acc + (long)i;
                break;
            default:
                break;
        }
    }
    return acc;
}

long loop_break_continue(int n) {
    long acc = 0L;
    for (int i = 0; i < n; i = i + 1) {
        if (i == 3) continue;
        if (i == 8) break;
        acc = acc + (long)i;
    }
    return acc;
}

int main() {
    long r1 = classify_sum(9);           // 1+30+4+60+7+90 = 192
    long r2 = loop_break_continue(10);    // 0+1+2+4+5+6+7 = 25

    int ok = 0;
    if (r1 == 192L) ok++;
    if (r2 == 25L) ok++;
    return ok;
}
