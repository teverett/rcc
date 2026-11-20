// EXPECT: 1
//
// An `unsigned char * unsigned char` multiply fits entirely within
// one target's narrowest hardware multiply (8x8->16), so every one of
// these products stays well under any platform's signed int range.
unsigned char a;
unsigned char b;
int main() {
    a = 200;
    b = 3;
    if (a * b != 600) return 0;

    a = 0;
    b = 255;
    if (a * b != 0) return 0;

    a = 17;
    b = 19;
    if (a * b != 323) return 0;

    return 1;
}
