#!/usr/bin/env bash
# Unit tests for IRBuilder.cpp's `autoMarkNonRecursive()`: auto-marks a
# function '@nonrecursive' (the same leaf-storage codegen benefit an
# explicit source annotation gets, see Pdp7Codegen.h's own "Leaf fast
# path" comment) whenever its own visible call graph never leads back
# to itself and its own address is never taken anywhere - without a
# human needing to notice and write the annotation by hand.
#
# Five tests:
#   test_provably_nonrecursive_function_gets_marked_automatically - an
#       ordinary function that calls another, with no annotation at
#       all, gets the leaf fast path and the pass's own diagnostic.
#   test_mutual_recursion_is_not_marked - two functions calling each
#       other (A -> B -> A), neither annotated, must both keep the
#       ordinary frame-based scheme - the same cycle rejection
#       `checkNonRecursiveFunctions()` already enforces for an
#       explicit assertion must also hold for auto-detection.
#   test_function_whose_address_is_taken_is_not_marked - a function
#       passed by address into a Global array (the
#       `int g_xAddr[1] = {f};` idiom this language's own real
#       `&global`-as-a-runtime-value workaround makes routine) is
#       never auto-marked, even though its own ordinary call graph
#       looks cycle-free - a callback context this pass can't see
#       could still call back into it.
#   test_explicit_annotation_is_never_overridden - a function already
#       marked '@nonrecursive' by hand is left alone, not re-diagnosed
#       as if newly discovered.
#   test_function_with_raw_asm_block_is_not_marked - a function whose
#       body contains a raw `asm { ... }` block, otherwise provably
#       cycle-free, is never auto-marked either: a real regression
#       caught testing this exact pass against a real downstream
#       kernel - `swap()`'s own `*(fp+1)`-based continuation capture
#       (an `asm{}` block reading `__fp` directly) silently broke once
#       auto-marked, since the leaf fast path removes the real call
#       frame that idiom depends on existing.
#
# Usage: tests/run_pdp7_auto_nonrecursive_tests.sh [path/to/rcc]

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

