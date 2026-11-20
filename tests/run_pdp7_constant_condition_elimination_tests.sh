#!/usr/bin/env bash
# Unit tests for
# src/optimizers/constant_condition_elimination/ConstantConditionElimination.cpp:
# an `if`/`while` whose condition is a compile-time constant should have its
# dead branch dropped before codegen ever sees it - proven here by planting
# a distinctive sentinel literal in the dead branch and checking it's
# present in the un-optimized assembly (so the fixture itself is sound) but
# gone once `--optimize` runs this pass.
#
# Three tests:
#   test_if_constant_true_drops_dead_else - `if (1) {...} else { sentinel }`.
#   test_while_constant_false_drops_dead_body - `while (0) { sentinel }`.
#   test_switch_inside_dead_branch_also_removed - the dead branch is a whole
#       `switch`, so its own desugared Label/Goto pairs (see IR.h's
#       StmtKind comment) must be proven unreachable through the CFG before
#       the branch - sentinel included - can be dropped.
#
# Usage: tests/run_pdp7_constant_condition_elimination_tests.sh [path/to/rcc]

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

# $1 = test name, $2 = source file, $3 = sentinel to search for
check_sentinel_dropped_only_when_optimized() {
    local name="$1" src="$2" sentinel="$3"
    local dir="$tmpdir/$name"
    mkdir -p "$dir"
    cp "$src" "$dir/t.c"

    if ! "$RCC" --target=pdp7 --format=asm --out="$dir/plain.s" "$dir/t.c" >"$dir/plain.log" 2>&1; then
        echo "FAIL $name: un-optimized fixture failed to compile"
        cat "$dir/plain.log"
        return 1
    fi
    if ! grep -q "$sentinel" "$dir/plain.s"; then
        echo "FAIL $name: sentinel '$sentinel' missing even without --optimize - fixture isn't exercising" \
             "the dead branch"
        return 1
    fi

    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/opt.s" "$dir/t.c" >"$dir/opt.log" 2>&1; then
        echo "FAIL $name: --optimize fixture failed to compile"
        cat "$dir/opt.log"
        return 1
    fi
    if grep -q "$sentinel" "$dir/opt.s"; then
        echo "FAIL $name: sentinel '$sentinel' still present under --optimize - dead branch wasn't removed"
        return 1
    fi

    echo "PASS $name (sentinel present unoptimized, dropped under --optimize)"
    return 0
}

test_if_constant_true_drops_dead_else() {
    local src="$tmpdir/if_true.c"
    cat >"$src" <<'EOF'
int f(int x) {
    int r;
    if (1) {
        r = x + 1;
    } else {
        r = 31337;
    }
    return r;
}
int main() {
    return f(41);
}
EOF
    check_sentinel_dropped_only_when_optimized "test_if_constant_true_drops_dead_else" "$src" "31337"
}

test_while_constant_false_drops_dead_body() {
    local src="$tmpdir/while_false.c"
    cat >"$src" <<'EOF'
int g() {
    int sum;
    sum = 0;
    while (0) {
        sum = sum + 31338;
    }
    return sum;
}
int main() {
    return g();
}
EOF
    check_sentinel_dropped_only_when_optimized "test_while_constant_false_drops_dead_body" "$src" "31338"
}

test_switch_inside_dead_branch_also_removed() {
    local src="$tmpdir/switch_in_dead_branch.c"
    cat >"$src" <<'EOF'
int h(int x) {
    int r;
    r = 0;
    if (0) {
        switch (x) {
            case 0: r = 31339; break;
            case 1: r = 2; break;
            default: r = 3; break;
        }
    }
    return r;
}
int main() {
    return h(0);
}
EOF
    check_sentinel_dropped_only_when_optimized "test_switch_inside_dead_branch_also_removed" "$src" "31339"
}

test_if_constant_true_drops_dead_else || fail=1
test_while_constant_false_drops_dead_body || fail=1
test_switch_inside_dead_branch_also_removed || fail=1

exit $fail
