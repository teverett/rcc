#!/usr/bin/env bash
# Exercises three of src/rt/apple2/apple2.c's Monitor-ROM wrapper functions
# for real under py65's 6502 core. None of the three needs a real Apple ][
# ROM image: the halt-loop/result convention (`ac` landing at memory[2]/[3])
# is the same one tests/run_tests.sh's own check_6502() uses, and every ROM
# entry point a wrapper here actually JSRs into is patched with a single
# RTS byte first - this only verifies that each wrapper stages its
# argument(s) into the right memory cell(s) before making that call, not
# anything the real ROM routine would itself do (see
# .github/workflows/platformtests.yml's own 6502-dos33 step for the same
# "patch the ROM entry point with a stub" idea, applied there to COUT/
# CROUT/HOME instead).
#
# Usage: tests/run_apple2_rom_wrapper_tests.sh [path/to/rcc]

set -eu

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
RCC="$ROOT_DIR/build/bin/rcc"
for arg in "$@"; do
    case "$arg" in
        *) RCC="$arg" ;;
    esac
done

if [ ! -x "$RCC" ]; then
    echo "rcc binary not found at $RCC - build it first ('make')" >&2
    exit 1
fi
if ! python3 -c "import py65" >/dev/null 2>&1; then
    echo "py65 not found - install it (e.g. 'pip install py65')" >&2
    exit 1
fi

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
cd "$tmpdir"

fail=0

# a2_vtab(row) stages `row` straight into rt_a before JSRing to VTAB
# ($FC22, patched here with a bare RTS stub).
cat >vtab_main.c <<'EOF'
extern unsigned char rt_a;
extern void a2_vtab(unsigned char row);
int main() {
    a2_vtab(17);
    return rt_a;
}
EOF
"$RCC" --target=6502 --format=coff vtab_main.c >/dev/null
"$RCC" --target=6502 --format=coff "$ROOT_DIR/src/rt/apple2/apple2.c" >/dev/null
"$RCC" --link --target=6502 --format=raw --out=vtab.flat build/vtab_main.o build/apple2.o

python3 - "$PWD" <<'PY' || fail=1
import sys
from py65.devices.mpu6502 import MPU

data = open(sys.argv[1] + "/vtab.flat", "rb").read()
base = 0x0600
mpu = MPU()
mpu.memory[base:base + len(data)] = data
mpu.memory[0xFC22] = 0x60  # RTS - stand in for the real VTAB routine
mpu.pc = base
for _ in range(200000):
    pc_before = mpu.pc
    mpu.step()
    if mpu.pc == pc_before:
        break
else:
    raise SystemExit("a2_vtab: simulator never reached the halt loop")
val = mpu.memory[2] | (mpu.memory[3] << 8)
assert val == 17, f"a2_vtab: expected rt_a == 17, got {val}"
print("PASS apple2_vtab_stages_row (rt_a=%d)" % val)
PY

# a2_cout1(c) ORs in bit 7 (the monitor's normal-text convention, same as
# a2_cout()'s own input) before JSRing to COUT1 ($FDF0, patched here with a
# bare RTS stub).
cat >cout1_main.c <<'EOF'
extern unsigned char rt_a;
extern void a2_cout1(unsigned char c);
int main() {
    a2_cout1(65);
    return rt_a;
}
EOF
"$RCC" --target=6502 --format=coff cout1_main.c >/dev/null
"$RCC" --link --target=6502 --format=raw --out=cout1.flat build/cout1_main.o build/apple2.o

python3 - "$PWD" <<'PY' || fail=1
import sys
from py65.devices.mpu6502 import MPU

data = open(sys.argv[1] + "/cout1.flat", "rb").read()
base = 0x0600
mpu = MPU()
mpu.memory[base:base + len(data)] = data
mpu.memory[0xFDF0] = 0x60  # RTS - stand in for the real COUT1 routine
mpu.pc = base
for _ in range(200000):
    pc_before = mpu.pc
    mpu.step()
    if mpu.pc == pc_before:
        break
else:
    raise SystemExit("a2_cout1: simulator never reached the halt loop")
val = mpu.memory[2] | (mpu.memory[3] << 8)
assert val == 65 | 128, f"a2_cout1: expected rt_a == 65|128, got {val}"
print("PASS apple2_cout1_sets_high_bit (rt_a=%d)" % val)
PY

