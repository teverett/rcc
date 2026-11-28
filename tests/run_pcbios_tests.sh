#!/usr/bin/env bash
# Exercises src/rt/pcbios/pcbios.c's public functions for real under DOSBox-X's own BIOS
# emulation (`INT 10h`/`INT 13h`/`INT 16h`) - no bootable image of any kind is built here or
# anywhere else in this project (see docs/known-bugs.md's `8086`/`8088` entry): every test
# program below is an ordinary MS-DOS `.com`, and DOSBox-X owns getting the CPU into real
# mode with a BIOS resident before it runs, the same way real MS-DOS would. Each test's
# result is read back from a file its own program writes via src/rt/msdos/'s `dos_create`/
# `dos_write` (not by capturing screen text, which DOSBox-X has no headless way to dump) -
# linking pcbios.lib and msdos.lib into the same program also doubles as a regression test
# that their globals (`pcbios_rt_w*` vs `rt_w*`) never collide.
#
# Every test program's own base filename is 8 characters or fewer: DOSBox-X's mounted
# drive is a classic DOS 8.3 short-filename volume, so `foo_bar_baz.com` would silently
# become `FOO_BAR~1.COM` on it - a longer, more "obvious" name here would just mean the
# `-c "name.com"` command below never finds the file it meant to run.
#
# Usage: tests/run_pcbios_tests.sh [path/to/rcc]

set -eu

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
RCC="$ROOT_DIR/build/bin/rcc"
for arg in "$@"; do
    case "$arg" in
        *) RCC="$arg" ;;
    esac
done
# Absolute, since this script cd's into a per-test tmpdir below.
RCC="$(cd "$(dirname "$RCC")" && pwd)/$(basename "$RCC")"

if [ ! -x "$RCC" ]; then
    echo "rcc binary not found at $RCC - build it first ('make')" >&2
    exit 1
fi
if ! command -v dosbox-x >/dev/null 2>&1; then
    echo "dosbox-x not found - install it (e.g. 'brew install dosbox-x' or 'apt-get install dosbox-x')" >&2
    exit 1
fi

PCBIOS_DIR="$ROOT_DIR/src/rt/pcbios"
MSDOS_DIR="$ROOT_DIR/src/rt/msdos"

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
cd "$tmpdir"
cp "$PCBIOS_DIR/pcbios.h" "$PCBIOS_DIR/pcbios.c" .
cp "$MSDOS_DIR/msdos.h" "$MSDOS_DIR/msdos.c" .
"$RCC" --target=8086-dos --format=coff --out=pcbios.o pcbios.c >/dev/null
"$RCC" --target=8086-dos --format=coff --out=msdos.o msdos.c >/dev/null

fail=0

# Reads back the single byte $1's own test program wrote via dos_create()/dos_write().
read_byte() {
    if [ ! -f "$1" ]; then
        echo "(missing - program crashed or never reached the write)"
        return 0
    fi
    od -An -tu1 "$1" | tr -d ' \n'
}

# True iff $1 is read_byte()'s own plain-digits form, not its "(missing - ...)"
# placeholder - guards every arithmetic/`-gt` use of a read_byte() result below from
# crashing under `set -u` (bash's arithmetic context treats a non-numeric word as a
# variable name to expand, which errors out on the placeholder's own words).
is_num() {
    case "$1" in
        ''|*[!0-9]*) return 1 ;;
        *) return 0 ;;
    esac
}

write_byte_helper_c='
void write_byte(char *name, unsigned char v) {
    int h;
    h = dos_create(name, 0);
    dos_buf_set(0, v);
    dos_write(h, 1);
    dos_close(h);
}
'

# Builds $1.c, links it against pcbios.o/msdos.o into $1.com, and runs it headlessly under
# DOSBox-X (mounting this tmpdir as drive C: - see dosbox-x's own `-c`/`-silent`/`-exit`
# flags, which need no display and exit on their own once AUTOEXEC's commands finish).
run_com() {
    local name="$1"
    "$RCC" --target=8086-dos --format=coff --out="$name.o" "$name.c" >/dev/null
    "$RCC" --link --target=8086-dos --format=raw --out="$name.com" "$name.o" pcbios.o msdos.o >/dev/null
    dosbox-x -c "mount c ." -c "c:" -c "$name.com" -c "exit" \
        -silent -exit -time-limit 20 -noconfig -fastlaunch -nogui -nomenu >/dev/null 2>&1
}

