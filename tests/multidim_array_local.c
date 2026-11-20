// EXPECT: 110
// Exercises every multi-dimensional-array access shape: a plain read/write
// (a[i][j] = ...), compound assignment (+=), pre/post '++', and '&'/'*'
// through a pointer to a real element's address.
int main() {
    int a[2][3];
    int i;
    int j;
    int *p;
    int total;
    for (i = 0; i < 2; i = i + 1) {
        for (j = 0; j < 3; j = j + 1) {
            a[i][j] = 0;
        }
    }
    a[0][0] = 5;
    a[0][0] += 3;
    a[1][2]++;
    ++a[1][1];
    p = &a[0][0];
    *p = *p + 100;
    total = 0;
    for (i = 0; i < 2; i = i + 1) {
        for (j = 0; j < 3; j = j + 1) {
            total = total + a[i][j];
        }
    }
    return total;
}
