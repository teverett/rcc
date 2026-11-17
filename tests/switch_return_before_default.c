// EXPECT: 9
//
// Same bug as switch_return_first_case.c, but with `default:` written
// first in the source so its body (ending in `return`) is the first
// statement in the switch's flat lowered block - the labels/bodies for
// the `case`s written after it in source must still survive dce.
int category(int x) {
    switch (x) {
        default: return -1;
        case 5: return 9;
        case 6: return 10;
    }
}

int main() {
    return category(5);
}