check() {
    local label="$1" ok="$2" detail="$3"
    if [ "$ok" = "1" ]; then
        echo "PASS $label"
    else
        echo "FAIL $label: $detail"
        fail=1
    fi
}

# pcbios_putchar_writes_char_to_screen: pcbios_putchar() at a known cursor position, then
# read the same cell back via INT 10h/08h (pcbios_peek_char()) - proves the BIOS teletype
# call actually changed video memory, not just that it didn't crash.
rm -f out.bin
cat >pchar.c <<EOF
#include "pcbios.h"
#include "msdos.h"
$write_byte_helper_c
int main() {
    pcbios_clear_screen();
    pcbios_set_cursor(5, 10);
    pcbios_putchar('Q');
    pcbios_set_cursor(5, 10);
    write_byte("out.bin", pcbios_peek_char());
    pcbios_exit(0);
    return 0;
}
EOF
run_com pchar
got="$(read_byte out.bin)"
[ "$got" = "81" ] && ok=1 || ok=0
check pcbios_putchar_writes_char_to_screen "$ok" "expected 81 ('Q'), got $got"

# pcbios_write_attr_writes_char_and_attr: pcbios_write_attr() sets both the character and
# its attribute byte at the cursor without moving it - read both back via
# pcbios_peek_char()/pcbios_peek_attr().
rm -f ch.bin attr.bin
cat >wattr.c <<EOF
#include "pcbios.h"
#include "msdos.h"
$write_byte_helper_c
int main() {
    pcbios_clear_screen();
    pcbios_set_cursor(3, 7);
    pcbios_write_attr('Z', 30, 1);
    pcbios_set_cursor(3, 7);
    write_byte("ch.bin", pcbios_peek_char());
    write_byte("attr.bin", pcbios_peek_attr());
    pcbios_exit(0);
    return 0;
}
EOF
run_com wattr
got_ch="$(read_byte ch.bin)"
got_attr="$(read_byte attr.bin)"
[ "$got_ch" = "90" ] && [ "$got_attr" = "30" ] && ok=1 || ok=0
check pcbios_write_attr_writes_char_and_attr "$ok" "expected char=90 ('Z') attr=30, got char=$got_ch attr=$got_attr"

# pcbios_cursor_set_get_roundtrip: pcbios_set_cursor() followed by pcbios_cursor_row()/
# pcbios_cursor_col() reports back the same position - exercises the dh/dl packing and
# extraction (row*256+col going in, /256 and truncating return coming out) independently
# of any character/attribute I/O.
rm -f row.bin col.bin
cat >curs.c <<EOF
#include "pcbios.h"
#include "msdos.h"
$write_byte_helper_c
int main() {
    pcbios_set_cursor(12, 40);
    write_byte("row.bin", pcbios_cursor_row());
    write_byte("col.bin", pcbios_cursor_col());
    pcbios_exit(0);
    return 0;
}
EOF
run_com curs
got_row="$(read_byte row.bin)"
got_col="$(read_byte col.bin)"
[ "$got_row" = "12" ] && [ "$got_col" = "40" ] && ok=1 || ok=0
check pcbios_cursor_set_get_roundtrip "$ok" "expected row=12 col=40, got row=$got_row col=$got_col"

# pcbios_haskey_false_when_idle: INT 16h/01h with nothing typed must report "no key
# waiting" - the one keyboard behavior this headless harness can exercise without a way to
# inject a real keystroke (pcbios_getchar()'s blocking read is compile/link-tested only -
# see examples/8086-pcxt/README.md).
rm -f out.bin
cat >hkey1.c <<EOF
#include "pcbios.h"
#include "msdos.h"
$write_byte_helper_c
int main() {
    write_byte("out.bin", pcbios_haskey());
    pcbios_exit(0);
    return 0;
}
EOF
run_com hkey1
got="$(read_byte out.bin)"
[ "$got" = "0" ] && ok=1 || ok=0
check pcbios_haskey_false_when_idle "$ok" "expected 0, got $got"