# a2_move(from, to, dest) stages the three address pairs into MOVE's own
# zero-page cells (A1L/A1H=$3C/$3D, A2L/A2H=$3E/$3F, A4L/A4H=$42/$43)
# before JSRing to MOVE ($FE2C, patched here with a bare RTS stub).
cat >move_main.c <<'EOF'
extern void a2_move(int from, int to, int dest);
int main() {
    a2_move(770, 1023, 1024);
    return 0;
}
EOF
"$RCC" --target=6502 --format=coff move_main.c >/dev/null
"$RCC" --link --target=6502 --format=raw --out=move.flat build/move_main.o build/apple2.o

python3 - "$PWD" <<'PY' || fail=1
import sys
from py65.devices.mpu6502 import MPU

data = open(sys.argv[1] + "/move.flat", "rb").read()
base = 0x0600
mpu = MPU()
mpu.memory[base:base + len(data)] = data
mpu.memory[0xFE2C] = 0x60  # RTS - stand in for the real MOVE routine
mpu.pc = base
for _ in range(200000):
    pc_before = mpu.pc
    mpu.step()
    if mpu.pc == pc_before:
        break
else:
    raise SystemExit("a2_move: simulator never reached the halt loop")

def word(lo_addr):
    return mpu.memory[lo_addr] | (mpu.memory[lo_addr + 1] << 8)

assert word(0x3C) == 770, f"a2_move: A1L/A1H expected 770, got {word(0x3C)}"
assert word(0x3E) == 1023, f"a2_move: A2L/A2H expected 1023, got {word(0x3E)}"
assert word(0x42) == 1024, f"a2_move: A4L/A4H expected 1024, got {word(0x42)}"
print("PASS apple2_move_zero_page_staging")
PY

# a2_plot(x, y) stages A = y, Y = x before JSRing to PLOT ($F800, patched
# here with a stub that saves A/Y to $0300/$0301 before its own RTS).
cat >plot_main.c <<'EOF'
extern void a2_plot(unsigned char x, unsigned char y);
int main() {
    a2_plot(23, 40);
    return 0;
}
EOF
"$RCC" --target=6502 --format=coff plot_main.c >/dev/null
"$RCC" --link --target=6502 --format=raw --out=plot.flat build/plot_main.o build/apple2.o

python3 - "$PWD" <<'PY' || fail=1
import sys
from py65.devices.mpu6502 import MPU

data = open(sys.argv[1] + "/plot.flat", "rb").read()
base = 0x0600
mpu = MPU()
mpu.memory[base:base + len(data)] = data
# STA $0300; STY $0301; RTS - save A (y) and Y (x), then return.
mpu.memory[0xF800:0xF806] = [0x8D, 0x00, 0x03, 0x8C, 0x01, 0x03, 0x60]
mpu.pc = base
for _ in range(200000):
    pc_before = mpu.pc
    mpu.step()
    if mpu.pc == pc_before:
        break
else:
    raise SystemExit("a2_plot: simulator never reached the halt loop")
assert mpu.memory[0x0300] == 40, f"a2_plot: expected A(y) == 40, got {mpu.memory[0x0300]}"
assert mpu.memory[0x0301] == 23, f"a2_plot: expected Y(x) == 23, got {mpu.memory[0x0301]}"
print("PASS apple2_plot_stages_a_y")
PY

# a2_hline(x1, x2, y) stages A = y, Y = x1, zero page $2C = x2 before
# JSRing to HLINE ($F819, patched with the same A/Y-saving stub as PLOT).
cat >hline_main.c <<'EOF'
extern void a2_hline(unsigned char x1, unsigned char x2, unsigned char y);
int main() {
    a2_hline(3, 30, 12);
    return 0;
}
EOF
"$RCC" --target=6502 --format=coff hline_main.c >/dev/null
"$RCC" --link --target=6502 --format=raw --out=hline.flat build/hline_main.o build/apple2.o

python3 - "$PWD" <<'PY' || fail=1
import sys
from py65.devices.mpu6502 import MPU

data = open(sys.argv[1] + "/hline.flat", "rb").read()
base = 0x0600
mpu = MPU()
mpu.memory[base:base + len(data)] = data
mpu.memory[0xF819:0xF81F] = [0x8D, 0x00, 0x03, 0x8C, 0x01, 0x03, 0x60]
mpu.pc = base
for _ in range(200000):
    pc_before = mpu.pc
    mpu.step()
    if mpu.pc == pc_before:
        break
else:
    raise SystemExit("a2_hline: simulator never reached the halt loop")
assert mpu.memory[0x002C] == 30, f"a2_hline: expected H2($2C) == x2(30), got {mpu.memory[0x002C]}"
assert mpu.memory[0x0300] == 12, f"a2_hline: expected A(y) == 12, got {mpu.memory[0x0300]}"
assert mpu.memory[0x0301] == 3, f"a2_hline: expected Y(x1) == 3, got {mpu.memory[0x0301]}"
print("PASS apple2_hline_stages_a_y_h2")
PY

