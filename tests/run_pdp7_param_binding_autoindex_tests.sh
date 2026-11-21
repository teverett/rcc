#!/usr/bin/env bash
# Unit tests for src/codegen/pdp7/Pdp7Codegen.cpp's emitLeafParamBinding():
# rather than recomputing `__sp+k` from scratch for every param k (`lac
# __sp; tad k; dac __addr; lac __addr i`, 4 words every time), it stages
# `__sp` into the PDP-7's own hardware autoindex register 15 (decimal -
# any *indirect* reference to a word address in [8,15] auto-increments
# it as a CPU side effect) and reads every param through it in order, at
# 2 words each instead of 5.
#
# The one real bug found building this, caught directly against the
# real simulator (not reasoned about): this register increments BEFORE
# the fetch it gates, not after - PDP-8-family lineage docs suggested
# post-increment, but the real hardware/simulator disagreed, so every
# param came out as its own successor's value until the register was
# staged to `__sp - 1` instead of `__sp`.
#
# Three tests:
#   test_three_params_read_in_correct_order - the real caught bug's own
#       regression test: a 3-param function returning a composite value
#       built from each param - would read as a shifted/garbled
#       composite if pre- vs post-increment were ever wrong again.
#   test_single_param_still_correct - the one-param edge case (no room
#       for an off-by-one to hide behind a second param's own result).
#   test_uses_autoindex_register_not_scratch_cell - structural check
#       that the new, cheaper path is actually what's emitted (so a
#       future change can't silently regress back to the old 5-word
#       form without this test noticing).
#
# Usage: tests/run_pdp7_param_binding_autoindex_tests.sh [path/to/rcc]

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RCC="$SCRIPT_DIR/../build/bin/rcc"
[ $# -ge 1 ] && RCC="$1"
PDP7_SIM="${PDP7_SIM:-pdp7}"

if [ ! -x "$RCC" ]; then
    echo "rcc binary not found at $RCC - build it first ('make')" >&2
    exit 1
fi

if ! command -v "$PDP7_SIM" >/dev/null 2>&1; then
    echo "pdp7 (open-simh) not found on PATH - install it (e.g. 'brew install open-simh')" >&2
    exit 1
fi

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
fail=0

run_and_get_ac() {
    local rim="$1"
    "$PDP7_SIM" <<EOF 2>&1 | grep -oE 'AC:[[:space:]]*[0-7]+' | tail -1 | grep -oE '[0-7]+$'
load $rim 10000
go 10000
e ac
EOF
}

test_three_params_read_in_correct_order() {
    local dir="$tmpdir/three"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int probe(int a, int b, int c) {
    return a * 100 + b * 10 + c;
}
int main() {
    return probe(1, 2, 3);
}
EOF
    if ! "$RCC" --target=pdp7 --format=coff --optimize --out="$dir/t.o" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_three_params_read_in_correct_order: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! "$RCC" --link --target=pdp7 --format=rim --out="$dir/t.rim" "$dir/t.o" >>"$dir/log" 2>&1; then
        echo "FAIL test_three_params_read_in_correct_order: fixture failed to link"
        cat "$dir/log"
        return 1
    fi
    local ac
    ac="$(run_and_get_ac "$dir/t.rim")"
    # 1*100 + 2*10 + 3 = 123 decimal = 173 octal
    if [ "$((8#$ac))" != "123" ]; then
        echo "FAIL test_three_params_read_in_correct_order: expected AC=173 (123 decimal)," \
             "got AC=$ac - a param came out as the wrong one's value"
        return 1
    fi
    echo "PASS test_three_params_read_in_correct_order (AC=173, params read in order)"
    return 0
}

test_single_param_still_correct() {
    local dir="$tmpdir/one"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int probe(int a) {
    return a + 1;
}
int main() {
    return probe(41);
}
EOF
    if ! "$RCC" --target=pdp7 --format=coff --optimize --out="$dir/t.o" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_single_param_still_correct: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! "$RCC" --link --target=pdp7 --format=rim --out="$dir/t.rim" "$dir/t.o" >>"$dir/log" 2>&1; then
        echo "FAIL test_single_param_still_correct: fixture failed to link"
        cat "$dir/log"
        return 1
    fi
    local ac
    ac="$(run_and_get_ac "$dir/t.rim")"
    # 42 decimal = 52 octal
    if [ "$((8#$ac))" != "42" ]; then
        echo "FAIL test_single_param_still_correct: expected AC=52 (42 decimal), got AC=$ac"
        return 1
    fi
    echo "PASS test_single_param_still_correct (AC=52)"
    return 0
}

test_uses_autoindex_register_not_scratch_cell() {
    local dir="$tmpdir/struct"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int probe(int a, int b) {
    return a + b;
}
int main() {
    return probe(1, 2);
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_uses_autoindex_register_not_scratch_cell: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! grep -q 'dac 15$' "$dir/t.s"; then
        echo "FAIL test_uses_autoindex_register_not_scratch_cell: expected the cursor to be" \
             "staged into register 15 ('dac 15'), not found:"
        cat "$dir/t.s"
        return 1
    fi
    if ! grep -q '15 i$' "$dir/t.s"; then
        echo "FAIL test_uses_autoindex_register_not_scratch_cell: expected a param read through" \
             "register 15 ('15 i'), not found:"
        cat "$dir/t.s"
        return 1
    fi
    if grep -q '__addr i$' "$dir/t.s"; then
        echo "FAIL test_uses_autoindex_register_not_scratch_cell: found the old 5-word" \
             "'__addr i' scratch-cell form still in use:"
        cat "$dir/t.s"
        return 1
    fi
    echo "PASS test_uses_autoindex_register_not_scratch_cell (register 15 staged and read, no '__addr i')"
    return 0
}

test_three_params_read_in_correct_order || fail=1
test_single_param_still_correct || fail=1
test_uses_autoindex_register_not_scratch_cell || fail=1

exit $fail
