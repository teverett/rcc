#!/usr/bin/env bash
# Unit tests for src/optimizers/induction_nonneg/InductionNonNegative.cpp:
# proving non-negativity for the `for(i=0; i<CONST; i=i+1)` idiom lets
# Pdp7Codegen.cpp's genCompareCells()/emitSignedLtCells() skip the loop
# variable's own dynamic sign dispatch (one `sma`-based check), on top of
# the already-precomputed-literal-negation fix
# run_pdp7_compare_constant_tests.sh covers. A matched loop's comparison
# needs only 1 `sma` (the shared subtract-and-check path); an otherwise
# identical loop that isn't safe to mark (the variable is reassigned, or
# its address taken, somewhere else in the function) must still pay the
# original 2 (the outer sign check, plus the subtract path's own).
#
# Three tests:
#   test_safe_for_loop_skips_outer_sign_check - the plain idiom, nothing
#       else touches `i` - expects exactly 1 `sma` and the pass's own
#       diagnostic.
#   test_reassigned_induction_variable_keeps_outer_sign_check - `i` is
#       also written elsewhere in the loop body - expects 2 `sma` and no
#       diagnostic (the pass must decline, not just do nothing quietly by
#       accident).
#   test_address_taken_keeps_outer_sign_check - `&i` is taken (passed to
#       another function) after the loop - expects the same decline, since
#       the callee could leave `i` negative.
#
# Usage: tests/run_pdp7_induction_nonneg_tests.sh [path/to/rcc]

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

test_safe_for_loop_skips_outer_sign_check() {
    local dir="$tmpdir/safe"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int main() {
    int i;
    int count;
    count = 0;
    for (i = 0; i < 10; i = i + 1) {
        count = count + i;
    }
    return count;
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_safe_for_loop_skips_outer_sign_check: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! grep -q "induction-nonneg: fn main: proved \`i\` stays non-negative" "$dir/log"; then
        echo "FAIL test_safe_for_loop_skips_outer_sign_check: expected the pass's own diagnostic, got:"
        cat "$dir/log"
        return 1
    fi
    local smaCount
    smaCount="$(grep -c 'sma' "$dir/t.s")"
    if [ "$smaCount" -ne 1 ]; then
        echo "FAIL test_safe_for_loop_skips_outer_sign_check: expected exactly 1 'sma' (the outer" \
             "sign check on \`i\` should be gone), found $smaCount"
        return 1
    fi
    echo "PASS test_safe_for_loop_skips_outer_sign_check (1 occurrence of 'sma', diagnostic present)"
    return 0
}

test_reassigned_induction_variable_keeps_outer_sign_check() {
    local dir="$tmpdir/reassigned"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int main() {
    int i;
    int count;
    count = 0;
    for (i = 0; i < 10; i = i + 1) {
        count = count + i;
        i = i + 0;
    }
    return count;
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_reassigned_induction_variable_keeps_outer_sign_check: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if grep -q "induction-nonneg" "$dir/log"; then
        echo "FAIL test_reassigned_induction_variable_keeps_outer_sign_check: pass fired despite the" \
             "extra write to \`i\` in the loop body:"
        cat "$dir/log"
        return 1
    fi
    local smaCount
    smaCount="$(grep -c 'sma' "$dir/t.s")"
    if [ "$smaCount" -ne 2 ]; then
        echo "FAIL test_reassigned_induction_variable_keeps_outer_sign_check: expected the original 2" \
             "'sma' occurrences (unsafe to drop the outer sign check), found $smaCount"
        return 1
    fi
    echo "PASS test_reassigned_induction_variable_keeps_outer_sign_check (2 occurrences of 'sma', no diagnostic)"
    return 0
}

test_address_taken_keeps_outer_sign_check() {
    local dir="$tmpdir/addrtaken"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
void setNegative(int *p) {
    *p = -1;
}
int main() {
    int i;
    int count;
    count = 0;
    for (i = 0; i < 10; i = i + 1) {
        count = count + i;
    }
    setNegative(&i);
    return count;
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_address_taken_keeps_outer_sign_check: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if grep -q "induction-nonneg" "$dir/log"; then
        echo "FAIL test_address_taken_keeps_outer_sign_check: pass fired despite \`&i\` being taken:"
        cat "$dir/log"
        return 1
    fi
    local smaCount
    smaCount="$(awk '/^FN_main:/{f=1} f{print} /jmp i FN_main/{exit}' "$dir/t.s" | grep -c 'sma')"
    if [ "$smaCount" -ne 2 ]; then
        echo "FAIL test_address_taken_keeps_outer_sign_check: expected the original 2 'sma'" \
             "occurrences in main() (unsafe to drop the outer sign check), found $smaCount"
        return 1
    fi
    echo "PASS test_address_taken_keeps_outer_sign_check (2 occurrences of 'sma' in main(), no diagnostic)"
    return 0
}

test_safe_for_loop_skips_outer_sign_check || fail=1
test_reassigned_induction_variable_keeps_outer_sign_check || fail=1
test_address_taken_keeps_outer_sign_check || fail=1

exit $fail
