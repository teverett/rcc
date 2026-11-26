#!/usr/bin/env bash
# Exercises src/rt/imsai/imsai.c's public functions for real under a real,
# independent Z80 core (`z80-python`, same package tests/run_coleco_rt_tests.sh
# uses) standing in for the 8080 (object-code compatible - see
# src/codegen/i8080/I8080Assembler.h's own header comment). imsai.c reaches
# hardware entirely through `IN`/`OUT` (the SIO-2 is port-mapped, not
# memory-mapped - see imsai.h's own header comment), so each test here
# supplies z80-python's own `read_port`/`write_port` callbacks instead of
# inspecting a plain memory array.
#
# One real Z80 hardware quirk this harness has to account for, same as
# run_coleco_rt_tests.sh: `OUT (n),A`/`IN A,(n)` put A/the port's own
# immediate on the address bus's own upper byte too, so every
# `write_port`/`read_port` callback here masks its own `port` argument
# down to 8 bits.
#
# Usage: tests/run_imsai_rt_tests.sh [path/to/rcc]

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

# Common synthetic-hardware harness: status port ($03) always reports
# whatever fixed bits the caller picks (so imsai_putc()/imsai_getc()'s own
# busy-wait never blocks), data port ($02) reads return a fixed byte and
# every write to it is recorded in order - enough to check imsai.c's own
# primitives directly without needing a model of a real 8251 at all.
run_z80() {
    # $1 = .flat path, $2 = status byte, $3 = data-port read value (optional)
    python3 - "$1" "$2" "${3:-0}" <<'PY'
import sys
from z80_python import Z80CPU

with open(sys.argv[1], "rb") as f:
    data = f.read()
status_value = int(sys.argv[2])
data_in_value = int(sys.argv[3])

base = 0x0100
memory = bytearray(0x10000)
memory[base:base + len(data)] = data

writes = []
reads = []


def read_port(port):
    port &= 0xFF
    reads.append(port)
    if port == 0x03:
        return status_value
    if port == 0x02:
        return data_in_value
    return 0xFF


def write_port(port, value):
    port &= 0xFF
    writes.append((port, value))


cpu = Z80CPU(memory.__getitem__, memory.__setitem__, read_port=read_port, write_port=write_port)
cpu.pc = base
for _ in range(2_000_000):
    pc_before = cpu.pc
    cpu.step()
    if cpu.pc == pc_before:  # "halt: jmp halt"
        break
else:
    sys.exit("simulator never reached the halt loop (step limit exceeded)")

hl = (cpu.h << 8) | cpu.l
sys.stdout.write(str(hl) + "|" + ",".join(f"{p}:{v}" for p, v in writes) + "|" + ",".join(str(r) for r in reads))
PY
}

cp "$ROOT_DIR/src/rt/imsai/imsai.h" "$ROOT_DIR/src/rt/imsai/imsai.c" .
"$RCC" --target=8080 --format=coff imsai.c >/dev/null

# imsai_putc(c) busy-waits on IMSAI_SIO_TXRDY (status bit 0), then issues
# exactly one OUT to the data port ($02) with that byte - nothing else
# observable.
cat >putc_main.c <<'EOF'
#include "imsai.h"
int main() {
    imsai_putc('A');
    return 0;
}
EOF
"$RCC" --target=8080 --format=coff putc_main.c >/dev/null
"$RCC" --link --target=8080 --format=raw --out=putc.flat build/putc_main.o build/imsai.o

result="$(run_z80 "$PWD/putc.flat" 1)" || { echo "FAIL imsai_putc: $result"; fail=1; }
IFS='|' read -r _ writes _ <<<"$result"
python3 - "$writes" <<'PY' || fail=1
import sys
writes = sys.argv[1]
assert writes == "2:65", f"expected exactly one write (2:65), got {writes!r}"
print("PASS imsai_putc_writes_one_byte_to_data_port")
PY

# imsai_puts(s) writes s's own bytes to the data port in order, one
# imsai_putc() call at a time, with no newline translation.
cat >puts_main.c <<'EOF'
#include "imsai.h"
int main() {
    imsai_puts("HI\r\n");
    return 0;
}
EOF
"$RCC" --target=8080 --format=coff puts_main.c >/dev/null
"$RCC" --link --target=8080 --format=raw --out=puts.flat build/puts_main.o build/imsai.o

result="$(run_z80 "$PWD/puts.flat" 1)" || { echo "FAIL imsai_puts: $result"; fail=1; }
IFS='|' read -r _ writes _ <<<"$result"
python3 - "$writes" <<'PY' || fail=1
import sys
writes = sys.argv[1]
expected = ",".join(f"2:{b}" for b in b"HI\r\n")
assert writes == expected, f"expected {expected!r}, got {writes!r}"
print("PASS imsai_puts_writes_string_in_order")
PY

# imsai_getc() busy-waits on IMSAI_SIO_RXRDY (status bit 1), then returns
# exactly the byte read from the data port ($02), with no other port
# touched besides the status poll.
cat >getc_main.c <<'EOF'
#include "imsai.h"
int main() {
    return imsai_getc();
}
EOF
"$RCC" --target=8080 --format=coff getc_main.c >/dev/null
"$RCC" --link --target=8080 --format=raw --out=getc.flat build/getc_main.o build/imsai.o

result="$(run_z80 "$PWD/getc.flat" 2 90)" || { echo "FAIL imsai_getc: $result"; fail=1; }
IFS='|' read -r hl writes reads <<<"$result"
python3 - "$hl" "$writes" "$reads" <<'PY' || fail=1
import sys
hl = int(sys.argv[1])
writes = sys.argv[2]
reads = sys.argv[3].split(",")
assert hl == 90, f"expected imsai_getc() == 90, got {hl}"
assert writes == "", f"expected no writes, got {writes!r}"
assert "3" in reads, "expected a status-port ($03) poll before the data-port read"
assert "2" in reads, "expected a data-port ($02) read"
print("PASS imsai_getc_returns_byte_from_data_port")
PY

exit $fail
