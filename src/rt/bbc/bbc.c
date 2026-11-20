#include "bbc.h"

// Staging cells for an MOS call's argument/result (see bbc.h's own header comment for why these
// can't just be locals/parameters).
unsigned char rt_a;
unsigned char rt_x;
unsigned char rt_y;

void bbc_oswrch(unsigned char c) {
    rt_a = c;
    asm {
        lda g_rt_a
        jsr 65518  ; OSWRCH, $FFEE
    }
}

void bbc_puts(char *s) {
    int i;
    i = 0;
    while (s[i] != 0) {
        bbc_oswrch(s[i]);
        i = i + 1;
    }
}

void bbc_osasci(unsigned char c) {
    rt_a = c;
    asm {
        lda g_rt_a
        jsr 65507  ; OSASCI, $FFE3
    }
}

void bbc_newline() {
    asm {
        jsr 65511  ; OSNEWL, $FFE7
    }
}

void bbc_cls() {
    bbc_oswrch(12);  // VDU 12: CLS
}

void bbc_home() {
    bbc_oswrch(30);  // VDU 30: cursor home
}

void bbc_mode(unsigned char mode) {
    bbc_oswrch(22);  // VDU 22: MODE change
    bbc_oswrch(mode);
}

void bbc_gotoxy(unsigned char x, unsigned char y) {
    bbc_oswrch(31);  // VDU 31: TAB(x,y)
    bbc_oswrch(x);
    bbc_oswrch(y);
}

void bbc_textcolour(unsigned char colour) {
    bbc_oswrch(17);  // VDU 17: COLOUR
    bbc_oswrch(colour);
}

void bbc_graphicscolour(unsigned char mode, unsigned char colour) {
    bbc_oswrch(18);  // VDU 18: GCOL
    bbc_oswrch(mode);
    bbc_oswrch(colour);
}

void bbc_plot(unsigned char code, int x, int y) {
    bbc_oswrch(25);  // VDU 25: PLOT
    bbc_oswrch(code);
    bbc_oswrch(x & 255);
    bbc_oswrch((x >> 8) & 255);
    bbc_oswrch(y & 255);
    bbc_oswrch((y >> 8) & 255);
}

void bbc_move(int x, int y) {
    bbc_plot(4, x, y);
}

void bbc_draw(int x, int y) {
    bbc_plot(5, x, y);
}

unsigned char bbc_osrdch() {
    asm {
        jsr 65504  ; OSRDCH, $FFE0
        sta g_rt_a
    }
    return rt_a;
}

unsigned char bbc_getch() {
    return bbc_osrdch();
}

unsigned char bbc_osbyte(unsigned char a, unsigned char x, unsigned char y) {
    rt_a = a;
    rt_x = x;
    rt_y = y;
    asm {
        lda g_rt_a
        ldx g_rt_x
        ldy g_rt_y
        jsr 65524  ; OSBYTE, $FFF4
        sta g_rt_a
    }
    return rt_a;
}

void bbc_acknowledge_escape() {
    bbc_osbyte(126, 0, 0);  // &7E
}

void bbc_escape_enable(unsigned char on) {
    if (on) {
        bbc_osbyte(125, 0, 0);  // &7D
    } else {
        bbc_osbyte(124, 0, 0);  // &7C
    }
}

void bbc_set_key_repeat_delay(unsigned char n) {
    bbc_osbyte(11, n, 0);  // &0B
}

void bbc_set_key_repeat_rate(unsigned char n) {
    bbc_osbyte(12, n, 0);  // &0C
}

void bbc_flush_buffer_class(unsigned char cls) {
    bbc_osbyte(15, cls, 0);  // &0F
}

void bbc_flush_buffer(unsigned char buf) {
    bbc_osbyte(21, buf, 0);  // &15
}

void bbc_wait_vsync() {
    bbc_osbyte(19, 0, 0);  // &13
}

// OSCLI's own command-string buffer, CR-terminated (see bbc_oscli()'s own header comment).
unsigned char bbc_cli_buf[80];

void bbc_oscli(char *cmd) {
    int i;
    i = 0;
    while (cmd[i] != 0 && i < 79) {
        bbc_cli_buf[i] = cmd[i];
        i = i + 1;
    }
    bbc_cli_buf[i] = 13;
    asm {
        ldxi g_bbc_cli_buf
        ldyi_hi g_bbc_cli_buf
        jsr 65527  ; OSCLI, $FFF7
    }
}

// OSWORD 7's own 8-byte SOUND parameter block: channel, amplitude, pitch, duration, each a
// 16-bit little-endian field (see bbc_sound()'s own header comment).
unsigned char bbc_sound_block[8];

