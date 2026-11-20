#include "gamegear.h"

// Staging cell for a port I/O instruction's own byte - see gamegear.h's/sms.h's own header
// comment for why this can't just be a local/parameter.
unsigned char rt_a;

void gg_vdp_write_byte(unsigned char value) {
    rt_a = value;
    asm {
        ld a, [g_rt_a]
        out [190], a  ; VDP data port, $BE
    }
}

unsigned char gg_vdp_read_byte() {
    asm {
        in a, [190]  ; VDP data port, $BE
        ld [g_rt_a], a
    }
    return rt_a;
}

void gg_vdp_out_ctrl(unsigned char value) {
    rt_a = value;
    asm {
        ld a, [g_rt_a]
        out [191], a  ; VDP control/status port, $BF
    }
}

unsigned char gg_vdp_in_ctrl() {
    asm {
        in a, [191]  ; VDP control/status port, $BF
        ld [g_rt_a], a
    }
    return rt_a;
}

void gg_vdp_write_reg(unsigned char reg, unsigned char value) {
    gg_vdp_out_ctrl(value);
    gg_vdp_out_ctrl(reg | 128);
}

void gg_vdp_set_write_address(unsigned int addr) {
    gg_vdp_out_ctrl(addr & 255);
    gg_vdp_out_ctrl(((addr >> 8) & 63) | 64);
}

void gg_vdp_set_read_address(unsigned int addr) {
    gg_vdp_out_ctrl(addr & 255);
    gg_vdp_out_ctrl((addr >> 8) & 63);
}

unsigned char gg_in_ctrl_a() {
    asm {
        in a, [220]  ; $DC - controller port A
        ld [g_rt_a], a
    }
    return rt_a;
}

unsigned char gg_in_ctrl_b() {
    asm {
        in a, [221]  ; $DD - controller port B
        ld [g_rt_a], a
    }
    return rt_a;
}

unsigned char gg_in_start() {
    asm {
        in a, [0]  ; $00 - Start button/region port
        ld [g_rt_a], a
    }
    return rt_a;
}

void gg_out_sound(unsigned char value) {
    rt_a = value;
    asm {
        ld a, [g_rt_a]
        out [127], a  ; $7F - SN76489 write port
    }
}
