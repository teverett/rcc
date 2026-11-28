#include "abios.h"

#define ABIOS_MAX_INIT_ENTRIES 32
#define ABIOS_MAX_LOGICAL_IDS 48
#define ABIOS_DEVBLOCK_CAP 256
#define ABIOS_FTT_CAP 160

// Common Data Area byte layout (manual Figure 2-2): a 4-byte header (Offset to Data
// Pointer 0, Count of Logical IDs), 4 reserved bytes, then one 8-byte (Device Block
// pointer + Function Transfer Table pointer) entry per Logical ID slot, then a reserved
// data-pointer area - sized to comfortably hold the 3 data pointers (6 bytes each) plus
// trailing count word Logical ID 2's own init call always writes (Figure 3-7), with ample
// slack either side of "offset to data pointer 0" since the exact growth direction isn't
// load-bearing at this scale.
#define ABIOS_CDA_HEADER_BYTES 4
#define ABIOS_CDA_RESERVED_BYTES 4
#define ABIOS_CDA_LID_BYTES (ABIOS_MAX_LOGICAL_IDS * 8)
#define ABIOS_CDA_DATAPTR_BYTES 98
#define ABIOS_CDA_BYTES (ABIOS_CDA_HEADER_BYTES + ABIOS_CDA_RESERVED_BYTES + ABIOS_CDA_LID_BYTES + ABIOS_CDA_DATAPTR_BYTES)

// System Parameters Table (INT 15h/04h, manual Figure 3-3), as 16 words - every field is
// word-sized or a word-pair far pointer (offset, segment), so no byte-level packing is
// needed anywhere in this one.
int abios_sysparams[16];
// Initialization Table (INT 15h/05h, manual Figure 3-5): 12 words (0x18 bytes) per entry,
// ABIOS_MAX_INIT_ENTRIES entries - see abios_init() for the field-to-word-index mapping.
int abios_inittable[384];  // ABIOS_MAX_INIT_ENTRIES (32) * 12 words/entry - array sizes in
                           // this dialect must be a bare literal, not an expression

// The Common Data Area itself. A plain byte array, not a word array, because of
// abios_cda_skew below - every field access goes through abios_cda_word_set(), never
// direct indexing, so the skew is applied exactly once per access rather than needing to
// be baked into a compile-time index.
unsigned char abios_cda_raw[506];  // ABIOS_CDA_BYTES (490) + 16 - see abios_inittable's
                                   // own comment on why this can't just be the expression
// How many bytes into abios_cda_raw the Common Data Area's own true start (its own
// "offset 0", the byte ABIOS's Anchor Pointer segment resolves to) actually begins -
// computed once in abios_init(). ABIOS requires the Anchor Pointer to be a segment value
// alone, with offset 0 assumed; a `.com` program has only one segment whose own offset 0
// is the Program Segment Prefix, not this array, so this library instead computes a
// synthetic segment (abios_anchor_segment) whose offset-0 address lands within
// abios_cda_raw's own trailing 16-byte pad - never before it, never into unrelated
// preceding data - and abios_cda_skew records exactly how far into the array that is.
int abios_cda_skew;
// The synthetic Common Data Area Anchor Pointer segment (see abios_cda_skew's own
// comment) - this, not a plain `ds`, is what every far call below pushes as the anchor.
int abios_anchor_segment;

int abios_num_entries;
// -1 until abios_init() finds and initializes the corresponding device; see
// abios_keyboard_available()/abios_video_available().
int abios_keyboard_lid;
int abios_video_lid;

// Staging cells for an INT 15h/far-call's own argument/result words - same convention as
// every other runtime library here (`src/rt/pcbios/pcbios.c`'s own header comment covers
// why these can't just be locals).
int abios_tmp;
int abios_tmp2;
// A scratch far pointer (offset, segment) for `callf [mem]`'s own operand, when the
// target address is a runtime value (read from the Initialization Table or System
// Parameters Table) rather than a compile-time label.
int abios_farptr_scratch[2];

