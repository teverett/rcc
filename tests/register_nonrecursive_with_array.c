// EXPECT: 16
int helper(int n) {
    return n * 2;
}

int calc(register int x) @nonrecursive {
    int arr[3];
    arr[0] = helper(x);
    register int y = x + 1;
    y = y + arr[0];
    return y;
}

int main() {
    return calc(5);
}
