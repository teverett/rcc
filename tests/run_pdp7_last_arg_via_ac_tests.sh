#!/usr/bin/env bash
# Unit tests for the pdp7 backend's fast-call convention no longer
# writing its LAST argument into a named Param cell at the call site
# at all (see genFunction()'s own comment on the matching "_fast"
# prologue `dac`, and the Call case's own comment on both of its
# sub-branches). `jms` never touches AC, so whatever a call site's
# own last `genExpr()`/`lac` left there survives into the callee's
# own entry - its "_fast" prologue now stores it itself, in one `dac`
# paid ONCE per function, instead of once per CALLER. Every argument
# before the last is completely unaffected (still written into its
# own cell as before).
#
# This applies unconditionally to every fast-call-eligible function
# with at least one parameter - no new attribute, no per-call-site
# choice (unlike '@inline_args'): the two compose cleanly since
# '@inline_args' only ever uses this same "_fast" entry as its OWN
# fallback (its own "_lit" entry is a separate thing, used instead of
# "_fast" only when the trailing arguments are all compile-time
# constants).
#
# Five tests:
#   test_last_arg_not_written_to_param_cell - structural: neither
#       call site below writes the LAST argument's own cell; the
#       callee's own "_fast" prologue does it instead.
#   test_runtime_last_arg_correct - a call whose last argument is a
#       genuine runtime value is still correct on the simulator.
#   test_call_containing_last_arg_correct - the "not every argument is
#       call-free" sub-branch (temps-then-copy) leaves the last
#       argument in AC too, still correct.
#   test_arity_one_function_correct - the narrowest case (nothing else
#       to write at all - the whole call becomes "lac expr; jms
#       <callee>_fast", matching the hand-written convention exactly).
#   test_arity_zero_function_unaffected - a function with no
#       parameters at all needs no change here; confirms the new
#       prologue `dac` is correctly skipped, not emitted unconditionally.
#
# Usage: tests/run_pdp7_last_arg_via_ac_tests.sh [path/to/rcc]

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

BETWEN_FIXTURE='int helper(int n) { return n; }
int betwen(int value, int low, int high) {
    if (value < low) return 0;
    if (value > high) return 0;
    return 1;
}
int main() {
    int hi;
    hi = helper(6);
    if (!betwen(5, 0, hi)) return 100;
    if (betwen(9, 0, hi)) return 200;
    return 7;
}'

test_last_arg_not_written_to_param_cell() {
    local dir="$tmpdir/struct"
    mkdir -p "$dir"
    printf '%s\n' "$BETWEN_FIXTURE" >"$dir/t.c"
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_last_arg_not_written_to_param_cell: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    # Checked structurally, per call site, not by a global count:
    # emitLeafParamBinding()'s own STACK-based entry (unrelated to
    # this fix) also legitimately writes this same cell once, reading
    # it off the pushed-argument stack - so the right check is "does
    # 'dac FN_betwen_P2' appear anywhere between a call site's own
    # 'jms FN_betwen_fast' and the nearest preceding 'dac
    # FN_betwen_P1'", not a flat count across the whole file.
    if ! grep -qE '^FN_betwen_fast: 0$' "$dir/t.s"; then
        echo "FAIL test_last_arg_not_written_to_param_cell: expected an FN_betwen_fast" \
             "entry point, not found:"
        cat "$dir/t.s"
        return 1
    fi
    if ! grep -A2 '^FN_betwen_fast: 0$' "$dir/t.s" | grep -qE '^   dac FN_betwen_P2$'; then
        echo "FAIL test_last_arg_not_written_to_param_cell: expected FN_betwen_fast's own" \
             "prologue to store the last argument, not found:"
        cat "$dir/t.s"
        return 1
    fi
    if grep -B1 '^   jms FN_betwen_fast$' "$dir/t.s" | grep -qE '^   dac FN_betwen_P2$'; then
        echo "FAIL test_last_arg_not_written_to_param_cell: a call site's own code, right" \
             "before its 'jms FN_betwen_fast', still writes FN_betwen_P2 directly:"
        cat "$dir/t.s"
        return 1
    fi
    echo "PASS test_last_arg_not_written_to_param_cell (moved from every call site into the callee)"
    return 0
}

