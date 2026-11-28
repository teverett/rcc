// EXPECT: 1
// `cpuid` (leaf 1, EDX bit 0) reports whether the CPU has an on-chip FPU - true on every
// real or emulated Pentium-class chip, so this is a deterministic way to confirm the
// instruction actually executed and returned real feature-flag state in EDX, not just that
// it assembled without trapping.
int has_fpu;

int main() {
    asm {
        mov eax, 1
        cpuid
        mov eax, edx
        and eax, 1
        mov [g_has_fpu], eax
    }
    return has_fpu;
}
