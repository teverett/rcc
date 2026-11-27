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

exit $fail
