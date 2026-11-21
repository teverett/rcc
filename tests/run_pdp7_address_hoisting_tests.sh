#!/usr/bin/env bash
# Unit tests for the AddressHoisting optimizer pass
# (src/optimizers/address_hoisting/) on the pdp7 backend, where the
# pattern it fixes is most visible: pdp7's `law` instruction loads its
# own raw opcode bits alongside the address it's naming, so every
# re-derivation of a real array's base address costs a `law` plus a
# masking `and` (`law G_name` / `and 8191`) - 2 words every time, repeated
# at every textual array access before this pass existed (see
# docs/known-bugs.md's former "pattern 1" entry). With the pass, a
# repeatedly-accessed array's base address is computed once (into a
# fresh `__addrhoist<N>` temporary) regardless of how many times the
# array is touched.
#
# Three tests:
#   test_law_emitted_once      - a function touching the same global
#       array 3 times with a NON-constant index (two reads, one write,
#       all through a parameter) must emit exactly one `law G_arr` in
#       its generated assembly, not three.
#   test_single_use_not_hoisted - a function touching a global array only
#       ONCE must NOT get a hoisting temporary at all (hoisting a
#       single-use address only adds overhead, see AddressHoisting.cpp's
#       own `uses.size() < 2` guard) - confirmed by the absence of any
#       `__addrhoist` label.
#   test_all_constant_indices_not_hoisted - a function touching the same
#       global array 3 times, every access a compile-time-constant
#       index, must NOT hoist at all (see AddressHoisting.cpp's own
#       guard for this - direct `label+offset` addressing, covered by
#       tests/run_pdp7_direct_element_operand_tests.sh, dominates a
#       hoisted base plus a runtime add at every one of those sites).
#
# Usage: tests/run_pdp7_address_hoisting_tests.sh [path/to/rcc]

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RCC="$SCRIPT_DIR/../build/bin/rcc"
[ $# -ge 1 ] && RCC="$1"

if [ ! -x "$RCC" ]; then
    echo "rcc binary not found at $RCC - build it first ('make')" >&2
    exit 1
fi

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
fail=0

test_law_emitted_once() {
    local dir="$tmpdir/law_once"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int arr[5];
int main() {
    int i;
    int x;
    i = get();
    x = arr[i] + arr[i + 1];
    arr[i + 2] = x;
    return x;
}
int get() { return 0; }
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_law_emitted_once: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    local lawCount
    lawCount="$(grep -c 'law G_arr' "$dir/t.s")"
    if [ "$lawCount" -ne 1 ]; then
        echo "FAIL test_law_emitted_once: expected exactly 1 'law G_arr' (one hoisted base" \
             "address reused by all 3 accesses), found $lawCount"
        return 1
    fi
    echo "PASS test_law_emitted_once (1 occurrence of 'law G_arr' for 3 accesses)"
    return 0
}

test_all_constant_indices_not_hoisted() {
    local dir="$tmpdir/all_constant"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int arr[5];
int main() {
    int x;
    x = arr[0] + arr[1];
    arr[2] = x;
    return x;
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_all_constant_indices_not_hoisted: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if grep -q '__addrhoist' "$dir/t.s"; then
        echo "FAIL test_all_constant_indices_not_hoisted: expected no hoisting when every" \
             "index is a compile-time constant, found an '__addrhoist' temporary:"
        cat "$dir/t.s"
        return 1
    fi
    if grep -q 'law G_arr' "$dir/t.s"; then
        echo "FAIL test_all_constant_indices_not_hoisted: expected no 'law G_arr' at all" \
             "(every access should address G_arr+offset directly), found one:"
        cat "$dir/t.s"
        return 1
    fi
    echo "PASS test_all_constant_indices_not_hoisted (no hoisting, no 'law G_arr')"
    return 0
}

test_single_use_not_hoisted() {
    local dir="$tmpdir/single_use"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int arr[5];
int main() {
    return arr[0];
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_single_use_not_hoisted: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    local hoistCount
    hoistCount="$(grep -c '__addrhoist' "$dir/t.s")"
    if [ "$hoistCount" -ne 0 ]; then
        echo "FAIL test_single_use_not_hoisted: expected no '__addrhoist' temporary for a" \
             "single array access, found $hoistCount occurrence(s)"
        return 1
    fi
    echo "PASS test_single_use_not_hoisted (no hoisting temporary for a single access)"
    return 0
}

test_law_emitted_once || fail=1
test_single_use_not_hoisted || fail=1
test_all_constant_indices_not_hoisted || fail=1

exit $fail
