#include "../../src/rt/sms/sms.h"

// There's no BIOS-supplied font or established VDP mode a cartridge here can rely on (see
// sms.h's own header comment), so this is the closest thing to a "hello world" that's actually
// honest about that limit - the same approach ../6502-atari2600/'s own example takes: poke a
// recognizable, byte-exact pattern straight into VRAM and the sound chip, verified by CI as
// exactly that, not as readable text.
int main() {
    sms_vdp_set_write_address(0);
    sms_vdp_write_byte(1);
    sms_vdp_write_byte(2);
    sms_vdp_write_byte(3);
    sms_vdp_write_byte(4);
    sms_out_sound(159);
    return 0;
}
