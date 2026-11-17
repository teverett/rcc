// EXPECT: 42
//
// Regression test: a switch's own first case ending in `return` must not
// let dead-code elimination delete the later cases' labels along with it.
// IRBuilder.cpp's buildSwitch() lowers a switch to one flat block of
// comparisons dispatched by `goto` into `L1: B1  L2: B2  ... Lend:` - a
// Label anywhere after that first `return` is a real jump target, not
// unreachable code, even though it textually follows a `return`.
int classify(int x) {
    switch (x) {
        case 0: return 1;
        case 1: return 42;
        case 2: return 3;
        default: return -1;
    }
}

int main() {
    return classify(1);
}