// Device Blocks and Function Transfer Tables for the 3 Logical IDs this library ever
// initializes (ABIOS Internal Calls, Keyboard, Video - see abios.h's own opening
// comment). Their contents are entirely ABIOS's own - this library only allocates the
// space and registers it in the Common Data Area; it never writes into these itself.
unsigned char abios_devblock_internal[ABIOS_DEVBLOCK_CAP];
unsigned char abios_devblock_keyboard[ABIOS_DEVBLOCK_CAP];
unsigned char abios_devblock_video[ABIOS_DEVBLOCK_CAP];
unsigned char abios_ftt_internal[ABIOS_FTT_CAP];
unsigned char abios_ftt_keyboard[ABIOS_FTT_CAP];
unsigned char abios_ftt_video[ABIOS_FTT_CAP];

// The one Request Block this library ever builds (manual Figure 4-2) - 16 words (0x20
// bytes), exactly the fixed length Function 01h (Return Logical ID Parameters) always
// uses. Reused sequentially (never concurrently) by abios_ping_keyboard()/
// abios_ping_video(). Word-index map: 0 Request Block Length, 1 Logical ID, 2 Unit,
// 3 Function, 4-5 Reserved, 6 Return Code, 7 Time-out, 8 Reserved, 9 Device ID,
// 10 Count of Units, 11 Logical ID flags, 12 Request Block Length (returned),
// 13 Secondary Device ID (low byte) + Revision (high byte), 14-15 Reserved.
int abios_rb[16];

// Writes one little-endian word at `byteOffset` from the Common Data Area's own true
// start (abios_cda_skew + byteOffset into abios_cda_raw) - see abios_cda_skew's own
// comment for why this indirection exists instead of plain array indexing.
void abios_cda_word_set(int byteOffset, int value) {
    int idx;
    idx = abios_cda_skew + byteOffset;
    abios_cda_raw[idx] = value & 255;
    abios_cda_raw[idx + 1] = (value / 256) & 255;
}

// Each of these returns its own buffer's byte offset - a compile-time constant in the
// linked program, but only reachable from C through a bare-label asm load (see
// `pcbios.c`'s own header comment on why: no local/parameter has a fixed address an asm
// block's literal text could name directly, but a global's label does).
int abios_off_devblock_internal() {
    asm {
        mov bx, g_abios_devblock_internal
        mov [g_abios_tmp], bx
    }
    return abios_tmp;
}
int abios_off_devblock_keyboard() {
    asm {
        mov bx, g_abios_devblock_keyboard
        mov [g_abios_tmp], bx
    }
    return abios_tmp;
}
int abios_off_devblock_video() {
    asm {
        mov bx, g_abios_devblock_video
        mov [g_abios_tmp], bx
    }
    return abios_tmp;
}
int abios_off_ftt_internal() {
    asm {
        mov bx, g_abios_ftt_internal
        mov [g_abios_tmp], bx
    }
    return abios_tmp;
}
int abios_off_ftt_keyboard() {
    asm {
        mov bx, g_abios_ftt_keyboard
        mov [g_abios_tmp], bx
    }
    return abios_tmp;
}
int abios_off_ftt_video() {
    asm {
        mov bx, g_abios_ftt_video
        mov [g_abios_tmp], bx
    }
    return abios_tmp;
}

