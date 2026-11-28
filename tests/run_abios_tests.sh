#!/usr/bin/env bash
# Exercises src/rt/abios/'s real Advanced BIOS (ABIOS) client library and the two new
# `src/codegen/x86_8086/I8086Assembler.cpp` primitives it needed (`push`/`pop` of a
# segment register, and `callf [mem]`, a far indirect call through a stored real-mode far
# pointer). Build/link and byte-exact-encoding checks only - no emulator available to this
# project (DOSBox-X, QEMU) implements real ABIOS, only the conventional BIOS
# `src/rt/pcbios/`'s own tests (`run_pcbios_tests.sh`) exercise for real - see
# `src/rt/abios/abios.h`'s own opening comment.
#
# Usage: tests/run_abios_tests.sh [path/to/rcc]

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

ABIOS_DIR="$ROOT_DIR/src/rt/abios"

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
cd "$tmpdir"
cp "$ABIOS_DIR/abios.h" "$ABIOS_DIR/abios.c" .

fail=0
check() {
    local label="$1" ok="$2" detail="$3"
    if [ "$ok" = "1" ]; then
        echo "PASS $label"
    else
        echo "FAIL $label: $detail"
        fail=1
    fi
}

# abios_lib_builds_with_no_warnings: compiles clean (any warning here would fail the
# build under this project's own -Werror-equivalent discipline - see AGENTS.md).
out="$("$RCC" --target=8086-dos --format=coff --out=abios.o abios.c 2>&1)"
[ -z "$out" ] && [ -f abios.o ] && ok=1 || ok=0
check abios_lib_builds_with_no_warnings "$ok" "expected clean build, got: $out"

# abios_client_program_links: a program that calls every public function in the library
# (abios_init(), both *_available() checks, both ping functions and their three
# accessors) compiles, links against abios.o, and produces a nonempty raw binary -
# proving every symbol this header declares actually resolves, not just that abios.c
# itself compiles in isolation.
cat >client.c <<'EOF'
#include "abios.h"

int main() {
    int status;
    status = abios_init();
    if (status == ABIOS_INIT_OK) {
        if (abios_keyboard_available()) {
            abios_ping_keyboard();
            abios_ping_device_id();
            abios_ping_secondary_device_id();
            abios_ping_revision();
        }
        if (abios_video_available()) {
            abios_ping_video();
        }
    }
    return status;
}
EOF
"$RCC" --target=8086-dos --format=coff --out=client.o client.c >/dev/null
"$RCC" --link --target=8086-dos --format=raw --out=client.com client.o abios.o >/dev/null
[ -s client.com ] && ok=1 || ok=0
check abios_client_program_links "$ok" "expected a nonempty linked client.com"

# abios_link_is_deterministic: linking the same inputs twice produces byte-identical
# output - same regression shape `run_debugmap_link_tests.sh` uses, catching anything
# that reads uninitialized state (e.g. a missed zero-init on abios_keyboard_lid/
# abios_video_lid) and so varies between runs.
"$RCC" --link --target=8086-dos --format=raw --out=client2.com client.o abios.o >/dev/null
if cmp -s client.com client2.com; then ok=1; else ok=0; fi
check abios_link_is_deterministic "$ok" "expected two linked runs to produce identical bytes"

# push_pop_segreg_and_callf_encode_correctly: byte-exact check of the two new assembler
# primitives abios.c depends on, against the Intel 8086 reference encodings -
# push es/cs/ss/ds = 0x06/0x0E/0x16/0x1E, pop es/ss/ds = 0x07/0x17/0x1F, and
# `callf [mem]` = 0xFF 0x1E <disp16> (ModRM mod=00,reg=011,rm=110, i.e. CALL FAR
# m16:16 through a direct memory operand).
cat >asmtest.c <<'EOF'
int ptr;
void foo() {
    asm {
        push es
        push cs
        push ss
        push ds
        pop es
        pop ss
        pop ds
        callf [g_ptr]
    }
}
int main() {
    foo();
    return 0;
}
EOF
"$RCC" --target=8086-dos --format=raw --out=asmtest.bin asmtest.c >/dev/null
got="$(od -An -tx1 asmtest.bin | tr -d ' \n')"
case "$got" in
    *060e161e07171fff1e*) ok=1 ;;
    *) ok=0 ;;
esac
check push_pop_segreg_and_callf_encode_correctly "$ok" "expected 06 0e 16 1e 07 17 1f ff 1e somewhere in the output, got $got"

# pop_cs_is_rejected: `pop cs` is not a valid 8086 instruction (no such opcode exists -
# see I8086Assembler.cpp's own comment) - the assembler must fail it, not silently emit
# something else or accept it as a general-register pop.
cat >popcs.c <<'EOF'
void foo() {
    asm {
        pop cs
    }
}
int main() {
    foo();
    return 0;
}
EOF
if "$RCC" --target=8086-dos --format=raw --out=popcs.bin popcs.c >popcs.out 2>&1; then
    ok=0
else
    ok=1
fi
[ -f popcs.bin ] && ok=0
check pop_cs_is_rejected "$ok" "expected a compile failure and no output file for 'pop cs'"

exit $fail
