#!/usr/bin/env bash
# Unit tests for Pdp7Codegen.cpp's fast calling convention: a direct
# call to a leaf/`@nonrecursive` callee, with every argument itself
# call-free, writes each argument straight into the callee's own
# position-named static cell (`FN_<name>_P<i>`) and calls a second
# entry point (`FN_<name>_fast`) that starts right after the primary
# entry's own stack-based parameter-binding code - skipping the
# push/`jms __push` per argument and the post-call `__sp`
# adjustment/return-value shuffle entirely, both dead weight once the
# callee already reads every Param through one cheap static cell
# (see Pdp7Codegen.h's own "Leaf fast path"). An `extern` declaration
# can carry its own `@nonrecursive` tag (grammar/BuildAst.cpp/
# IRBuilder.cpp), letting this apply across file boundaries too -
# trusted the same way `@nonrecursive`'s own reentrancy claim already
# is cross-file.
#
# Six tests:
#   test_literal_arg_call_uses_fast_path - a same-file, literal-arg
#       call to an `@nonrecursive` function uses `jms <callee>_fast`,
#       no `__push`.
#   test_call_containing_arg_still_uses_fast_path - the SAME callee,
#       called with an argument expression that itself makes a call,
#       STILL uses the fast path (evaluating every argument into its
#       own temp first, then copying all of them into the real Param
#       cells in one call-free burst right before the call - see
#       Pdp7Codegen.cpp's own Call case comment on why that's safe).
#   test_non_nonrecursive_callee_uses_stack_path - calling a function
#       with no `@nonrecursive` tag and a real call in its own body
#       never uses the fast path at all, even with literal arguments.
#   test_cross_file_extern_nonrecursive_runs_correctly - an `extern
#       ... @nonrecursive` declaration in one file, a real
#       `@nonrecursive` definition in another, linked together - the
#       fast path round-trips correctly through a real link, not just
#       a single compile.
#   test_multi_arg_and_nested_fast_calls_correct - a 3-argument fast
#       call, and one fast-eligible function calling another, both
#       decided correctly on the real simulator.
#   test_same_callee_both_conventions_coexist - the SAME function
#       called once with a call-free argument (writes straight into
#       its Param cell) and once with a call-containing one (goes
#       through a temp first) in the same program - both argument-
#       evaluation strategies land in the same cells correctly,
#       neither corrupts the other.
#
# Usage: tests/run_pdp7_fast_call_tests.sh [path/to/rcc]

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

run_ac() {
    local rimFile="$1"
    "$PDP7_SIM" <<EOF 2>&1 | grep -oE 'AC:[[:space:]]*[0-7]+' | tail -1 | grep -oE '[0-7]+$'
load $rimFile 10000
go 10000
e ac
EOF
}

