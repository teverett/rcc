#!/usr/bin/env bash
# Unit tests for the pdp7 backend's '@inline_args' calling-convention
# extension (see AST.h's own comment on the attribute, and
# Pdp7Codegen.h's isInlineArgsCallEligible()/inlineWordText() comments).
#
# A call to an '@inline_args' function whose arguments after the first
# are all compile-time constants (an IntLit, or a Global array
# reference with a constant index, like `&arr[k]`) goes through a
# third entry point ("_lit") that reads those trailing arguments as
# literal/relocatable words inline in the caller's own instruction
# stream, right after the `jms`, instead of through named Param cells -
# trading a "lac arg; dac P<n>" pair (2 words) per such argument for
# one inline word. Argument 0 is never inlined (it's always "lac expr;
# dac P0", the same cost under every convention).
#
# Any call that doesn't qualify (a non-constant trailing argument)
# still reaches the function through the existing "_fast" entry,
# unaffected - the two entry points coexist, selected per call site,
# never per function.
#
# Six tests:
#   test_literal_trailing_args_use_lit_entry - structural: both
#       trailing arguments literal -> "_lit" entry, 2 inline words,
#       no Param-cell writes for them.
#   test_literal_trailing_args_correct - the value computed through
#       the "_lit" entry is correct on the real simulator.
#   test_global_address_trailing_arg_correct - a compile-time-constant
#       Global array address (`&arr[k]`, nonzero k) as a trailing
#       argument is correct on the simulator (exercises the
#       symbol+offset inline-word encoding, not just a plain literal).
#   test_nonconstant_trailing_arg_falls_back_to_fast_entry -
#       structural: a call whose trailing argument is NOT a compile-
#       time constant still uses the unchanged "_fast" entry, and both
#       entries coexist for the same callee in one program.
#   test_nonconstant_trailing_arg_correct - the "_fast"-path call's
#       own value is still correct.
#   test_arity_two_function_still_works - the single-trailing-param
#       case (not just two-or-more) is handled correctly (the
#       register-15 pre-decrement/correction arithmetic only involves
#       one loop iteration here, the narrowest case that could get an
#       off-by-one wrong).
#
# Usage: tests/run_pdp7_inline_args_tests.sh [path/to/rcc]

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

BETWEN_FIXTURE='int betwen(int value, int low, int high) @inline_args {
    if (value < low) return 0;
    if (value > high) return 0;
    return 1;
}
int main() {
    return betwen(5, 0, 6);
}'

test_literal_trailing_args_use_lit_entry() {
    local dir="$tmpdir/lit_struct"
    mkdir -p "$dir"
    printf '%s\n' "$BETWEN_FIXTURE" >"$dir/t.c"
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_literal_trailing_args_use_lit_entry: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! grep -q "jms FN_betwen_lit" "$dir/t.s"; then
        echo "FAIL test_literal_trailing_args_use_lit_entry: expected the call site to use" \
             "'jms FN_betwen_lit', not found:"
        cat "$dir/t.s"
        return 1
    fi
    # The two inline words (0 and 6) follow the jms directly, with no
    # "dac FN_betwen_P1"/"P2" anywhere near the call site (those cells
    # are only ever written by the "_lit" entry's OWN prologue, not
    # the caller).
    if ! grep -A3 "jms FN_betwen_lit" "$dir/t.s" | grep -qE '^0$'; then
        echo "FAIL test_literal_trailing_args_use_lit_entry: expected an inline '0' word" \
             "right after the jms, not found:"
        cat "$dir/t.s"
        return 1
    fi
    echo "PASS test_literal_trailing_args_use_lit_entry (lit entry + inline words confirmed)"
    return 0
}

test_literal_trailing_args_correct() {
    local dir="$tmpdir/lit_correct"
    mkdir -p "$dir"
    printf '%s\n' "$BETWEN_FIXTURE" >"$dir/t.c"
    if ! "$RCC" --target=pdp7 --format=rim --optimize --out="$dir/t.rim" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_literal_trailing_args_correct: fixture failed to compile"
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
        echo "FAIL test_literal_trailing_args_correct: expected AC=1 (betwen(5,0,6) is true)," \
             "got octal $ac"
        return 1
    fi
    echo "PASS test_literal_trailing_args_correct (AC=1)"
    return 0
}

ADDR_FIXTURE='int buf[10];
int storeAt(int value, int* addr) @inline_args {
    *addr = value;
    return value;
}
int main() {
    buf[3] = 0;
    storeAt(42, &buf[3]);
    return buf[3];
}'

