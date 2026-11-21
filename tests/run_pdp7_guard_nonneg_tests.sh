#!/usr/bin/env bash
# Unit tests for src/optimizers/guard_nonneg/GuardNonNegative.cpp: proving
# non-negativity across a dominating `if (x >= 0) { ...uses of x... }` guard
# (the idiom InductionNonNegative.h's own header lists as still open) lets
# Pdp7Codegen.cpp's genCompareCells()/emitSignedLtCells() skip `x`'s own
# dynamic sign dispatch for every comparison inside the guarded block, the
# same way InductionNonNegative.cpp already does for a loop induction
# variable. A guarded `x < 10` comparison inside the matched `if` needs 2
# fewer `sma`s once the pass fires (the guard's own `x >= 0` check is
# unconditional either way, but the inner comparison now only pays the
# subtract-and-check path's own, not a second, separate outer check).
#
# Four tests:
#   test_guarded_use_skips_outer_sign_check - the plain idiom, `x` used only
#       inside the guard - expects the drop in `sma` count and the pass's
#       own diagnostic.
#   test_use_outside_guard_keeps_outer_sign_check - `x` is also read after
#       the guarded `if`, where it isn't proven non-negative - expects no
#       diagnostic and the un-optimized `sma` count.
#   test_address_taken_keeps_outer_sign_check - `&x` is taken (passed to
#       another function) after the guard - expects the same decline, since
#       the callee could leave `x` negative.
#   test_else_branch_read_keeps_outer_sign_check - `x` is read in the
#       guard's own `else` branch, reachable only when `x < 0` - expects the
#       same decline.
#
# Usage: tests/run_pdp7_guard_nonneg_tests.sh [path/to/rcc]

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

test_guarded_use_skips_outer_sign_check() {
    local dir="$tmpdir/safe"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int get() { return 5; }
int main() {
    int x = get();
    int count;
    count = 0;
    if (x >= 0) {
        if (x < 10) {
            count = 1;
        }
    }
    return count;
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --verbose --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_guarded_use_skips_outer_sign_check: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! grep -q "guard-nonneg: fn main: proved \`x\` stays non-negative" "$dir/log"; then
        echo "FAIL test_guarded_use_skips_outer_sign_check: expected the pass's own diagnostic, got:"
        cat "$dir/log"
        return 1
    fi
    local smaCount
    smaCount="$(grep -c 'sma' "$dir/t.s")"
    if [ "$smaCount" -ne 2 ]; then
        echo "FAIL test_guarded_use_skips_outer_sign_check: expected exactly 2 'sma' (the outer" \
             "\`x >= 0\` guard is a literal-zero comparison, always 1 'sma' via" \
             "Pdp7Codegen.cpp's own bIsZero fast path regardless of this pass; the inner" \
             "comparison's own SEPARATE sign check on \`x\` is the one this pass removes," \
             "leaving just its 1-'sma' subtract), found $smaCount"
        return 1
    fi
    echo "PASS test_guarded_use_skips_outer_sign_check (2 occurrences of 'sma', diagnostic present)"
    return 0
}

test_use_outside_guard_keeps_outer_sign_check() {
    local dir="$tmpdir/escapes"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int get() { return 5; }
int main() {
    int x = get();
    int count;
    count = 0;
    if (x >= 0) {
        if (x < 10) {
            count = 1;
        }
    }
    return count + x;
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --verbose --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_use_outside_guard_keeps_outer_sign_check: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if grep -q "guard-nonneg" "$dir/log"; then
        echo "FAIL test_use_outside_guard_keeps_outer_sign_check: pass fired despite \`x\`" \
             "being read after the guarded if:"
        cat "$dir/log"
        return 1
    fi
    local smaCount
    smaCount="$(grep -c 'sma' "$dir/t.s")"
    if [ "$smaCount" -ne 3 ]; then
        echo "FAIL test_use_outside_guard_keeps_outer_sign_check: expected 3 'sma' occurrences" \
             "(unsafe to drop the INNER sign check on \`x\` - the outer \`x >= 0\` guard's own" \
             "check collapses to 1 'sma' regardless, via Pdp7Codegen.cpp's bIsZero fast path)," \
             "found $smaCount"
        return 1
    fi
    echo "PASS test_use_outside_guard_keeps_outer_sign_check (3 occurrences of 'sma', no diagnostic)"
    return 0
}

test_address_taken_keeps_outer_sign_check() {
    local dir="$tmpdir/addrtaken"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int get() { return 5; }
void setNegative(int *p) { *p = -1; }
int main() {
    int x = get();
    int count;
    count = 0;
    if (x >= 0) {
        if (x < 10) {
            count = 1;
        }
    }
    setNegative(&x);
    return count;
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --verbose --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_address_taken_keeps_outer_sign_check: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if grep -q "guard-nonneg" "$dir/log"; then
        echo "FAIL test_address_taken_keeps_outer_sign_check: pass fired despite \`&x\` being taken:"
        cat "$dir/log"
        return 1
    fi
    local smaCount
    smaCount="$(grep -c 'sma' "$dir/t.s")"
    if [ "$smaCount" -ne 3 ]; then
        echo "FAIL test_address_taken_keeps_outer_sign_check: expected 3 'sma' occurrences" \
             "(unsafe to drop the INNER sign check on \`x\` - the outer \`x >= 0\` guard's own" \
             "check collapses to 1 'sma' regardless, via Pdp7Codegen.cpp's bIsZero fast path)," \
             "found $smaCount"
        return 1
    fi
    echo "PASS test_address_taken_keeps_outer_sign_check (3 occurrences of 'sma', no diagnostic)"
    return 0
}

test_else_branch_read_keeps_outer_sign_check() {
    local dir="$tmpdir/elsebranch"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int get() { return 5; }
int main() {
    int x = get();
    int count;
    count = 0;
    if (x >= 0) {
        if (x < 10) {
            count = 1;
        }
    } else {
        count = x;
    }
    return count;
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --verbose --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_else_branch_read_keeps_outer_sign_check: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if grep -q "guard-nonneg" "$dir/log"; then
        echo "FAIL test_else_branch_read_keeps_outer_sign_check: pass fired despite \`x\` being" \
             "read in the guard's own else branch:"
        cat "$dir/log"
        return 1
    fi
    local smaCount
    smaCount="$(grep -c 'sma' "$dir/t.s")"
    if [ "$smaCount" -ne 3 ]; then
        echo "FAIL test_else_branch_read_keeps_outer_sign_check: expected 3 'sma' occurrences" \
             "(unsafe to drop the INNER sign check on \`x\` - the outer \`x >= 0\` guard's own" \
             "check collapses to 1 'sma' regardless, via Pdp7Codegen.cpp's bIsZero fast path)," \
             "found $smaCount"
        return 1
    fi
    echo "PASS test_else_branch_read_keeps_outer_sign_check (3 occurrences of 'sma', no diagnostic)"
    return 0
}

test_guarded_use_skips_outer_sign_check || fail=1
test_use_outside_guard_keeps_outer_sign_check || fail=1
test_address_taken_keeps_outer_sign_check || fail=1
test_else_branch_read_keeps_outer_sign_check || fail=1

exit $fail
