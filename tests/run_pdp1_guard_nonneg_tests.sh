#!/usr/bin/env bash
# Unit tests for Pdp1Codegen.cpp's emitLt() sign-dispatch skip applied to
# src/optimizers/guard_nonneg/GuardNonNegative.cpp's own idiom: a dominating
# `if (x >= 0) { ...uses of x... }` guard. Same port as
# run_pdp1_induction_nonneg_tests.sh, for the other of the two passes that
# set `ir::Slot::knownNonNegative` (see IR.h) - this one proves a slot
# non-negative across an early-narrowing guard rather than a loop's own
# increment/decrement.
#
# The guard's own `x >= 0` check is itself a comparison against the
# compile-time literal 0 - the one place this optimization could in
# principle be unsound if it ever let a comparison skip ALL live
# verification of its own operand (trusting a wrong proof with nothing left
# to catch it), the exact risk Pdp7Codegen.cpp's own emitSignedLtCells()
# documents and defends against for its "bIsZero" fast path. This target's
# emitLt() has no such dedicated zero-literal fast path - it always falls
# through to the same subtract-and-test-sign step (`Lsub`) regardless of
# which operand's dispatch was skipped, and since the literal 0 operand's
# own live test (always non-negative) always selects that fallthrough, the
# fallthrough's own subtract against 0 ends up being a genuine, un-skipped
# live test of the slot's real sign - confirmed directly against the real
# simulator below with an actually-negative `x`, not just by code
# inspection.
#
# Four tests (mirroring run_pdp7_guard_nonneg_tests.sh's own four):
#   test_guarded_use_skips_inner_sign_check - the plain idiom, `x` used only
#       inside the guard - expects the 'sma' count to drop (8 to 4) and the
#       pass's own diagnostic; also runs on the real simulator with both a
#       negative and a non-negative `x` to confirm the guard's own `>= 0`
#       check still decides correctly either way.
#   test_use_outside_guard_keeps_full_dispatch - `x` is also read after the
#       guarded `if`, where it isn't proven non-negative - expects no
#       diagnostic and the un-optimized 'sma' count.
#   test_address_taken_keeps_full_dispatch - `&x` is taken (passed to
#       another function) after the guard - expects the same decline, since
#       the callee could leave `x` negative.
#   test_else_branch_read_keeps_full_dispatch - `x` is read in the guard's
#       own `else` branch, reachable only when `x < 0` - expects the same
#       decline.
#
# Usage: tests/run_pdp1_guard_nonneg_tests.sh [path/to/rcc]

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

run_with_timeout() {
    local secs="$1"
    shift
    "$@" &
    local pid=$!
    (sleep "$secs" && kill -9 "$pid" 2>/dev/null) &
    local watcher=$!
    wait "$pid" 2>/dev/null
    local status=$?
    kill "$watcher" 2>/dev/null
    wait "$watcher" 2>/dev/null
    return $status
}

to_signed() {
    local value="$1" bits="$2"
    local max=$((1 << (bits - 1)))
    local mod=$((1 << bits))
    if [ "$value" -ge "$max" ]; then
        echo $((value - mod))
    else
        echo "$value"
    fi
}

