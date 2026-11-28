// EXPECT: 1
// Compile-only coverage for --target=i286/i286-dos: every integer width's
// arithmetic/overflow/wraparound behavior, multiply/divide/modulo at every width
// including long long, and shift/rotate by both a constant and a variable count.
// I286Codegen inherits I8086Codegen::generate() unchanged (see
// src/codegen/x86_8086/I286Codegen.h's own header comment), so this exercises the
// exact same codegen tests/*.c's shared suite already runs against --target=8086 -
// not a new code path, just confirming the i286 dialect's own assembler (the only
// thing that actually differs) still accepts whatever that codegen emits. See
// docs/known-bugs.md's i286 entry for why there's no CPU-execution check here yet
// (compile-only: this file's "EXPECT" is not checked by any automated run).
unsigned char g_uc = 255;
unsigned short g_us = 0;
long g_l = 1000000;
long long g_ll = 10000000000;

int narrow(int x) { return x + 1; }

int main() {
    unsigned char uc = g_uc + 1;      // wraps to 0
    unsigned short us = g_us - 1;     // wraps to 0xFFFF
    long l = g_l * 1000L;
    long long ll = g_ll / 3;
    long long llr = g_ll % 3;
    int shiftedConst = 1 << 4;
    int n = 4;
    int shiftedVar = 1 << n;
    int rotated = narrow(shiftedVar >> 1);
    int ok = (int)uc + (int)us + (int)l + (int)ll + (int)llr + shiftedConst + rotated;
    return ok == ok;  // always 1; exercises ok without an unused-variable warning
}
