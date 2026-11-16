// EXPECT: 462
// Exercises the CP1610 backend's addressing-mode substitutes for real indexed/displacement
// addressing, which this CPU doesn't have at all (CP1610Codegen.h's own header comment):
// signed/unsigned `char` globals, locals, arrays and pointers (every storage cell here is a
// full 16-bit word - no byte addressing exists either - so a `char`'s own 8-bit range is
// re-derived on every load/store via CP1610Codegen.cpp's own narrowR0To()), each of which
// crosses the word<->byte boundary the real Z8000/PDP-11 backends get for free from hardware.
char g_schar;
unsigned char g_uchar;
char g_arr[4];

int main() {
    char local_s;
    unsigned char local_u;
    char *p;
    int sum;

    g_schar = -5;
    g_uchar = 250;
    local_s = -1;
    local_u = 200;

    g_arr[0] = 10;
    g_arr[1] = -20;
    g_arr[2] = 30;
    g_arr[3] = -3;

    p = &g_arr[1];
    *p = *p + 1;  // g_arr[1] becomes -19

    sum = (int)g_schar + (int)g_uchar + (int)local_s + (int)local_u;
    sum = sum + g_arr[0] + g_arr[1] + g_arr[2] + g_arr[3];
    return sum;
}