// Registers Logical ID `lid`'s Device Block/Function Transfer Table pointers (both
// ordinary far pointers - offset then our own code segment, since only the Common Data
// Area's own Anchor Pointer needs abios_cda_skew's alignment trick) into the Common Data
// Area, then far-calls `initRoutineOff:initRoutineSeg` (the Initialization Table entry's
// own Initialize Device Block and Function Transfer Table Routine pointer, manual
// Figure 3-6) with (CX)=1, (DX)=lid, (DS)=the Anchor Pointer, to let ABIOS fill both in.
// Returns the routine's own exception condition byte (0 = success).
unsigned char abios_register_and_init(int lid, int devBlockOff, int fttOff, int initRoutineOff, int initRoutineSeg) {
    int cs;
    int base;
    asm {
        push cs
        pop ax
        mov [g_abios_tmp], ax
    }
    cs = abios_tmp;

    base = ABIOS_CDA_HEADER_BYTES + ABIOS_CDA_RESERVED_BYTES + lid * 8;
    abios_cda_word_set(base, devBlockOff);
    abios_cda_word_set(base + 2, cs);
    abios_cda_word_set(base + 4, fttOff);
    abios_cda_word_set(base + 6, cs);

    abios_farptr_scratch[0] = initRoutineOff;
    abios_farptr_scratch[1] = initRoutineSeg;
    abios_tmp2 = lid;

    asm {
        mov cx, 1
        mov dx, [g_abios_tmp2]
        mov ax, [g_abios_anchor_segment]
        mov ds, ax
        callf [g_abios_farptr_scratch]
        mov [g_abios_tmp], ax      ; al = exception condition; (ds) is left as the Anchor
                                   ; Pointer by the routine ("all registers except ax are
                                   ; restored" means restored to what we set, not to our
                                   ; own segment) - restore it before any further C runs
        push cs
        pop ds
    }
    return abios_tmp;
}

int abios_init() {
    int off;
    int cs;
    int rem;
    int i;
    int runningLid;
    int base;
    int deviceId;
    int numLids;
    int devBlockLen;
    int fttLen;
    int initOff;
    int initSeg;
    int foundInternal;
    unsigned char exception;

    abios_keyboard_lid = -1;
    abios_video_lid = -1;

    // Build System Parameters Table (INT 15h/04h). (DS) must be the segment of any
    // ABIOS RAM extensions, or 0 if none (confirmed against both the manual and Ralf
    // Brown's Interrupt List) - our own DS is saved/restored around the call since we
    // have none.
    asm {
        push ds
        mov di, g_abios_sysparams
        xor ax, ax
        mov ds, ax
        mov ax, 1024      ; ah = 4 (build system parameters table), al = 0
        int 0x15
        jc ABIOS_SP_FAIL
        mov ax, 0
        jmp ABIOS_SP_DONE
ABIOS_SP_FAIL:
        mov ax, 1
ABIOS_SP_DONE:
        pop ds
        mov [g_abios_tmp], ax
    }
    if (abios_tmp != 0) return ABIOS_INIT_NOT_PRESENT;

    abios_num_entries = abios_sysparams[15];
    if (abios_num_entries <= 0) return ABIOS_INIT_NOT_PRESENT;
    if (abios_num_entries > ABIOS_MAX_INIT_ENTRIES) return ABIOS_INIT_TOO_LARGE;

    // Build Initialization Table (INT 15h/05h) - same (DS) convention as above.
    asm {
        push ds
        mov di, g_abios_inittable
        xor ax, ax
        mov ds, ax
        mov ax, 1280      ; ah = 5 (build initialization table), al = 0
        int 0x15
        jc ABIOS_IT_FAIL
        mov ax, 0
        jmp ABIOS_IT_DONE
ABIOS_IT_FAIL:
        mov ax, 1
ABIOS_IT_DONE:
        pop ds
        mov [g_abios_tmp], ax
    }
    if (abios_tmp != 0) return ABIOS_INIT_NOT_PRESENT;

    // Compute the Common Data Area's own synthetic Anchor Pointer segment - see
    // abios_cda_skew's own comment.
    asm {
        mov bx, g_abios_cda_raw
        mov [g_abios_tmp], bx
        push cs
        pop ax
        mov [g_abios_tmp2], ax
    }
    off = abios_tmp;
    cs = abios_tmp2;
    rem = off % 16;
    if (rem == 0) {
        abios_cda_skew = 0;
    } else {
        abios_cda_skew = 16 - rem;
    }
    abios_anchor_segment = cs + (off + abios_cda_skew) / 16;

    // Common Data Area header: where the (unused, oversized) data-pointer area starts,
    // and how many Logical ID slots this Common Data Area has room for.
    abios_cda_word_set(0, ABIOS_CDA_HEADER_BYTES + ABIOS_CDA_RESERVED_BYTES + ABIOS_CDA_LID_BYTES);
    abios_cda_word_set(2, ABIOS_MAX_LOGICAL_IDS);

    foundInternal = 0;
    runningLid = 2;  // Logical IDs 0/1 are reserved; physical devices start at 2
    i = 0;
    while (i < abios_num_entries) {
        base = i * 12;
        deviceId = abios_inittable[base + 0];
        numLids = abios_inittable[base + 1];
        devBlockLen = abios_inittable[base + 2];
        initOff = abios_inittable[base + 3];
        initSeg = abios_inittable[base + 4];
        fttLen = abios_inittable[base + 6];

        if (runningLid >= ABIOS_MAX_LOGICAL_IDS) return ABIOS_INIT_TOO_LARGE;

        if (deviceId == 0 && foundInternal == 0) {
            if (devBlockLen > ABIOS_DEVBLOCK_CAP || fttLen > ABIOS_FTT_CAP) return ABIOS_INIT_TOO_LARGE;
            exception = abios_register_and_init(runningLid, abios_off_devblock_internal(), abios_off_ftt_internal(),
                                                 initOff, initSeg);
            if (exception == 0) foundInternal = 1;
        } else if (deviceId == 4 && abios_keyboard_lid < 0) {
            if (devBlockLen <= ABIOS_DEVBLOCK_CAP && fttLen <= ABIOS_FTT_CAP) {
                exception = abios_register_and_init(runningLid, abios_off_devblock_keyboard(),
                                                      abios_off_ftt_keyboard(), initOff, initSeg);
                if (exception == 0) abios_keyboard_lid = runningLid;
            }
        } else if (deviceId == 3 && abios_video_lid < 0) {
            if (devBlockLen <= ABIOS_DEVBLOCK_CAP && fttLen <= ABIOS_FTT_CAP) {
                exception = abios_register_and_init(runningLid, abios_off_devblock_video(), abios_off_ftt_video(),
                                                      initOff, initSeg);
                if (exception == 0) abios_video_lid = runningLid;
            }
        }

        runningLid = runningLid + numLids;
        i = i + 1;
    }

    if (foundInternal == 0) return ABIOS_INIT_NO_INTERNAL;
    return ABIOS_INIT_OK;
}

