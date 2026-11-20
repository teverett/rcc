#!/usr/bin/env bash
# Exercises src/rt/msx/msx.c's public functions for real under a real, independent Z80 core
# (`z80-python`, same package tests/run_coleco_rt_tests.sh uses). Unlike that file, every
# primitive here reaches hardware through a documented MSX BIOS `CALL`, not a raw port IN/OUT -
# so there's no real BIOS ROM image here to execute into (shipping one would mean bundling
# copyrighted firmware, and this project has none to check byte-for-byte against regardless, the
# same stance src/rt/trs80/'s own header comment takes). Instead, each BIOS entry point under
# test is patched with a single `RET` (0xC9) - or, when a test needs to check a *return* value's
# propagation, a `LD A,n` / `RET` pair - and the harness records every register right before that
# patched `RET` executes, which is exactly the moment msx.c's own `call` instruction expects the
# real BIOS to already be reading its input (or about to hand back its output). This checks the
# calling convention msx.c implements, not real BIOS behavior - there's no real BIOS running here
# to check that against at all.
#
# Usage: tests/run_msx_rt_tests.sh [path/to/rcc]

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

# Runs one linked --target=z80-msx --format=raw image, patching `entry_addr` with `stub_hex`
# (default: a bare RET, 0xc9) before execution, and stopping the moment PC first reaches
# `entry_addr` (recording every register at that instant) OR the halt loop, whichever comes
# first. Prints "A:B:C:D:E:H:L|reached_entry(0/1)".
run_msx() {
    # $1 = .flat path, $2 = entry_addr (decimal), $3 = stub hex bytes (default c9 = RET)
    python3 - "$1" "$2" "${3:-c9}" <<'PY'
import sys
from z80_python import Z80CPU

with open(sys.argv[1], "rb") as f:
    data = f.read()
entry_addr = int(sys.argv[2])
stub = bytes.fromhex(sys.argv[3])

base = 0x4000
memory = bytearray(0x10000)
memory[base:base + len(data)] = data
memory[entry_addr:entry_addr + len(stub)] = stub

reached = {"flag": False, "regs": None}


def tracer(cpu):
    if cpu.pc == entry_addr and not reached["flag"]:
        reached["flag"] = True
        reached["regs"] = (cpu.a, cpu.b, cpu.c, cpu.d, cpu.e, cpu.h, cpu.l)


cpu = Z80CPU(memory.__getitem__, memory.__setitem__, read_port=lambda p: 0xFF, write_port=lambda p, v: None)
cpu.pc = base + 10  # skip the 10-byte cartridge header (sig+init+statement+device+text); start at _start
cpu.sp = 0xF380
for _ in range(2_000_000):
    pc_before = cpu.pc
    tracer(cpu)
    if reached["flag"]:
        break
    cpu.step()
    if cpu.pc == pc_before:  # "halt: jp halt"
        break
else:
    sys.exit("simulator never reached the patched entry point or the halt loop (step limit exceeded)")

a, b, c, d, e, h, l = reached["regs"] if reached["regs"] else (0, 0, 0, 0, 0, 0, 0)
sys.stdout.write(f"{a}:{b}:{c}:{d}:{e}:{h}:{l}|{1 if reached['flag'] else 0}")
PY
}

cp "$ROOT_DIR/src/rt/msx/msx.h" "$ROOT_DIR/src/rt/msx/msx.c" .
"$RCC" --target=z80-msx --format=coff msx.c >/dev/null

compile_link() {
    # $1 = main.c path, $2 = output .flat path
    "$RCC" --target=z80-msx --format=coff "$1" >/dev/null
    "$RCC" --link --target=z80-msx --format=raw --out="$2" "build/$(basename "$1" .c).o" build/msx.o
}

check() {
    # $1 = description, $2 = .flat, $3 = entry addr, $4 = expected, comma-separated "reg=value"
    # pairs to check (only these registers matter - the rest hold whatever an address computation
    # last left behind, which no caller of this BIOS routine is supposed to depend on either).
    # $5 = stub hex (optional)
    local result
    result="$(run_msx "$2" "$3" "${5:-c9}")" || { echo "FAIL $1: $result"; fail=1; return; }
    local regs reached a b c d e h l
    IFS='|' read -r regs reached <<<"$result"
    if [ "$reached" != "1" ]; then
        echo "FAIL $1: never reached entry point $3"
        fail=1
        return
    fi
    IFS=':' read -r a b c d e h l <<<"$regs"
    local pair name expected actual ok=1
    for pair in ${4//,/ }; do
        name="${pair%%=*}"
        expected="${pair##*=}"
        case "$name" in
            a) actual="$a" ;;
            b) actual="$b" ;;
            c) actual="$c" ;;
            d) actual="$d" ;;
            e) actual="$e" ;;
            h) actual="$h" ;;
            l) actual="$l" ;;
        esac
        if [ "$actual" != "$expected" ]; then
            echo "FAIL $1: register $name was $actual, expected $expected (full: $regs)"
            fail=1
            ok=0
        fi
    done
    [ "$ok" -eq 1 ] && echo "PASS $1"
}