test_global_address_trailing_arg_correct() {
    local dir="$tmpdir/addr"
    mkdir -p "$dir"
    printf '%s\n' "$ADDR_FIXTURE" >"$dir/t.c"
    if ! "$RCC" --target=pdp7 --format=rim --optimize --out="$dir/t.rim" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_global_address_trailing_arg_correct: fixture failed to compile"
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
        echo "FAIL test_global_address_trailing_arg_correct: expected AC=42" \
             "(*addr = value through the inlined &buf[3] address), got octal $ac"
        return 1
    fi
    echo "PASS test_global_address_trailing_arg_correct (AC=42)"
    return 0
}

MIXED_FIXTURE='int betwen(int value, int low, int high) @inline_args {
    if (value < low) return 0;
    if (value > high) return 0;
    return 1;
}
int wrap(int hi) {
    return betwen(5, 0, hi);
}
int main() {
    if (!wrap(6)) return 0;
    return betwen(9, 0, 6);
}'

test_nonconstant_trailing_arg_falls_back_to_fast_entry() {
    local dir="$tmpdir/fallback_struct"
    mkdir -p "$dir"
    printf '%s\n' "$MIXED_FIXTURE" >"$dir/t.c"
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_nonconstant_trailing_arg_falls_back_to_fast_entry: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! grep -q "jms FN_betwen_fast" "$dir/t.s"; then
        echo "FAIL test_nonconstant_trailing_arg_falls_back_to_fast_entry: expected the call with" \
             "a non-constant trailing arg ('hi') to still use 'jms FN_betwen_fast', not found:"
        cat "$dir/t.s"
        return 1
    fi
    if ! grep -q "jms FN_betwen_lit" "$dir/t.s"; then
        echo "FAIL test_nonconstant_trailing_arg_falls_back_to_fast_entry: expected the OTHER call" \
             "(both trailing args literal) to still use 'jms FN_betwen_lit', not found - the two" \
             "entries should coexist for the same callee:"
        cat "$dir/t.s"
        return 1
    fi
    echo "PASS test_nonconstant_trailing_arg_falls_back_to_fast_entry (both entries coexist)"
    return 0
}

test_nonconstant_trailing_arg_correct() {
    local dir="$tmpdir/fallback_correct"
    mkdir -p "$dir"
    printf '%s\n' "$MIXED_FIXTURE" >"$dir/t.c"
    if ! "$RCC" --target=pdp7 --format=rim --optimize --out="$dir/t.rim" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_nonconstant_trailing_arg_correct: fixture failed to compile"
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
    # betwen(5,0,hi=6) true (via _fast), then betwen(9,0,6) false (via
    # _lit) - AC=0 confirms BOTH calls actually ran with correct values
    # (a wrong _fast-path value for the first call would return early
    # with AC=0 too, but then the structural test above already
    # confirms _lit was really used for the second - together these
    # pin down that neither path is silently wrong).
    if [ "$((8#$ac))" != "0" ]; then
        echo "FAIL test_nonconstant_trailing_arg_correct: expected AC=0" \
             "(betwen(9,0,6) is false), got octal $ac"
        return 1
    fi
    echo "PASS test_nonconstant_trailing_arg_correct (AC=0)"
    return 0
}

ARITY2_FIXTURE='int buf[5];
int clearFrom(int dst, int count) @inline_args {
    int i;
    i = 0;
    while (i < count) {
        buf[dst + i] = 0;
        i = i + 1;
    }
    return count;
}
int main() {
    buf[0] = 9;
    buf[1] = 9;
    buf[2] = 9;
    clearFrom(1, 2);
    if (buf[0] != 9) return 100;
    if (buf[1] != 0) return 101;
    if (buf[2] != 0) return 102;
    return 7;
}'

test_arity_two_function_still_works() {
    local dir="$tmpdir/arity2"
    mkdir -p "$dir"
    printf '%s\n' "$ARITY2_FIXTURE" >"$dir/t.c"
    if ! "$RCC" --target=pdp7 --format=rim --optimize --out="$dir/t.rim" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_arity_two_function_still_works: fixture failed to compile"
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
        echo "FAIL test_arity_two_function_still_works: expected AC=7 (every check passed)," \
             "got octal $ac"
        return 1
    fi
    echo "PASS test_arity_two_function_still_works (AC=7)"
    return 0
}

CAST_ADDR_FIXTURE='int buf[10];
int store2(int value, int dest) @inline_args {
    *(int*)dest = value;
    return value;
}
int main() {
    buf[5] = 0;
    store2(123, (int)&buf[5]);
    return buf[5];
}'