test_literal_arg_call_uses_fast_path() {
    local dir="$tmpdir/literal"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int doubleIt(int x) @nonrecursive {
    return x * 2;
}
int main() {
    return doubleIt(3);
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_literal_arg_call_uses_fast_path: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! grep -q 'jms FN_doubleIt_fast' "$dir/t.s"; then
        echo "FAIL test_literal_arg_call_uses_fast_path: expected 'jms FN_doubleIt_fast', not found:"
        cat "$dir/t.s"
        return 1
    fi
    if grep -q '__push' "$dir/t.s"; then
        echo "FAIL test_literal_arg_call_uses_fast_path: expected no '__push' at all (the" \
             "fast path never pushes), found one:"
        cat "$dir/t.s"
        return 1
    fi
    echo "PASS test_literal_arg_call_uses_fast_path (jms FN_doubleIt_fast, no __push)"
    return 0
}

test_call_containing_arg_still_uses_fast_path() {
    local dir="$tmpdir/callarg"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int get5() { return 5; }
int doubleIt(int x) @nonrecursive {
    return x * 2;
}
int main() {
    return doubleIt(get5());
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_call_containing_arg_still_uses_fast_path: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! grep -q 'jms FN_doubleIt_fast' "$dir/t.s"; then
        echo "FAIL test_call_containing_arg_still_uses_fast_path: expected" \
             "'jms FN_doubleIt_fast' even though the argument makes a call, not found:"
        cat "$dir/t.s"
        return 1
    fi
    if grep -q '__push' "$dir/t.s"; then
        echo "FAIL test_call_containing_arg_still_uses_fast_path: expected no '__push' at" \
             "all (the temp-first evaluation order should need no real stack traffic for" \
             "this callee's own arguments), found one:"
        cat "$dir/t.s"
        return 1
    fi
    echo "PASS test_call_containing_arg_still_uses_fast_path (jms FN_doubleIt_fast, no __push)"
    return 0
}

test_non_nonrecursive_callee_uses_stack_path() {
    local dir="$tmpdir/notnonrec"
    mkdir -p "$dir"
    # A genuinely (indirectly) recursive function: `rcc`'s own auto-
    # `@nonrecursive` inference pass must decline here (correctly, it
    # really does call back into itself), unlike a function that
    # merely calls something else - that still gets auto-inferred
    # nonrecursive, which isn't what this test wants to exercise.
    cat >"$dir/t.c" <<'EOF'
int countDown(int x) {
    if (x <= 0) {
        return 0;
    }
    return countDown(x - 1);
}
int main() {
    return countDown(3);
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_non_nonrecursive_callee_uses_stack_path: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if grep -q 'jms FN_countDown_fast' "$dir/t.s"; then
        echo "FAIL test_non_nonrecursive_callee_uses_stack_path: 'countDown' really does" \
             "recurse - expected no fast entry/call at all, found one:"
        cat "$dir/t.s"
        return 1
    fi
    echo "PASS test_non_nonrecursive_callee_uses_stack_path (no fast path for a genuinely recursive callee)"
    return 0
}

test_cross_file_extern_nonrecursive_runs_correctly() {
    local dir="$tmpdir/crossfile"
    mkdir -p "$dir"
    cat >"$dir/caller.c" <<'EOF'
extern int addOne(int x) @nonrecursive;
int main() {
    return addOne(5);
}
EOF
    cat >"$dir/callee.c" <<'EOF'
int addOne(int x) @nonrecursive {
    return x + 1;
}
EOF
    if ! "$RCC" --target=pdp7 --format=coff --optimize --out="$dir/caller.o" "$dir/caller.c" >"$dir/log" 2>&1; then
        echo "FAIL test_cross_file_extern_nonrecursive_runs_correctly: caller.c failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! "$RCC" --target=pdp7 --format=coff --optimize --out="$dir/callee.o" "$dir/callee.c" >>"$dir/log" 2>&1; then
        echo "FAIL test_cross_file_extern_nonrecursive_runs_correctly: callee.c failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! "$RCC" --link --target=pdp7 --format=rim --out="$dir/t.rim" "$dir/caller.o" "$dir/callee.o" >>"$dir/log" 2>&1; then
        echo "FAIL test_cross_file_extern_nonrecursive_runs_correctly: fixture failed to link"
        cat "$dir/log"
        return 1
    fi
    local ac
    ac="$(run_ac "$dir/t.rim")"
    if [ "$((8#$ac))" != "6" ]; then
        echo "FAIL test_cross_file_extern_nonrecursive_runs_correctly: expected AC=6" \
             "(addOne(5)), got octal $ac"
        return 1
    fi
    echo "PASS test_cross_file_extern_nonrecursive_runs_correctly (AC=6)"
    return 0
}

test_multi_arg_and_nested_fast_calls_correct() {
    local dir="$tmpdir/multi"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int add3(int a, int b, int c) @nonrecursive {
    return a + b + c;
}
int helper(int x) @nonrecursive {
    return add3(x, x + 1, x + 2);
}
int main() {
    int r;
    r = 0;
    r = r + (add3(1, 2, 3) == 6);
    r = r + (helper(10) == 33);
    return r;
}
EOF
    if ! "$RCC" --target=pdp7 --format=rim --optimize --out="$dir/t.rim" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_multi_arg_and_nested_fast_calls_correct: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    local ac
    ac="$(run_ac "$dir/t.rim")"
    if [ "$((8#$ac))" != "2" ]; then
        echo "FAIL test_multi_arg_and_nested_fast_calls_correct: expected AC=2 (both checks" \
             "correct), got octal $ac"
        return 1
    fi
    echo "PASS test_multi_arg_and_nested_fast_calls_correct (AC=2)"
    return 0
}

test_multiple_call_containing_args_at_every_position_correct() {
    local dir="$tmpdir/multicall"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int get5() { return 5; }
int getN(int n) { return n; }
int add3(int a, int b, int c) @nonrecursive {
    return a + b + c;
}
int main() {
    int r;
    r = 0;
    r = r + (add3(get5(), 1, 2) == 8);          // first arg calls
    r = r + (add3(1, getN(2), 3) == 6);         // middle arg calls
    r = r + (add3(1, 2, get5()) == 8);          // last arg calls
    r = r + (add3(get5(), getN(10), 1) == 16);  // two args call
    return r;
}
EOF
    if ! "$RCC" --target=pdp7 --format=rim --optimize --out="$dir/t.rim" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_multiple_call_containing_args_at_every_position_correct: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    local ac
    ac="$(run_ac "$dir/t.rim")"
    if [ "$((8#$ac))" != "4" ]; then
        echo "FAIL test_multiple_call_containing_args_at_every_position_correct: expected" \
             "AC=4 (all 4 combinations correct), got octal $ac"
        return 1
    fi
    echo "PASS test_multiple_call_containing_args_at_every_position_correct (AC=4)"
    return 0
}

test_same_callee_both_conventions_coexist() {
    local dir="$tmpdir/coexist"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int get5() { return 5; }
int doubleIt(int x) @nonrecursive {
    return x * 2;
}
int main() {
    int r;
    r = 0;
    r = r + (doubleIt(3) == 6);        // fast path: literal, call-free
    r = r + (doubleIt(get5()) == 10);  // stack path: arg makes a call
    return r;
}
EOF
    if ! "$RCC" --target=pdp7 --format=rim --optimize --out="$dir/t.rim" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_same_callee_both_conventions_coexist: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    local ac
    ac="$(run_ac "$dir/t.rim")"
    if [ "$((8#$ac))" != "2" ]; then
        echo "FAIL test_same_callee_both_conventions_coexist: expected AC=2 (both the fast" \
             "and stack paths to the same callee decided correctly), got octal $ac"
        return 1
    fi
    echo "PASS test_same_callee_both_conventions_coexist (AC=2)"
    return 0
}

test_literal_arg_call_uses_fast_path || fail=1
test_call_containing_arg_still_uses_fast_path || fail=1
test_non_nonrecursive_callee_uses_stack_path || fail=1
test_cross_file_extern_nonrecursive_runs_correctly || fail=1
test_multi_arg_and_nested_fast_calls_correct || fail=1
test_multiple_call_containing_args_at_every_position_correct || fail=1
test_same_callee_both_conventions_coexist || fail=1

exit $fail
