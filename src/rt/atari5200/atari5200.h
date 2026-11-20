#ifndef RCC_RT_ATARI5200_H
#define RCC_RT_ATARI5200_H

// Atari 5200 runtime library for rcc's 6502 backend (--target=5200).
//
// Unlike src/rt/atari400/ (whose library is a thin wrapper around the Atari OS's own CIO
// handler), the 5200 has no OS device drivers to call into at all - a cartridge talks straight
// to the hardware's own memory-mapped registers (GTIA/ANTIC/POKEY), the same way src/rt/
// ti994a/'s VDP access does. Every function here is a direct read/write of one of those
// registers through an ordinary C pointer, no `asm { ... }` block needed - see
// Atari5200RomFormat.h's own header comment for the cartridge/memory-map facts this is built
// on, and M6502Codegen.h's own `Atari5200` comment for why `--target=5200` needs its own
// zero-page/frame-storage layout.
//
// Scope and limitations (verified against the 5200 BIOS's own disassembly, not just inferred):
//  - The BIOS enables VBLANK/DLI NMIs (sets ANTIC's NMIEN, $D40E - ANTIC keeps the same address
//    on both the 400/800 and the 5200, unlike GTIA/POKEY which move) before jumping to the
//    cartridge, and already has its own default NMI handler chain (reached
//    through RAM vectors at $0200-$0206) that just restarts the controller potentiometer scan
//    and returns - this library never touches NMIEN or any BIOS vector, so that default
//    handling keeps running untouched. It also never calls into the BIOS's keyboard-IRQ
//    handling, so keypad input (the controller's 0-9/*/# matrix, and its START/PAUSE/RESET
//    buttons) isn't supported - only the analog joystick pots and the single bottom fire
//    button are.
//  - No text/graphics display: ANTIC's own display list is whatever the BIOS's splash screen
//    left behind (this library never builds a display list of its own), so there's no
//    "putc"-equivalent here - only hardware register access (color, sound, controller input).
//  - Reading the analog joystick position needs a fresh POKEY POT scan (`atari5200_potgo()`),
//    which takes most of a video frame to complete; `atari5200_pot()` just returns whatever
//    the most recent scan last measured; it's the caller's job to call `atari5200_potgo()`
//    on a steady cadence (e.g. once per frame, however the program measures that) rather than
//    assume one value is always fresh. This library has no VBI hook to drive that for the
//    caller (see the NMI note above).

// Sets one of GTIA's 9 color registers: `index` 0-3 selects a player/missile color (COLPM0-3),
// 4-7 a playfield color (COLPF0-3), 8 the background color (COLBK). Any other index is ignored.
void atari5200_setcolor(unsigned char index, unsigned char value);

// Returns nonzero if controller `stick`'s (0-3) single fire button is currently held down
// (GTIA's TRIG0-3, active-low in hardware - already inverted here so the caller sees "pressed"
// as true).
unsigned char atari5200_trig(unsigned char stick);

// Sets channel `channel`'s (0-3) frequency divisor and distortion/volume nibble (POKEY's
// AUDFn/AUDCn, standard square-wave synthesis - see POKEY's own register map).
void atari5200_sound(unsigned char channel, unsigned char freq, unsigned char distvol);

// Silences every one of POKEY's four sound channels.
void atari5200_soundoff();

// Restarts POKEY's 8-channel potentiometer scan (writes POTGO). Doesn't block - see this
// header's own scope note on why a caller must pace calls to this itself.
void atari5200_potgo();

// Returns potentiometer `stick`'s (0-7; each analog joystick uses two, one per axis) value as
// of the most recently completed scan - not necessarily this one, if `atari5200_potgo()` was
// just called (see this header's own scope note).
unsigned char atari5200_pot(unsigned char stick);

// Zeroes every POKEY audio register (equivalent to `atari5200_soundoff()`) and AUDCTL, for a
// deterministic, silent starting state - POKEY's own state isn't otherwise guaranteed clean
// when the cartridge's entry point is reached.
void atari5200_init();

#endif
