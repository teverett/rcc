#ifndef RCC_RT_SMS_H
#define RCC_RT_SMS_H

// Sega Master System runtime for rcc's Z80 backend (--target=z80 --base=0, --format=sms - see
// src/fileformat/smsrom/SmsRomFormat.h's own header comment for why no separate --target exists,
// the same "one ISA, several platform conventions" shape `zx spectrum`/`amstrad cpc` already use
// under plain `z80`).
//
// Every piece of hardware this file touches - the VDP, the SN76489 sound chip, and the two
// controller ports - is port-mapped, the same "stage through a global, `IN`/`OUT` immediately
// around it" shape src/rt/coleco/coleco.c uses (see that file's own header comment for why a C
// expression can't name a port directly). Port numbers and the VDP register-write/address-set
// sequence are cross-checked against nakardo's SMS/GG emulator-development port map and Charles
// MacDonald's own SMS VDP documentation (both mirrored on smspower.org) - the VDP's own
// register-write protocol (data byte, then register-number-ORed-with-$80) and
// address-set protocol (low byte, then high-byte-ORed-with-$40 for a write / plain high byte for
// a read) match the TMS9918-family sequence `src/rt/coleco/coleco.c` already uses verbatim, since
// the SMS VDP (Sega's own 315-5124/315-5246) is a documented superset of it.
//
// Deliberately not covered, same "racing the beam"/"no byte-identical ROM to verify against"
// reasons `docs/platforms.md`'s own `atari2600`/`zx spectrum` entries give: any kind of text/tile
// output. A cartridge here boots straight into user code with no BIOS-supplied font or
// pre-established VDP mode the way ColecoVision's own BIOS leaves one (see coleco.c's own header
// comment) - every VDP primitive below is a raw register/VRAM/address primitive a caller has to
// sequence correctly itself, same as `zx spectrum`'s/`amstrad cpc`'s own screen-memory
// primitives. Also not covered: the Pause button's NMI (vector $66) - this backend has no
// interrupt-vector/ISR support to hook it with - and anything PSG-tone-timing-related, for the
// same cycle-exact-loop reason `zx spectrum`'s beeper isn't covered either.

// --- VDP (315-5124, TMS9918-superset) ------------------------------------

// Writes one byte to the VDP's data port ($BE) at whatever address the VDP's own internal
// pointer currently holds (set by msx_vdp_set_write_address() - sms_vdp_set_write_address()
// below), which then auto-increments.
void sms_vdp_write_byte(unsigned char value);
// Reads one byte from the VDP's data port ($BE), from whatever address
// sms_vdp_set_read_address() last set.
unsigned char sms_vdp_read_byte();
// Writes one byte to the VDP's control port ($BF) - either half of a register-write or
// address-set sequence (see the functions below).
void sms_vdp_out_ctrl(unsigned char value);
// Reads the VDP's status register via its control port ($BF).
unsigned char sms_vdp_in_ctrl();

// Sets VDP register `reg` (0-10) to `value`, via the two-byte value-then-register(|$80) sequence
// every VDP register write needs.
void sms_vdp_write_reg(unsigned char reg, unsigned char value);
// Sets the VDP's internal address pointer to `addr` for a following sms_vdp_write_byte() (or
// several - the pointer auto-increments).
void sms_vdp_set_write_address(unsigned int addr);
// Same, for a following sms_vdp_read_byte().
void sms_vdp_set_read_address(unsigned int addr);

// --- Controllers -----------------------------------------------------------

// Reads controller port A ($DC), undecoded: bits 0-3 are joypad 1's up/down/left/right, bits 4-5
// are joypad 1's two buttons, bits 6-7 are joypad 2's up/down.
unsigned char sms_in_ctrl_a();
// Reads controller port B ($DD), undecoded: bits 0-1 are joypad 2's left/right, bits 2-3 are
// joypad 2's two buttons, the remaining bits are reset/region/misc status this file doesn't
// decode.
unsigned char sms_in_ctrl_b();

// --- Sound (SN76489) ---------------------------------------------------------

// Writes one byte directly to the SN76489's single write port ($7F).
void sms_out_sound(unsigned char value);

#endif
