// EXPECT: 42
// ISO C '(void)' parameter lists - see docs/known-bugs.md's entry for
// this before it was fixed (only K&R-style empty '()' used to parse).
void setValue(void);
int gVal;

void setValue(void) {
    gVal = 42;
}

int main(void) {
    setValue();
    return gVal;
}
