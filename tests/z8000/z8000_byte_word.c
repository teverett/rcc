// EXPECT: 462
// Exercises the Z8000 backend's byte/word register duality (Z8000Codegen.cpp's own `rl0`/`extsb`
// convention - see its header comment): signed and unsigned `char` loads/stores through globals,
// locals, arrays and pointers, each of which crosses the word<->byte boundary that `ldb`/`extsb`
// (not an auto-sign-extending `movb` the way the PDP-11 backend gets for free) has to handle by
// hand on this target.
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
