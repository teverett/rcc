#!/usr/bin/env bash
# Unit tests for LocalCSE.cpp's new runWithExpensiveVarCache(): on a
# target where Backend::isLocalReadExpensive() says re-reading a
# Local/Param costs real instructions AND caching it in a fresh temp
# would actually be cheaper (see Codegen.h's own comment on that
# hook, and Pdp7Codegen.cpp's own isLocalReadExpensive() for exactly
# which functions qualify), a second read of the same slot with
# nothing invalidating it in between is cached in a temp instead of
# re-derived.
#
# The function has to be `@nonrecursive` (or provably call-free) for
# this to pay off at all: Pdp7Codegen's own usesStaticCell() only
# ever gives a Local/Param a cheap static cell inside a function
# that's reentrancy-safe, and a LocalCSE-introduced cache temp is no
# exception - caching an expensive read in a function that ISN'T
# reentrancy-safe (e.g. sys/dskswap.c's own swap(), neither
# @nonrecursive nor call-free) would only add the new temp's own
# write, never save anything, since the temp would be exactly as
# expensive to read back as the original. This was a real bug caught
# during this fix's own development (see docs/known-bugs.md) - the
# first version of isLocalReadExpensive() didn't check this, and
# caused a measured REGRESSION (+6 words) in swap() before it was
# caught by a before/after word-count comparison and fixed.
#
# Two tests:
#   test_repeated_plain_local_read_is_cached - structural: the
#       verbose log reports the new pass promoting a repeated read,
#       and the second read reaches the cached value through a
#       direct static-cell read (one `tad`/`dac` to a Local/Param's
#       own FN_f_LF<n> label, no frame-address computation), not a
#       frame-relative reload.
#   test_repeated_plain_local_read_still_correct - the actual value
#       computed through the cached read is still right, verified on
#       the real simulator.
#
# Usage: tests/run_pdp7_local_cse_expensive_var_tests.sh [path/to/rcc]

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

# `f` is `@nonrecursive` but disqualified from the full leaf path by
# its own array local `junk[3]` (same general shape as
# tests/run_pdp7_hoisted_addr_const_tests.sh's own fixture) - this is
# exactly the function shape isLocalReadExpensive() requires: non-
# leaf, but still reentrancy-safe, so a cached value CAN get its own
# static cell. `plain` is read twice with nothing between (no call,
# no write to `plain`) to invalidate the cache.
FIXTURE='int helper(int n) { return n; }
int f(int idx) @nonrecursive {
    int junk[3];
    int plain;
    junk[0] = helper(idx);
    plain = idx + 1;
    return plain + plain;
}
int main() {
    return f(5);
}'

test_repeated_plain_local_read_is_cached() {
    local dir="$tmpdir/cached"
    mkdir -p "$dir"
    printf '%s\n' "$FIXTURE" >"$dir/t.c"
    if ! "$RCC" --target=pdp7 --format=asm --optimize --verbose --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_repeated_plain_local_read_is_cached: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! grep -q "local-cse: .*hoisted repeated subexpression \`plain\`" "$dir/log"; then
        echo "FAIL test_repeated_plain_local_read_is_cached: expected a local-cse log line" \
             "promoting a repeated read of \`plain\`, not found:"
        cat "$dir/log"
        return 1
    fi
    # The cached value is read back through a direct static-cell
    # access ("tad FN_f_LF<n>", no frame-address computation ahead of
    # it) - a version without this fix reaches `plain` only through
    # "lac __fp; tad N; dac __addr; lac __addr i" every time.
    if ! grep -qE '^   tad FN_f_LF[0-9]+$' "$dir/t.s"; then
        echo "FAIL test_repeated_plain_local_read_is_cached: expected a direct" \
             "'tad FN_f_LF<n>' read of the cached temp, not found:"
        cat "$dir/t.s"
        return 1
    fi
    echo "PASS test_repeated_plain_local_read_is_cached (log + direct static-cell read confirm the cache)"
    return 0
}

test_repeated_plain_local_read_still_correct() {
    local dir="$tmpdir/correct"
    mkdir -p "$dir"
    printf '%s\n' "$FIXTURE" >"$dir/t.c"
    if ! "$RCC" --target=pdp7 --format=rim --optimize --out="$dir/t.rim" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_repeated_plain_local_read_still_correct: fixture failed to compile"
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
    # plain = idx + 1 = 6; f() returns plain + plain = 12 (014 octal,
    # printed with leading zeros by the simulator's own "e ac"), both
    # operands read through the cached value.
    if [ "$((8#$ac))" != "12" ]; then
        echo "FAIL test_repeated_plain_local_read_still_correct: expected AC=12 decimal" \
             "(014 octal, through the cached read), got octal $ac"
        return 1
    fi
    echo "PASS test_repeated_plain_local_read_still_correct (AC=000014 octal = 12 decimal)"
    return 0
}

# Regression test for the bug this fix's own development caught: `g`
# is kept out of auto-nonrecursive by its own `asm{}` block (the same
# exclusion swap() hits for real), so it's NEVER reentrancy-safe -
# `plain2` must NOT be cached, since a cache temp there would be
# exactly as expensive to read back as re-deriving `plain2` directly,
# a pure loss (the first version of this fix cached it anyway,
# costing swap() itself 6 extra words in the real kernel before this
# was caught).
test_plain_local_not_cached_when_function_not_call_safe() {
    local dir="$tmpdir/notcached"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int helper(int n) { return n; }
int g(int idx) {
    int plain2;
    plain2 = helper(idx);
    asm {
        nop
    }
    return plain2 + plain2;
}
int main() {
    return g(5);
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --verbose --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_plain_local_not_cached_when_function_not_call_safe: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if grep -q "local-cse: .*hoisted repeated subexpression \`plain2\`" "$dir/log"; then
        echo "FAIL test_plain_local_not_cached_when_function_not_call_safe: \`plain2\` was cached" \
             "in a function that isn't reentrancy-safe - this is strictly worse, never better:"
        cat "$dir/log"
        return 1
    fi
    echo "PASS test_plain_local_not_cached_when_function_not_call_safe (correctly left uncached)"
    return 0
}

test_repeated_plain_local_read_is_cached || fail=1
test_repeated_plain_local_read_still_correct || fail=1
test_plain_local_not_cached_when_function_not_call_safe || fail=1

exit $fail
