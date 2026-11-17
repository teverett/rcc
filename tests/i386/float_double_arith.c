// EXPECT: 1
//
// Real x87 hardware floating point (I386Codegen.cpp's genFloatExpr()/genFloatBinary()):
// +,-,*,/ on both `float` and `double`, global and local scalars, a float/double array
// element, comparisons, &&/||/! truthiness on a float condition, int<->float/double
// conversion in both directions (including an unsigned source whose top bit is set, where
// a naive 32-bit FILD would misread the value as negative), and a float/double argument
// and return value through a real function call.
double gd = 2.5;
float gf = 1.25f;
float farr[3] = {1.5f, 2.5f, 3.5f};

double dmul(double a, double b) { return a * b; }
float fadd(float a, float b) { return a + b; }

int main() {
    double d = gd + 1.5;        // 4.0
    if (d != 4.0) return 0;

    float f = gf * 2.0f;        // 2.5
    if (f != 2.5f) return 0;

    double sub = 10.0 - 3.25;   // 6.75
    if (sub != 6.75) return 0;

    double div = 9.0 / 2.0;     // 4.5
    if (div != 4.5) return 0;

    float sum = farr[0] + farr[1] + farr[2];  // 7.5
    if ((int)sum != 7) return 0;

    if (!(d > 3.0 && f < 3.0)) return 0;
    if (d < 0.0 || !(f > 0.0)) return 0;
    if (!(!(d == 0.0))) return 0;

    double called = dmul(d, 2.0);  // 8.0
    if (called != 8.0) return 0;

    float fcalled = fadd(f, gf);  // 2.5 + 1.25 = 3.75
    if (fcalled != 3.75f) return 0;

    int i = 41;
    double fromInt = (double)i + 0.5;  // 41.5
    if ((int)fromInt != 41) return 0;  // truncates toward zero, not round-to-nearest

    unsigned int u = 4000000000u;
    double fromUnsigned = (double)u;
    if (!(fromUnsigned > 3999999999.0)) return 0;

    double neg = -gd;  // -2.5
    if (neg != -2.5) return 0;

    return 1;
}
