#include "apple2.h"

// Staging cells for a ROM call's argument(s)/result (see apple2.h's own
// header comment for why this can't just be a local/parameter). `rt_a`
// stages the accumulator; `rt_x`/`rt_y` stage X/Y for the calls that need
// more than one register loaded right before their own `jsr`.
unsigned char rt_a;
unsigned char rt_x;
unsigned char rt_y;

void a2_cout(unsigned char c) {
    // COUT reads the high bit as "normal, unshifted display text", not
    // part of the character code - a byte with bit 7 clear is a control
    // code or inverse/flash text instead, depending on ROM revision - so
    // every ordinary character needs it set.
    rt_a = c | 128;
    asm {
        lda g_rt_a
        jsr 65005  ; COUT, $FDED
    }
}

void a2_puts(char *s) {
    int i;
    i = 0;
    while (s[i] != 0) {
        a2_cout(s[i]);
        i = i + 1;
    }
}

void a2_crout() {
    asm {
        jsr 64910  ; CROUT, $FD8E
    }
}

unsigned char a2_rdkey() {
    asm {
        jsr 64780  ; RDKEY, $FD0C
        sta g_rt_a
    }
    return rt_a;
}

void a2_home() {
    asm {
        jsr 64600  ; HOME, $FC58
    }
}

void a2_prbyte(unsigned char b) {
    rt_a = b;
    asm {
        lda g_rt_a
        jsr 64986  ; PRBYTE, $FDDA
    }
}

void a2_prhex(unsigned char n) {
    rt_a = n;
    asm {
        lda g_rt_a
        jsr 64995  ; PRHEX, $FDE3
    }
}

void a2_bell() {
    asm {
        jsr 65338  ; BELL, $FF3A
    }
}

void a2_wait(unsigned char n) {
    rt_a = n;
    asm {
        lda g_rt_a
        jsr 64680  ; WAIT, $FCA8
    }
}

void a2_cout1(unsigned char c) {
    rt_a = c | 128;
    asm {
        lda g_rt_a
        jsr 65008  ; COUT1, $FDF0
    }
}

unsigned char a2_rdchar() {
    asm {
        jsr 64821  ; RDCHAR, $FD35
        sta g_rt_a
    }
    return rt_a;
}

unsigned char a2_keyin() {
    asm {
        jsr 64795  ; KEYIN, $FD1B
        sta g_rt_a
    }
    return rt_a;
}

void a2_vtab(unsigned char row) {
    rt_a = row;
    asm {
        lda g_rt_a
        jsr 64546  ; VTAB, $FC22
    }
}

void a2_clreol() {
    asm {
        jsr 64668  ; CLREOL, $FC9C
    }
}

void a2_clreop() {
    asm {
        jsr 64578  ; CLREOP, $FC42
    }
}

void a2_settxt() {
    asm {
        jsr 64313  ; SETTXT, $FB39
    }
}

void a2_setgr() {
    asm {
        jsr 64320  ; SETGR, $FB40
    }
}

void a2_setinv() {
    asm {
        jsr 65152  ; SETINV, $FE80
    }
}

void a2_setnorm() {
    asm {
        jsr 65156  ; SETNORM, $FE84
    }
}

void a2_setkbd() {
    asm {
        jsr 65161  ; SETKBD, $FE89
    }
}

void a2_setvid() {
    asm {
        jsr 65171  ; SETVID, $FE93
    }
}

void a2_grclrscr() {
    asm {
        jsr 63538  ; CLRSCR, $F832
    }
}

void a2_clrtop() {
    asm {
        jsr 63542  ; CLRTOP, $F836
    }
}

void a2_getln() {
    asm {
        jsr 64874  ; GETLN, $FD6A
    }
}

void a2_move(int from, int to, int dest) {
    rt_a = from & 255;
    asm {
        lda g_rt_a
        sta 60  ; A1L, $3C
    }
    rt_a = (from >> 8) & 255;
    asm {
        lda g_rt_a
        sta 61  ; A1H, $3D
    }
    rt_a = to & 255;
    asm {
        lda g_rt_a
        sta 62  ; A2L, $3E
    }
    rt_a = (to >> 8) & 255;
    asm {
        lda g_rt_a
        sta 63  ; A2H, $3F
    }
    rt_a = dest & 255;
    asm {
        lda g_rt_a
        sta 66  ; A4L, $42
    }
    rt_a = (dest >> 8) & 255;
    asm {
        lda g_rt_a
        sta 67  ; A4H, $43
    }
    asm {
        jsr 65068  ; MOVE, $FE2C
    }
}

