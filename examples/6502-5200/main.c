#include "../../src/rt/atari5200/atari5200.h"

// The 5200 has no OS/CIO call to print text through (see atari5200.h's own header comment) -
// the de facto "hello world" here is the same kind of "it's alive" demo examples/6502-
// atari2600/ uses: set a background color so something visible changes, plus exercise the
// library's controller/sound primitives so there's more than one register poke to verify.
int main() {
    atari5200_init();
    atari5200_setcolor(8, 0x3A);  // COLBK - a bright, easily-recognized background color
    atari5200_sound(0, 60, 0xAF);  // channel 0: a fixed tone, full volume
    atari5200_potgo();
    return 0;
}
