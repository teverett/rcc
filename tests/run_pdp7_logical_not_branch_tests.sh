#!/usr/bin/env bash
# Unit tests for Pdp7Codegen.cpp's genCondBranch()/genCondBranchOnTrue():
# `!A` as an `if`/`while` condition used to fall through to the generic
# path - materializing A's negation to a 0/1 value via genLogicalNot()
# and then testing THAT - instead of testing A's own raw value directly
# with the skip sense flipped. `!A` as a false-branching condition is
# exactly "jump when A is true" (genCondBranchOnTrue()'s own job), and
# vice versa, so both now unwrap it directly. Confirmed directly
# against `sys/s6.c`'s own `pget()` (`if (!(inode[I_FLAGS] &
# I_LARGE_FILE_FLAG))`).
#
# Three tests:
#   test_not_bitwise_in_if_skips_materialization - `!(x & 2)` as an
#       `if` condition must not produce a `__one`/`__zero`
#       0/1-materialize sequence at all.
#   test_not_truth_table_correct - `!(x & 2)` decided correctly for a
#       flag both set and clear, on the real simulator.
#   test_not_nested_in_or_and_and_correct - `!(a<0) || b==5` and
#       `!(a>0 && b>0)`, several combinations each, on the real
#       simulator - exercises genCondBranch()/genCondBranchOnTrue()'s
#       own mutual recursion between `!`, `||`, and `&&`.
#
# Usage: tests/run_pdp7_logical_not_branch_tests.sh [path/to/rcc]

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

test_not_bitwise_in_if_skips_materialization() {
    local dir="$tmpdir/skip"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int get() { return 1; }
int main() {
    int x;
    x = get();
    if (!(x & 2)) {
        return 1;
    }
    return 0;
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_not_bitwise_in_if_skips_materialization: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if grep -qE 'lac __one|lac __zero' "$dir/t.s"; then
        echo "FAIL test_not_bitwise_in_if_skips_materialization: expected no" \
             "0/1-materializing sequence at all (genLogicalNot()'s own tell), found one:"
        cat "$dir/t.s"
        return 1
    fi
    echo "PASS test_not_bitwise_in_if_skips_materialization (no __one/__zero materialization)"
    return 0
}

test_not_truth_table_correct() {
    local dir="$tmpdir/truth"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int classify(int flags) {
    if (!(flags & 2)) {
        return 1;
    }
    return 0;
}
int main() {
    int r;
    r = 0;
    r = r + (classify(0) == 1);   // flag clear -> !(0) -> 1
    r = r + (classify(2) == 0);   // flag set -> !(2) -> 0
    r = r + (classify(3) == 0);   // flag set among others -> 0
    return r;
}
EOF
    if ! "$RCC" --target=pdp7 --format=coff --optimize --out="$dir/t.o" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_not_truth_table_correct: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! "$RCC" --link --target=pdp7 --format=rim --out="$dir/t.rim" "$dir/t.o" >>"$dir/log" 2>&1; then
        echo "FAIL test_not_truth_table_correct: fixture failed to link"
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
        echo "FAIL test_not_truth_table_correct: expected AC=3 (all 3 cases decided" \
             "correctly), got octal $ac"
        return 1
    fi
    echo "PASS test_not_truth_table_correct (AC=3, all 3 cases correct)"
    return 0
}

test_not_nested_in_or_and_and_correct() {
    local dir="$tmpdir/nested"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int classifyOr(int a, int b) {
    if (!(a < 0) || b == 5) {
        return 1;
    }
    return 0;
}
int classifyAnd(int a, int b) {
    if (!(a > 0 && b > 0)) {
        return 1;
    }
    return 0;
}
int main() {
    int r;
    r = 0;
    r = r + (classifyOr(5, 0) == 1);     // !(5<0)=T -> T||F -> 1
    r = r + (classifyOr(-1, 0) == 0);    // !(-1<0)=F, 0==5=F -> F||F -> 0
    r = r + (classifyOr(5, 5) == 1);     // !(5<0)=T -> 1 (short-circuits)
    r = r + (classifyOr(-1, 5) == 1);    // !(-1<0)=F, 5==5=T -> F||T -> 1
    r = r + (classifyAnd(1, 1) == 0);    // (T&&T)=T, !T -> 0
    r = r + (classifyAnd(1, 0) == 1);    // (T&&F)=F, !F -> 1
    r = r + (classifyAnd(0, 0) == 1);    // (F&&F)=F, !F -> 1
    return r;
}
EOF
    if ! "$RCC" --target=pdp7 --format=coff --optimize --out="$dir/t.o" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_not_nested_in_or_and_and_correct: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! "$RCC" --link --target=pdp7 --format=rim --out="$dir/t.rim" "$dir/t.o" >>"$dir/log" 2>&1; then
        echo "FAIL test_not_nested_in_or_and_and_correct: fixture failed to link"
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
    if [ "$((8#$ac))" != "7" ]; then
        echo "FAIL test_not_nested_in_or_and_and_correct: expected AC=7 (all 7 combinations" \
             "decided correctly), got octal $ac"
        return 1
    fi
    echo "PASS test_not_nested_in_or_and_and_correct (AC=7, all 7 combinations correct)"
    return 0
}

test_not_bitwise_in_if_skips_materialization || fail=1
test_not_truth_table_correct || fail=1
test_not_nested_in_or_and_and_correct || fail=1

exit $fail