# Runs `src` on the real PDP-1 simulator (open-simh) and checks AC against
# `expect`, the same way tests/run_tests.sh's own check_pdp1() does.
run_on_simulator() {
    local src="$1" name="$2" expect="$3"
    if ! command -v pdp1 >/dev/null 2>&1; then
        echo "SKIP $name (real-hardware check): pdp1 (open-simh) not found on PATH"
        return 0
    fi
    local rimfile="$tmpdir/$name.rim"
    if ! "$RCC" --target=pdp1 --format=rim --optimize --out="$rimfile" "$src" >"$tmpdir/$name.log" 2>&1; then
        echo "FAIL $name (real-hardware check): compile error"
        cat "$tmpdir/$name.log"
        return 1
    fi
    local simfile="$tmpdir/$name.simh" outfile="$tmpdir/$name.out"
    cat >"$simfile" <<SIMH
attach ptr $rimfile
boot ptr
e ac
SIMH
    run_with_timeout 10 bash -c "pdp1 '$simfile' </dev/null >'$outfile' 2>/dev/null"
    local ac_octal
    ac_octal="$(grep '^AC:' "$outfile" | awk '{print $2}')"
    if [ -z "$ac_octal" ]; then
        echo "FAIL $name (real-hardware check): simulator produced no AC (timed out or crashed)"
        return 1
    fi
    local ac_dec=$((8#$ac_octal))
    ac_dec="$(to_signed "$ac_dec" 18)"
    if [ "$ac_dec" -ne "$expect" ]; then
        echo "FAIL $name (real-hardware check): expected $expect, got $ac_dec (AC octal: $ac_octal)"
        return 1
    fi
    echo "PASS $name (real-hardware check, AC=$ac_dec)"
    return 0
}

test_guarded_use_skips_inner_sign_check() {
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
    if ! "$RCC" --target=pdp1 --format=asm --optimize --verbose --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_guarded_use_skips_inner_sign_check: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! grep -q "guard-nonneg: fn main: proved \`x\` stays non-negative" "$dir/log"; then
        echo "FAIL test_guarded_use_skips_inner_sign_check: expected the pass's own diagnostic, got:"
        cat "$dir/log"
        return 1
    fi
    local smaCount
    smaCount="$(grep -c 'sma' "$dir/t.s")"
    if [ "$smaCount" -ne 4 ]; then
        echo "FAIL test_guarded_use_skips_inner_sign_check: expected exactly 4 'sma' (down from the" \
             "unoptimized 8 - the inner \`x < 10\` comparison's own dispatch on \`x\` should be gone)," \
             "found $smaCount"
        return 1
    fi
    echo "PASS test_guarded_use_skips_inner_sign_check (4 occurrences of 'sma', diagnostic present)"
    # x=5: guard true, inner (x<10) true -> count=1.
    run_on_simulator "$dir/t.c" "test_guarded_use_skips_inner_sign_check_pos" 1 || return 1
    return 0
}

test_guarded_use_negative_x_still_falls_through_correctly() {
    local dir="$tmpdir/neg"
    mkdir -p "$dir"
    # Same shape, but `x` is actually negative at runtime - the guard's own
    # `x >= 0` check is the one comparison in this program that is itself
    # against the compile-time literal 0 (see this file's own header
    # comment on why that needs checking directly against the simulator).
    cat >"$dir/t.c" <<'EOF'
int get() { return -3; }
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
    run_on_simulator "$dir/t.c" "test_guarded_use_negative_x_still_falls_through_correctly" 0 || return 1
    return 0
}

test_use_outside_guard_keeps_full_dispatch() {
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
    count = count + x;
    return count;
}
EOF
    if ! "$RCC" --target=pdp1 --format=asm --optimize --verbose --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_use_outside_guard_keeps_full_dispatch: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if grep -q "guard-nonneg" "$dir/log"; then
        echo "FAIL test_use_outside_guard_keeps_full_dispatch: pass fired despite \`x\` being read" \
             "after the guarded \`if\`:"
        cat "$dir/log"
        return 1
    fi
    local smaCount
    smaCount="$(grep -c 'sma' "$dir/t.s")"
    if [ "$smaCount" -ne 8 ]; then
        echo "FAIL test_use_outside_guard_keeps_full_dispatch: expected the original 8 'sma'" \
             "occurrences (unsafe to drop \`x\`'s own dispatch), found $smaCount"
        return 1
    fi
    echo "PASS test_use_outside_guard_keeps_full_dispatch (8 occurrences of 'sma', no diagnostic)"
    return 0
}

test_address_taken_keeps_full_dispatch() {
    local dir="$tmpdir/addrtaken"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
void setNegative(int *p) {
    *p = -1;
}
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
    setNegative(&x);
    return count;
}
EOF
    if ! "$RCC" --target=pdp1 --format=asm --optimize --verbose --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_address_taken_keeps_full_dispatch: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if grep -q "guard-nonneg" "$dir/log"; then
        echo "FAIL test_address_taken_keeps_full_dispatch: pass fired despite \`&x\` being taken:"
        cat "$dir/log"
        return 1
    fi
    local smaCount
    smaCount="$(grep -c 'sma' "$dir/t.s")"
    if [ "$smaCount" -ne 8 ]; then
        echo "FAIL test_address_taken_keeps_full_dispatch: expected the original 8 'sma'" \
             "occurrences (unsafe to drop \`x\`'s own dispatch), found $smaCount"
        return 1
    fi
    echo "PASS test_address_taken_keeps_full_dispatch (8 occurrences of 'sma', no diagnostic)"
    return 0
}

test_else_branch_read_keeps_full_dispatch() {
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
    if ! "$RCC" --target=pdp1 --format=asm --optimize --verbose --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_else_branch_read_keeps_full_dispatch: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if grep -q "guard-nonneg" "$dir/log"; then
        echo "FAIL test_else_branch_read_keeps_full_dispatch: pass fired despite \`x\` being read in" \
             "the guard's own else branch:"
        cat "$dir/log"
        return 1
    fi
    local smaCount
    smaCount="$(grep -c 'sma' "$dir/t.s")"
    if [ "$smaCount" -ne 8 ]; then
        echo "FAIL test_else_branch_read_keeps_full_dispatch: expected the original 8 'sma'" \
             "occurrences (unsafe to drop \`x\`'s own dispatch), found $smaCount"
        return 1
    fi
    echo "PASS test_else_branch_read_keeps_full_dispatch (8 occurrences of 'sma', no diagnostic)"
    return 0
}

test_guarded_use_skips_inner_sign_check || fail=1
test_guarded_use_negative_x_still_falls_through_correctly || fail=1
test_use_outside_guard_keeps_full_dispatch || fail=1
test_address_taken_keeps_full_dispatch || fail=1
test_else_branch_read_keeps_full_dispatch || fail=1

exit $fail
