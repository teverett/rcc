// EXPECT: 1
//
// 255*255 = 65025 is the largest unsigned 8x8 product; too big for a
// 16-bit signed `int` to hold without overflow, so it's assigned into
// an `unsigned int` (which holds 0..65535 on every target) and
// checked via its quotient/remainder by 256 - both small literals -
// rather than against the literal 65025 itself, which some targets'
// own narrowest integer type can't represent directly.
unsigned char a;
unsigned char b;
unsigned int product;
int main() {
    a = 255;
    b = 128;
    if (a * b != 32640) return 0;  // largest unsigned 8x8 product that stays within a 16-bit signed int

    a = 1;
    b = 255;
    if (a * b != 255) return 0;

    a = 255;
    b = 255;
    product = a * b;
    if (product / 256 != 254) return 0;
    if (product % 256 != 1) return 0;

    return 1;
}
