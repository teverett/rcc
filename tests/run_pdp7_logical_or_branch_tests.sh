#!/usr/bin/env bash
# Unit tests for Pdp7Codegen.cpp's genCondBranch()/genCondBranchOnTrue():
# an `if`/`while` condition built from `||` used to fall through to the
# generic path entirely - materializing a 0/1 value for EACH side via
# emitLt()/emitEq() (genLogicalOr()'s own machinery), then testing that
# value - instead of branching directly the way a plain comparison or
# an `&&` of comparisons already did. `||` (and `&&` nested inside it,
# via genCondBranchOnTrue()'s own mirror-image short-circuit) now
# branches directly too. Confirmed directly against `sys/s5.c`'s own
# `isown()` (`u_uid < 0 || u_uid == inode[8]` as an `if` condition).
#
# Four tests:
#   test_or_in_if_skips_materialization - `x < 0 || y == 5` as an `if`
#       condition must not produce a `__one`/`__zero` 0/1-materialize
#       sequence at all (checked by the absence of any `__one`/`__zero`
#       reference, `genLogicalOr()`'s own tell).
#   test_or_truth_table_correct - all 4 true/false combinations of a
#       2-operand `||`, decided correctly on the real simulator.
#   test_and_inside_or_truth_table_correct - `(a>0 && b>0) || c==9`,
#       all 6 combinations from the comment below, decided correctly -
#       exercises genCondBranchOnTrue()'s own `&&` short-circuit path,
#       the one shape genCondBranch()'s existing `&&` case alone can't
#       reach (an `&&` nested inside an `||`'s left side).
#   test_or_in_while_still_loops_correctly - `||` as a `while`
#       condition (not just `if`) still loops the right number of
#       times.
#
# Usage: tests/run_pdp7_logical_or_branch_tests.sh [path/to/rcc]

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

test_or_in_if_skips_materialization() {
    local dir="$tmpdir/skip"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int get() { return 1; }
int main() {
    int x;
    int y;
    x = get();
    y = get();
    if (x < 0 || y == 5) {
        return 1;
    }
    return 0;
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_or_in_if_skips_materialization: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    # `__one`/`__zero` are always DECLARED at the bottom of every
    # function's own output (boilerplate runtime constants) - look for
    # them actually being LOADED (`lac __one`/`lac __zero`), the real
    # tell that some value got materialized to 0/1, not just declared.
    if grep -qE 'lac __one|lac __zero' "$dir/t.s"; then
        echo "FAIL test_or_in_if_skips_materialization: expected no 0/1-materializing" \
             "sequence at all (genLogicalOr()'s own tell), found one:"
        cat "$dir/t.s"
        return 1
    fi
    echo "PASS test_or_in_if_skips_materialization (no __one/__zero materialization)"
    return 0
}

test_or_truth_table_correct() {
    local dir="$tmpdir/truth"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int classify(int x, int y) {
    if (x < 0 || y == 5) {
        return 1;
    }
    return 0;
}
int main() {
    int r;
    r = 0;
    r = r + (classify(-1, 0) == 1);   // T||F -> 1
    r = r + (classify(1, 5) == 1);    // F||T -> 1
    r = r + (classify(-1, 5) == 1);   // T||T -> 1
    r = r + (classify(1, 0) == 0);    // F||F -> 0
    return r;
}
EOF
    if ! "$RCC" --target=pdp7 --format=coff --optimize --out="$dir/t.o" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_or_truth_table_correct: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! "$RCC" --link --target=pdp7 --format=rim --out="$dir/t.rim" "$dir/t.o" >>"$dir/log" 2>&1; then
        echo "FAIL test_or_truth_table_correct: fixture failed to link"
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
    if [ "$((8#$ac))" != "4" ]; then
        echo "FAIL test_or_truth_table_correct: expected AC=4 (all 4 combinations decided" \
             "correctly), got octal $ac"
        return 1
    fi
    echo "PASS test_or_truth_table_correct (AC=4, all 4 combinations correct)"
    return 0
}

test_and_inside_or_truth_table_correct() {
    local dir="$tmpdir/nested"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int classify(int a, int b, int c) {
    if ((a > 0 && b > 0) || c == 9) {
        return 1;
    }
    return 0;
}
int main() {
    int r;
    r = 0;
    r = r + (classify(1, 1, 0) == 1);   // (T&&T)||F -> 1
    r = r + (classify(1, 0, 0) == 0);   // (T&&F)||F -> 0
    r = r + (classify(0, 1, 0) == 0);   // (F&&T)||F -> 0
    r = r + (classify(0, 0, 9) == 1);   // (F&&F)||T -> 1
    r = r + (classify(1, 1, 9) == 1);   // (T&&T)||T -> 1
    r = r + (classify(0, 0, 0) == 0);   // (F&&F)||F -> 0
    return r;
}
EOF
    if ! "$RCC" --target=pdp7 --format=coff --optimize --out="$dir/t.o" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_and_inside_or_truth_table_correct: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! "$RCC" --link --target=pdp7 --format=rim --out="$dir/t.rim" "$dir/t.o" >>"$dir/log" 2>&1; then
        echo "FAIL test_and_inside_or_truth_table_correct: fixture failed to link"
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
    if [ "$((8#$ac))" != "6" ]; then
        echo "FAIL test_and_inside_or_truth_table_correct: expected AC=6 (all 6 combinations" \
             "decided correctly), got octal $ac"
        return 1
    fi
    echo "PASS test_and_inside_or_truth_table_correct (AC=6, all 6 combinations correct)"
    return 0
}

test_or_in_while_still_loops_correctly() {
    local dir="$tmpdir/loop"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int main() {
    int i;
    int count;
    i = 0;
    count = 0;
    while (i < 3 || i == 5) {
        count = count + 1;
        i = i + 1;
    }
    return count;
}
EOF
    if ! "$RCC" --target=pdp7 --format=coff --optimize --out="$dir/t.o" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_or_in_while_still_loops_correctly: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! "$RCC" --link --target=pdp7 --format=rim --out="$dir/t.rim" "$dir/t.o" >>"$dir/log" 2>&1; then
        echo "FAIL test_or_in_while_still_loops_correctly: fixture failed to link"
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
    # i: 0,1,2 each satisfy `i<3` (3 iterations); at i=3, `i<3` is
    # false and `i==5` is also false, so the loop exits. count=3.
    if [ "$((8#$ac))" != "3" ]; then
        echo "FAIL test_or_in_while_still_loops_correctly: expected AC=3 (i=0,1,2 each" \
             "satisfy 'i<3', the loop exits at i=3 since 'i==5' is also false there)," \
             "got octal $ac"
        return 1
    fi
    echo "PASS test_or_in_while_still_loops_correctly (AC=3, loop iterated correctly)"
    return 0
}

test_or_in_if_skips_materialization || fail=1
test_or_truth_table_correct || fail=1
test_and_inside_or_truth_table_correct || fail=1
test_or_in_while_still_loops_correctly || fail=1

exit $fail
