#!/usr/bin/env bash
# Unit tests for the '@nonrecursive' function attribute: AST.h/IR.h's
# own `isNonRecursive`, IRBuilder.cpp's `checkNonRecursiveFunctions()`
# (same-file call-graph cycle rejection), `autoMarkNonRecursive()` (the
# same property auto-detected for a function with no explicit
# annotation at all, whenever its own visible call graph never leads
# back to itself and its address is never taken), and
# src/codegen/pdp7/Pdp7Codegen.cpp's own use of it to grant the "leaf
# fast path" (see Pdp7Codegen.h's own comment) to a function that calls
# out, as long as it's never re-entered - see docs/language.md's own
# "Non-recursion" section for the full contract.
#
# Four tests:
#   test_nonrecursive_function_with_calls_gets_leaf_storage - a function
#       that calls another function, annotated '@nonrecursive', must use
#       the flat per-slot storage scheme (no `lac __fp`/frame teardown at
#       all) and still compute the right answer on the real simulator.
#   test_same_function_without_annotation_keeps_real_frame - a genuinely
#       self-recursive function, with no annotation at all, must still
#       use the ordinary frame-based scheme (`lac __fp` present) -
#       confirms `autoMarkNonRecursive()` correctly declines to mark a
#       function unsafe to mark, not that an unannotated function never
#       gets the leaf path at all (a provably non-recursive one now
#       does, automatically - see
#       run_pdp7_auto_nonrecursive_tests.sh).
#   test_direct_self_recursion_is_rejected - an '@nonrecursive' function
#       calling itself must fail to compile.
#   test_same_file_mutual_recursion_is_rejected - two '@nonrecursive'-
#       adjacent functions calling each other (A -> B -> A), entirely
#       within one file, must also fail to compile - not just the direct
#       self-call case.
#
# Usage: tests/run_pdp7_nonrecursive_tests.sh [path/to/rcc]

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

test_nonrecursive_function_with_calls_gets_leaf_storage() {
    local dir="$tmpdir/annotated"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int helper(int x) {
    return x + 1;
}
int caller(int a, int b) @nonrecursive {
    int sum;
    sum = helper(a) + helper(b);
    return sum;
}
int main() {
    return caller(3, 4);
}
EOF
    if ! "$RCC" --target=pdp7 --format=rim --optimize --out="$dir/t.rim" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_nonrecursive_function_with_calls_gets_leaf_storage: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >/dev/null 2>&1; then
        echo "FAIL test_nonrecursive_function_with_calls_gets_leaf_storage: asm dump failed to compile"
        return 1
    fi
    local fpCount
    fpCount="$(awk '/^FN_caller:/{f=1} f{print} /jmp i FN_caller/{exit}' "$dir/t.s" | grep -c 'lac __fp')"
    if [ "$fpCount" -ne 0 ]; then
        echo "FAIL test_nonrecursive_function_with_calls_gets_leaf_storage: expected no frame-pointer" \
             "setup ('lac __fp') in caller() - the '@nonrecursive' leaf path should avoid it entirely," \
             "found $fpCount occurrence(s)"
        return 1
    fi
    printf 'load %s 10000\ngo 10000\ne ac\nq\n' "$dir/t.rim" >"$dir/t.simh"
    timeout 15 pdp7 "$dir/t.simh" </dev/null >"$dir/t.out" 2>&1
    local ac_octal
    ac_octal="$(grep '^AC:' "$dir/t.out" | awk '{print $2}')"
    if [ "$((8#$ac_octal))" -ne 9 ]; then
        echo "FAIL test_nonrecursive_function_with_calls_gets_leaf_storage: expected AC=9" \
             "(helper(3)+helper(4)), got AC=$((8#$ac_octal))"
        return 1
    fi
    echo "PASS test_nonrecursive_function_with_calls_gets_leaf_storage (no frame setup, AC=9)"
    return 0
}

test_same_function_without_annotation_keeps_real_frame() {
    local dir="$tmpdir/control"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int fact(int n) {
    if (n <= 1) return 1;
    return n * fact(n - 1);
}
int main() {
    return fact(5);
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --verbose --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_same_function_without_annotation_keeps_real_frame: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if grep -q "auto-nonrecursive: fn fact" "$dir/log"; then
        echo "FAIL test_same_function_without_annotation_keeps_real_frame: autoMarkNonRecursive()" \
             "marked a genuinely self-recursive function:"
        cat "$dir/log"
        return 1
    fi
    local fpCount
    fpCount="$(awk '/^FN_fact:/{f=1} f{print} /jmp i __retaddr/{exit}' "$dir/t.s" | grep -c 'lac __fp')"
    if [ "$fpCount" -eq 0 ]; then
        echo "FAIL test_same_function_without_annotation_keeps_real_frame: expected the ordinary" \
             "frame-based scheme ('lac __fp') for a genuinely recursive function, found none"
        return 1
    fi
    echo "PASS test_same_function_without_annotation_keeps_real_frame ($fpCount occurrence(s) of 'lac __fp')"
    return 0
}

test_direct_self_recursion_is_rejected() {
    local dir="$tmpdir/direct_rec"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int fact(int n) @nonrecursive {
    if (n == 0) {
        return 1;
    }
    return n * fact(n - 1);
}
int main() {
    return fact(3);
}
EOF
    if "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_direct_self_recursion_is_rejected: expected a compile error, fixture compiled cleanly"
        return 1
    fi
    if ! grep -q "leads back to itself" "$dir/log"; then
        echo "FAIL test_direct_self_recursion_is_rejected: compile failed, but not with the expected message:"
        cat "$dir/log"
        return 1
    fi
    echo "PASS test_direct_self_recursion_is_rejected (rejected with the expected message)"
    return 0
}

test_same_file_mutual_recursion_is_rejected() {
    local dir="$tmpdir/mutual_rec"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int isEven(int n);
int isOdd(int n) @nonrecursive {
    if (n == 0) {
        return 0;
    }
    return isEven(n - 1);
}
int isEven(int n) {
    if (n == 0) {
        return 1;
    }
    return isOdd(n - 1);
}
int main() {
    return isEven(4);
}
EOF
    if "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_same_file_mutual_recursion_is_rejected: expected a compile error, fixture compiled cleanly"
        return 1
    fi
    if ! grep -q "leads back to itself" "$dir/log"; then
        echo "FAIL test_same_file_mutual_recursion_is_rejected: compile failed, but not with the expected message:"
        cat "$dir/log"
        return 1
    fi
    echo "PASS test_same_file_mutual_recursion_is_rejected (rejected with the expected message)"
    return 0
}

test_nonrecursive_function_with_calls_gets_leaf_storage || fail=1
test_same_function_without_annotation_keeps_real_frame || fail=1
test_direct_self_recursion_is_rejected || fail=1
test_same_file_mutual_recursion_is_rejected || fail=1

exit $fail
