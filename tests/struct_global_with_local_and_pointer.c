// EXPECT: 19
// A local struct, a pointer-to-struct, and a global struct together
// in one function - the exact combination that exposed MCS51's
// former 2-byte-global-struct-sizing bug (see
// docs/known-bugs.md's former entry on this; each of these three on
// its own, or any two without the global struct, happened not to
// corrupt anything observable).
struct Point {
    int x;
    int y;
};

struct Point g_origin;

int main() {
    int result;
    struct Point pt;
    struct Point *pp;

    result = 0;

    pt.x = 3;
    pt.y = 4;
    result += pt.x * pt.y;   /* 12 */
    pp = &pt;
    pp->x = pp->x + 1;
    result += pp->x;          /* 4 */

    g_origin.x = 1;
    g_origin.y = 2;
    result += g_origin.x + g_origin.y; /* 3 */

    return result;  /* 19 */
}
