// EXPECT: 35
// Comma-separated declarators sharing one base type/storage class, both
// at global and local scope (rcc previously only parsed one declarator
// per declaration statement - see docs/known-bugs.md's own entry for
// this before it was fixed).
unsigned int gRow, gCol = 10;

int main() {
    unsigned int row, col;
    row = 5;
    col = gCol;
    int a, b = 3, c = 7;
    return gRow + gCol + row + col + a + b + c;
}
