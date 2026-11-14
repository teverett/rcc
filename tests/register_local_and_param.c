// EXPECT: 36
int add(register int a, register const int b) {
    register int sum = a + b;
    return sum;
}

int main() {
    register int x = 10;
    register int y = 20;
    return add(x, y) + 6;
}