// ProDOS MLI call trampoline (see ProDOS Technical Reference Manual ch. 5):
// JSR $BF00, the command byte, then the parameter-list address - all three
// have to sit in the instruction stream immediately after the JSR, so
// mli_call() builds this once and patches bytes 3-5 before every call
// instead of trying to pass them as ordinary C arguments/registers.
// Byte layout: 32,0,191 = JSR $BF00; byte 3 = cmd; bytes 4-5 = paramsAddr
// lo/hi; 96 = RTS (returns to mli_call()'s own caller).
unsigned char mli_trampoline[7] = {32, 0, 191, 0, 0, 0, 96};

void mli_patch(unsigned char cmd, int paramsAddr) {
    mli_trampoline[3] = cmd;
    mli_trampoline[4] = paramsAddr & 255;
    mli_trampoline[5] = (paramsAddr >> 8) & 255;
}

int mli_call(unsigned char cmd, int paramsAddr) {
    mli_patch(cmd, paramsAddr);
    asm {
        jsr g_mli_trampoline
        sta g_rt_a
    }
    return rt_a;
}

void a2_plot(unsigned char x, unsigned char y) {
    rt_y = x;
    rt_a = y;
    asm {
        ldy g_rt_y
        lda g_rt_a
        jsr 63488  ; PLOT, $F800
    }
}

void a2_hline(unsigned char x1, unsigned char x2, unsigned char y) {
    rt_y = x1;
    rt_x = x2;
    rt_a = y;
    asm {
        lda g_rt_x
        sta 44  ; H2, $2C: right X (inclusive)
        ldy g_rt_y
        lda g_rt_a
        jsr 63513  ; HLINE, $F819
    }
}

void a2_vline(unsigned char x, unsigned char y1, unsigned char y2) {
    rt_y = x;
    rt_x = y2;
    rt_a = y1;
    asm {
        lda g_rt_x
        sta 45  ; V2, $2D: bottom row (inclusive)
        ldy g_rt_y
        lda g_rt_a
        jsr 63528  ; VLINE, $F828
    }
}

void a2_color(unsigned char c) {
    rt_a = c;
    asm {
        lda g_rt_a
        jsr 63588  ; SETCOL, $F864
    }
}

unsigned char a2_scrn(unsigned char x, unsigned char y) {
    rt_y = x;
    rt_a = y;
    asm {
        ldy g_rt_y
        lda g_rt_a
        jsr 63601  ; SCRN, $F871
        sta g_rt_a
    }
    return rt_a;
}

void a2_hgr() {
    asm {
        jsr 62434  ; HGR, $F3E2
    }
}

void a2_hgr2() {
    asm {
        jsr 62424  ; HGR2, $F3D8
    }
}

void a2_hcolor(unsigned char c) {
    rt_a = c;
    asm {
        lda g_rt_a
        sta 228  ; HGR_COLOR, $E4 - read directly by HPOSN/HPLOT
    }
}

void a2_hplot(int x, unsigned char y) {
    rt_x = x & 255;
    rt_y = (x >> 8) & 255;
    rt_a = y;
    asm {
        ldx g_rt_x
        ldy g_rt_y
        lda g_rt_a
        jsr 62551  ; HPLOT (HPOSN + plot a dot), $F457
    }
}

void a2_hlineto(int x, unsigned char y) {
    rt_a = x & 255;
    rt_x = (x >> 8) & 255;
    rt_y = y;
    asm {
        ldx g_rt_x
        ldy g_rt_y
        lda g_rt_a
        jsr 62778  ; HGLIN, $F53A
    }
}

unsigned char a2_pread(unsigned char n) {
    rt_x = n;
    asm {
        ldx g_rt_x
        jsr 64286  ; PREAD, $FB1E
        sty g_rt_a
    }
    return rt_a;
}