# pcbios_haskey_idempotent: calling it three times in a row with nothing typed must never
# "consume" a phantom key or otherwise change its own answer.
rm -f out.bin
cat >hkey2.c <<EOF
#include "pcbios.h"
#include "msdos.h"
$write_byte_helper_c
int main() {
    unsigned char a, b, c;
    a = pcbios_haskey();
    b = pcbios_haskey();
    c = pcbios_haskey();
    write_byte("out.bin", a + b + c);
    pcbios_exit(0);
    return 0;
}
EOF
run_com hkey2
got="$(read_byte out.bin)"
[ "$got" = "0" ] && ok=1 || ok=0
check pcbios_haskey_idempotent "$ok" "expected 0 (three 0 answers), got sum=$got"

# pcbios_disk_*_roundtrip (x2) both need a real, sector-addressable floppy image -
# DOSBox-X's own directory mount (used for every test above) is a virtual DOS filesystem,
# not something INT 13h can seek into by cylinder/head/sector, so these two go through
# `imgmount` instead of `mount`. `imgmake` only creates a blank data image (a 1.44MB
# floppy's usual geometry); nothing here ever writes a boot sector into it or anything
# else that would make it bootable.
disk_test() {
    local name="$1" cyl="$2" head="$3" sector="$4" label="$5"
    cat >"$name.c" <<EOF
#include "pcbios.h"
#include "msdos.h"
$write_byte_helper_c
int main() {
    int i;
    unsigned char status, mismatch;

    i = 0;
    while (i < 512) {
        pcbios_buf_set(i, i & 255);
        i = i + 1;
    }
    status = pcbios_disk_write(0, $cyl, $head, $sector);
    if (status != 0) {
        pcbios_disk_reset(0);
        status = pcbios_disk_write(0, $cyl, $head, $sector);
    }
    write_byte("wstat.bin", status);

    i = 0;
    while (i < 512) {
        pcbios_buf_set(i, 0);
        i = i + 1;
    }
    status = pcbios_disk_read(0, $cyl, $head, $sector);
    if (status != 0) {
        pcbios_disk_reset(0);
        status = pcbios_disk_read(0, $cyl, $head, $sector);
    }
    write_byte("rstat.bin", status);

    mismatch = 0;
    i = 0;
    while (i < 512) {
        if (pcbios_buf_get(i) != (i & 255)) mismatch = 1;
        i = i + 1;
    }
    write_byte("cmp.bin", mismatch);
    pcbios_exit(0);
    return 0;
}
EOF
    "$RCC" --target=8086-dos --format=coff --out="$name.o" "$name.c" >/dev/null
    "$RCC" --link --target=8086-dos --format=raw --out="$name.com" "$name.o" pcbios.o msdos.o >/dev/null
    rm -f flop.img wstat.bin rstat.bin cmp.bin
    dosbox-x -c "imgmake flop.img -t fd_1440" -c "imgmount 0 flop.img -t floppy" \
        -c "mount c ." -c "c:" -c "$name.com" -c "exit" \
        -silent -exit -time-limit 20 -noconfig -fastlaunch -nogui -nomenu >/dev/null 2>&1

    local wstat rstat cmp ok
    wstat="$(read_byte wstat.bin)"
    rstat="$(read_byte rstat.bin)"
    cmp="$(read_byte cmp.bin)"
    [ "$wstat" = "0" ] && [ "$rstat" = "0" ] && [ "$cmp" = "0" ] && ok=1 || ok=0
    check "$label" "$ok" "write status=$wstat read status=$rstat mismatch=$cmp (0/0/0 expected)"
}

# cyl=0/head=0/sector=1: the simplest case (also the one that first surfaced INT 13h
# status 6, "disk changed", on the read right after the write - see pcbios.h's own
# pcbios_disk_read() comment; the test retries once after a reset, which is exactly the
# documented recovery, not a bug being masked).
disk_test dsk1 0 0 1 pcbios_disk_write_read_roundtrip
# cyl=1/head=1/sector=5: nonzero in every field, to exercise pcbios_disk_read()/
# pcbios_disk_write()'s own `cyl*256+sector`/`head*256+drive` packing beyond the all-zero
# case above.
disk_test dsk2 1 1 5 pcbios_disk_nonzero_chs_roundtrip

