// EXPECT: 15
// A '(void)'-declared function alongside ordinary parameterized ones,
// and a K&R-style empty '()' declaration of the same function - both
// forms of "no parameters" must keep working side by side.
int add(int a, int b);
int zero();

int add(int a, int b) {
    return a + b;
}

int zero() {
    return 0;
}

int main(void) {
    return add(7, 8) + zero();
}