void bbc_sound(int channel, int amplitude, int pitch, int duration) {
    bbc_sound_block[0] = channel & 255;
    bbc_sound_block[1] = (channel >> 8) & 255;
    bbc_sound_block[2] = amplitude & 255;
    bbc_sound_block[3] = (amplitude >> 8) & 255;
    bbc_sound_block[4] = pitch & 255;
    bbc_sound_block[5] = (pitch >> 8) & 255;
    bbc_sound_block[6] = duration & 255;
    bbc_sound_block[7] = (duration >> 8) & 255;
    asm {
        ldxi g_bbc_sound_block
        ldyi_hi g_bbc_sound_block
        ldai 7     ; OSWORD 7: SOUND
        jsr 65521  ; OSWORD, $FFF1
    }
}

// OSWORD 8's own 14-byte ENVELOPE parameter block, one byte per parameter in the order
// bbc_envelope() takes them.
unsigned char bbc_envelope_block[14];

void bbc_envelope(unsigned char n, unsigned char t, unsigned char ps1, unsigned char ps2, unsigned char ps3,
                  unsigned char ns1, unsigned char ns2, unsigned char ns3, unsigned char ar, unsigned char dr,
                  unsigned char sr, unsigned char rr, unsigned char targetAmp, unsigned char repeats) {
    bbc_envelope_block[0] = n;
    bbc_envelope_block[1] = t;
    bbc_envelope_block[2] = ps1;
    bbc_envelope_block[3] = ps2;
    bbc_envelope_block[4] = ps3;
    bbc_envelope_block[5] = ns1;
    bbc_envelope_block[6] = ns2;
    bbc_envelope_block[7] = ns3;
    bbc_envelope_block[8] = ar;
    bbc_envelope_block[9] = dr;
    bbc_envelope_block[10] = sr;
    bbc_envelope_block[11] = rr;
    bbc_envelope_block[12] = targetAmp;
    bbc_envelope_block[13] = repeats;
    asm {
        ldxi g_bbc_envelope_block
        ldyi_hi g_bbc_envelope_block
        ldai 8     ; OSWORD 8: ENVELOPE
        jsr 65521  ; OSWORD, $FFF1
    }
}

// OSFIND/OSFILE's own filename buffer, CR-terminated (MOS's own filename convention, not NUL -
// see bbc_osfind_open()'s own header comment).
unsigned char bbc_name_buf[39];

void bbc_set_filename(char *name) {
    int i;
    i = 0;
    while (name[i] != 0 && i < 38) {
        bbc_name_buf[i] = name[i];
        i = i + 1;
    }
    bbc_name_buf[i] = 13;
}

unsigned char bbc_osfind_open(char *name, unsigned char mode) {
    bbc_set_filename(name);
    rt_a = mode;
    asm {
        ldxi g_bbc_name_buf
        ldyi_hi g_bbc_name_buf
        lda g_rt_a
        jsr 65486  ; OSFIND, $FFCE
        sta g_rt_a
    }
    return rt_a;
}

// The dynamic (not compile-time-known) handle argument OSFIND's close call, OSBGET, and OSBPUT
// all need in Y - staged through a global and loaded with the absolute-addressing `ldy`
// M6502Assembler.cpp added for exactly this (see bbc.h's own header comment).
unsigned char rt_handle;

void bbc_osfind_close(unsigned char handle) {
    rt_handle = handle;
    rt_a = 0;
    asm {
        ldy g_rt_handle
        lda g_rt_a
        jsr 65486  ; OSFIND, $FFCE
    }
}

// Set by bbc_osbget() right after its own OSBGET call, since this dialect has no way to read the
// 6502's carry flag back into C-visible state other than a branch (see bbc_osbget_eof()'s own
// header comment).
unsigned char bbc_bget_eof_flag;

unsigned char bbc_osbget(unsigned char handle) {
    rt_handle = handle;
    asm {
        ldy g_rt_handle
        jsr 65495  ; OSBGET, $FFD7
        sta g_rt_a
        ldai 0
        sta g_bbc_bget_eof_flag
        bcc bbc_osbget_not_eof
        ldai 1
        sta g_bbc_bget_eof_flag
bbc_osbget_not_eof:
    }
    return rt_a;
}

unsigned char bbc_osbget_eof() {
    return bbc_bget_eof_flag;
}

void bbc_osbput(unsigned char handle, unsigned char b) {
    rt_handle = handle;
    rt_a = b;
    asm {
        ldy g_rt_handle
        lda g_rt_a
        jsr 65492  ; OSBPUT, $FFD4
    }
}

// OSGBPB's own 13-byte control block: handle, data address (4 bytes), count (4 bytes),
// sequential pointer (4 bytes) - see bbc_osgbpb_write()'s own header comment. Every "high half"
// byte above the 16 bits a plain `int` address/count actually needs stays 0: no transfer this
// library issues is ever that large.
unsigned char bbc_gbpb_block[13];

