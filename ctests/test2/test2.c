// EXPECT: 330
//
// Exercises the parts of rcc's supported C subset test1.c doesn't
// touch: pointers (address-of, dereference, pointer arithmetic,
// pointer comparison, ++/--, write-through), fixed-size arrays
// (indexing; `&arr[0]` since an array never decays to a pointer -
// see ../../docs/language.md), struct (file-scope, local and
// global instances, `.`/`->` field access, pointer-to-struct), enum,
// casts, sizeof(type)/sizeof(expr), and calling through a computed
// function address (`(expr)()`, including a literal dispatch-table
// array of function names - see ../../docs/language.md's own note
// that a call through a computed address takes no arguments).
//
// Avoids anything whose correct value depends on a type's actual
// per-target width (an absolute sizeof() value, or a cast whose
// result depends on exactly how many bits get truncated) so the
// same EXPECT holds on every target, 8-bit or 32-bit alike; the
// sizeof checks below only compare relative rank, which
// docs/language.md guarantees is non-decreasing on every target.
// Also avoids `long`/`long long`, same reason as test1.c.
//
// See this directory's Makefile for which targets are excluded and
// why (struct support gaps, not anything this file does wrong).

struct Point {
    int x;
    int y;
};

enum Color { RED, GREEN, BLUE };

struct Point g_origin;

int get_ten() { return 10; }
int get_twenty() { return 20; }

int table[2] = { get_ten, get_twenty };

int main() {
    int result;
    int arr[5];
    int i;
    int *p;
    int *q;
    struct Point pt;
    struct Point *pp;
    enum Color c;
    int fp;

    result = 0;

    /* arrays: fixed-size, indexed, never decay - &arr[0] gets the address */
    for (i = 0; i < 5; i++) {
        arr[i] = i * 10;
    }
    result += arr[0] + arr[1] + arr[2] + arr[3] + arr[4];  /* 100 -> 100 */

    /* pointers: &, *, pointer arithmetic, pointer comparison, write-through */
    p = &arr[0];
    result += *p;          /* arr[0]=0   -> 100 */
    p = p + 2;
    result += *p;          /* arr[2]=20  -> 120 */
    q = &arr[4];
    result += (p < q);     /* 1          -> 121 */
    *p = 99;
    result += arr[2];      /* write-through -> 99  -> 220 */

    /* pointer ++/-- */
    p++;
    result += *p;          /* arr[3]=30  -> 250 */
    p--;
    p--;
    result += *p;          /* arr[1]=10  -> 260 */

    /* struct: local instance, field math, pointer-to-struct */
    pt.x = 3;
    pt.y = 4;
    result += pt.x * pt.y; /* 12         -> 272 */
    pp = &pt;
    pp->x = pp->x + 1;     /* pt.x = 4 */
    result += pp->x;       /* 4          -> 276 */

    /* struct: global instance */
    g_origin.x = 1;
    g_origin.y = 2;
    result += g_origin.x + g_origin.y; /* 3 -> 279 */

    /* enum */
    c = GREEN;
    result += c;              /* GREEN=1 -> 280 */
    result += (c == GREEN);   /* 1       -> 281 */
    result += BLUE;           /* 2       -> 283 */

    /* casts: sign-extending a small, width-independent value */
    result += (int)(char)(-1);  /* -1    -> 282 */
    result += (int)(short)5;    /* 5     -> 287 */

    /* sizeof(type) and sizeof(expr): relative rank, never an absolute width */
    result += (sizeof(int) >= sizeof(short));    /* 1 -> 288 */
    result += (sizeof(short) >= sizeof(char));   /* 1 -> 289 */
    result += (sizeof(arr[0]) == sizeof(int));   /* 1 -> 290 */

    /* call through a computed function address - no arguments */
    fp = &get_ten;
    result += (fp)();          /* 10 -> 300 */
    result += (table[0])();    /* 10 -> 310 */
    result += (table[1])();    /* 20 -> 330 */

    return result;
}