# a2_vline(x, y1, y2) stages A = y1, Y = x, zero page $2D = y2 before
# JSRing to VLINE ($F828, patched with the same A/Y-saving stub).
cat >vline_main.c <<'EOF'
extern void a2_vline(unsigned char x, unsigned char y1, unsigned char y2);
int main() {
    a2_vline(5, 2, 44);
    return 0;
}
EOF
"$RCC" --target=6502 --format=coff vline_main.c >/dev/null
"$RCC" --link --target=6502 --format=raw --out=vline.flat build/vline_main.o build/apple2.o

python3 - "$PWD" <<'PY' || fail=1
import sys
from py65.devices.mpu6502 import MPU

data = open(sys.argv[1] + "/vline.flat", "rb").read()
base = 0x0600
mpu = MPU()
mpu.memory[base:base + len(data)] = data
mpu.memory[0xF828:0xF82E] = [0x8D, 0x00, 0x03, 0x8C, 0x01, 0x03, 0x60]
mpu.pc = base
for _ in range(200000):
    pc_before = mpu.pc
    mpu.step()
    if mpu.pc == pc_before:
        break
else:
    raise SystemExit("a2_vline: simulator never reached the halt loop")
assert mpu.memory[0x002D] == 44, f"a2_vline: expected V2($2D) == y2(44), got {mpu.memory[0x002D]}"
assert mpu.memory[0x0300] == 2, f"a2_vline: expected A(y1) == 2, got {mpu.memory[0x0300]}"
assert mpu.memory[0x0301] == 5, f"a2_vline: expected Y(x) == 5, got {mpu.memory[0x0301]}"
print("PASS apple2_vline_stages_a_y_v2")
PY

# a2_color(c) stages A = c before JSRing to SETCOL ($F864, patched with a
# stub that just saves A to $0300 before its own RTS).
cat >color_main.c <<'EOF'
extern void a2_color(unsigned char c);
int main() {
    a2_color(9);
    return 0;
}
EOF
"$RCC" --target=6502 --format=coff color_main.c >/dev/null
"$RCC" --link --target=6502 --format=raw --out=color.flat build/color_main.o build/apple2.o

python3 - "$PWD" <<'PY' || fail=1
import sys
from py65.devices.mpu6502 import MPU

data = open(sys.argv[1] + "/color.flat", "rb").read()
base = 0x0600
mpu = MPU()
mpu.memory[base:base + len(data)] = data
mpu.memory[0xF864:0xF867] = [0x8D, 0x00, 0x03, 0x60]
mpu.pc = base
for _ in range(200000):
    pc_before = mpu.pc
    mpu.step()
    if mpu.pc == pc_before:
        break
else:
    raise SystemExit("a2_color: simulator never reached the halt loop")
assert mpu.memory[0x0300] == 9, f"a2_color: expected A(c) == 9, got {mpu.memory[0x0300]}"
print("PASS apple2_color_stages_a")
PY

# a2_scrn(x, y) stages A = y, Y = x before JSRing to SCRN ($F871), and
# copies whatever SCRN leaves in A back into rt_a as its own return value.
# The stub saves the incoming A/Y, then loads a sentinel into A before its
# own RTS, so the test checks both directions in one call.
cat >scrn_main.c <<'EOF'
extern unsigned char a2_scrn(unsigned char x, unsigned char y);
int main() {
    return a2_scrn(11, 22);
}
EOF
"$RCC" --target=6502 --format=coff scrn_main.c >/dev/null
"$RCC" --link --target=6502 --format=raw --out=scrn.flat build/scrn_main.o build/apple2.o

python3 - "$PWD" <<'PY' || fail=1
import sys
from py65.devices.mpu6502 import MPU

data = open(sys.argv[1] + "/scrn.flat", "rb").read()
base = 0x0600
mpu = MPU()
mpu.memory[base:base + len(data)] = data
# STA $0300; STY $0301; LDA #$2A; RTS
mpu.memory[0xF871:0xF877] = [0x8D, 0x00, 0x03, 0x8C, 0x01, 0x03, 0xA9, 0x2A, 0x60]
mpu.pc = base
for _ in range(200000):
    pc_before = mpu.pc
    mpu.step()
    if mpu.pc == pc_before:
        break
else:
    raise SystemExit("a2_scrn: simulator never reached the halt loop")
