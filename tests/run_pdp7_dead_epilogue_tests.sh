#!/usr/bin/env bash
# Unit tests for Pdp7Codegen.cpp's genFunction(): a non-void function
# whose body already returns on every path (an explicit final
# `return`, or an `if`/`else` where both arms return) used to get an
# UNCONDITIONAL, always-dead copy of the return epilogue appended
# after it anyway - the leaf fast path's own 1-word `jmp i FN_name`,
# or the full frame-based path's own 7-word teardown (`lac __fp; dac
# __sp; jms __pop; dac __fp; jms __pop; dac __retaddr; jmp __retaddr
# i`) - neither ever reachable once every real path already jumps
# back itself. `stmtAlwaysReturns()` now proves the common case (the
# body's own last top-level statement is a Return, or an if/else
# where both arms are) and skips emitting it.
#
# Three tests:
#   test_leaf_final_return_drops_dead_jmp - a leaf (no-frame) function
#       ending in an explicit `return` loses its own dead trailing
#       `jmp i FN_name`.
#   test_framed_if_else_both_return_drops_dead_epilogue - a function
#       forced onto the full frame-based path (via an `asm{}` block,
#       same as `sys/s1.c`'s own `swap()`) whose body is `if (...)
#       {return 1;} return 0;` loses its own dead trailing epilogue
#       after both arms' own real ones.
#   test_missing_final_return_keeps_epilogue - an `if` with no `else`
#       genuinely CAN fall through (undefined in C, but the compiler
#       still needs to emit something) - the fallback epilogue must
#       stay.
#
# Usage: tests/run_pdp7_dead_epilogue_tests.sh [path/to/rcc]

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

test_leaf_final_return_drops_dead_jmp() {
    local dir="$tmpdir/leaf"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int get() { return 1; }
int main() {
    int x;
    x = get();
    if (x) {
        return 1;
    }
    return 0;
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_leaf_final_return_drops_dead_jmp: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    # Exactly 2 reachable `jmp i FN_main` (one per real return arm) -
    # a 3rd, dead one used to follow them.
    local jmpCount
    jmpCount="$(grep -c 'jmp i FN_main' "$dir/t.s")"
    if [ "$jmpCount" -ne 2 ]; then
        echo "FAIL test_leaf_final_return_drops_dead_jmp: expected exactly 2 'jmp i FN_main'" \
             "(one per real return arm, no dead trailing copy), found $jmpCount:"
        cat "$dir/t.s"
        return 1
    fi
    echo "PASS test_leaf_final_return_drops_dead_jmp (2 'jmp i FN_main', no dead copy)"
    return 0
}

test_framed_if_else_both_return_drops_dead_epilogue() {
    local dir="$tmpdir/framed"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int get() { return 1; }
int f() {
    int x;
    x = get();
    if (x) {
        asm {
            nop
        }
        return 1;
    }
    return 0;
}
int main() {
    return f();
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_framed_if_else_both_return_drops_dead_epilogue: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    # Exactly 2 reachable `jmp __retaddr i` (one per real return arm,
    # each with its own full epilogue ahead of it) - a 3rd, dead one
    # with no return-value setup ahead of it used to follow them.
    local retCount
    retCount="$(grep -c 'jmp __retaddr i' "$dir/t.s")"
    if [ "$retCount" -ne 2 ]; then
        echo "FAIL test_framed_if_else_both_return_drops_dead_epilogue: expected exactly 2" \
             "'jmp __retaddr i' (one per real return arm, no dead trailing copy), found" \
             "$retCount:"
        cat "$dir/t.s"
        return 1
    fi
    echo "PASS test_framed_if_else_both_return_drops_dead_epilogue (2 'jmp __retaddr i', no dead copy)"
    return 0
}

test_missing_final_return_keeps_epilogue() {
    local dir="$tmpdir/missing"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int get() { return 1; }
int f() {
    int x;
    x = get();
    if (x) {
        return 1;
    }
}
int main() {
    return f();
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_missing_final_return_keeps_epilogue: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    # An `if` with no `else` genuinely can fall through - 2 `jmp i
    # FN_f` (the if-true arm's own, plus the real fallback) must both
    # still be there.
    local jmpCount
    jmpCount="$(grep -c 'jmp i FN_f' "$dir/t.s")"
    if [ "$jmpCount" -ne 2 ]; then
        echo "FAIL test_missing_final_return_keeps_epilogue: expected 2 'jmp i FN_f' (the" \
             "if-true arm's own return, plus the real fallback for falling off the end)," \
             "found $jmpCount:"
        cat "$dir/t.s"
        return 1
    fi
    echo "PASS test_missing_final_return_keeps_epilogue (2 'jmp i FN_f', fallback kept)"
    return 0
}

test_leaf_final_return_drops_dead_jmp || fail=1
test_framed_if_else_both_return_drops_dead_epilogue || fail=1
test_missing_final_return_keeps_epilogue || fail=1

exit $fail
