#!/usr/bin/env bash
# Unit tests for src/optimizers/compound_assign_recognition/
# CompoundAssignRecognition.cpp: folds a hand-desugared compound
# assignment - `t = t <op> e`, `t[i] = t[i] <op> e`, or `*p = *p <op>
# e` - into the real `CompoundAssign`/`CompoundDerefAssign` IR node,
# which computes the target's own address/pointer once instead of the
# desugared form's two independent computations.
#
# Seven tests:
#   test_array_element_self_xor_is_folded - `a[i] = a[i] ^ x` compiles
#       identically (same word count, byte-identical assembly) to
#       hand-writing `a[i] ^= x`.
#   test_pointer_dereference_self_add_is_folded - `*p = *p + x`
#       likewise matches hand-written `*p += x`.
#   test_different_indices_are_not_folded - `a[i] = a[j] + x` (i != j)
#       is left alone: folding it would merge two genuinely different
#       elements into one.
#   test_noncommutative_wrong_side_is_not_folded - `x = y - x` is left
#       alone: unlike addition, subtraction's operand order matters,
#       and the self-reference is on the wrong (right) side here.
#   test_side_effecting_kept_operand_is_not_folded - `x = f() + x`
#       (`f()` a real call) is left alone even though `x` is the
#       matched, foldable operand: a real implementation only picks
#       one evaluation order for each shape it emits, and changing
#       which one a side-effecting operand gets evaluated relative to
#       could be an observable behavior change.
#   test_commutative_self_on_right_is_folded - `x = y + x` (`y` a
#       pure expression) IS folded into `x += y`, since addition's
#       operand order never changes the result.
#   test_wide_target_is_not_folded - `t = t + 1` on a `long t` is left
#       alone: a real, caught-for-real regression - this exact shape
#       was a pdp7 compile error once this pass started firing on it
#       (`long`/`long long` CompoundAssign codegen isn't equivalent to
#       the desugared form on every backend, and "wide" is relative to
#       each backend's own native width, which this backend-
#       independent pass has no way to check).
#
# Usage: tests/run_pdp7_compound_assign_recognition_tests.sh [path/to/rcc]

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

test_array_element_self_xor_is_folded() {
    local dir="$tmpdir/arrxor"
    mkdir -p "$dir"
    cat >"$dir/desugared.c" <<'EOF'
int a[10];
int flip(int i, int x) {
    a[i] = a[i] ^ x;
    return 0;
}
int main() { return flip(1, 5); }
EOF
    cat >"$dir/direct.c" <<'EOF'
int a[10];
int flip(int i, int x) {
    a[i] ^= x;
    return 0;
}
int main() { return flip(1, 5); }
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --verbose --out="$dir/d.s" "$dir/desugared.c" >"$dir/log" 2>&1; then
        echo "FAIL test_array_element_self_xor_is_folded: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/r.s" "$dir/direct.c" >/dev/null 2>&1; then
        echo "FAIL test_array_element_self_xor_is_folded: control fixture failed to compile"
        return 1
    fi
    if ! grep -q "compound-assign-recognition: fn flip" "$dir/log"; then
        echo "FAIL test_array_element_self_xor_is_folded: expected the pass's own diagnostic, got:"
        cat "$dir/log"
        return 1
    fi
    if ! diff -q "$dir/d.s" "$dir/r.s" >/dev/null; then
        echo "FAIL test_array_element_self_xor_is_folded: expected byte-identical output to the" \
             "hand-written 'a[i] ^= x' - diff:"
        diff "$dir/d.s" "$dir/r.s"
        return 1
    fi
    echo "PASS test_array_element_self_xor_is_folded (byte-identical to 'a[i] ^= x', diagnostic present)"
    return 0
}

test_pointer_dereference_self_add_is_folded() {
    local dir="$tmpdir/derefadd"
    mkdir -p "$dir"
    cat >"$dir/desugared.c" <<'EOF'
int flip(int p, int x) {
    *p = *p + x;
    return 0;
}
int main() { return flip(0, 5); }
EOF
    cat >"$dir/direct.c" <<'EOF'
int flip(int p, int x) {
    *p += x;
    return 0;
}
int main() { return flip(0, 5); }
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --verbose --out="$dir/d.s" "$dir/desugared.c" >"$dir/log" 2>&1; then
        echo "FAIL test_pointer_dereference_self_add_is_folded: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/r.s" "$dir/direct.c" >/dev/null 2>&1; then
        echo "FAIL test_pointer_dereference_self_add_is_folded: control fixture failed to compile"
        return 1
    fi
    if ! grep -q "compound-assign-recognition: fn flip" "$dir/log"; then
        echo "FAIL test_pointer_dereference_self_add_is_folded: expected the pass's own diagnostic, got:"
        cat "$dir/log"
        return 1
    fi
    if ! diff -q "$dir/d.s" "$dir/r.s" >/dev/null; then
        echo "FAIL test_pointer_dereference_self_add_is_folded: expected byte-identical output to" \
             "the hand-written '*p += x' - diff:"
        diff "$dir/d.s" "$dir/r.s"
        return 1
    fi
    echo "PASS test_pointer_dereference_self_add_is_folded (byte-identical to '*p += x', diagnostic present)"
    return 0
}

