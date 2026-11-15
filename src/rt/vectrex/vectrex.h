#ifndef RCC_RT_VECTREX_H
#define RCC_RT_VECTREX_H

// GCE Vectrex runtime for rcc's 6809 backend (--target=6809-vectrex).
//
// UNLIKE src/rt/coco/coco.h (text VRAM the backend's own pointer types can poke directly), the
// Vectrex has no video RAM at all - the display is an analog vector CRT the BIOS drives through
// the DAC/multiplexer in real time, so there is no memory-mapped way to draw anything. Every
// function here is a thin inline-`asm` wrapper (same idiom as src/rt/coleco/coleco.c's own `asm {
// }` blocks) around one fixed BIOS subroutine address in the console's own $E000-$FFFF ROM - this
// file does not and cannot include that ROM, so these addresses/calling conventions are taken from
// third-party community documentation, not a byte-identical reference dump. No official GCE
// technical reference is bundled with this project to confirm them - same best-effort caveat as
// src/rt/trs80/'s own header comment - cross-checked against two independent sources where
// possible (noted per function below).
//
// Scope cut, not a bug - see the project's final porting report for what's missing and why:
//   - No text printing (Print_Str_* family): the BIOS's own vector font uses a character
//     encoding this project has no verified table for.
//   - No sound (the AY-3-8912 PSG init/play routines): needs register-level PSG documentation
//     beyond what was verified while porting this.
//   - No low-level timed vector-list drawing (Mov_Draw_VLc_b/VLcs, i.e. real line segments with
//     a duration/intensity-ramp) - only the single-dot primitive below, whose calling convention
//     is simpler and verified from two sources. A game can still draw lines itself, one dot at a
//     time, via repeated vectrex_dot() calls.
//
// Every program must call vectrex_wait_recal() once per frame, before drawing anything, to
// resynchronize to the display's own ~50Hz refresh and recenter the beam - the same requirement
// real Vectrex BIOS documentation describes (see "Timing, Vectors" in the Vectrex programming
// tutorials this was ported from).

// Blocks until the next ~50Hz frame boundary and recenters the beam (jsr $F192) - addresses
// confirmed by two independent sources under the name "Wait_Recal". Call this once per frame
// before any drawing.
void vectrex_wait_recal();

// Re-zeroes the beam-position integrators (jsr $F354), i.e. explicitly recenters the beam to
// (0, 0) - documented as part of the "integrator zeroing" routine group.
void vectrex_reset0ref();

// Sets the beam intensity (brightness) for everything drawn after this call (jsr $F2AB, A =
// `level`) - 0 is off, higher values are brighter; the exact usable range is BIOS-version
// dependent.
void vectrex_intensity(signed char level);

// Moves the beam to (x, y) without drawing anything (jsr $F312, A = y, B = x) - signed
// coordinates relative to screen center, same convention vectrex_dot() uses.
void vectrex_moveto(signed char x, signed char y);

// Moves the beam to (x, y) and plots a single dot there (jsr $F2C3, A = y, B = x) - address and
// name ("Dot_d") confirmed by two independent sources. This is the only drawing primitive this
// runtime exposes; see this header's own scope-cut note for line drawing.
void vectrex_dot(signed char x, signed char y);

// Returns a pseudo-random 16-bit value from the BIOS's own generator (jsr $F517) - address
// confirmed by two independent sources under the name "Random".
int vectrex_random();

// Reads the four controller-1 button states into the low 4 bits (jsr $F1F8) - best-effort: this
// routine's exact bit layout isn't independently confirmed, unlike the functions above.
unsigned char vectrex_read_buttons();

#endif
