#!/usr/bin/env bash
# Regression test for a fixed bug: M6502Assembler.cpp's/M65C02Assembler.cpp's
# emitImmLow()/emitImmHigh() used to relocate a locally-defined global's
# address by applying the object's own base-shift to its low and high bytes
# as two independent 8-bit additions (needed because the low/high halves
# come from two separate instructions, `adci`/`adci_hi`, not adjacent bytes
# of one field) - dropping any carry from the low byte into the high byte.
# Once (the symbol's own pre-link low byte + the shift's own low byte) >=
# 256, the resulting address came out exactly 0x100 low, under separate
# compilation only. See docs/known-bugs.md's history (the entry has since
# been removed - fixed) and M6502Assembler.cpp's own emitImmLow() comment
# for the full mechanism and the fix (relocate a bare local-label operand
# by name, reusing the linker's already carry-safe named-symbol lookup).
#
# This links two separately-compiled objects where one writes a global
# array indexed with a compile-time-constant index (the vulnerable shape -
# `g_arr[k] = v` lowers to `ac = g_arr_address_as_immediate + k`, split
# across `adci`/`adci_hi`) and the other reads it back behind an `if`,
# which is exactly the shape the bug corrupted. No simulator ROM/ hardware
# dependency at all - this is pure separate-compilation correctness, same
# "ac" register convention as tests/run_tests.sh's own check_6502()/
# check_65c02().
#
# Usage: tests/run_m6502_cross_object_global_test.sh [path/to/rcc]

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

cat >lib.c <<'EOF'
unsigned char arr[4];
void fill(int a, int b) {
    arr[0] = 4;
    arr[2] = 8;
    arr[3] = 0;
}
EOF

cat >main.c <<'EOF'
extern unsigned char arr[4];
extern void fill(int a, int b);
int main() {
    fill(1, 2);
    if (arr[0] != 4) return 11;
    if (arr[2] != 8 || arr[3] != 0) return 12;
    return 1;
}
EOF

run_py65_check() {
    name="$1"
    target="$2"
    python3 - "$PWD" "$target" "$name" <<'PY' || fail=1
import sys
sys.path.insert(0, sys.argv[1])
from py65.devices.mpu6502 import MPU
try:
    from py65.devices.mpu65c02 import MPU as MPU65C02
except ImportError:
    MPU65C02 = None

d, target, name = sys.argv[1] + "/", sys.argv[2], sys.argv[3]
data = open(d + "prog_" + target + ".flat", "rb").read()
base = 0x0600
mpu = (MPU65C02 if target == "65c02" else MPU)()
mpu.memory[base:base + len(data)] = data
mpu.pc = base
for _ in range(300000):
    pc_before = mpu.pc
    mpu.step()
    if mpu.pc == pc_before:
        break
else:
    raise SystemExit(f"{target}: simulator never reached the halt loop (name={name})")
val = mpu.memory[2] | (mpu.memory[3] << 8)
assert val == 1, f"{target} ({name}): expected 1, got {val}"
print(f"PASS {name}_{target}")
PY
}

for target in 6502 65c02; do
    "$RCC" --target="$target" --format=coff main.c >/dev/null
    "$RCC" --target="$target" --format=coff lib.c >/dev/null
    "$RCC" --link --target="$target" --format=raw --out="prog_$target.flat" "build/main.o" "build/lib.o"
    run_py65_check m6502_cross_object_global "$target"
done

# Second case: the historical "goto-skipped local" shape (docs/known-bugs.md
# used to carry this as its own, separately-discovered entry - same root
# cause, confirmed on --target=6502 by a controlled before/after A-B test
# against an isolated pre-fix build, so the entry was removed rather than
# kept as two). `pad[]`'s size (40) is load-bearing for 6502: found by
# sweeping pad sizes until one shifted `stack`'s own pre-link offset enough,
# relative to this object's own base-shift, to cross the carry boundary -
# a different size (or none at all) can dodge the overflow and pass even on
# a broken assembler, so this exact size is what makes the 6502 half of
# this test meaningful, not incidental. The 65c02 half is still a genuine
# correctness check of the same shape, but a matching 65c02-specific
# trigger size wasn't found in a (fairly wide) sweep, so it isn't confirmed
# to fail on a pre-fix 65c02 build the way the 6502 half is.
cat >switchlib.c <<'EOF'
char *lastPtr;
void printPtr(char *s) {
    lastPtr = s;
}
EOF

cat >switchmain.c <<'EOF'
extern void printPtr(char *s);
extern char *lastPtr;
unsigned char pad[40];
int stack[3];
int sp;
char msg[2];
int main() {
    int gsp;
    pad[0] = 1;
    msg[0] = 65;
    msg[1] = 0;
    stack[0] = 10;
    stack[1] = 11;
    stack[2] = 12;
    sp = 2;
    gsp = sp;
    goto dispatch;
    printPtr(&msg[0]);
dispatch:
    switch (stack[gsp]) {
        case 10:
            break;
        case 11:
            break;
        case 12:
            printPtr(&msg[0]);
            break;
        default:
            break;
    }
    return (lastPtr == &msg[0]) ? 1 : 0;
}
EOF

for target in 6502 65c02; do
    "$RCC" --target="$target" --format=coff switchmain.c >/dev/null
    "$RCC" --target="$target" --format=coff switchlib.c >/dev/null
    "$RCC" --link --target="$target" --format=raw --out="switchprog_$target.flat" "build/switchmain.o" "build/switchlib.o"
    mv "switchprog_$target.flat" "prog_$target.flat"
    run_py65_check m6502_goto_skipped_local "$target"
done

exit $fail
