#!/usr/bin/env bash
# Exercises src/rt/sms/sms.c's public functions for real under a real, independent Z80 core
# (`z80-python`, same package tests/run_coleco_rt_tests.sh uses) - sms.c reaches hardware
# entirely through IN/OUT, the same shape coleco.c's own harness already covers, so this reuses
# its synthetic-hardware approach rather than inventing a new one. One real difference from
# coleco's own harness: `OUT (n),A` masks `port` down to 8 bits for the same reason
# run_coleco_rt_tests.sh's own header comment gives (A ends up on the address bus's own upper
# byte too, which a real SMS's simple port decoder never looks at).
#
# Usage: tests/run_sms_rt_tests.sh [path/to/rcc]

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
    # $1 = .flat path, $2 = ctrl_a value, $3 = ctrl_b value
    # prints "VRAM=<hex>|WRITES=<port>:<value>,...|READS=<port>,..."
    python3 - "$1" "${2:-255}" "${3:-255}" <<'PY'
import sys
from z80_python import Z80CPU

with open(sys.argv[1], "rb") as f:
    data = f.read()
ctrl_a_value = int(sys.argv[2])
ctrl_b_value = int(sys.argv[3])

base = 0
memory = bytearray(0x10000)
memory[base:base + len(data)] = data

vram = bytearray(0x4000)
state = {"addr": 0, "latch": None}
writes = []
reads = []


def read_port(port):
    port &= 0xFF
    reads.append(port)
    if port == 0xBE:
        val = vram[state["addr"] & 0x3FFF]
        state["addr"] = (state["addr"] + 1) & 0x3FFF
        return val
    if port == 0xDC:
        return ctrl_a_value
    if port == 0xDD:
        return ctrl_b_value
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

sys.stdout.write(vram.hex() + "|" + ",".join(f"{p}:{v}" for p, v in writes) + "|" + ",".join(str(r) for r in reads))
PY
}

cp "$ROOT_DIR/src/rt/sms/sms.h" "$ROOT_DIR/src/rt/sms/sms.c" .
"$RCC" --target=z80 --format=coff sms.c >/dev/null

# sms_vdp_write_reg(7, 241) writes the data byte, then the register ORed with $80 - two ordered
# VDP control-port writes, nothing else.
cat >reg_main.c <<'EOF'
#include "sms.h"
int main() {
    sms_vdp_write_reg(7, 241);
    return 0;
}
EOF
"$RCC" --target=z80 --format=coff reg_main.c >/dev/null
"$RCC" --link --target=z80 --base=0 --format=raw --out=reg.flat build/reg_main.o build/sms.o
result="$(run_z80 "$PWD/reg.flat")" || { echo "FAIL sms_vdp_write_reg: $result"; fail=1; }
IFS='|' read -r _ writes _ <<<"$result"
if [ "$writes" != "191:241,191:135" ]; then
    echo "FAIL sms_vdp_write_reg: writes were '$writes', expected '191:241,191:135' (241, then reg 7|0x80=135)"
    fail=1
else
    echo "PASS sms_vdp_write_reg writes data then register|0x80"
fi

# sms_vdp_set_write_address(0x2000) followed by two sms_vdp_write_byte() calls should land both
# bytes at VRAM $2000/$2001, auto-incrementing, through the VDP data port ($BE).
cat >vram_main.c <<'EOF'
#include "sms.h"
int main() {
    sms_vdp_set_write_address(8192);
    sms_vdp_write_byte(1);
    sms_vdp_write_byte(2);
    return 0;
}
EOF
"$RCC" --target=z80 --format=coff vram_main.c >/dev/null
"$RCC" --link --target=z80 --base=0 --format=raw --out=vram.flat build/vram_main.o build/sms.o
result="$(run_z80 "$PWD/vram.flat")" || { echo "FAIL sms vram write: $result"; fail=1; }
IFS='|' read -r vram_hex _ _ <<<"$result"
python3 - "$vram_hex" <<'PY' || fail=1
import sys
vram = bytes.fromhex(sys.argv[1])
assert vram[0x2000] == 1 and vram[0x2001] == 2, "the two writes should land at $2000/$2001 in order"
assert vram[0x1FFF] == 0 and vram[0x2002] == 0, "neighboring VRAM should be untouched"
print("PASS sms_vdp_set_write_address/sms_vdp_write_byte auto-increment correctly")
PY

# sms_in_ctrl_a()/sms_in_ctrl_b() should read back exactly the port's own raw byte, undecoded.
cat >ctrl_main.c <<'EOF'
#include "sms.h"
unsigned char g_a;
unsigned char g_b;
int main() {
    g_a = sms_in_ctrl_a();
    g_b = sms_in_ctrl_b();
    sms_vdp_write_byte(g_a);
    sms_vdp_write_byte(g_b);
    return 0;
}
EOF
"$RCC" --target=z80 --format=coff ctrl_main.c >/dev/null
"$RCC" --link --target=z80 --base=0 --format=raw --out=ctrl.flat build/ctrl_main.o build/sms.o
result="$(run_z80 "$PWD/ctrl.flat" 190 101)" || { echo "FAIL sms controller read: $result"; fail=1; }
IFS='|' read -r vram_hex _ _ <<<"$result"
python3 - "$vram_hex" <<'PY' || fail=1
import sys
vram = bytes.fromhex(sys.argv[1])
assert vram[0] == 190, f"sms_in_ctrl_a() should return the raw port A byte (190), got {vram[0]}"
assert vram[1] == 101, f"sms_in_ctrl_b() should return the raw port B byte (101), got {vram[1]}"
print("PASS sms_in_ctrl_a/sms_in_ctrl_b return the raw port bytes")
PY

# sms_out_sound() writes exactly one byte to the SN76489's own port ($7F = 127), nothing else.
cat >sound_main.c <<'EOF'
#include "sms.h"
int main() {
    sms_out_sound(159);
    return 0;
}
EOF
"$RCC" --target=z80 --format=coff sound_main.c >/dev/null
"$RCC" --link --target=z80 --base=0 --format=raw --out=sound.flat build/sound_main.o build/sms.o
result="$(run_z80 "$PWD/sound.flat")" || { echo "FAIL sms_out_sound: $result"; fail=1; }
IFS='|' read -r _ writes _ <<<"$result"
if [ "$writes" != "127:159" ]; then
    echo "FAIL sms_out_sound: writes were '$writes', expected '127:159'"
    fail=1
else
    echo "PASS sms_out_sound writes the SN76489's own port"
fi

if [ "$fail" -ne 0 ]; then
    echo "sms rt tests FAILED" >&2
    exit 1
fi
echo "all sms rt tests passed"
