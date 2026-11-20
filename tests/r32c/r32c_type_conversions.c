// Conversions across every integer width this target has, both narrowing
// (with wraparound) and widening (sign/zero-extend), including the
// 2-word-`long` <-> 4-word-`long long` boundary genConvert() handles
// generically by word count (not specialized for R32C's `r2r0` fast path,
// since it never needs pair registers itself - see R32cCodegen.cpp's
// genConvert()).
// EXPECT: 5

int main() {
    unsigned char uc = (unsigned char)300;   // 300 mod 256 = 44
    unsigned short ush = (unsigned short)70000;  // 70000 mod 65536 = 4464

    long long llbig = 5000000000LL;    // exceeds 32 bits, needs 'long long'
    long truncated = (long)llbig;      // low 32 bits = 705032704

    long src = 1000000L;
    long long widened = (long long)src;  // sign-extended, same value

    // `int` is 16 bits on this target (see R32cCodegen.h), so narrowing
    // `truncated` (705032704) down to `int` keeps only its low 16 bits:
    // 705032704 mod 65536 = 61952, which as a signed 16-bit value is -3584.
    int narrowedBack = (int)truncated;

    int ok = 0;
    if (uc == 44) ok++;
    if (ush == 4464) ok++;
    if (truncated == 705032704L) ok++;
    if (widened == 1000000LL) ok++;
    if (narrowedBack == -3584) ok++;
    return ok;
}
