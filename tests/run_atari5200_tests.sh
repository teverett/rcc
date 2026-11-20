#!/usr/bin/env bash
# Checks --target=5200: that it actually relocates its zero-page pseudo-registers/frame-storage
# region clear of the Atari 5200 BIOS's own zero-page use and its $0200-$021B interrupt vector
# table (see M6502Codegen.h's own `Atari5200` comment), that --link --format=a5200 produces a
# correctly-sized, correctly-vectored cartridge image at every standard size, that a real
# program actually runs correctly under that memory map (in py65's bare 6502 core - no BIOS ROM
# is simulated, so this only confirms the compiler's own codegen/linker correctness, not real
# hardware behavior), and that --link rejects mixing a 5200 object with a plain 6502 one - the
# real point of giving 5200 its own distinct machine ID (kM6502Atari5200CoffMachine).
#
# Usage: tests/run_atari5200_tests.sh [path/to/rcc]

set -eu

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
RCC="$ROOT_DIR/build/bin/rcc"
for arg in "$@"; do
    case "$arg" in
        *) RCC="$arg" ;;
    esac
done
# Resolve to an absolute path before the later `cd "$tmpdir"` below - see
# docs/known-bugs.md's "make test's Apple II/TRS-80/ColecoVision runtime
# test scripts break when $(TARGET) is a relative path" entry, which this
# script avoids from the start.
RCC="$(cd "$(dirname "$RCC")" && pwd)/$(basename "$RCC")"

if [ ! -x "$RCC" ]; then
    echo "rcc binary not found at $RCC - build it first ('make')" >&2
    exit 1
fi

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
cd "$tmpdir"

fail=0

cat >main.c <<'EOF'
int main() {
    return 42;
}
EOF
"$RCC" --target=5200 --format=coff --out=main.o main.c >/dev/null

# --format=a5200 at every standard cartridge size: correct total size, with CARTENTRY (the last
# 2 bytes) pointing at the program's own base, and the 20-byte name field ahead of it blank.
for pair in "B000:4096" "A000:8192" "8000:16384" "4000:32768"; do
    base="${pair%%:*}"
    size="${pair##*:}"
    "$RCC" --link --target=5200 --format=a5200 --base="\$$base" --out="main_$base.a5200" main.o

    python3 - "$PWD" "$base" "$size" <<'PY' || fail=1
import sys
d, base, size = sys.argv[1] + "/", int(sys.argv[2], 16), int(sys.argv[3])
data = open(d + "main_%04X.a5200" % base, "rb").read()
assert len(data) == size, f"${base:04X}: expected {size} bytes, got {len(data)}"
entry = data[-2] | (data[-1] << 8)
assert entry == base, f"${base:04X}: CARTENTRY should equal {base:04X}, got {entry:04X}"
assert data[-24:-4] == b" " * 20, f"${base:04X}: name field should be blank"
print(f"PASS atari5200_a5200_format_{base:04X} (size={size})")
PY
done

# The zero-page pseudo-registers: codegen for 5200 must place every one of them at $0020 or
# above (clear of the BIOS's own zero-page use), checked directly against the assembled bytes
# (the `sta sp`/`sta sp+1` initialization in _start - `sp` itself is one of the pseudo-
# registers, at zeroPageBase()+12), not just by inference from the source. The *value* loaded
# into it (the frame-storage bump allocator's own seed) must likewise be $0300 or above, clear
# of the BIOS's own $0200-$021B interrupt vector table.
"$RCC" --link --target=5200 --format=raw --base='$8000' --out=main.raw main.o
python3 - "$PWD" <<'PY' || fail=1
import sys
d = sys.argv[1] + "/"
data = open(d + "main.raw", "rb").read()
# _start: ldai <sp_lo>; sta sp; ldai <sp_hi>; sta sp+1 - opcodes A9 <lo> 8D <addr_lo> <addr_hi> A9 <hi> 8D <addr_lo+1> <addr_hi>
assert data[0] == 0xA9, "expected _start to begin with LDA #imm (A9)"
assert data[2] == 0x8D, "expected STA abs (8D) after the first LDA"
sp_addr = data[3] | (data[4] << 8)
assert sp_addr >= 0x0020, f"sp's own zero-page address ({sp_addr:#x}) must be at $0020 or above - $0000-$001F is the BIOS's own zero-page use"
sp_seed = data[1] | (data[6] << 8)
assert sp_seed >= 0x0300, f"sp's seeded value ({sp_seed:#x}) must be at $0300 or above - $0200-$021B is the BIOS's own interrupt vector table"
print(f"PASS atari5200_zero_page_and_frame_stack_clear_of_bios (sp at {sp_addr:#x}, seeded to {sp_seed:#x})")
PY

# A real program actually executes correctly under this memory map (py65 bare core - no BIOS ROM
# simulated, so this exercises the compiler's own codegen/linker output, not real 5200
# hardware behavior).
python3 - "$PWD" <<'PY' || fail=1
import sys
from py65.devices.mpu6502 import MPU

d = sys.argv[1] + "/"
data = open(d + "main.raw", "rb").read()
base = 0x8000
mpu = MPU()
mpu.memory[base:base + len(data)] = data
mpu.pc = base
for _ in range(200000):
    pc_before = mpu.pc
    mpu.step()
    if mpu.pc == pc_before:
        break
else:
    raise SystemExit("simulator never reached the halt loop")
val = mpu.memory[0x22] | (mpu.memory[0x23] << 8)  # ac/ac+1 at zeroPageBase()+2 ($0020+2)
if val >= 0x8000:
    val -= 0x10000
assert val == 42, f"expected 42, got {val}"
print("PASS atari5200_runs_correctly (result=%d)" % val)
PY

# The real point of 5200's own distinct machine ID: --link must reject mixing a 5200 object with
# a plain 6502 one, instead of silently linking two objects whose zero-page/frame-storage layouts
# disagree.
"$RCC" --target=6502 --format=coff --out=main6502.o main.c >/dev/null
if "$RCC" --link --target=6502 --format=raw --out=bad.raw main.o 2>/tmp/atari5200_mismatch_err.txt; then
    echo "FAIL atari5200_rejects_cross_target_link: expected this link to fail" >&2
    fail=1
elif ! grep -q "machine type" /tmp/atari5200_mismatch_err.txt; then
    echo "FAIL atari5200_rejects_cross_target_link: wrong error:" >&2
    cat /tmp/atari5200_mismatch_err.txt >&2
    fail=1
else
    echo "PASS atari5200_rejects_cross_target_link"
fi
rm -f /tmp/atari5200_mismatch_err.txt

exit $fail