assert mpu.memory[0x0300] == 22, f"a2_scrn: expected A(y) == 22, got {mpu.memory[0x0300]}"
assert mpu.memory[0x0301] == 11, f"a2_scrn: expected Y(x) == 11, got {mpu.memory[0x0301]}"
val = mpu.memory[2] | (mpu.memory[3] << 8)
assert val == 0x2A, f"a2_scrn: expected return value 0x2A, got {val}"
print("PASS apple2_scrn_stages_a_y_and_returns_a")
PY

# a2_hcolor(c) stores c straight into zero page $E4 (HGR_COLOR) - no ROM
# call at all, so no stub is needed.
cat >hcolor_main.c <<'EOF'
extern void a2_hcolor(unsigned char c);
int main() {
    a2_hcolor(5);
    return 0;
}
EOF
"$RCC" --target=6502 --format=coff hcolor_main.c >/dev/null
"$RCC" --link --target=6502 --format=raw --out=hcolor.flat build/hcolor_main.o build/apple2.o

python3 - "$PWD" <<'PY' || fail=1
import sys
from py65.devices.mpu6502 import MPU

data = open(sys.argv[1] + "/hcolor.flat", "rb").read()
base = 0x0600
mpu = MPU()
mpu.memory[base:base + len(data)] = data
mpu.pc = base
for _ in range(200000):
    pc_before = mpu.pc
    mpu.step()
    if mpu.pc == pc_before:
        break
else:
    raise SystemExit("a2_hcolor: simulator never reached the halt loop")
assert mpu.memory[0x00E4] == 5, f"a2_hcolor: expected HGR_COLOR($E4) == 5, got {mpu.memory[0x00E4]}"
print("PASS apple2_hcolor_stages_e4")
PY

# a2_hplot(x, y) splits a 9-bit X-coordinate across X (low byte) and Y
# (high byte), with A = the Y-coordinate, before JSRing to HPLOT ($F457,
# patched with a stub that saves A/X/Y to $0300-$0302 before its own RTS).
# x=300 forces a non-zero high byte (300 = 0x12C: low=44, high=1).
cat >hplot_main.c <<'EOF'
extern void a2_hplot(int x, unsigned char y);
int main() {
    a2_hplot(300, 100);
    return 0;
}
EOF
"$RCC" --target=6502 --format=coff hplot_main.c >/dev/null
"$RCC" --link --target=6502 --format=raw --out=hplot.flat build/hplot_main.o build/apple2.o

python3 - "$PWD" <<'PY' || fail=1
import sys
from py65.devices.mpu6502 import MPU

data = open(sys.argv[1] + "/hplot.flat", "rb").read()
base = 0x0600
mpu = MPU()
mpu.memory[base:base + len(data)] = data
# STA $0300; STX $0301; STY $0302; RTS
mpu.memory[0xF457:0xF45E] = [0x8D, 0x00, 0x03, 0x8E, 0x01, 0x03, 0x8C, 0x02, 0x03, 0x60]
mpu.pc = base
for _ in range(200000):
    pc_before = mpu.pc
    mpu.step()
    if mpu.pc == pc_before:
        break
else:
    raise SystemExit("a2_hplot: simulator never reached the halt loop")
assert mpu.memory[0x0300] == 100, f"a2_hplot: expected A(y) == 100, got {mpu.memory[0x0300]}"
assert mpu.memory[0x0301] == 44, f"a2_hplot: expected X(x-lo) == 44, got {mpu.memory[0x0301]}"
assert mpu.memory[0x0302] == 1, f"a2_hplot: expected Y(x-hi) == 1, got {mpu.memory[0x0302]}"
print("PASS apple2_hplot_splits_x_across_x_y")
PY

# a2_hlineto(x, y) stages A = x's low byte, X = x's high byte, Y = y
# before JSRing to HGLIN ($F53A, same A/X/Y-saving stub as HPLOT).
cat >hlineto_main.c <<'EOF'
extern void a2_hlineto(int x, unsigned char y);
int main() {
    a2_hlineto(260, 50);
    return 0;
}
EOF
"$RCC" --target=6502 --format=coff hlineto_main.c >/dev/null
"$RCC" --link --target=6502 --format=raw --out=hlineto.flat build/hlineto_main.o build/apple2.o

python3 - "$PWD" <<'PY' || fail=1
import sys
from py65.devices.mpu6502 import MPU

