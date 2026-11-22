#!/usr/bin/env bash
# Unit tests for AddressHoisting.cpp's own makeAddrTemp(): the hoisted
# base-address temp it introduces is assigned exactly once, by its own
# declaration - never reassigned afterward, by construction - which is
# precisely `const`'s own "never written after declaration" contract.
# Marking it `isConst` lets Pdp7Codegen.h's own per-slot `register`/
# `const` extension of the leaf fast path ("usesStaticCell()") give it
# one cheap static cell even in a non-leaf-but-reentrancy-safe
# function (one `@nonrecursive`, with some OTHER wide/array/struct
# local disqualifying the FULL leaf path) - instead of the full
# frame-relative reload (`lac __fp; tad N; dac __addr; lac __addr i` -
# 4 words) at every one of its own uses. Confirmed directly against a
# repro matching `sys/s6.c`'s own `pget()` et al.'s general shape (an
# `@nonrecursive` function with both an array local and a dynamically-
# indexed array access).
#
# Two tests:
#   test_hoisted_addr_uses_static_cell_in_nonrecursive_function - the
#       hoisted temp is read via a direct `lac`, not a frame-relative
#       reload, on its SECOND use (the one that actually benefits -
#       its first use, right at its own declaration, costs the same
#       either way).
#   test_hoisted_addr_correct_with_array_local_forcing_frame - the
#       real read/write values through the hoisted address are still
#       correct, verified on the real simulator.
#
# Usage: tests/run_pdp7_hoisted_addr_const_tests.sh [path/to/rcc]

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RCC="$SCRIPT_DIR/../build/bin/rcc"
[ $# -ge 1 ] && RCC="$1"
PDP7_SIM="${PDP7_SIM:-pdp7}"

if [ ! -x "$RCC" ]; then
    echo "rcc binary not found at $RCC - build it first ('make')" >&2
    exit 1
fi

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
fail=0

FIXTURE='int arr[20];
int helper(int n) { return n; }
int f(int idx) @nonrecursive {
    int junk[3];
    junk[0] = helper(idx);
    arr[idx] = arr[idx] - 1;
    return arr[idx];
}
int main() {
    return f(5);
}'

test_hoisted_addr_uses_static_cell_in_nonrecursive_function() {
    local dir="$tmpdir/static"
    mkdir -p "$dir"
    printf '%s\n' "$FIXTURE" >"$dir/t.c"
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_hoisted_addr_uses_static_cell_in_nonrecursive_function: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    # The hoisted temp's own direct-cell read ("lac FN_f_LF<n>", no
    # frame-address computation ahead of it) must appear at least
    # once; a version without this fix only ever reaches it through
    # "lac __addr i" after computing __fp+offset first.
    if ! grep -qE '^   lac FN_f_LF[0-9]+$' "$dir/t.s"; then
        echo "FAIL test_hoisted_addr_uses_static_cell_in_nonrecursive_function: expected a" \
             "direct 'lac FN_f_LF<n>' read of the hoisted temp, not found:"
        cat "$dir/t.s"
        return 1
    fi
    echo "PASS test_hoisted_addr_uses_static_cell_in_nonrecursive_function (direct cell read found)"
    return 0
}

test_hoisted_addr_correct_with_array_local_forcing_frame() {
    local dir="$tmpdir/correct"
    mkdir -p "$dir"
    printf '%s\n' "$FIXTURE" >"$dir/t.c"
    if ! "$RCC" --target=pdp7 --format=rim --optimize --out="$dir/t.rim" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_hoisted_addr_correct_with_array_local_forcing_frame: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    local ac
    ac="$("$PDP7_SIM" <<EOF 2>&1 | grep -oE 'AC:[[:space:]]*[0-7]+' | tail -1 | grep -oE '[0-7]+$'
load $dir/t.rim 10000
go 10000
e ac
EOF
)"
    # arr[5] starts at 0 (zero-initialized global); arr[5] = arr[5]-1
    # = -1 (0777777 octal, 18-bit two's complement); f() returns it.
    if [ "$ac" != "777777" ]; then
        echo "FAIL test_hoisted_addr_correct_with_array_local_forcing_frame: expected AC=777777" \
             "(-1, arr[5]-1 through the hoisted address), got octal $ac"
        return 1
    fi
    echo "PASS test_hoisted_addr_correct_with_array_local_forcing_frame (AC=777777)"
    return 0
}

test_hoisted_addr_uses_static_cell_in_nonrecursive_function || fail=1
test_hoisted_addr_correct_with_array_local_forcing_frame || fail=1

exit $fail
