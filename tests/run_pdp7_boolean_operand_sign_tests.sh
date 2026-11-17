#!/usr/bin/env bash
# Unit tests for Pdp7Codegen.cpp's genCompareCells()'s `hasStaticSign()`:
# a boolean-valued expression (a comparison/logical operator, or `!`)
# always evaluates to 0 or 1, so it's statically known non-negative with no
# runtime check, regardless of its own operands' values. This doesn't make
# the boolean expression itself a "ready" operand (simpleCell() still can't
# produce a cell for a Binary/Unary node without evaluating it into a
# temp first), but it's enough to let a *sibling* operand already proven
# non-negative by the induction-nonneg pass (see
# run_pdp7_induction_nonneg_tests.sh) skip its own dynamic sign dispatch
# and avoid the shared `__cmp_lt` subroutine - see docs/known-bugs.md's
# PDP-7 entry for the "narrower value range" case this closes a sliver of.
#
# Two tests:
#   test_boolean_other_operand_avoids_shared_subroutine - the induction
#       variable compared against a non-foldable boolean-valued expression
#       must not route through `__cmp_lt` at all (its own sign is now
#       known, so the dispatch becomes an inline one-sided check).
#   test_nonboolean_other_operand_still_uses_shared_subroutine - the same
#       induction variable compared against an ordinary unknown-sign
#       variable (not boolean-valued) must still route through `__cmp_lt`,
#       confirming the broadened check doesn't fire where it shouldn't.
#
# Usage: tests/run_pdp7_boolean_operand_sign_tests.sh [path/to/rcc]

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

test_boolean_other_operand_avoids_shared_subroutine() {
    local dir="$tmpdir/boolop"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int check(int x) {
    int i;
    int count;
    count = 0;
    for (i = 0; i < (x == 5); i = i + 1) {
        count = count + 1;
    }
    return count;
}
int main() {
    return check(5) + check(2);
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_boolean_other_operand_avoids_shared_subroutine: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    local cmpLtCount
    cmpLtCount="$(grep -c '__cmp_lt' "$dir/t.s")"
    if [ "$cmpLtCount" -ne 0 ]; then
        echo "FAIL test_boolean_other_operand_avoids_shared_subroutine: expected no '__cmp_lt' at all" \
             "(the boolean-valued other operand's sign is always known), found $cmpLtCount occurrence(s)"
        return 1
    fi

    if ! "$RCC" --target=pdp7 --format=rim --out="$dir/t.rim" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_boolean_other_operand_avoids_shared_subroutine: rim build unexpectedly failed:"
        cat "$dir/log"
        return 1
    fi
    if command -v pdp7 >/dev/null 2>&1; then
        local simfile="$dir/run.simh" outfile="$dir/run.out"
        cat >"$simfile" <<SIMH
load $dir/t.rim 10000
go 10000
e ac
exit
SIMH
        pdp7 <"$simfile" >"$outfile" 2>/dev/null
        local acOctal
        acOctal="$(grep '^AC:' "$outfile" | awk '{print $2}')"
        if [ -n "$acOctal" ]; then
            local acDec=$((8#$acOctal))
            if [ "$acDec" -ne 1 ]; then
                echo "FAIL test_boolean_other_operand_avoids_shared_subroutine: expected AC=1" \
                     "(check(5) loops once, check(2) loops zero times), got AC=$acDec"
                return 1
            fi
        fi
    fi

    echo "PASS test_boolean_other_operand_avoids_shared_subroutine (no '__cmp_lt' emitted)"
    return 0
}

test_nonboolean_other_operand_still_uses_shared_subroutine() {
    local dir="$tmpdir/nonboolop"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int check(int x, int y) {
    int i;
    int count;
    count = 0;
    for (i = 0; i < x + y; i = i + 1) {
        count = count + 1;
    }
    return count;
}
int main() {
    return check(2, 1);
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_nonboolean_other_operand_still_uses_shared_subroutine: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    local cmpLtCount
    cmpLtCount="$(grep -c '__cmp_lt' "$dir/t.s")"
    if [ "$cmpLtCount" -eq 0 ]; then
        echo "FAIL test_nonboolean_other_operand_still_uses_shared_subroutine: expected '__cmp_lt' to" \
             "still be used (x+y is not a boolean-valued expression, so its sign is genuinely" \
             "unknown), found none"
        return 1
    fi
    echo "PASS test_nonboolean_other_operand_still_uses_shared_subroutine ('__cmp_lt' still used)"
    return 0
}

test_boolean_other_operand_avoids_shared_subroutine || fail=1
test_nonboolean_other_operand_still_uses_shared_subroutine || fail=1

exit $fail
