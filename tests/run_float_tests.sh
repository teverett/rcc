#!/usr/bin/env bash
# Unit tests for float/double language support (src/Type.h's Float/Double
# BaseType, grammar/c/CLexer.g4's FLOAT_NUMBER, IRBuilder.cpp's
# resolveStructType()) - specifically the "backend doesn't support it"
# path every target not yet migrated to Backend::floatSupport() != None
# takes (see docs/platforms.md and the per-target TypeWidths::floatUnits/
# doubleUnits default of 0 in src/Type.h).
#
# Tests (no simulator needed - --format=asm only):
#   test_float_rejected_on_unsupported_target_cp1610 - a target with no
#       floatUnits set must reject 'float' with a clear, specific error,
#       not a crash or a silent miscompile.
#   test_double_rejected_on_unsupported_target_pdp1  - same, for 'double',
#       on a second, differently-shaped unsupported target (word-
#       addressed, not byte-addressed) - the "at least two" AGENTS.md asks
#       for, on two structurally different backends rather than the same
#       one twice.
#   test_float_literal_with_no_declared_variable_still_rejected - a bare
#       float literal with no variable of that type anywhere in the
#       program must still be rejected (the IRBuilder.cpp buildExprImpl
#       FloatLit case's own resolveStructType() call, not just the
#       Global/Local/Param declaration path) - regression coverage for
#       the gap a declaration-only check would have left.
#   test_float_accepted_in_grammar_but_rejected_in_backend - confirms the
#       rejection is a backend capability check, not a parse error: the
#       exact same source parses fine up to the point the type is
#       resolved against this target's TypeWidths.
#
# Usage: tests/run_float_tests.sh [path/to/rcc]

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

test_float_rejected_on_unsupported_target_cp1610() {
    local dir="$tmpdir/cp1610float"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
float g = 1.5f;
int main() {
    return 0;
}
EOF
    if "$RCC" --target=cp1610 --format=asm --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_float_rejected_on_unsupported_target_cp1610: expected a compile error, but it compiled"
        return 1
    fi
    if ! grep -qi "'float' is not supported" "$dir/log"; then
        echo "FAIL test_float_rejected_on_unsupported_target_cp1610: compile failed, but not with the expected message:"
        cat "$dir/log"
        return 1
    fi
    echo "PASS test_float_rejected_on_unsupported_target_cp1610"
    return 0
}

test_double_rejected_on_unsupported_target_pdp1() {
    local dir="$tmpdir/pdp1double"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int main() {
    double x;
    x = 1;
    return 0;
}
EOF
    if "$RCC" --target=pdp1 --format=asm --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_double_rejected_on_unsupported_target_pdp1: expected a compile error, but it compiled"
        return 1
    fi
    if ! grep -qi "'double' is not supported" "$dir/log"; then
        echo "FAIL test_double_rejected_on_unsupported_target_pdp1: compile failed, but not with the expected message:"
        cat "$dir/log"
        return 1
    fi
    echo "PASS test_double_rejected_on_unsupported_target_pdp1"
    return 0
}

test_float_literal_with_no_declared_variable_still_rejected() {
    local dir="$tmpdir/bareliteral"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int main() {
    return (int)3.5;
}
EOF
    if "$RCC" --target=pic16 --format=asm --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_float_literal_with_no_declared_variable_still_rejected: expected a compile error, but it compiled"
        return 1
    fi
    if ! grep -qi "is not supported" "$dir/log"; then
        echo "FAIL test_float_literal_with_no_declared_variable_still_rejected: compile failed, but not with the expected message:"
        cat "$dir/log"
        return 1
    fi
    echo "PASS test_float_literal_with_no_declared_variable_still_rejected"
    return 0
}

test_float_accepted_in_grammar_but_rejected_in_backend() {
    local dir="$tmpdir/parseok"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int main() {
    double x = 1.0 + 2.5e1 - 3.0 / 4.0 * 5.0f;
    return 0;
}
EOF
    "$RCC" --target=pic18 --format=asm --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1
    if grep -qi "syntax error\|mismatched input\|token recognition" "$dir/log"; then
        echo "FAIL test_float_accepted_in_grammar_but_rejected_in_backend: expected a clean parse (backend-capability rejection only), got a parse error:"
        cat "$dir/log"
        return 1
    fi
    if ! grep -qi "'double' is not supported" "$dir/log"; then
        echo "FAIL test_float_accepted_in_grammar_but_rejected_in_backend: expected the backend-capability rejection message:"
        cat "$dir/log"
        return 1
    fi
    echo "PASS test_float_accepted_in_grammar_but_rejected_in_backend"
    return 0
}

test_float_rejected_on_unsupported_target_cp1610 || fail=1
test_double_rejected_on_unsupported_target_pdp1 || fail=1
test_float_literal_with_no_declared_variable_still_rejected || fail=1
test_float_accepted_in_grammar_but_rejected_in_backend || fail=1

exit $fail