# msx_putc(65) ('A') should load A with the character before CALLing CHPUT ($00a2 = 162).
cat >putc_main.c <<'EOF'
#include "msx.h"
int main() {
    msx_putc(65);
    return 0;
}
EOF
compile_link putc_main.c putc.flat
check "msx_putc loads A with the character" putc.flat 162 "a=65"

# msx_vdp_write_reg(7, 241) should load C with the register number and B with the value before
# CALLing WRTVDP ($0047 = 71) - C=register, B=data per the MSX2 Technical Handbook's own listing.
cat >vdp_main.c <<'EOF'
#include "msx.h"
int main() {
    msx_vdp_write_reg(7, 241);
    return 0;
}
EOF
compile_link vdp_main.c vdp.flat
check "msx_vdp_write_reg loads C=register, B=data" vdp.flat 71 "c=7,b=241"

# msx_psg_write(14, 9) should load A with the register number and E with the value before
# CALLing WRTPSG ($0093 = 147) - A=register, E=data per the same source.
cat >psg_main.c <<'EOF'
#include "msx.h"
int main() {
    msx_psg_write(14, 9);
    return 0;
}
EOF
compile_link psg_main.c psg.flat
check "msx_psg_write loads A=register, E=data" psg.flat 147 "a=14,e=9"

# msx_vram_write(0x2000, 42) should load HL with the address and A with the value before CALLing
# WRTVRM ($004d = 77).
cat >vrmw_main.c <<'EOF'
#include "msx.h"
int main() {
    msx_vram_write(8192, 42);
    return 0;
}
EOF
compile_link vrmw_main.c vrmw.flat
check "msx_vram_write loads HL=address, A=value" vrmw.flat 77 "a=42,h=32,l=0"

# msx_vram_read(0x2000) should load HL with the address before CALLing RDVRM ($004a = 74).
cat >vrmr_main.c <<'EOF'
#include "msx.h"
int main() {
    msx_vram_read(8192);
    return 0;
}
EOF
compile_link vrmr_main.c vrmr.flat
check "msx_vram_read loads HL=address" vrmr.flat 74 "h=32,l=0"

# msx_getc() should return CHGET's own result (A) - here CHGET is stubbed as "LD A,88 ; RET"
# (0x3e 0x58 0xc9), and the function's own C return value should come back as 88 ('X').
cat >getc_main.c <<'EOF'
#include "msx.h"
unsigned char rt_result;
int main() {
    rt_result = msx_getc();
    return 0;
}
EOF
compile_link getc_main.c getc.flat
result="$(run_msx "$PWD/getc.flat" 159 3e58c9)" || { echo "FAIL msx_getc propagates CHGET's result: $result"; fail=1; }
# After the stubbed CHGET returns, execution continues past the CALL into "ld [g_rt_a],a" and the
# halt loop - re-run tracing the halt loop's own final A value via a second pass that lets the
# whole stub execute (tracer only fires once per address, so letting the program run to
# completion and checking the stored global in build output isn't available here without a VRAM
# dump mechanism - instead this reuses run_msx's register snapshot at the *next* occurrence of
# the halt address, which already has A=88 from the stub).
python3 - "$PWD/getc.flat" <<'PY' || fail=1
import sys
from z80_python import Z80CPU

with open(sys.argv[1], "rb") as f:
    data = f.read()
base = 0x4000
memory = bytearray(0x10000)
memory[base:base + len(data)] = data
memory[159:159 + 3] = bytes.fromhex("3e58c9")  # CHGET stub: LD A,88 ; RET

cpu = Z80CPU(memory.__getitem__, memory.__setitem__, read_port=lambda p: 0xFF, write_port=lambda p, v: None)
cpu.pc = base + 10
cpu.sp = 0xF380
for _ in range(2_000_000):
    pc_before = cpu.pc
    cpu.step()
    if cpu.pc == pc_before:
        break
else:
    sys.exit("simulator never reached the halt loop (step limit exceeded)")

assert cpu.a == 88, f"msx_getc() should return CHGET's own A (88), got {cpu.a}"
print("PASS msx_getc propagates CHGET's return value")
PY

if [ "$fail" -ne 0 ]; then
    echo "msx rt tests FAILED" >&2
    exit 1
fi
echo "all msx rt tests passed"