test_runtime_last_arg_correct() {
    local dir="$tmpdir/runtime"
    mkdir -p "$dir"
    printf '%s\n' "$BETWEN_FIXTURE" >"$dir/t.c"
    if ! "$RCC" --target=pdp7 --format=rim --optimize --out="$dir/t.rim" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_runtime_last_arg_correct: fixture failed to compile"
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
        echo "FAIL test_runtime_last_arg_correct: expected AC=7 (every check passed)," \
             "got octal $ac"
        return 1
    fi
    echo "PASS test_runtime_last_arg_correct (AC=7)"
    return 0
}

CALLARG_FIXTURE='int helper(int n) { return n; }
int betwen(int value, int low, int high) {
    if (value < low) return 0;
    if (value > high) return 0;
    return 1;
}
int main() {
    return betwen(helper(5), 0, helper(6));
}'

test_call_containing_last_arg_correct() {
    local dir="$tmpdir/callarg"
    mkdir -p "$dir"
    printf '%s\n' "$CALLARG_FIXTURE" >"$dir/t.c"
    if ! "$RCC" --target=pdp7 --format=rim --optimize --out="$dir/t.rim" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_call_containing_last_arg_correct: fixture failed to compile"
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
    if [ "$((8#$ac))" != "1" ]; then
        echo "FAIL test_call_containing_last_arg_correct: expected AC=1" \
             "(betwen(helper(5),0,helper(6)) is true), got octal $ac"
        return 1
    fi
    echo "PASS test_call_containing_last_arg_correct (AC=1)"
    return 0
}

ARITY1_FIXTURE='int double_it(int n) {
    return n + n;
}
int main() {
    return double_it(21);
}'

test_arity_one_function_correct() {
    local dir="$tmpdir/arity1"
    mkdir -p "$dir"
    printf '%s\n' "$ARITY1_FIXTURE" >"$dir/t.c"
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_arity_one_function_correct: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    # The whole call collapses to "lac <literal>; jms <callee>_fast" -
    # checked structurally (not a flat count: emitLeafParamBinding()'s
    # own unrelated stack-based entry also legitimately writes this
    # same cell once, off the pushed-argument stack). The call site
    # itself (right before its own "jms") never writes it, matching
    # the hand-written convention's own word count exactly for a
    # single-argument call.
    if grep -B1 '^   jms FN_double_it_fast$' "$dir/t.s" | grep -qE '^   dac FN_double_it_P0$'; then
        echo "FAIL test_arity_one_function_correct: the call site's own code, right before" \
             "its 'jms FN_double_it_fast', still writes FN_double_it_P0 directly:"
        cat "$dir/t.s"
        return 1
    fi
    if ! "$RCC" --target=pdp7 --format=rim --optimize --out="$dir/t.rim" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_arity_one_function_correct: fixture failed to compile to rim"
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
    if [ "$((8#$ac))" != "42" ]; then
        echo "FAIL test_arity_one_function_correct: expected AC=42 (21+21), got octal $ac"
        return 1
    fi
    echo "PASS test_arity_one_function_correct (no Param cell at all, AC=42)"
    return 0
}

ARITY0_FIXTURE='int getseven() {
    return 7;
}
int main() {
    return getseven();
}'

test_arity_zero_function_unaffected() {
    local dir="$tmpdir/arity0"
    mkdir -p "$dir"
    printf '%s\n' "$ARITY0_FIXTURE" >"$dir/t.c"
    if ! "$RCC" --target=pdp7 --format=rim --optimize --out="$dir/t.rim" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_arity_zero_function_unaffected: fixture failed to compile"
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
        echo "FAIL test_arity_zero_function_unaffected: expected AC=7, got octal $ac"
        return 1
    fi
    echo "PASS test_arity_zero_function_unaffected (AC=7)"
    return 0
}

test_last_arg_not_written_to_param_cell || fail=1
test_runtime_last_arg_correct || fail=1
test_call_containing_last_arg_correct || fail=1
test_arity_one_function_correct || fail=1
test_arity_zero_function_unaffected || fail=1

exit $fail
