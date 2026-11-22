#!/usr/bin/env bash
# Unit tests for Pdp7Codegen.cpp's genConvert(): narrowing a WIDE
# LITERAL (a plain decimal constant past a narrow signed int's own
# positive range - real C's own literal-typing rules make e.g.
# `131072` a `long`, even when it's immediately assigned to a narrow
# `unsigned int` it fits fine in - exactly bit 17, a valid 18-bit
# unsigned value) down to a narrow target used to push every one of
# pushWideLiteral()'s own words onto the real stack and pop them all
# back, discarding every word but the low one - full runtime stack
# traffic for a value already completely known at compile time.
# Confirmed directly against `sys/sleep.c`'s own `sleep()`
# (`unsigned int bit; bit = 131072;`).
#
# Four tests:
#   test_wide_literal_narrow_target_skips_stack_traffic - `unsigned
#       int x = 131072;` must not push/pop at all.
#   test_narrow_literal_narrow_target_unaffected - an ordinary literal
#       already in `int` range through the SAME conversion shape
#       keeps behaving the same (sanity check the fast path's own
#       guard - IntLit only - doesn't misfire more broadly).
#   test_wide_literal_values_correct - several values (0, exactly bit
#       17, all 18 bits set) assigned to a narrow target, each
#       decided correctly on the real simulator.
#   test_non_literal_wide_source_still_uses_runtime_path - narrowing
#       a wide value that ISN'T a compile-time constant (returned from
#       a call) must still work, unaffected by this fast path.
#
# Usage: tests/run_pdp7_wide_literal_convert_tests.sh [path/to/rcc]

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

test_wide_literal_narrow_target_skips_stack_traffic() {
    local dir="$tmpdir/skip"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int main() {
    unsigned int bit;
    bit = 131072;
    return bit;
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_wide_literal_narrow_target_skips_stack_traffic: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if grep -qE '__push|__pop' "$dir/t.s"; then
        echo "FAIL test_wide_literal_narrow_target_skips_stack_traffic: expected no runtime" \
             "stack traffic at all for a compile-time-constant wide literal, found some:"
        cat "$dir/t.s"
        return 1
    fi
    echo "PASS test_wide_literal_narrow_target_skips_stack_traffic (no __push/__pop)"
    return 0
}

test_narrow_literal_narrow_target_unaffected() {
    local dir="$tmpdir/narrow"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int main() {
    int x;
    x = 5;
    return x;
}
EOF
    if ! "$RCC" --target=pdp7 --format=rim --optimize --out="$dir/t.rim" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_narrow_literal_narrow_target_unaffected: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    local ac
    ac="$(run_ac "$dir/t.rim")"
    if [ "$((8#$ac))" != "5" ]; then
        echo "FAIL test_narrow_literal_narrow_target_unaffected: expected AC=5, got octal $ac"
        return 1
    fi
    echo "PASS test_narrow_literal_narrow_target_unaffected (AC=5)"
    return 0
}

test_wide_literal_values_correct() {
    local dir="$tmpdir/values"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int main() {
    unsigned int a;
    unsigned int b;
    unsigned int c;
    int r;
    a = 0;
    b = 131072;   // exactly bit 17
    c = 262143;   // all 18 bits set (0777777 octal)
    r = 0;
    r = r + (a == 0);
    r = r + (b == 131072);
    r = r + (c == 262143);
    return r;
}
EOF
    if ! "$RCC" --target=pdp7 --format=rim --optimize --out="$dir/t.rim" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_wide_literal_values_correct: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    local ac
    ac="$(run_ac "$dir/t.rim")"
    if [ "$((8#$ac))" != "3" ]; then
        echo "FAIL test_wide_literal_values_correct: expected AC=3 (all 3 values correct)," \
             "got octal $ac"
        return 1
    fi
    echo "PASS test_wide_literal_values_correct (AC=3)"
    return 0
}

test_non_literal_wide_source_still_uses_runtime_path() {
    local dir="$tmpdir/nonliteral"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
long getWide() { return 4294967296; }  // 2^32 - low 18 bits are 0
int main() {
    long w;
    int narrow;
    w = getWide();
    narrow = w;
    return narrow;
}
EOF
    if ! "$RCC" --target=pdp7 --format=rim --optimize --out="$dir/t.rim" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_non_literal_wide_source_still_uses_runtime_path: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    local ac
    ac="$(run_ac "$dir/t.rim")"
    if [ "$((8#$ac))" != "0" ]; then
        echo "FAIL test_non_literal_wide_source_still_uses_runtime_path: expected AC=0" \
             "(2^32's own low 18 bits), got octal $ac"
        return 1
    fi
    echo "PASS test_non_literal_wide_source_still_uses_runtime_path (AC=0)"
    return 0
}

test_wide_literal_narrow_target_skips_stack_traffic || fail=1
test_narrow_literal_narrow_target_unaffected || fail=1
test_wide_literal_values_correct || fail=1
test_non_literal_wide_source_still_uses_runtime_path || fail=1

exit $fail
