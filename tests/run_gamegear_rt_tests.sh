#!/usr/bin/env bash
# Exercises src/rt/gamegear/gamegear.c's public functions for real under a real, independent
# Z80 core (`z80-python`) - same synthetic-hardware harness shape as run_sms_rt_tests.sh, since
# gamegear.c's VDP/controller/sound primitives are byte-for-byte identical to sms.c's own (see
# gamegear.h's own header comment). This only re-covers the one genuinely different primitive,
# gg_in_start(), plus one VDP/controller primitive each as a sanity check that this port's own
# build of the library behaves the same way.
#
# Usage: tests/run_gamegear_rt_tests.sh [path/to/rcc]

set -eu

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
RCC="$ROOT_DIR/build/bin/rcc"
for arg in "$@"; do
    case "$arg" in
        *) RCC="$arg" ;;
    esac
done
RCC="$(cd "$(dirname "$RCC")" && pwd)/$(basename "$RCC")"

if [ ! -x "$RCC" ]; then
    echo "rcc binary not found at $RCC - build it first ('make')" >&2
    exit 1
fi
if ! python3 -c "import z80_python" >/dev/null 2>&1; then
    echo "z80-python not found - install it (e.g. 'pip install z80-python')" >&2
    exit 1
fi

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
cd "$tmpdir"

fail=0

run_z80() {
    # $1 = .flat path, $2 = start_port value, $3 = ctrl_a value
    python3 - "$1" "${2:-255}" "${3:-255}" <<'PY'
import sys
from z80_python import Z80CPU

with open(sys.argv[1], "rb") as f:
    data = f.read()
start_value = int(sys.argv[2])
ctrl_a_value = int(sys.argv[3])

base = 0
memory = bytearray(0x10000)
memory[base:base + len(data)] = data

vram = bytearray(0x4000)
state = {"addr": 0, "latch": None}
writes = []


def read_port(port):
    port &= 0xFF
    if port == 0x00:
        return start_value
    if port == 0xBE:
        val = vram[state["addr"] & 0x3FFF]
        state["addr"] = (state["addr"] + 1) & 0x3FFF
        return val
    if port == 0xDC:
        return ctrl_a_value
    return 0xFF


def write_port(port, value):
    port &= 0xFF
    writes.append((port, value))
    if port == 0xBE:
        vram[state["addr"] & 0x3FFF] = value
        state["addr"] = (state["addr"] + 1) & 0x3FFF
    elif port == 0xBF:
        if state["latch"] is None:
            state["latch"] = value
        else:
            lo, hi = state["latch"], value
            state["latch"] = None
            if not (hi & 0x80):
                state["addr"] = (lo | ((hi & 0x3F) << 8)) & 0x3FFF


cpu = Z80CPU(memory.__getitem__, memory.__setitem__, read_port=read_port, write_port=write_port)
cpu.pc = base
for _ in range(2_000_000):
    pc_before = cpu.pc
    cpu.step()
    if cpu.pc == pc_before:
        break
else:
    sys.exit("simulator never reached the halt loop (step limit exceeded)")

sys.stdout.write(vram.hex() + "|" + ",".join(f"{p}:{v}" for p, v in writes))
PY
}

cp "$ROOT_DIR/src/rt/gamegear/gamegear.h" "$ROOT_DIR/src/rt/gamegear/gamegear.c" .
"$RCC" --target=z80 --format=coff gamegear.c >/dev/null

# gg_in_start() returns the raw $00 port byte, undecoded - bit 7 is Start, the rest is
# region/status this file deliberately doesn't interpret.
cat >start_main.c <<'EOF'
#include "gamegear.h"
int main() {
    gg_vdp_write_byte(gg_in_start());
    return 0;
}
EOF
"$RCC" --target=z80 --format=coff start_main.c >/dev/null
"$RCC" --link --target=z80 --base=0 --format=raw --out=start.flat build/start_main.o build/gamegear.o
result="$(run_z80 "$PWD/start.flat" 127 255)" || { echo "FAIL gg_in_start: $result"; fail=1; }
IFS='|' read -r vram_hex _ <<<"$result"
python3 - "$vram_hex" <<'PY' || fail=1
import sys
vram = bytes.fromhex(sys.argv[1])
assert vram[0] == 127, f"gg_in_start() should return the raw $00 port byte (127), got {vram[0]}"
print("PASS gg_in_start returns the raw Start-button port byte")
PY

# gg_vdp_write_reg(7, 241) writes the data byte, then the register ORed with $80 - same VDP
# protocol as the SMS's own.
cat >reg_main.c <<'EOF'
#include "gamegear.h"
int main() {
    gg_vdp_write_reg(7, 241);
    return 0;
}
EOF
"$RCC" --target=z80 --format=coff reg_main.c >/dev/null
"$RCC" --link --target=z80 --base=0 --format=raw --out=reg.flat build/reg_main.o build/gamegear.o
result="$(run_z80 "$PWD/reg.flat")" || { echo "FAIL gg_vdp_write_reg: $result"; fail=1; }
IFS='|' read -r _ writes <<<"$result"
if [ "$writes" != "191:241,191:135" ]; then
    echo "FAIL gg_vdp_write_reg: writes were '$writes', expected '191:241,191:135'"
    fail=1
else
    echo "PASS gg_vdp_write_reg writes data then register|0x80"
fi

# gg_in_ctrl_a() returns the raw port A byte, same as sms_in_ctrl_a().
cat >ctrl_main.c <<'EOF'
#include "gamegear.h"
int main() {
    gg_vdp_write_byte(gg_in_ctrl_a());
    return 0;
}
EOF
"$RCC" --target=z80 --format=coff ctrl_main.c >/dev/null
"$RCC" --link --target=z80 --base=0 --format=raw --out=ctrl.flat build/ctrl_main.o build/gamegear.o
result="$(run_z80 "$PWD/ctrl.flat" 255 173)" || { echo "FAIL gg_in_ctrl_a: $result"; fail=1; }
IFS='|' read -r vram_hex _ <<<"$result"
python3 - "$vram_hex" <<'PY' || fail=1
import sys
vram = bytes.fromhex(sys.argv[1])
assert vram[0] == 173, f"gg_in_ctrl_a() should return the raw port A byte (173), got {vram[0]}"
print("PASS gg_in_ctrl_a returns the raw port A byte")
PY

if [ "$fail" -ne 0 ]; then
    echo "gamegear rt tests FAILED" >&2
    exit 1
fi
echo "all gamegear rt tests passed"