data = open(sys.argv[1] + "/hlineto.flat", "rb").read()
base = 0x0600
mpu = MPU()
mpu.memory[base:base + len(data)] = data
mpu.memory[0xF53A:0xF541] = [0x8D, 0x00, 0x03, 0x8E, 0x01, 0x03, 0x8C, 0x02, 0x03, 0x60]
mpu.pc = base
for _ in range(200000):
    pc_before = mpu.pc
    mpu.step()
    if mpu.pc == pc_before:
        break
else:
    raise SystemExit("a2_hlineto: simulator never reached the halt loop")
# 260 = 0x104: low=4, high=1.
assert mpu.memory[0x0300] == 4, f"a2_hlineto: expected A(x-lo) == 4, got {mpu.memory[0x0300]}"
assert mpu.memory[0x0301] == 1, f"a2_hlineto: expected X(x-hi) == 1, got {mpu.memory[0x0301]}"
assert mpu.memory[0x0302] == 50, f"a2_hlineto: expected Y(y) == 50, got {mpu.memory[0x0302]}"
print("PASS apple2_hlineto_stages_a_x_y")
PY

# a2_pread(n) stages X = n before JSRing to PREAD ($FB1E), then uses the
# new `sty` absolute opcode (M6502Assembler.cpp) to move PREAD's own
# result - left in Y, not A - back into rt_a. The stub saves the incoming
# X, then loads a sentinel into Y before its own RTS.
cat >pread_main.c <<'EOF'
extern unsigned char a2_pread(unsigned char n);
int main() {
    return a2_pread(2);
}
EOF
"$RCC" --target=6502 --format=coff pread_main.c >/dev/null
"$RCC" --link --target=6502 --format=raw --out=pread.flat build/pread_main.o build/apple2.o

python3 - "$PWD" <<'PY' || fail=1
import sys
from py65.devices.mpu6502 import MPU

data = open(sys.argv[1] + "/pread.flat", "rb").read()
base = 0x0600
mpu = MPU()
mpu.memory[base:base + len(data)] = data
# STX $0300; LDY #$99; RTS
mpu.memory[0xFB1E:0xFB24] = [0x8E, 0x00, 0x03, 0xA0, 0x99, 0x60]
mpu.pc = base
for _ in range(200000):
    pc_before = mpu.pc
    mpu.step()
    if mpu.pc == pc_before:
        break
else:
    raise SystemExit("a2_pread: simulator never reached the halt loop")
assert mpu.memory[0x0300] == 2, f"a2_pread: expected X(n) == 2, got {mpu.memory[0x0300]}"
val = mpu.memory[2] | (mpu.memory[3] << 8)
assert val == 0x99, f"a2_pread: expected return value (from Y via sty) == 0x99, got {val}"
print("PASS apple2_pread_stages_x_and_returns_y")
PY

# a2_tone(cycles, halfPeriod) loops calling a2_click() (LDA $C030, no ROM
# call) and a2_wait() (JSR WAIT, $FCA8, patched here as a bare RTS so it
# returns immediately) exactly `cycles` times - checked by counting how
# many times the simulator's PC visits a2_click()'s own LDA instruction.
cat >tone_main.c <<'EOF'
extern void a2_tone(unsigned char cycles, unsigned char halfPeriod);
int main() {
    a2_tone(4, 1);
    return 0;
}
EOF
"$RCC" --target=6502 --format=coff tone_main.c >/dev/null
"$RCC" --link --target=6502 --format=raw --out=tone.flat build/tone_main.o build/apple2.o

python3 - "$PWD" <<'PY' || fail=1
import sys
from py65.devices.mpu6502 import MPU

data = open(sys.argv[1] + "/tone.flat", "rb").read()
base = 0x0600
mpu = MPU()
mpu.memory[base:base + len(data)] = data
mpu.memory[0xFCA8] = 0x60  # RTS - stand in for the real WAIT routine

click_opcode_addr = None
for addr in range(base, base + len(data) - 2):
    if mpu.memory[addr] == 0xAD and mpu.memory[addr + 1] == 0x30 and mpu.memory[addr + 2] == 0xC0:
        click_opcode_addr = addr
        break
assert click_opcode_addr is not None, "a2_tone: couldn't find a2_click()'s own LDA $C030 in the linked image"

mpu.pc = base
click_count = 0
for _ in range(200000):
    pc_before = mpu.pc
    if mpu.pc == click_opcode_addr:
        click_count += 1
    mpu.step()
    if mpu.pc == pc_before:
        break
else:
    raise SystemExit("a2_tone: simulator never reached the halt loop")
assert click_count == 4, f"a2_tone: expected a2_click() to run 4 times, got {click_count}"
print("PASS apple2_tone_clicks_cycles_times")
PY

exit $fail
