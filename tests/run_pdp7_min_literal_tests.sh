#!/usr/bin/env bash
# Unit tests for IRBuilder.cpp's literalType(): a negative integer literal
# was being sized by its *positive* magnitude before negation, so a value
# like `-131072` (this project's 18-bit pdp7 `int` own legal minimum,
# `-2^17`) looked like it needed more than 18 bits (`+131072` doesn't fit a
# signed 18-bit value, whose max is `2^17-1`) and got silently promoted to
# `long`, forcing every arithmetic/bitwise op on it through a multi-word
# carry-chain instead of a single pdp7 word. Fixed by sizing a `-<literal>`
# from its real, negated value, checking a signed candidate type's own
# correct (asymmetric) range instead of assuming non-negative.
#
# Two tests:
#   test_min_negative_literal_avoids_widening - `-131072` (the exact
#       boundary value) stays a plain `int`: expects a small push/pop
#       count (ordinary call overhead only, no wide-arithmetic carry
#       chain) and the correct result on the real simulator.
#   test_genuinely_oversized_negative_literal_still_widens - `-200000`
#       (too big even negated) must still go through the real multi-word
#       path - the fix must not disable widening when it's actually
#       needed.
#
# Usage: tests/run_pdp7_min_literal_tests.sh [path/to/rcc]

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

test_min_negative_literal_avoids_widening() {
    local dir="$tmpdir/minlit"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int get() { return 5; }
int main() {
    int x;
    x = get();
    return x | -131072;
}
EOF
    if ! "$RCC" --target=pdp7 --format=rim --optimize --out="$dir/t.rim" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_min_negative_literal_avoids_widening: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >/dev/null 2>&1; then
        echo "FAIL test_min_negative_literal_avoids_widening: asm dump failed to compile"
        return 1
    fi
    local pushPopCount
    pushPopCount="$(grep -c 'jms __push\|jms __pop' "$dir/t.s")"
    if [ "$pushPopCount" -gt 8 ]; then
        echo "FAIL test_min_negative_literal_avoids_widening: expected at most 8 push/pop" \
             "(ordinary call overhead, no wide-arithmetic carry chain), found $pushPopCount"
        return 1
    fi
    printf 'load %s 10000\ngo 10000\ne ac\nq\n' "$dir/t.rim" >"$dir/t.simh"
    timeout 15 pdp7 "$dir/t.simh" </dev/null >"$dir/t.out" 2>&1
    local ac_octal
    ac_octal="$(grep '^AC:' "$dir/t.out" | awk '{print $2}')"
    if [ "$ac_octal" != "400005" ]; then
        echo "FAIL test_min_negative_literal_avoids_widening: expected AC=400005 (octal)," \
             "got AC=$ac_octal"
        return 1
    fi
    echo "PASS test_min_negative_literal_avoids_widening ($pushPopCount push/pop, AC=400005)"
    return 0
}

test_genuinely_oversized_negative_literal_still_widens() {
    local dir="$tmpdir/stillwide"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int get() { return 5; }
int main() {
    int x;
    x = get();
    return x | -200000;
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_genuinely_oversized_negative_literal_still_widens: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    local pushPopCount
    pushPopCount="$(grep -c 'jms __push\|jms __pop' "$dir/t.s")"
    if [ "$pushPopCount" -lt 12 ]; then
        echo "FAIL test_genuinely_oversized_negative_literal_still_widens: expected the real" \
             "multi-word carry-chain (at least 12 push/pop) since -200000 doesn't fit even" \
             "negated, found only $pushPopCount - the fix may have over-corrected"
        return 1
    fi
    echo "PASS test_genuinely_oversized_negative_literal_still_widens ($pushPopCount push/pop, still widens)"
    return 0
}

test_min_negative_literal_avoids_widening || fail=1
test_genuinely_oversized_negative_literal_still_widens || fail=1

exit $fail
