#ifndef RCC_RT_GAMEGEAR_H
#define RCC_RT_GAMEGEAR_H

// Sega Game Gear runtime for rcc's Z80 backend (--target=z80 --base=0, --format=gg - see
// src/fileformat/smsrom/SmsRomFormat.h's own header comment). Port map, VDP register-write/
// address-set sequences, and controller ports are identical to src/rt/sms/sms.c's own (nakardo's
// SMS/GG port-map document: "Game Gear port maps match SMS for the $3E/$3F and PSG ranges") -
// this file duplicates them rather than share, the same "each machine gets its own runtime, even
// when two overlap almost completely" precedent src/rt/vic20/ and src/rt/c64/ already set (see
// c64.h's own header comment for why). The one real difference covered here is the Start
// button, read through its own dedicated port ($00) rather than either controller port - the
// Game Gear has no separate Pause button/NMI the way the SMS does, and this backend has no
// interrupt-vector/ISR support to hook one with regardless (see sms.h's own header comment).
//
// Not covered, for the same reasons sms.h's own header comment gives: any text/tile VDP output
// (no BIOS-supplied font/established mode to rely on), and PSG tone timing.

// --- VDP (315-5246, TMS9918-superset) --------------------------------------

void gg_vdp_write_byte(unsigned char value);
unsigned char gg_vdp_read_byte();
void gg_vdp_out_ctrl(unsigned char value);
unsigned char gg_vdp_in_ctrl();
void gg_vdp_write_reg(unsigned char reg, unsigned char value);
void gg_vdp_set_write_address(unsigned int addr);
void gg_vdp_set_read_address(unsigned int addr);

// --- Controllers -----------------------------------------------------------

unsigned char gg_in_ctrl_a();
unsigned char gg_in_ctrl_b();

// Reads the Start button's own dedicated port ($00). Bit 7 is Start (0 = pressed, 1 = released);
// the remaining bits are region/console-identification status this file doesn't decode.
unsigned char gg_in_start();

// --- Sound (SN76489, stereo-panned) -----------------------------------------

// Writes one byte directly to the SN76489's single write port ($7F) - same chip/port as the
// SMS's own (sms_out_sound()); the Game Gear's stereo left/right panning register ($06) isn't
// covered here, since no source consulted during this port gave its exact bit layout.
void gg_out_sound(unsigned char value);

#endif
