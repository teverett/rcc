// EXPECT: 1
//
// A `sizeof(int)`-scaled single-step walk of a plain `int` standing in for
// an address (see pointer_through_int_dispatch.c's own header comment for
// the convention): `p = p + sizeof(int)` must land on the *next* array
// element on every target, regardless of that target's own native `int`
// width (4 bytes on most, 2 on the 6502/8051/8080/z80 family, a single
// word on pdp1/pdp7) - unlike a hardcoded byte count, this expression's
// addressing unit always matches sizeof's own.
int arr[3];
int main() {
    int p;
    arr[0] = 10;
    arr[1] = 20;
    arr[2] = 30;
    p = &arr[0];
    p = p + sizeof(int);
    if (*p != 20) return 0;
    p = p + sizeof(int);
    if (*p != 30) return 0;
    return 1;
}
