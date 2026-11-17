// Exercises the reference devices directly (docs/SPEC.md section 5) via asm{} blocks, the same
// way src/rt/vex/vex.c itself does: writes a byte to stdout (port 0x18) and stderr (port 0x19),
// then returns a value computed from ordinary C so the shared harness still checks something
// beyond "it didn't fault".
// EXPECT: 142

int vex_scratch;

void device_putc(unsigned char c) {
    vex_scratch = c;
    asm {
        ldi r0, G_vex_scratch
        ld r0, r0
        ldi r1, 0x18
        deo r1, r0
    }
}

int main() {
    int a;
    int b;
    device_putc('V');
    device_putc(10);  // '\n'
    a = 70;
    b = 72;
    return a + b;
}
