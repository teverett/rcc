// EXPECT: 1
//
// Compound assignment and ++/-- through a real `int *` pointer target.
// On a backend where a pointer's own storage width differs from
// `int`'s (e.g. riscv64's 8-byte pointer vs. 4-byte int), a pointer must
// still be accepted here the same way `int`/`char`/`short` are, rather
// than mistaken for a `long`/`long long` target just because its width
// happens to match theirs.
int arr[4];
int main() {
    int *p;
    p = &arr[0];
    arr[0] = 10;
    arr[1] = 20;
    arr[2] = 30;
    *p += 5;    // arr[0] = 15
    p++;        // p -> arr[1]
    *p += 5;    // arr[1] = 25
    p--;        // p -> arr[0]
    if (*p != 15) return 0;
    p++;
    if (*p != 25) return 0;
    p++;
    *p -= 1;    // arr[2] = 29
    if (arr[2] != 29) return 0;
    return 1;
}