# pcbios_write_char_leaves_attribute_alone: unlike pcbios_write_attr(), pcbios_write_char()
# must not touch whatever attribute byte is already on screen.
rm -f out.bin
cat >wchar.c <<EOF
#include "pcbios.h"
#include "msdos.h"
$write_byte_helper_c
int main() {
    pcbios_clear_screen();
    pcbios_set_cursor(4, 4);
    pcbios_write_attr('X', 50, 1);
    pcbios_set_cursor(4, 4);
    pcbios_write_char('Y', 1);
    pcbios_set_cursor(4, 4);
    write_byte("out.bin", pcbios_peek_attr());
    pcbios_exit(0);
    return 0;
}
EOF
run_com wchar
got="$(read_byte out.bin)"
[ "$got" = "50" ] && ok=1 || ok=0
check pcbios_write_char_leaves_attribute_alone "$ok" "expected attr=50 unchanged, got $got"

# pcbios_video_mode_columns_page_defaults: the BIOS's own default 80x25 text mode (3),
# 80 columns, page 0 - set explicitly rather than assumed, then read back via INT 10h/0Fh.
rm -f mode.bin cols.bin page.bin
cat >vmode.c <<EOF
#include "pcbios.h"
#include "msdos.h"
$write_byte_helper_c
int main() {
    pcbios_set_mode(3);
    write_byte("mode.bin", pcbios_get_video_mode());
    write_byte("cols.bin", pcbios_get_video_columns());
    write_byte("page.bin", pcbios_get_video_page());
    pcbios_exit(0);
    return 0;
}
EOF
run_com vmode
got_mode="$(read_byte mode.bin)"
got_cols="$(read_byte cols.bin)"
got_page="$(read_byte page.bin)"
[ "$got_mode" = "3" ] && [ "$got_cols" = "80" ] && [ "$got_page" = "0" ] && ok=1 || ok=0
check pcbios_video_mode_columns_page_defaults "$ok" "expected mode=3 cols=80 page=0, got mode=$got_mode cols=$got_cols page=$got_page"

# pcbios_write_string_writes_chars_and_attr: INT 10h/13h's own BP-pointer call - also
# regression-tests that pcbios_write_string() saves/restores bp (this dialect's own frame
# pointer - see pcbios.c's own comment) around it, since a clobbered bp would corrupt
# every local variable access for the rest of main(), not just this one call.
rm -f ch.bin attr.bin
cat >wstr.c <<EOF
#include "pcbios.h"
#include "msdos.h"
$write_byte_helper_c
int main() {
    int marker;
    marker = 42;
    pcbios_clear_screen();
    pcbios_write_string(6, 6, 20, "HI", 2);
    pcbios_set_cursor(6, 6);
    write_byte("ch.bin", pcbios_peek_char());
    write_byte("attr.bin", pcbios_peek_attr());
    write_byte("mark.bin", marker);
    pcbios_exit(0);
    return 0;
}
EOF
run_com wstr
got_ch="$(read_byte ch.bin)"
got_attr="$(read_byte attr.bin)"
got_mark="$(read_byte mark.bin)"
[ "$got_ch" = "72" ] && [ "$got_attr" = "20" ] && [ "$got_mark" = "42" ] && ok=1 || ok=0
check pcbios_write_string_writes_chars_and_attr "$ok" "expected char=72 ('H') attr=20 mark=42, got char=$got_ch attr=$got_attr mark=$got_mark"

# pcbios_pixel_write_read_roundtrip: INT 10h/0Ch written, then INT 10h/0Dh read back, in a
# CGA graphics mode (4).
rm -f out.bin
cat >pixel.c <<EOF
#include "pcbios.h"
#include "msdos.h"
$write_byte_helper_c
int main() {
    pcbios_set_mode(4);
    pcbios_write_pixel(20, 30, 3);
    write_byte("out.bin", pcbios_read_pixel(20, 30));
    pcbios_exit(0);
    return 0;
}
EOF
run_com pixel
got="$(read_byte out.bin)"
[ "$got" = "3" ] && ok=1 || ok=0
check pcbios_pixel_write_read_roundtrip "$ok" "expected 3, got $got"