void a2_click() {
    asm {
        lda 49200  ; SPKR soft switch, $C030 - any access toggles it
    }
}

void a2_tone(unsigned char cycles, unsigned char halfPeriod) {
    unsigned char i;
    i = 0;
    while (i < cycles) {
        a2_click();
        a2_wait(halfPeriod);
        i = i + 1;
    }
}

unsigned char pd_create_params[12];

int pd_create(int pathname, unsigned char access, unsigned char fileType, int auxType, unsigned char storageType) {
    pd_create_params[0] = 7;
    pd_create_params[1] = pathname & 255;
    pd_create_params[2] = (pathname >> 8) & 255;
    pd_create_params[3] = access;
    pd_create_params[4] = fileType;
    pd_create_params[5] = auxType & 255;
    pd_create_params[6] = (auxType >> 8) & 255;
    pd_create_params[7] = storageType;
    pd_create_params[8] = 0;
    pd_create_params[9] = 0;
    pd_create_params[10] = 0;
    pd_create_params[11] = 0;
    return mli_call(192, (int)(&pd_create_params[0]));  // CREATE, $C0
}

unsigned char pd_open_params[6];

int pd_open(int pathname, int buffer) {
    pd_open_params[0] = 3;
    pd_open_params[1] = pathname & 255;
    pd_open_params[2] = (pathname >> 8) & 255;
    pd_open_params[3] = buffer & 255;
    pd_open_params[4] = (buffer >> 8) & 255;
    pd_open_params[5] = 0;
    return mli_call(200, (int)(&pd_open_params[0]));  // OPEN, $C8
}

unsigned char pd_open_refnum() {
    return pd_open_params[5];
}

unsigned char pd_read_params[8];

int pd_read(unsigned char refNum, int dataBuffer, int requestCount) {
    pd_read_params[0] = 4;
    pd_read_params[1] = refNum;
    pd_read_params[2] = dataBuffer & 255;
    pd_read_params[3] = (dataBuffer >> 8) & 255;
    pd_read_params[4] = requestCount & 255;
    pd_read_params[5] = (requestCount >> 8) & 255;
    pd_read_params[6] = 0;
    pd_read_params[7] = 0;
    return mli_call(202, (int)(&pd_read_params[0]));  // READ, $CA
}

int pd_read_transcount() {
    return pd_read_params[6] + (pd_read_params[7] * 256);
}

unsigned char pd_write_params[8];

int pd_write(unsigned char refNum, int dataBuffer, int requestCount) {
    pd_write_params[0] = 4;
    pd_write_params[1] = refNum;
    pd_write_params[2] = dataBuffer & 255;
    pd_write_params[3] = (dataBuffer >> 8) & 255;
    pd_write_params[4] = requestCount & 255;
    pd_write_params[5] = (requestCount >> 8) & 255;
    pd_write_params[6] = 0;
    pd_write_params[7] = 0;
    return mli_call(203, (int)(&pd_write_params[0]));  // WRITE, $CB
}

int pd_write_transcount() {
    return pd_write_params[6] + (pd_write_params[7] * 256);
}

unsigned char pd_close_params[2];

int pd_close(unsigned char refNum) {
    pd_close_params[0] = 1;
    pd_close_params[1] = refNum;
    return mli_call(204, (int)(&pd_close_params[0]));  // CLOSE, $CC
}

unsigned char pd_info_params[18];

int pd_get_file_info(int pathname) {
    pd_info_params[0] = 10;
    pd_info_params[1] = pathname & 255;
    pd_info_params[2] = (pathname >> 8) & 255;
    return mli_call(196, (int)(&pd_info_params[0]));  // GET_FILE_INFO, $C4
}

unsigned char pd_info_access() {
    return pd_info_params[3];
}

unsigned char pd_info_file_type() {
    return pd_info_params[4];
}

int pd_info_aux_type() {
    return pd_info_params[5] + (pd_info_params[6] * 256);
}

unsigned char pd_info_storage_type() {
    return pd_info_params[7];
}

int pd_info_blocks_used() {
    return pd_info_params[8] + (pd_info_params[9] * 256);
}