unsigned char abios_keyboard_available() {
    return abios_keyboard_lid >= 0;
}

unsigned char abios_video_available() {
    return abios_video_lid >= 0;
}

// Issues Function 01h (Return Logical ID Parameters) to Logical ID `lid` via the ABIOS
// Transfer Convention (manual Figure 4-6/the pseudo code beneath it): push the Common
// Data Area Anchor Pointer segment, the Request Block segment (our own `cs` - an
// ordinary far pointer, not the Anchor's "assumed offset 0" case) and offset, reserve 8
// bytes of placeholder space for the pointers the common routine fills in, then far-call
// the Common Start Routine (its own far pointer sits at abios_sysparams[0..1], exactly
// the layout `callf [mem]` expects). The caller removes all 14 bytes from the stack
// afterward, per the manual's own convention.
unsigned char abios_ping(int lid) {
    abios_rb[0] = 0x20;    // Request Block Length
    abios_rb[1] = lid;     // Logical ID
    abios_rb[2] = 0;       // Unit
    abios_rb[3] = 1;       // Function: Return Logical ID Parameters
    abios_rb[4] = 0;
    abios_rb[5] = 0;
    abios_rb[6] = 0xFFFF;  // Return Code Field Not Valid - required before any Start call
    abios_rb[7] = 0;

    asm {
        mov ax, [g_abios_anchor_segment]
        push ax
        push cs
        mov ax, g_abios_rb
        push ax
        sub sp, 8
        callf [g_abios_sysparams]
        add sp, 14
    }
    return abios_rb[6];
}

unsigned char abios_ping_keyboard() {
    return abios_ping(abios_keyboard_lid);
}

unsigned char abios_ping_video() {
    return abios_ping(abios_video_lid);
}

int abios_ping_device_id() {
    return abios_rb[9];
}

unsigned char abios_ping_secondary_device_id() {
    return abios_rb[13];
}

unsigned char abios_ping_revision() {
    return abios_rb[13] / 256;
}
