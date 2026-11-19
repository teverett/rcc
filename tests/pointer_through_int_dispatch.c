// EXPECT: 1
//
// Two more "plain int standing in for an address" idioms (see
// string_literal.c/pointers.c/test_func_ptr_array_loop.c for the
// convention this subset allows): a dispatch table indexed and then
// called, and a plain `int` reassigned to walk across array elements
// via ordinary integer addition, dereferenced and compound-assigned
// through. On a target where a runtime-computed address must be
// re-canonicalized before use (e.g. riscv64 zero-extending it back from
// a narrower-than-pointer-width int's own sign-extended load), both of
// these must still resolve to the correct underlying storage.
int arr[3];
int f0() { return 7; }
int f1() { return 8; }
int table[2];

int main() {
    int p;
    table[0] = &f0;
    table[1] = &f1;
    if ((table[1])() != 8) return 0;

    arr[0] = 1;
    arr[1] = 2;
    arr[2] = 3;
    p = &arr[0];
    p = p + 8;   // unscaled byte addition (p is plain int, not int*) - arr[2] sits 2*4 bytes on

    *p += 100;   // arr[2] = 103
    if (arr[2] != 103) return 0;
    if (p[0] != 103) return 0;
    return 1;
}
