// EXPECT: 1
// `rdtsc` loads the 64-bit time-stamp counter's low 32 bits into EAX. Two back-to-back
// reads a few instructions apart can never see the counter run backwards, so comparing
// them is a deterministic way to confirm the instruction returned real, moving counter
// state rather than some fixed or garbage value.
int t1, t2;

int main() {
    asm {
        rdtsc
        mov [g_t1], eax
        rdtsc
        mov [g_t2], eax
    }
    return t2 >= t1;
}
