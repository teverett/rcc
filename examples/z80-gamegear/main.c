#include "../../src/rt/gamegear/gamegear.h"

// See ../z80-sms/main.c's own comment - same "no font/established mode to rely on, so poke a
// byte-exact pattern instead" scope cut, via gamegear.h's own VDP/sound primitives.
int main() {
    gg_vdp_set_write_address(0);
    gg_vdp_write_byte(1);
    gg_vdp_write_byte(2);
    gg_vdp_write_byte(3);
    gg_vdp_write_byte(4);
    gg_out_sound(159);
    return 0;
}
