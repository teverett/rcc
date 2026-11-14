// EXPECT: 16
int helper(int n) {
    return n * 2;
}

int calc(const int x) @nonrecursive {
    int arr[3];
    arr[0] = helper(x);
    const int y = x + 1;
    return y + arr[0];
}

int main() {
    return calc(5);
}
