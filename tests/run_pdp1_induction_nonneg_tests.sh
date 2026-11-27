#!/usr/bin/env bash
# Unit tests for Pdp1Codegen.cpp's emitLt() sign-dispatch skip: a port of
# Pdp7Codegen.cpp's own genCompareCells()/emitSignedLtCells() optimization
# (see docs/known-bugs.md's own guard entry and src/optimizers/
# induction_nonneg/InductionNonNegative.cpp) to this target's one's-
# complement, no-condition-code `emitLt()`. A Local the induction-nonneg
# pass proved always non-negative (`ir::Slot::knownNonNegative`, IR.h) lets
# emitLt() skip that operand's own runtime `sma` sign test, as long as the
# *other* operand's sign is also statically known (a literal, or another
# proven-non-negative slot) - otherwise the dispatch still needs a live
# check on the other side anyway, so nothing is saved.
#
# Five tests:
#   test_safe_for_loop_skips_one_sign_check - the plain idiom, loop
#       variable compared against a literal bound - expects the 'sma'
#       count to drop from 4 to 2 (A's own dispatch skipped, B's own and
#       the final subtract's own are untouched) and the pass's own
#       diagnostic; also runs the compiled program on the real simulator
#       to confirm the result is unchanged.
#   test_reassigned_induction_variable_keeps_full_dispatch - the loop
#       variable is also written elsewhere in the loop body, so the pass
#       must decline - expects the original 4 'sma' and no diagnostic.
#   test_decrementing_loop_skips_one_sign_check - the decrementing
#       counterpart, `for(i=N; i>0; i=i-1)` - same 4-to-2 drop and
#       diagnostic.
#   test_both_operands_known_skips_all_sign_checks - two induction
#       variables compared against each other (`i<j` with both proven
#       non-negative) - expects the dispatch to collapse all the way to
#       just the final subtract's own 'sma' (1, not 4), since neither
#       side's sign ever needs a runtime test; also runs on the real
#       simulator.
#   test_unsigned_comparison_keeps_full_dispatch - the loop variable is
#       `unsigned`, so the optimization must not apply at all (the
#       sign-bit-flip trick operates on the flipped representation, not
#       the original signed value `knownNonNegative` was proven about) -
#       expects the original, unreduced 'sma' count and no diagnostic.
#
# Usage: tests/run_pdp1_induction_nonneg_tests.sh [path/to/rcc]

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

run_with_timeout() {
    local secs="$1"
    shift
    "$@" &
    local pid=$!
    (sleep "$secs" && kill -9 "$pid" 2>/dev/null) &
    local watcher=$!
    wait "$pid" 2>/dev/null
    local status=$?
    kill "$watcher" 2>/dev/null
    wait "$watcher" 2>/dev/null
    return $status
}

to_signed() {
    local value="$1" bits="$2"
    local max=$((1 << (bits - 1)))
    local mod=$((1 << bits))
    if [ "$value" -ge "$max" ]; then
        echo $((value - mod))
    else
        echo "$value"
    fi
}

