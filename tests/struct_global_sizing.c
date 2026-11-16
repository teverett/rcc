// EXPECT: 33
// A 3-field global struct (6 bytes) is wider than the fixed 2 bytes
// a non-array scalar global gets - `sentinel`, declared right after
// it, previously landed inside what should have been the struct's
// own storage (see MCS51Codegen.cpp's declareGlobalStruct()/
// declareGlobalTemp() and docs/known-bugs.md's former entry on this).
// Writing every field, then checking `sentinel` is still exactly what
// it was set to, catches that aliasing directly.
struct Point3 {
    int x;
    int y;
    int z;
};

struct Point3 g;
int sentinel;

int main() {
    sentinel = 1000;
    g.x = 10;
    g.y = 11;
    g.z = 12;
    if (sentinel != 1000) return -1;
    return g.x + g.y + g.z;
}
