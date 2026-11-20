// EXPECT: 1
//
// Same `sizeof(int)`-scaled plain-`int`-as-address walk as
// pointer_through_int_sizeof_single_step.c, but through a function
// parameter and written back through the moved pointer rather than just
// read, confirming the addressing unit stays correct across a call
// boundary too.
int arr[4];

int advance_and_set(int p, int value) {
    p = p + sizeof(int) + sizeof(int);
    *p = value;
    return p;
}

int main() {
    int p;
    arr[0] = 1;
    arr[1] = 2;
    arr[2] = 3;
    arr[3] = 4;
    p = advance_and_set(&arr[0], 99);
    if (arr[2] != 99) return 0;
    if (*p != 99) return 0;
    if (arr[0] != 1) return 0;
    if (arr[3] != 4) return 0;
    return 1;
}
