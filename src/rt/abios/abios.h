#ifndef RCC_RT_ABIOS_H
#define RCC_RT_ABIOS_H

// Advanced BIOS (ABIOS) client library for rcc's `--target=8086-dos` backend, built
// against IBM's own "PS/2 and PC BIOS Interface Technical Reference" (May 1988), Advanced
// BIOS part - the real protected-mode-aware interface IBM PS/2 Model 50/60/70/80 firmware
// exposed, and the one OS/2 used, as distinct from the conventional `INT 10h`/`13h`/`16h`
// BIOS `src/rt/pcbios/` wraps. See `docs/platforms.md`'s own `8086-dos` entry for why no
// emulator available to this project (DOSBox-X, QEMU) implements real ABIOS: every
// function here is built strictly to the manual's documented field layouts and transfer
// conventions, verified by compiling/linking/disassembling the output bytes, but never by
// running against real or emulated ABIOS firmware. Treat it as a faithful, inspectable
// client skeleton, not as hardware-verified code.
//
// ABIOS's own initialization protocol (Section 3 of the manual) requires the caller to:
// build the System Parameters Table (`INT 15h`/`04h`), build the Initialization Table
// (`INT 15h`/`05h`), then allocate and build its own Common Data Area, Device Blocks, and
// Function Transfer Tables from that information, before any device request can be made.
// This is real-mode-only initialization (the manual's own words): this library never
// builds the protected-mode tables a bimodal/protected-mode OS would also need.
//
// Only a small, fixed capacity is supported, since this C subset has no dynamic
// allocation: at most ABIOS_MAX_LOGICAL_IDS Logical IDs total (every device ABIOS
// reports, not just the ones this library initializes - Logical ID numbering is
// positional across every Initialization Table entry, so the Common Data Area has to
// reserve a slot for each one even if left null) and at most
// ABIOS_MAX_INIT_ENTRIES Initialization Table entries. If a real system reports more
// than either, abios_init() fails rather than silently truncating.
//
// This library only initializes two devices beyond the mandatory one: ABIOS Internal
// Calls (Device ID 0, always required - see the manual's own "Initialize Pointers"
// section), Keyboard (Device ID 4), and Video (Device ID 3). Every other Device ID
// (Diskette, Disk, Parallel, Asynchronous Communications, System Timer, Real-Time Clock,
// System Services, NMI, Pointing Device, NVRAM, DMA, POS, Keyboard Security) is left
// uninitialized (a null Common Data Area entry), which the manual documents as always
// valid - ABIOS only requires initializing the devices an operating system actually wants
// operational.
//
// Only the one function every ABIOS device supports identically - Function 01h, Return
// Logical ID Parameters, a standard single-staged function - is wrapped here
// (abios_ping_keyboard()/abios_ping_video()). Every other device-specific function
// (Keyboard's Continuous Read, Video's Reset/Initialize, etc.) is either itself
// multistaged (needs the caller to field a real hardware interrupt through the Function
// Transfer Table's own Interrupt Routine, which this library does not hook - see this
// header's own closing comment) or was simply out of scope for this pass; Return Logical
// ID Parameters alone is enough to prove the full chain - System Parameters Table,
// Initialization Table, Common Data Area, Device Block/Function Transfer Table
// allocation, and the ABIOS Transfer Convention's own far-call dispatch - is wired
// together correctly end to end.
//
// Real-mode addressing note: ABIOS requires its Common Data Area Anchor Pointer to be a
// segment value whose offset-0 address is the CDA's own true start address - but a `.com`
// program has only one segment (DS = CS = ES = SS), and that segment's own offset 0 is
// the Program Segment Prefix, not this library's data. abios_init() instead computes a
// synthetic segment value at runtime: `CS + (byte_offset_of(abios_cda) >> 4)`, rounded up
// to the next 16-byte (paragraph) boundary via a 15-byte pad built into abios_cda's own
// storage, so the computed segment's offset 0 always lands exactly on (or within that
// pad, immediately before) the CDA's real start - never on unrelated preceding data. The
// same trick is used for every other structure (Device Blocks, Function Transfer Tables,
// Request Blocks) a far pointer has to name.

// Return codes abios_init() itself can return (every device-specific function below has
// its own, separate Return Code value - see each one's own comment).
#define ABIOS_INIT_OK 0
// INT 15h/04h or /05h returned a carry (exception error) or AH != 0 - ABIOS isn't
// present on this machine, or this emulator's BIOS doesn't implement it (expected under
// DOSBox-X/QEMU - see this header's own opening comment).
#define ABIOS_INIT_NOT_PRESENT 1
// The real System Parameters Table reported more Initialization Table entries, or an
// Initialization Table entry reported a larger Device Block/Function Transfer Table than
// this library's fixed capacity supports (see the opening comment's "ABIOS_MAX_*"
// constants).
#define ABIOS_INIT_TOO_LARGE 2
// Device ID 0 (ABIOS Internal Calls) was not found in the Initialization Table - every
// real ABIOS implementation must report it (the manual's own requirement), so its
// absence means something else is already wrong.
#define ABIOS_INIT_NO_INTERNAL 3

// Performs the full real-mode initialization sequence (System Parameters Table,
// Initialization Table, Common Data Area, Device Blocks/Function Transfer Tables for
// Device ID 0/3/4 only) described in this header's own opening comment. Must be called
// exactly once, before any other function in this library. Returns one of the
// ABIOS_INIT_* codes above.
int abios_init();

// Whether abios_init() found and initialized the Keyboard (Device ID 4) / Video
// (Device ID 3) Logical ID. abios_ping_keyboard()/abios_ping_video() are only meaningful
// when the corresponding one of these returns nonzero.
unsigned char abios_keyboard_available();
unsigned char abios_video_available();

// Issues Function 01h (Return Logical ID Parameters) to the Keyboard/Video Logical ID
// via the ABIOS Transfer Convention (a real far call through the Common Start Routine -
// see this header's own opening comment on why this is the one function exercised).
// Returns the Return Code field (0 = Operation Completed Successfully; see the manual's
// own Figure 4-3 for every other value - 0xC000 "Invalid Logical ID" is what a
// not-actually-present ABIOS or an unimplemented BIOS call under an emulator will leave
// behind, since the Start Routine pointer itself then points at uninitialized/garbage
// memory). On success, abios_ping_device_id()/abios_ping_secondary_device_id()/
// abios_ping_revision() decode the reply (same "one call, several decode-only accessors"
// shape `src/rt/pcbios/pcbios.h`'s own pcbios_disk_get_params() uses).
unsigned char abios_ping_keyboard();
unsigned char abios_ping_video();
int abios_ping_device_id();
unsigned char abios_ping_secondary_device_id();
unsigned char abios_ping_revision();

// Deliberately not provided:
// - Any device-specific function beyond Return Logical ID Parameters (Keyboard's
//   Continuous Read, Video's Reset/Initialize, Read Device Parameters, palette/font
//   functions, etc.) - seethis header's own opening comment.
// - Hooking the hardware interrupt vector (`INT 09h`/IRQ1 for keyboard, etc.) that every
//   multistaged ABIOS function needs its own Interrupt Routine driven from - this would
//   mean replacing/chaining a real interrupt vector, which this library doesn't attempt
//   with no way to verify the result against real or emulated ABIOS hardware at all.
// - Building the protected-mode Common Data Area/Function Transfer Tables a bimodal or
//   protected-mode-only operating system needs (the manual's own Section 3, "Build
//   Protected Mode Tables") - this library is real-mode-only.
// - Any Device ID besides 0 (ABIOS Internal Calls), 3 (Video), and 4 (Keyboard).

#endif
