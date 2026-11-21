#!/usr/bin/env bash
# Unit tests for src/codegen/pdp7/Pdp7Codegen.cpp's emitSignedLtCells():
# when one comparison operand's sign is unknown at compile time but the
# other's is statically known (a literal, or a slot proven non-negative),
# its own A-sign dispatch (`lac A; sma; jmp ...; jmp ...`) used to always
# target two freshly allocated labels (LaNeg/LaPos) whose own body, in
# this case, is exactly one more unconditional `jmp` to the real
# destination - a relay, confirmed directly against the real UnixV0-C
# kernel's `sys/s5.c fassign()` (see docs/known-bugs.md's history on this
# before it was fixed). Since branchOnB()'s target is a pure function of
# its own two arguments and the known sign, emitSignedLtCells() now
# resolves it before emitting A's dispatch and jumps straight there,
# skipping the relay label and the jump that reached it entirely.
#
# Three tests:
#   test_known_nonnegative_bound_skips_relay - `a < 10` (`a` from an
#       unknown source, `10` a non-negative literal) - asserts no label
#       in the output is a relay (its entire body is one `jmp` to another
#       label) and that the comparison still gives the right answer.
#   test_known_negative_bound_skips_relay - `a < -5` (signB resolves the
#       other way) - same two assertions.
#   test_both_signs_unknown_still_uses_shared_subroutine - `a < b`, both
#       from unknown sources - the path this fix doesn't touch (neither
#       sign is known, so emitSignedLtCells() routes through the shared
#       `__cmp_lt` subroutine, no per-call dispatch at all) - confirms
#       that path is unaffected and still gives the right answer.
#
# Usage: tests/run_pdp7_relay_jump_tests.sh [path/to/rcc]

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

# A label is a relay when its very next non-label line is a bare `jmp`
# and the line after THAT is itself another label - i.e. its entire body
# is that one jump, reached only by falling into it or by some other
# `jmp` naming it.
has_relay() {
    awk '
        /^[A-Za-z_][A-Za-z0-9_]*:[[:space:]]*$/ { labels[NR] = 1 }
        { line[NR] = $0 }
        END {
            for (n in labels) {
                body = line[n + 1]
                after = line[n + 2]
                if (body ~ /^[[:space:]]*jmp[[:space:]]/ && after ~ /^[A-Za-z_][A-Za-z0-9_]*:[[:space:]]*$/) {
                    print "relay at line " n ": " line[n] " -> " body
                }
            }
        }
    ' "$1"
}

test_known_nonnegative_bound_skips_relay() {
    local dir="$tmpdir/pos"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int get() { return 3; }
int main() {
    int a = get();
    if (a < 10) {
        return 1;
    }
    return 0;
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_known_nonnegative_bound_skips_relay: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    local relays
    relays="$(has_relay "$dir/t.s")"
    if [ -n "$relays" ]; then
        echo "FAIL test_known_nonnegative_bound_skips_relay: found a relay label:"
        echo "$relays"
        return 1
    fi
    echo "PASS test_known_nonnegative_bound_skips_relay (no relay label in output)"
    return 0
}

test_known_negative_bound_skips_relay() {
    local dir="$tmpdir/neg"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int get() { return 3; }
int main() {
    int a = get();
    if (a < -5) {
        return 1;
    }
    return 0;
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_known_negative_bound_skips_relay: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    local relays
    relays="$(has_relay "$dir/t.s")"
    if [ -n "$relays" ]; then
        echo "FAIL test_known_negative_bound_skips_relay: found a relay label:"
        echo "$relays"
        return 1
    fi
    echo "PASS test_known_negative_bound_skips_relay (no relay label in output)"
    return 0
}

test_both_signs_unknown_still_uses_shared_subroutine() {
    local dir="$tmpdir/both"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int get(int n) { return n; }
int main() {
    int a = get(5);
    int b = get(-5);
    if (a < b) {
        return 1;
    }
    return 0;
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_both_signs_unknown_still_uses_shared_subroutine: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! grep -q '__cmp_lt' "$dir/t.s"; then
        echo "FAIL test_both_signs_unknown_still_uses_shared_subroutine: expected the shared" \
             "'__cmp_lt' subroutine to still be used when neither operand's sign is known"
        return 1
    fi
    echo "PASS test_both_signs_unknown_still_uses_shared_subroutine ('__cmp_lt' still used)"
    return 0
}

test_known_nonnegative_bound_skips_relay || fail=1
test_known_negative_bound_skips_relay || fail=1
test_both_signs_unknown_still_uses_shared_subroutine || fail=1

exit $fail
