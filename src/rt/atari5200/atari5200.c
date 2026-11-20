#include "atari5200.h"

// GTIA (5200: $C000-$C0FF, same chip as the 400/800's $D000-$D0FF - see atari5200.h's own
// header comment).
#define GTIA_TRIG0 0xC010u
#define GTIA_COLPM0 0xC012u
#define GTIA_COLPF0 0xC016u
#define GTIA_COLBK *(unsigned char *)0xC01Au

// POKEY (5200: $E800-$E8FF, $D200-$D2FF on the 400/800). AUDF1/AUDC1/.../AUDF4/AUDC4 occupy
// POKEY_BASE+0..7 in that order; the same 8 addresses double as the read-side POT0-7 pot
// registers.
#define POKEY_BASE 0xE800u
#define POKEY_AUDCTL *(unsigned char *)0xE808u
#define POKEY_POTGO *(unsigned char *)0xE80Bu

void atari5200_setcolor(unsigned char index, unsigned char value) {
    if (index < 4) {
        *(unsigned char *)(GTIA_COLPM0 + index) = value;
    } else if (index < 8) {
        *(unsigned char *)(GTIA_COLPF0 + index - 4) = value;
    } else if (index == 8) {
        GTIA_COLBK = value;
    }
}

unsigned char atari5200_trig(unsigned char stick) {
    return (*(unsigned char *)(GTIA_TRIG0 + stick) & 1) == 0;
}

void atari5200_sound(unsigned char channel, unsigned char freq, unsigned char distvol) {
    *(unsigned char *)(POKEY_BASE + channel * 2) = freq;
    *(unsigned char *)(POKEY_BASE + channel * 2 + 1) = distvol;
}

void atari5200_soundoff() {
    unsigned char i;
    i = 0;
    while (i < 4) {
        atari5200_sound(i, 0, 0);
        i = i + 1;
    }
}

void atari5200_potgo() {
    POKEY_POTGO = 0;
}

unsigned char atari5200_pot(unsigned char stick) {
    return *(unsigned char *)(POKEY_BASE + stick);
}

void atari5200_init() {
    atari5200_soundoff();
    POKEY_AUDCTL = 0;
}