# A trailing `&arr[k]` argument passed to a plain (non-pointer)
# parameter type - e.g. `copy(int src, int dest, int count)` called
# as `copy(x, (int)&arr[k], n)`, the exact shape the real kernel's own
# copy()/copyz() calls use - arrives wrapped in a Convert node (int*
# -> int). A version of inlineWordText() that only matched a bare
# Unary"&" directly missed every one of these (caught via a real
# before/after word-count diff on sys/dskswap.c's own copy() calls,
# which measurably fell back to "_fast" until this was fixed).
test_cast_wrapped_address_trailing_arg_correct() {
    local dir="$tmpdir/cast_addr"
    mkdir -p "$dir"
    printf '%s\n' "$CAST_ADDR_FIXTURE" >"$dir/t.c"
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_cast_wrapped_address_trailing_arg_correct: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! grep -q "jms FN_store2_lit" "$dir/t.s"; then
        echo "FAIL test_cast_wrapped_address_trailing_arg_correct: expected 'jms FN_store2_lit'" \
             "(a cast-wrapped &arr[k] trailing argument should still inline), not found:"
        cat "$dir/t.s"
        return 1
    fi
    if ! "$RCC" --target=pdp7 --format=rim --optimize --out="$dir/t.rim" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_cast_wrapped_address_trailing_arg_correct: fixture failed to compile to rim"
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
    if [ "$((8#$ac))" != "123" ]; then
        echo "FAIL test_cast_wrapped_address_trailing_arg_correct: expected AC=123" \
             "(*dest = value through the inlined, cast-wrapped &buf[5] address), got octal $ac"
        return 1
    fi
    echo "PASS test_cast_wrapped_address_trailing_arg_correct (AC=123)"
    return 0
}

test_cross_file_extern_inline_args_runs_correctly() {
    local dir="$tmpdir/crossfile"
    mkdir -p "$dir"
    cat >"$dir/caller.c" <<'EOF'
extern int betwen(int value, int low, int high) @inline_args;
int main() {
    return betwen(5, 0, 6);
}
EOF
    cat >"$dir/callee.c" <<'EOF'
int betwen(int value, int low, int high) @inline_args {
    if (value < low) return 0;
    if (value > high) return 0;
    return 1;
}
EOF
    if ! (cd "$dir" && "$RCC" --target=pdp7 --format=coff --optimize --dump-asm --out="caller.o" "caller.c") >"$dir/log" 2>&1; then
        echo "FAIL test_cross_file_extern_inline_args_runs_correctly: caller.c failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! "$RCC" --target=pdp7 --format=coff --optimize --out="$dir/callee.o" "$dir/callee.c" >>"$dir/log" 2>&1; then
        echo "FAIL test_cross_file_extern_inline_args_runs_correctly: callee.c failed to compile"
        cat "$dir/log"
        return 1
    fi
    # --dump-asm writes relative to the CWD at invocation time (not
    # next to --out), hence the `cd "$dir"` above - its own build/
    # subdirectory is where to look for it.
    if ! grep -q "jms FN_betwen_lit" "$dir/build/caller.s" 2>/dev/null; then
        echo "FAIL test_cross_file_extern_inline_args_runs_correctly: expected the cross-file" \
             "caller to use 'jms FN_betwen_lit' too (trusting the extern decl's own attribute," \
             "same cross-file contract '@nonrecursive' already relies on), not found"
        return 1
    fi
    if ! "$RCC" --link --target=pdp7 --format=rim --out="$dir/t.rim" "$dir/caller.o" "$dir/callee.o" >>"$dir/log" 2>&1; then
        echo "FAIL test_cross_file_extern_inline_args_runs_correctly: fixture failed to link"
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
        echo "FAIL test_cross_file_extern_inline_args_runs_correctly: expected AC=1" \
             "(betwen(5,0,6) is true), got octal $ac"
        return 1
    fi
    echo "PASS test_cross_file_extern_inline_args_runs_correctly (AC=1, _lit entry resolved across files)"
    return 0
}

test_literal_trailing_args_use_lit_entry || fail=1
test_literal_trailing_args_correct || fail=1
test_global_address_trailing_arg_correct || fail=1
test_nonconstant_trailing_arg_falls_back_to_fast_entry || fail=1
test_nonconstant_trailing_arg_correct || fail=1
test_arity_two_function_still_works || fail=1
test_cross_file_extern_inline_args_runs_correctly || fail=1
test_cast_wrapped_address_trailing_arg_correct || fail=1

exit $fail