# Runs `src` on the real PDP-1 simulator (open-simh) and checks AC against
# `expect`, the same way tests/run_tests.sh's own check_pdp1() does -
# confirms the optimized comparison still produces the right answer, not
# just a smaller instruction count.
run_on_simulator() {
    local src="$1" name="$2" expect="$3"
    if ! command -v pdp1 >/dev/null 2>&1; then
        echo "SKIP $name (real-hardware check): pdp1 (open-simh) not found on PATH"
        return 0
    fi
    local rimfile="$tmpdir/$name.rim"
    if ! "$RCC" --target=pdp1 --format=rim --optimize --out="$rimfile" "$src" >"$tmpdir/$name.log" 2>&1; then
        echo "FAIL $name (real-hardware check): compile error"
        cat "$tmpdir/$name.log"
        return 1
    fi
    local simfile="$tmpdir/$name.simh" outfile="$tmpdir/$name.out"
    cat >"$simfile" <<SIMH
attach ptr $rimfile
boot ptr
e ac
SIMH
    run_with_timeout 10 bash -c "pdp1 '$simfile' </dev/null >'$outfile' 2>/dev/null"
    local ac_octal
    ac_octal="$(grep '^AC:' "$outfile" | awk '{print $2}')"
    if [ -z "$ac_octal" ]; then
        echo "FAIL $name (real-hardware check): simulator produced no AC (timed out or crashed)"
        return 1
    fi
    local ac_dec=$((8#$ac_octal))
    ac_dec="$(to_signed "$ac_dec" 18)"
    if [ "$ac_dec" -ne "$expect" ]; then
        echo "FAIL $name (real-hardware check): expected $expect, got $ac_dec (AC octal: $ac_octal)"
        return 1
    fi
    echo "PASS $name (real-hardware check, AC=$ac_dec)"
    return 0
}

test_safe_for_loop_skips_one_sign_check() {
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
    if ! "$RCC" --target=pdp1 --format=asm --optimize --verbose --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_safe_for_loop_skips_one_sign_check: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! grep -q "induction-nonneg: fn main: proved \`i\` stays non-negative" "$dir/log"; then
        echo "FAIL test_safe_for_loop_skips_one_sign_check: expected the pass's own diagnostic, got:"
        cat "$dir/log"
        return 1
    fi
    local smaCount
    smaCount="$(grep -c 'sma' "$dir/t.s")"
    if [ "$smaCount" -ne 2 ]; then
        echo "FAIL test_safe_for_loop_skips_one_sign_check: expected exactly 2 'sma' (down from the" \
             "unoptimized 4 - \`i\`'s own dispatch should be gone, leaving the literal bound's own B" \
             "test and the final subtract's own), found $smaCount"
        return 1
    fi
    echo "PASS test_safe_for_loop_skips_one_sign_check (2 occurrences of 'sma', diagnostic present)"
    run_on_simulator "$dir/t.c" "test_safe_for_loop_skips_one_sign_check" 45 || return 1
    return 0
}

test_reassigned_induction_variable_keeps_full_dispatch() {
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
    if ! "$RCC" --target=pdp1 --format=asm --optimize --verbose --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_reassigned_induction_variable_keeps_full_dispatch: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if grep -q "induction-nonneg" "$dir/log"; then
        echo "FAIL test_reassigned_induction_variable_keeps_full_dispatch: pass fired despite the" \
             "extra write to \`i\` in the loop body:"
        cat "$dir/log"
        return 1
    fi
    local smaCount
    smaCount="$(grep -c 'sma' "$dir/t.s")"
    if [ "$smaCount" -ne 4 ]; then
        echo "FAIL test_reassigned_induction_variable_keeps_full_dispatch: expected the original 4" \
             "'sma' occurrences (unsafe to skip \`i\`'s own dispatch), found $smaCount"
        return 1
    fi
    echo "PASS test_reassigned_induction_variable_keeps_full_dispatch (4 occurrences of 'sma', no diagnostic)"
    return 0
}

test_decrementing_loop_skips_one_sign_check() {
    local dir="$tmpdir/decr"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int main() {
    int i;
    int sum;
    sum = 0;
    for (i = 10; i > 0; i = i - 1) {
        sum = sum + i;
    }
    return sum;
}
EOF
    if ! "$RCC" --target=pdp1 --format=asm --optimize --verbose --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_decrementing_loop_skips_one_sign_check: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! grep -q "induction-nonneg: fn main: proved \`i\` stays non-negative" "$dir/log"; then
        echo "FAIL test_decrementing_loop_skips_one_sign_check: expected the pass's own diagnostic, got:"
        cat "$dir/log"
        return 1
    fi
    local smaCount
    smaCount="$(grep -c 'sma' "$dir/t.s")"
    if [ "$smaCount" -ne 2 ]; then
        echo "FAIL test_decrementing_loop_skips_one_sign_check: expected exactly 2 'sma', found $smaCount"
        return 1
    fi
    echo "PASS test_decrementing_loop_skips_one_sign_check (2 occurrences of 'sma', diagnostic present)"
    run_on_simulator "$dir/t.c" "test_decrementing_loop_skips_one_sign_check" 55 || return 1
    return 0
}

test_both_operands_known_skips_all_sign_checks() {
    local dir="$tmpdir/bothknown"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int main() {
    int i, j;
    int count;
    count = 0;
    for (i = 0; i < 10; i = i + 1) {
        for (j = 0; j < 10; j = j + 1) {
            if (i < j) {
                count = count + 1;
            }
        }
    }
    return count;
}
EOF
    if ! "$RCC" --target=pdp1 --format=asm --optimize --verbose --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_both_operands_known_skips_all_sign_checks: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! grep -q "induction-nonneg: fn main: proved \`i\` stays non-negative" "$dir/log" ||
       ! grep -q "induction-nonneg: fn main: proved \`j\` stays non-negative" "$dir/log"; then
        echo "FAIL test_both_operands_known_skips_all_sign_checks: expected both diagnostics, got:"
        cat "$dir/log"
        return 1
    fi
    local smaCount
    smaCount="$(grep -c 'sma' "$dir/t.s")"
    # `i<10` and `j<10` each drop from 4 to 2 (2+2), and `i<j` (both sides
    # known) drops all the way to 1 (just the final subtract) - 5 total,
    # against 12 unoptimized.
    if [ "$smaCount" -ne 5 ]; then
        echo "FAIL test_both_operands_known_skips_all_sign_checks: expected exactly 5 'sma' (2 for" \
             "each loop guard's own literal-bound comparison, 1 for \`i<j\`'s fully-collapsed" \
             "comparison), found $smaCount"
        return 1
    fi
    echo "PASS test_both_operands_known_skips_all_sign_checks (5 occurrences of 'sma', both diagnostics present)"
    # 45 pairs (i,j) in [0,10)x[0,10) with i<j: C(10,2) = 45.
    run_on_simulator "$dir/t.c" "test_both_operands_known_skips_all_sign_checks" 45 || return 1
    return 0
}

test_unsigned_comparison_keeps_full_dispatch() {
    local dir="$tmpdir/unsignedcmp"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int main() {
    unsigned int i;
    int count;
    count = 0;
    for (i = 0; i < 10; i = i + 1) {
        count = count + 1;
    }
    return count;
}
EOF
    if ! "$RCC" --target=pdp1 --format=asm --optimize --verbose --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_unsigned_comparison_keeps_full_dispatch: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    local smaCount
    smaCount="$(grep -c 'sma' "$dir/t.s")"
    if [ "$smaCount" -ne 4 ]; then
        echo "FAIL test_unsigned_comparison_keeps_full_dispatch: expected the original 4 'sma'" \
             "occurrences (the sign-dispatch skip must not apply to an unsigned comparison)," \
             "found $smaCount"
        return 1
    fi
    echo "PASS test_unsigned_comparison_keeps_full_dispatch (4 occurrences of 'sma')"
    return 0
}

test_safe_for_loop_skips_one_sign_check || fail=1
test_reassigned_induction_variable_keeps_full_dispatch || fail=1
test_decrementing_loop_skips_one_sign_check || fail=1
test_both_operands_known_skips_all_sign_checks || fail=1
test_unsigned_comparison_keeps_full_dispatch || fail=1

exit $fail