# pcbios_memory_size_kb_plausible: INT 12h's own base-memory size, in KB - must be
# positive and no more than the 640KB conventional-memory ceiling every PC/XT-class BIOS
# enforces (real hardware and every BIOS emulator alike).
rm -f lo.bin hi.bin
cat >memsz.c <<EOF
#include "pcbios.h"
#include "msdos.h"
$write_byte_helper_c
int main() {
    int kb;
    kb = pcbios_memory_size_kb();
    write_byte("lo.bin", kb & 255);
    write_byte("hi.bin", (kb / 256) & 255);
    pcbios_exit(0);
    return 0;
}
EOF
run_com memsz
got_lo="$(read_byte lo.bin)"
got_hi="$(read_byte hi.bin)"
if is_num "$got_lo" && is_num "$got_hi"; then
    kb=$((got_hi * 256 + got_lo))
    [ "$kb" -gt 0 ] && [ "$kb" -le 640 ] && ok=1 || ok=0
else
    kb="(missing - program crashed or never reached the write)"
    ok=0
fi
check pcbios_memory_size_kb_plausible "$ok" "expected 1-640, got $kb"

# pcbios_key_shift_status_idle: like pcbios_haskey_false_when_idle, nothing typed means
# every shift/toggle bit (INT 16h/02h) must read back as 0 under this headless harness.
# Named "shkey", not "shift" - "shift" is DOS's own internal batch command, which
# COMMAND.COM runs instead of a same-named external .com ever being launched at all.
rm -f out.bin
cat >shkey.c <<EOF
#include "pcbios.h"
#include "msdos.h"
$write_byte_helper_c
int main() {
    write_byte("out.bin", pcbios_key_shift_status());
    pcbios_exit(0);
    return 0;
}
EOF
run_com shkey
got="$(read_byte out.bin)"
[ "$got" = "0" ] && ok=1 || ok=0
check pcbios_key_shift_status_idle "$ok" "expected 0, got $got"

# pcbios_rtc_time_set_get_roundtrip / pcbios_rtc_date_set_get_roundtrip: INT 1Ah/03h-05h
# write the real motherboard RTC, INT 1Ah/02h-04h read it back - every field packed BCD.
rm -f hr.bin mn.bin sc.bin st.bin
cat >rtctime.c <<EOF
#include "pcbios.h"
#include "msdos.h"
$write_byte_helper_c
int main() {
    unsigned char status;
    pcbios_set_rtc_time(0x08, 0x15, 0x30, 0);
    status = pcbios_get_rtc_time();
    write_byte("st.bin", status);
    write_byte("hr.bin", pcbios_rtc_hour_bcd());
    write_byte("mn.bin", pcbios_rtc_minute_bcd());
    write_byte("sc.bin", pcbios_rtc_second_bcd());
    pcbios_exit(0);
    return 0;
}
EOF
run_com rtctime
got_st="$(read_byte st.bin)"
got_hr="$(read_byte hr.bin)"
got_mn="$(read_byte mn.bin)"
[ "$got_st" = "0" ] && [ "$got_hr" = "8" ] && [ "$got_mn" = "21" ] && ok=1 || ok=0
check pcbios_rtc_time_set_get_roundtrip "$ok" "expected status=0 hour_bcd=8(0x08) min_bcd=21(0x15), got status=$got_st hour=$got_hr min=$got_mn"