void bbc_osgbpb_write(unsigned char handle, int bufAddr, int count) {
    int i;
    bbc_gbpb_block[0] = handle;
    bbc_gbpb_block[1] = bufAddr & 255;
    bbc_gbpb_block[2] = (bufAddr >> 8) & 255;
    bbc_gbpb_block[5] = count & 255;
    bbc_gbpb_block[6] = (count >> 8) & 255;
    i = 3;
    while (i < 13) {
        if (i != 5 && i != 6) bbc_gbpb_block[i] = 0;
        i = i + 1;
    }
    rt_a = 2;  // OSGBPB function 2: write bytes using the file's current pointer
    asm {
        ldxi g_bbc_gbpb_block
        ldyi_hi g_bbc_gbpb_block
        lda g_rt_a
        jsr 65489  ; OSGBPB, $FFD1
    }
}

int bbc_osgbpb_read(unsigned char handle, int bufAddr, int count) {
    int i;
    bbc_gbpb_block[0] = handle;
    bbc_gbpb_block[1] = bufAddr & 255;
    bbc_gbpb_block[2] = (bufAddr >> 8) & 255;
    bbc_gbpb_block[5] = count & 255;
    bbc_gbpb_block[6] = (count >> 8) & 255;
    i = 3;
    while (i < 13) {
        if (i != 5 && i != 6) bbc_gbpb_block[i] = 0;
        i = i + 1;
    }
    rt_a = 4;  // OSGBPB function 4: read bytes using the file's current pointer
    asm {
        ldxi g_bbc_gbpb_block
        ldyi_hi g_bbc_gbpb_block
        lda g_rt_a
        jsr 65489  ; OSGBPB, $FFD1
    }
    return bbc_gbpb_block[5] | (bbc_gbpb_block[6] << 8);
}

// OSFILE's own 18-byte control block: filename pointer (2 bytes), load address (4), execution
// address (4), start address/length (4), end address/attributes (4) - see DfsFormat.h's own
// header comment for the same fields' catalogue encoding, and bbc_osfile_save()'s/_load()'s own
// header comments for exactly what each OSFILE function reads from or writes into it.
unsigned char bbc_file_block[18];

void bbc_osfile_set_filename_pointer() {
    asm {
        ldai g_bbc_name_buf
        sta g_bbc_file_block
        ldai_hi g_bbc_name_buf
        sta g_bbc_file_block+1
    }
}

unsigned char bbc_osfile_call(unsigned char op) {
    rt_a = op;
    asm {
        ldxi g_bbc_file_block
        ldyi_hi g_bbc_file_block
        lda g_rt_a
        jsr 65501  ; OSFILE, $FFDD
        sta g_rt_a
    }
    return rt_a;
}

unsigned char bbc_osfile_save(char *name, int start, int end, int loadAddr, int execAddr) {
    int i;
    bbc_set_filename(name);
    bbc_osfile_set_filename_pointer();
    bbc_file_block[2] = loadAddr & 255;
    bbc_file_block[3] = (loadAddr >> 8) & 255;
    bbc_file_block[6] = execAddr & 255;
    bbc_file_block[7] = (execAddr >> 8) & 255;
    bbc_file_block[10] = start & 255;
    bbc_file_block[11] = (start >> 8) & 255;
    bbc_file_block[14] = end & 255;
    bbc_file_block[15] = (end >> 8) & 255;
    i = 4;
    while (i < 18) {
        if (i != 6 && i != 7 && i != 10 && i != 11 && i != 14 && i != 15) bbc_file_block[i] = 0;
        i = i + 1;
    }
    return bbc_osfile_call(0);  // OSFILE function 0: Save
}

unsigned char bbc_osfile_load(char *name) {
    int i;
    bbc_set_filename(name);
    bbc_osfile_set_filename_pointer();
    i = 2;
    while (i < 18) {
        bbc_file_block[i] = 0;
        i = i + 1;
    }
    return bbc_osfile_call(255);  // OSFILE function 255 (&FF): Load
}

unsigned char bbc_osfile_info(char *name) {
    int i;
    bbc_set_filename(name);
    bbc_osfile_set_filename_pointer();
    i = 2;
    while (i < 18) {
        bbc_file_block[i] = 0;
        i = i + 1;
    }
    return bbc_osfile_call(5);  // OSFILE function 5: read catalogue information
}

unsigned char bbc_osfile_delete(char *name) {
    bbc_set_filename(name);
    bbc_osfile_set_filename_pointer();
    return bbc_osfile_call(6);  // OSFILE function 6: delete
}

int bbc_osfile_load_address() {
    return bbc_file_block[2] | (bbc_file_block[3] << 8);
}

int bbc_osfile_exec_address() {
    return bbc_file_block[6] | (bbc_file_block[7] << 8);
}

int bbc_osfile_length() {
    return bbc_file_block[10] | (bbc_file_block[11] << 8);
}

unsigned char bbc_osfile_attributes() {
    return bbc_file_block[14];
}
