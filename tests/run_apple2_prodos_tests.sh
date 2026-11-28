#!/usr/bin/env bash
# Exercises src/rt/apple2/apple2.c's ProDOS convenience wrappers (pd_create/
# pd_open/pd_read/pd_write/pd_close/pd_get_file_info and their accessors)
# under py65's 6502 core.
#
# The one piece every wrapper needs help with is $BF00 (the real ProDOS
# MLI entry point, which doesn't exist in this bare simulator): each call
# goes through `asm { jsr g_mli_trampoline; ... }` -> `jsr $BF00`, and
# ProDOS's own calling convention requires returning to (the JSR's return
# address + 3), skipping the three bytes (cmd, paramsAddr lo/hi) the real
# MLI reads out of the instruction stream - a bare RTS stub would instead
# fall through into those bytes as if they were code. MLI_STUB below
# performs that "pop return address, add 3, push it back, return success
# (A=0)" sequence in hand-written bytes, so it's a faithful (if minimal)
# stand-in for the real MLI entry, not just a short-circuit.
#
# Usage: tests/run_apple2_prodos_tests.sh [path/to/rcc]

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

"$RCC" --target=6502 --format=coff "$ROOT_DIR/src/rt/apple2/apple2.c" >/dev/null

run_prodos_test() {
    name="$1"
    "$RCC" --target=6502 --format=coff "$name.c" >/dev/null
    "$RCC" --link --target=6502 --format=raw --out="$name.flat" "build/$name.o" build/apple2.o

    python3 - "$PWD" "$name" <<'PY' || fail=1
import sys
from py65.devices.mpu6502 import MPU

d, name = sys.argv[1] + "/", sys.argv[2]
data = open(d + name + ".flat", "rb").read()
base = 0x0600
mpu = MPU()
mpu.memory[base:base + len(data)] = data

MLI_STUB = [
    0x68, 0x85, 0xFE,        # pla; sta $FE        (low byte of retaddr-1)
    0x68, 0x85, 0xFF,        # pla; sta $FF        (high byte of retaddr-1)
    0x18,                     # clc
    0xA5, 0xFE, 0x69, 0x03, 0x85, 0xFE,  # lda $FE; adc #3; sta $FE
    0xA5, 0xFF, 0x69, 0x00, 0x85, 0xFF,  # lda $FF; adc #0; sta $FF
    0xA5, 0xFF, 0x48,         # lda $FF; pha
    0xA5, 0xFE, 0x48,         # lda $FE; pha
    0xA9, 0x00,               # lda #0              (error code 0)
    0x60,                     # rts
]
mpu.memory[0xBF00:0xBF00 + len(MLI_STUB)] = MLI_STUB

mpu.pc = base
for _ in range(200000):
    pc_before = mpu.pc
    mpu.step()
    if mpu.pc == pc_before:
        break
else:
    raise SystemExit(f"{name}: simulator never reached the halt loop")
val = mpu.memory[2] | (mpu.memory[3] << 8)
assert val == 1, f"{name}: expected return value 1, got {val}"
print(f"PASS {name}")
PY
}

cat >pd_create_test.c <<'EOF'
extern int pd_create(int pathname, unsigned char access, unsigned char fileType, int auxType, unsigned char storageType);
extern unsigned char pd_create_params[12];
int main() {
    int err;
    err = pd_create(4096, 195, 4, 0, 1);
    if (err != 0) return 10;
    if (pd_create_params[0] != 7) return 11;
    if (pd_create_params[1] != 0) return 12;
    if (pd_create_params[2] != 16) return 13;
    if (pd_create_params[3] != 195) return 14;
    if (pd_create_params[4] != 4) return 15;
    if (pd_create_params[5] != 0 || pd_create_params[6] != 0) return 16;
    if (pd_create_params[7] != 1) return 17;
    return 1;
}
EOF
run_prodos_test pd_create_test

cat >pd_open_test.c <<'EOF'
extern int pd_open(int pathname, int buffer);
extern unsigned char pd_open_params[6];
extern unsigned char pd_open_refnum();
int main() {
    int err;
    err = pd_open(8192, 1024);
    if (err != 0) return 10;
    if (pd_open_params[0] != 3) return 11;
    if (pd_open_params[1] != 0 || pd_open_params[2] != 32) return 12;
    if (pd_open_params[3] != 0 || pd_open_params[4] != 4) return 13;
    pd_open_params[5] = 7;
    if (pd_open_refnum() != 7) return 14;
    return 1;
}
EOF
run_prodos_test pd_open_test

cat >pd_read_test.c <<'EOF'
extern int pd_read(unsigned char refNum, int dataBuffer, int requestCount);
extern unsigned char pd_read_params[8];
extern int pd_read_transcount();
int main() {
    int err;
    err = pd_read(5, 2048, 512);
    if (err != 0) return 10;
    if (pd_read_params[0] != 4) return 11;
    if (pd_read_params[1] != 5) return 12;
    if (pd_read_params[2] != 0 || pd_read_params[3] != 8) return 13;
    if (pd_read_params[4] != 0 || pd_read_params[5] != 2) return 14;
    pd_read_params[6] = 52;
    pd_read_params[7] = 18;
    if (pd_read_transcount() != 4660) return 15;
    return 1;
}
EOF
run_prodos_test pd_read_test

cat >pd_write_test.c <<'EOF'
extern int pd_write(unsigned char refNum, int dataBuffer, int requestCount);
extern unsigned char pd_write_params[8];
extern int pd_write_transcount();
int main() {
    int err;
    err = pd_write(6, 3072, 256);
    if (err != 0) return 10;
    if (pd_write_params[0] != 4) return 11;
    if (pd_write_params[1] != 6) return 12;
    if (pd_write_params[2] != 0 || pd_write_params[3] != 12) return 13;
    if (pd_write_params[4] != 0 || pd_write_params[5] != 1) return 14;
    pd_write_params[6] = 0;
    pd_write_params[7] = 1;
    if (pd_write_transcount() != 256) return 15;
    return 1;
}
EOF
run_prodos_test pd_write_test

cat >pd_close_test.c <<'EOF'
extern int pd_close(unsigned char refNum);
extern unsigned char pd_close_params[2];
int main() {
    int err;
    err = pd_close(3);
    if (err != 0) return 10;
    if (pd_close_params[0] != 1) return 11;
    if (pd_close_params[1] != 3) return 12;
    return 1;
}
EOF
run_prodos_test pd_close_test

cat >pd_get_file_info_test.c <<'EOF'
extern int pd_get_file_info(int pathname);
extern unsigned char pd_info_params[18];
extern unsigned char pd_info_access();
extern unsigned char pd_info_file_type();
extern int pd_info_aux_type();
extern unsigned char pd_info_storage_type();
extern int pd_info_blocks_used();
int main() {
    int err;
    err = pd_get_file_info(300);
    if (err != 0) return 10;
    if (pd_info_params[0] != 10) return 11;
    if (pd_info_params[1] != 44 || pd_info_params[2] != 1) return 12;
    pd_info_params[3] = 195;
    pd_info_params[4] = 4;
    pd_info_params[5] = 120;
    pd_info_params[6] = 86;
    pd_info_params[7] = 2;
    pd_info_params[8] = 17;
    pd_info_params[9] = 34;
    if (pd_info_access() != 195) return 13;
    if (pd_info_file_type() != 4) return 14;
    if (pd_info_aux_type() != 22136) return 15;
    if (pd_info_storage_type() != 2) return 16;
    if (pd_info_blocks_used() != 8721) return 17;
    return 1;
}
EOF
run_prodos_test pd_get_file_info_test

exit $fail
