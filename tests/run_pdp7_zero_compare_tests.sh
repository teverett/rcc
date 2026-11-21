#!/usr/bin/env bash
# Unit tests for Pdp7Codegen.cpp's `bIsZero` fast path
# (genCompareCells()/emitSignedLtCells()): comparing a signed value of
# unknown compile-time sign against the literal 0 (`x < 0`, `x >= 0`)
# is a pure sign test on that one operand - no subtraction is ever
# needed, since A-0 == A. Before this fast path, every such comparison
# still went through the generic same-sign-subtract fallback (the sign
# dispatch AND a redundant `tad <0-literal>` AND a second `sma`) - 9
# words instead of 3, confirmed directly against `sys/s1.c`'s own
# `swap()` in the UnixV0-C kernel (`inIndex >= 0` et al.).
#
# Four tests:
#   test_ge_zero_collapses_to_single_sma - `x >= 0` on a sign-unknown
#       variable compiles to exactly one `sma`, no `tad` at all.
#   test_lt_zero_collapses_to_single_sma - same for `x < 0`.
#   test_gt_zero_and_le_zero_not_collapsed - `x > 0`/`x <= 0` are NOT a
#       pure sign test (0 itself fails `x > 0` but passes `x >= 0`) -
#       must still use the full subtract machinery, confirmed by the
#       presence of a `tad`.
#   test_ge_zero_runtime_correct - real simulator run confirming the
#       fast path's own branch sense is still correct at the boundary
#       (x = 0, x = -1, x = 1 all decided correctly).
#
# Usage: tests/run_pdp7_zero_compare_tests.sh [path/to/rcc]

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

test_ge_zero_collapses_to_single_sma() {
    local dir="$tmpdir/ge"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int get() { return 1; }
int main() {
    int x;
    x = get();
    if (x >= 0) {
        return 1;
    }
    return 0;
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_ge_zero_collapses_to_single_sma: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    local smaCount tadCount
    smaCount="$(grep -c 'sma' "$dir/t.s")"
    tadCount="$(grep -cE '^\s*tad\b' "$dir/t.s")"
    if [ "$smaCount" -ne 1 ] || [ "$tadCount" -ne 0 ]; then
        echo "FAIL test_ge_zero_collapses_to_single_sma: expected exactly 1 'sma' and 0 'tad'" \
             "(a pure sign test, no subtraction), found $smaCount 'sma' and $tadCount 'tad':"
        cat "$dir/t.s"
        return 1
    fi
    echo "PASS test_ge_zero_collapses_to_single_sma (1 'sma', 0 'tad')"
    return 0
}

test_lt_zero_collapses_to_single_sma() {
    local dir="$tmpdir/lt"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int get() { return 1; }
int main() {
    int x;
    x = get();
    if (x < 0) {
        return 1;
    }
    return 0;
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_lt_zero_collapses_to_single_sma: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    local smaCount tadCount
    smaCount="$(grep -c 'sma' "$dir/t.s")"
    tadCount="$(grep -cE '^\s*tad\b' "$dir/t.s")"
    if [ "$smaCount" -ne 1 ] || [ "$tadCount" -ne 0 ]; then
        echo "FAIL test_lt_zero_collapses_to_single_sma: expected exactly 1 'sma' and 0 'tad'" \
             "(a pure sign test, no subtraction), found $smaCount 'sma' and $tadCount 'tad':"
        cat "$dir/t.s"
        return 1
    fi
    echo "PASS test_lt_zero_collapses_to_single_sma (1 'sma', 0 'tad')"
    return 0
}

test_gt_zero_and_le_zero_not_collapsed() {
    local dir="$tmpdir/gtle"
    mkdir -p "$dir"
    cat >"$dir/gt.c" <<'EOF'
int get() { return 1; }
int main() {
    int x;
    x = get();
    if (x > 0) {
        return 1;
    }
    return 0;
}
EOF
    cat >"$dir/le.c" <<'EOF'
int get() { return 1; }
int main() {
    int x;
    x = get();
    if (x <= 0) {
        return 1;
    }
    return 0;
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/gt.s" "$dir/gt.c" >"$dir/log" 2>&1; then
        echo "FAIL test_gt_zero_and_le_zero_not_collapsed: gt.c fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/le.s" "$dir/le.c" >>"$dir/log" 2>&1; then
        echo "FAIL test_gt_zero_and_le_zero_not_collapsed: le.c fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    # `0` is on the LEFT of the real `A<B` test for both `x>0` (swapped
    # to `0<x`) and `x<=0` (swapped to `0<x`, negated) - neither is a
    # pure sign test on `x` (0 itself must compare false for `x>0` but
    # true for `x>=0`), so both must still carry a `tad` from the
    # ordinary subtract-and-test-sign fallback.
    if ! grep -qE '^\s*tad\b' "$dir/gt.s"; then
        echo "FAIL test_gt_zero_and_le_zero_not_collapsed: 'x > 0' lost its subtraction" \
             "fallback (not a pure sign test on x - must still compare in full):"
        cat "$dir/gt.s"
        return 1
    fi
    if ! grep -qE '^\s*tad\b' "$dir/le.s"; then
        echo "FAIL test_gt_zero_and_le_zero_not_collapsed: 'x <= 0' lost its subtraction" \
             "fallback (not a pure sign test on x - must still compare in full):"
        cat "$dir/le.s"
        return 1
    fi
    echo "PASS test_gt_zero_and_le_zero_not_collapsed ('x > 0'/'x <= 0' still subtract, as they must)"
    return 0
}

test_ge_zero_runtime_correct() {
    local dir="$tmpdir/runtime"
    mkdir -p "$dir"
    # c1: x=0 (boundary, must be >=0 true) -> AC=1
    # c2: x=-1 (must be >=0 false) -> AC=2
    # c3: x=1 (must be >=0 true) -> AC=1, accumulated with c1's own
    #     result below into a single checkable sum.
    cat >"$dir/t.c" <<'EOF'
int classify(int x) {
    if (x >= 0) {
        return 1;
    }
    return 2;
}
int main() {
    int result;
    result = 0;
    result = result + (classify(0) == 1);   // c1: boundary, true
    result = result + (classify(-1) == 2);  // c2: negative, false
    result = result + (classify(1) == 1);   // c3: positive, true
    return result;
}
EOF
    if ! "$RCC" --target=pdp7 --format=coff --optimize --out="$dir/t.o" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_ge_zero_runtime_correct: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! "$RCC" --link --target=pdp7 --format=rim --out="$dir/t.rim" "$dir/t.o" >>"$dir/log" 2>&1; then
        echo "FAIL test_ge_zero_runtime_correct: fixture failed to link"
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
    if [ "$((8#$ac))" != "3" ]; then
        echo "FAIL test_ge_zero_runtime_correct: expected AC=3 (all three cases decided" \
             "correctly, including the x=0 boundary), got octal $ac"
        return 1
    fi
    echo "PASS test_ge_zero_runtime_correct (AC=3, boundary and both signs all correct)"
    return 0
}

test_ge_zero_collapses_to_single_sma || fail=1
test_lt_zero_collapses_to_single_sma || fail=1
test_gt_zero_and_le_zero_not_collapsed || fail=1
test_ge_zero_runtime_correct || fail=1

exit $fail