test_different_indices_are_not_folded() {
    local dir="$tmpdir/diffidx"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int a[10];
int f(int i, int j, int y) {
    a[i] = a[j] + y;
    return 0;
}
int main() { return f(1, 2, 3); }
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --verbose --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_different_indices_are_not_folded: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if grep -q "compound-assign-recognition" "$dir/log"; then
        echo "FAIL test_different_indices_are_not_folded: pass folded two different array elements" \
             "into one:"
        cat "$dir/log"
        return 1
    fi
    echo "PASS test_different_indices_are_not_folded (no diagnostic)"
    return 0
}

test_noncommutative_wrong_side_is_not_folded() {
    local dir="$tmpdir/wrongside"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int f(int x, int y) {
    x = y - x;
    return x;
}
int main() { return f(5, 10); }
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --verbose --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_noncommutative_wrong_side_is_not_folded: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if grep -q "compound-assign-recognition" "$dir/log"; then
        echo "FAIL test_noncommutative_wrong_side_is_not_folded: pass folded 'x = y - x' as if it" \
             "were 'x -= y':"
        cat "$dir/log"
        return 1
    fi
    echo "PASS test_noncommutative_wrong_side_is_not_folded (no diagnostic)"
    return 0
}

test_side_effecting_kept_operand_is_not_folded() {
    local dir="$tmpdir/sideeffect"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int g;
int f() {
    g = g + 1;
    return g;
}
int x;
int h() {
    x = f() + x;
    return x;
}
int main() { return h(); }
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --verbose --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_side_effecting_kept_operand_is_not_folded: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if grep -q "compound-assign-recognition: fn h" "$dir/log"; then
        echo "FAIL test_side_effecting_kept_operand_is_not_folded: pass folded 'x = f() + x' despite" \
             "the call it would have to keep:"
        cat "$dir/log"
        return 1
    fi
    echo "PASS test_side_effecting_kept_operand_is_not_folded (no diagnostic for h())"
    return 0
}

test_commutative_self_on_right_is_folded() {
    local dir="$tmpdir/rightside"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int f(int x, int y) {
    x = y + x;
    return x;
}
int main() { return f(5, 10); }
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --verbose --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_commutative_self_on_right_is_folded: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! grep -q "compound-assign-recognition: fn f" "$dir/log"; then
        echo "FAIL test_commutative_self_on_right_is_folded: expected the pass's own diagnostic for" \
             "'x = y + x', got:"
        cat "$dir/log"
        return 1
    fi
    "$RCC" --target=pdp7 --format=rim --optimize --out="$dir/t.rim" "$dir/t.c" >/dev/null 2>&1
    printf 'load %s 10000\ngo 10000\ne ac\nq\n' "$dir/t.rim" >"$dir/t.simh"
    local ac_octal
    ac_octal="$(timeout 15 pdp7 "$dir/t.simh" </dev/null 2>&1 | awk -F'[ \t]+' '/^AC:/{print $2}')"
    if [ "$((8#$ac_octal))" -ne 15 ]; then
        echo "FAIL test_commutative_self_on_right_is_folded: expected AC=15 (5+10), got AC=$((8#$ac_octal))"
        return 1
    fi
    echo "PASS test_commutative_self_on_right_is_folded (diagnostic present, AC=15)"
    return 0
}

test_array_element_self_xor_is_folded || fail=1
test_pointer_dereference_self_add_is_folded || fail=1
test_different_indices_are_not_folded || fail=1
test_noncommutative_wrong_side_is_not_folded || fail=1
test_side_effecting_kept_operand_is_not_folded || fail=1
test_wide_target_is_not_folded() {
    local dir="$tmpdir/wide"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
long t;
int main() {
    int i;
    t = 0;
    i = 0;
    while (i < 10) {
        t = t + 1;
        i = i + 1;
    }
    return (int)t;
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --verbose --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_wide_target_is_not_folded: fixture failed to compile - the exact real" \
             "regression this test guards against:"
        cat "$dir/log"
        return 1
    fi
    # `i = i + 1` (an ordinary, narrow int) legitimately folds too - only
    # check that the wide `t = t + 1`, on line 7, specifically does not.
    if grep -q "compound-assign-recognition.*line 7:" "$dir/log"; then
        echo "FAIL test_wide_target_is_not_folded: pass folded a wide (long) target:"
        cat "$dir/log"
        return 1
    fi
    "$RCC" --target=pdp7 --format=rim --optimize --out="$dir/t.rim" "$dir/t.c" >/dev/null 2>&1
    printf 'load %s 10000\ngo 10000\ne ac\nq\n' "$dir/t.rim" >"$dir/t.simh"
    local ac_octal
    ac_octal="$(timeout 15 pdp7 "$dir/t.simh" </dev/null 2>&1 | awk -F'[ \t]+' '/^AC:/{print $2}')"
    if [ "$((8#$ac_octal))" -ne 10 ]; then
        echo "FAIL test_wide_target_is_not_folded: expected AC=10, got AC=$((8#$ac_octal))"
        return 1
    fi
    echo "PASS test_wide_target_is_not_folded (compiles, no diagnostic, AC=10)"
    return 0
}

test_commutative_self_on_right_is_folded || fail=1
test_wide_target_is_not_folded || fail=1

exit $fail