test_provably_nonrecursive_function_gets_marked_automatically() {
    local dir="$tmpdir/safe"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int helper(int x) {
    return x + 1;
}
int caller(int a, int b) {
    int sum;
    sum = helper(a) + helper(b);
    return sum;
}
int main() {
    return caller(3, 4);
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --verbose --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_provably_nonrecursive_function_gets_marked_automatically: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! grep -q "auto-nonrecursive: fn caller: proved it never calls back into itself" "$dir/log"; then
        echo "FAIL test_provably_nonrecursive_function_gets_marked_automatically: expected the" \
             "pass's own diagnostic for 'caller', got:"
        cat "$dir/log"
        return 1
    fi
    local fpCount
    fpCount="$(awk '/^FN_caller:/{f=1} f{print} /jmp i __retaddr/{exit}' "$dir/t.s" | grep -c 'lac __fp')"
    if [ "$fpCount" -ne 0 ]; then
        echo "FAIL test_provably_nonrecursive_function_gets_marked_automatically: expected no" \
             "frame setup ('lac __fp') in caller() - the leaf path should have been auto-granted," \
             "found $fpCount occurrence(s)"
        return 1
    fi
    echo "PASS test_provably_nonrecursive_function_gets_marked_automatically (no frame setup, diagnostic present)"
    return 0
}

test_mutual_recursion_is_not_marked() {
    local dir="$tmpdir/mutual"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int isEven(int n);
int isOdd(int n) {
    if (n == 0) return 0;
    return isEven(n - 1);
}
int isEven(int n) {
    if (n == 0) return 1;
    return isOdd(n - 1);
}
int main() {
    return isEven(4);
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --verbose --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_mutual_recursion_is_not_marked: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if grep -q "auto-nonrecursive: fn is[OE]" "$dir/log"; then
        echo "FAIL test_mutual_recursion_is_not_marked: autoMarkNonRecursive() marked a mutually" \
             "recursive function:"
        cat "$dir/log"
        return 1
    fi
    local fpCountOdd fpCountEven
    fpCountOdd="$(awk '/^FN_isOdd:/{f=1} f{print} /jmp i __retaddr/{exit}' "$dir/t.s" | grep -c 'lac __fp')"
    fpCountEven="$(awk '/^FN_isEven:/{f=1} f{print} /jmp i __retaddr/{exit}' "$dir/t.s" | grep -c 'lac __fp')"
    if [ "$fpCountOdd" -eq 0 ] || [ "$fpCountEven" -eq 0 ]; then
        echo "FAIL test_mutual_recursion_is_not_marked: expected the ordinary frame-based scheme" \
             "in both isOdd()/isEven(), found $fpCountOdd/$fpCountEven occurrence(s) of 'lac __fp'"
        return 1
    fi
    echo "PASS test_mutual_recursion_is_not_marked ($fpCountOdd/$fpCountEven occurrences of 'lac __fp', no diagnostic)"
    return 0
}

test_function_whose_address_is_taken_is_not_marked() {
    local dir="$tmpdir/addrtaken"
    mkdir -p "$dir"
    # `callback()` calls `helper()` so it would need the leaf path
    # granted *for* it (either explicitly or by auto-detection) to use
    # flat storage at all - a callee-free function already qualifies
    # regardless of '@nonrecursive', which would never distinguish
    # "marked" from "not marked" here.
    cat >"$dir/t.c" <<'EOF'
int helper(int x) {
    return x + 1;
}
int callback(int x) {
    return helper(x);
}
int g_cb[1] = {callback};
int main() {
    return g_cb[0];
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --verbose --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_function_whose_address_is_taken_is_not_marked: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if grep -q "auto-nonrecursive: fn callback" "$dir/log"; then
        echo "FAIL test_function_whose_address_is_taken_is_not_marked: autoMarkNonRecursive() marked" \
             "a function whose own address is taken:"
        cat "$dir/log"
        return 1
    fi
    local fpCount
    fpCount="$(awk '/^FN_callback:/{f=1} f{print} /jmp i __retaddr/{exit}' "$dir/t.s" | grep -c 'lac __fp')"
    if [ "$fpCount" -eq 0 ]; then
        echo "FAIL test_function_whose_address_is_taken_is_not_marked: expected the ordinary" \
             "frame-based scheme in callback(), found none"
        return 1
    fi
    echo "PASS test_function_whose_address_is_taken_is_not_marked ($fpCount occurrence(s) of 'lac __fp', no diagnostic)"
    return 0
}

test_explicit_annotation_is_never_overridden() {
    local dir="$tmpdir/explicit"
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
    if ! "$RCC" --target=pdp7 --format=asm --optimize --verbose --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_explicit_annotation_is_never_overridden: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if grep -q "auto-nonrecursive: fn caller" "$dir/log"; then
        echo "FAIL test_explicit_annotation_is_never_overridden: pass re-diagnosed an already" \
             "explicitly annotated function:"
        cat "$dir/log"
        return 1
    fi
    echo "PASS test_explicit_annotation_is_never_overridden (no redundant diagnostic for caller())"
    return 0
}

test_function_with_raw_asm_block_is_not_marked() {
    local dir="$tmpdir/rawasm"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int helper(int x) {
    return x + 1;
}
int capturesFp(int a) {
    int stash;
    stash = helper(a);
    asm {
        lac __fp
        dac __stmp
    }
    return stash;
}
int main() {
    return capturesFp(3);
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --verbose --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_function_with_raw_asm_block_is_not_marked: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if grep -q "auto-nonrecursive: fn capturesFp" "$dir/log"; then
        echo "FAIL test_function_with_raw_asm_block_is_not_marked: autoMarkNonRecursive() marked a" \
             "function containing a raw asm{} block:"
        cat "$dir/log"
        return 1
    fi
    local fpCount
    fpCount="$(awk '/^FN_capturesFp:/{f=1} f{print} /jmp i __retaddr/{exit}' "$dir/t.s" | grep -c 'lac __fp')"
    if [ "$fpCount" -eq 0 ]; then
        echo "FAIL test_function_with_raw_asm_block_is_not_marked: expected the ordinary" \
             "frame-based scheme in capturesFp(), found none"
        return 1
    fi
    echo "PASS test_function_with_raw_asm_block_is_not_marked ($fpCount occurrence(s) of 'lac __fp', no diagnostic)"
    return 0
}

test_provably_nonrecursive_function_gets_marked_automatically || fail=1
test_mutual_recursion_is_not_marked || fail=1
test_function_whose_address_is_taken_is_not_marked || fail=1
test_explicit_annotation_is_never_overridden || fail=1
test_function_with_raw_asm_block_is_not_marked || fail=1

exit $fail