rm -f ce.bin yr.bin mo.bin da.bin st.bin
cat >rtcdate.c <<EOF
#include "pcbios.h"
#include "msdos.h"
$write_byte_helper_c
int main() {
    unsigned char status;
    pcbios_set_rtc_date(0x20, 0x26, 0x03, 0x11);
    status = pcbios_get_rtc_date();
    write_byte("st.bin", status);
    write_byte("ce.bin", pcbios_rtc_century_bcd());
    write_byte("yr.bin", pcbios_rtc_year_bcd());
    write_byte("mo.bin", pcbios_rtc_month_bcd());
    write_byte("da.bin", pcbios_rtc_day_bcd());
    pcbios_exit(0);
    return 0;
}
EOF
run_com rtcdate
got_st="$(read_byte st.bin)"
got_mo="$(read_byte mo.bin)"
got_da="$(read_byte da.bin)"
[ "$got_st" = "0" ] && [ "$got_mo" = "3" ] && [ "$got_da" = "17" ] && ok=1 || ok=0
check pcbios_rtc_date_set_get_roundtrip "$ok" "expected status=0 month_bcd=3(0x03) day_bcd=17(0x11), got status=$got_st month=$got_mo day=$got_da"

# pcbios_disk_get_params_reports_plausible_geometry / pcbios_disk_verify_after_write:
# both need the same real, sector-addressable floppy image disk_test() uses (imgmount,
# not the directory mount every test above runs under).
rm -f cl.bin ch.bin hd.bin sp.bin dc.bin wst.bin vst.bin
cat >dparm.c <<EOF
#include "pcbios.h"
#include "msdos.h"
$write_byte_helper_c
int main() {
    int cyl;
    unsigned char status;

    status = pcbios_disk_get_params(0);
    write_byte("st.bin", status);
    cyl = pcbios_disk_cylinders();
    write_byte("cl.bin", cyl & 255);
    write_byte("ch.bin", (cyl / 256) & 255);
    write_byte("hd.bin", pcbios_disk_heads());
    write_byte("sp.bin", pcbios_disk_sectors_per_track());
    write_byte("dc.bin", pcbios_disk_count());

    status = pcbios_disk_write(0, 0, 0, 3);
    if (status != 0) {
        pcbios_disk_reset(0);
        status = pcbios_disk_write(0, 0, 0, 3);
    }
    write_byte("wst.bin", status);
    status = pcbios_disk_verify(0, 0, 0, 3);
    if (status != 0) {
        pcbios_disk_reset(0);
        status = pcbios_disk_verify(0, 0, 0, 3);
    }
    write_byte("vst.bin", status);

    pcbios_exit(0);
    return 0;
}
EOF
"$RCC" --target=8086-dos --format=coff --out=dparm.o dparm.c >/dev/null
"$RCC" --link --target=8086-dos --format=raw --out=dparm.com dparm.o pcbios.o msdos.o >/dev/null
rm -f flop.img st.bin cl.bin ch.bin hd.bin sp.bin dc.bin wst.bin vst.bin
dosbox-x -c "imgmake flop.img -t fd_1440" -c "imgmount 0 flop.img -t floppy" \
    -c "mount c ." -c "c:" -c "dparm.com" -c "exit" \
    -silent -exit -time-limit 20 -noconfig -fastlaunch -nogui -nomenu >/dev/null 2>&1
got_st="$(read_byte st.bin)"
got_cl="$(read_byte cl.bin)"
got_ch="$(read_byte ch.bin)"
got_hd="$(read_byte hd.bin)"
got_sp="$(read_byte sp.bin)"
got_dc="$(read_byte dc.bin)"
if is_num "$got_ch" && is_num "$got_cl"; then
    cyl=$((got_ch * 256 + got_cl))
else
    cyl="(missing - program crashed or never reached the write)"
fi
[ "$got_st" = "0" ] && is_num "$cyl" && [ "$cyl" -gt 0 ] && [ "$got_hd" -gt 0 ] && [ "$got_sp" -gt 0 ] && [ "$got_dc" -gt 0 ] && ok=1 || ok=0
check pcbios_disk_get_params_reports_plausible_geometry "$ok" "expected status=0 and positive cyl/heads/sectors-per-track/count, got status=$got_st cyl=$cyl heads=$got_hd spt=$got_sp count=$got_dc"

got_wst="$(read_byte wst.bin)"
got_vst="$(read_byte vst.bin)"
[ "$got_wst" = "0" ] && [ "$got_vst" = "0" ] && ok=1 || ok=0
check pcbios_disk_verify_after_write "$ok" "expected write status=0 verify status=0, got write=$got_wst verify=$got_vst"

exit $fail
