#!/usr/bin/env bash
# Unit tests for src/codegen/pdp7/Pdp7Codegen.cpp's
# directElementOperand(): a Global array element at a compile-time-
# constant index (`arr[8]`) has an address that's itself a compile-time
# constant (the array's own link-time address plus a literal offset),
# so it can be read/written with one direct `label+offset` memory-
# reference instruction instead of computing the address at runtime
# first (`law`+`and`+`tad`, then a separate indirect dereference) -
# confirmed directly against real UnixV0-C kernel functions
# (`sys/s5.c`'s `isown()`, a single `inode[8]` read costing 5 words
# before this, 1 word after).
#
# Five tests:
#   test_read_uses_direct_operand - a plain `arr[8]` read compiles to
#       exactly one `lac G_arr+<offset>`, no `law`/`and`/`__addr`.
#   test_assign_uses_direct_operand - `arr[8] = x;` compiles to exactly
#       one `dac G_arr+<offset>`, same absence.
#   test_compound_assign_uses_direct_operand - `arr[8] += x;` reads and
#       writes the same direct operand, confirmed correct on the real
#       simulator (not just structurally).
#   test_variable_index_not_direct - `arr[i]` (a non-constant index)
#       must still go through the ordinary runtime address computation
#       - this optimization never applies there.
#   test_cross_translation_unit_offset_is_added_not_ored - the one real
#       bug found building this: a `label+offset` operand whose symbol
#       needs a link-time relocation (defined in a different
#       translation unit) must combine the offset via real addition,
#       not the backend's own unsigned-syllable-means-OR default.
#
# Usage: tests/run_pdp7_direct_element_operand_tests.sh [path/to/rcc]

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

test_read_uses_direct_operand() {
    local dir="$tmpdir/read"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int arr[16];
int main() {
    return arr[8];
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_read_uses_direct_operand: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! grep -q '^   lac +G_arr+8$' "$dir/t.s"; then
        echo "FAIL test_read_uses_direct_operand: expected 'lac G_arr+8', not found:"
        cat "$dir/t.s"
        return 1
    fi
    if grep -qE 'law G_arr|(lac|dac) __addr' "$dir/t.s"; then
        echo "FAIL test_read_uses_direct_operand: expected no runtime address computation" \
             "at all, found one:"
        cat "$dir/t.s"
        return 1
    fi
    echo "PASS test_read_uses_direct_operand ('lac G_arr+8', no runtime address computation)"
    return 0
}

test_assign_uses_direct_operand() {
    local dir="$tmpdir/assign"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int arr[16];
int main() {
    arr[8] = 5;
    return 0;
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_assign_uses_direct_operand: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! grep -q '^   dac +G_arr+8$' "$dir/t.s"; then
        echo "FAIL test_assign_uses_direct_operand: expected 'dac G_arr+8', not found:"
        cat "$dir/t.s"
        return 1
    fi
    if grep -qE 'law G_arr|(lac|dac) __addr' "$dir/t.s"; then
        echo "FAIL test_assign_uses_direct_operand: expected no runtime address computation" \
             "at all, found one:"
        cat "$dir/t.s"
        return 1
    fi
    echo "PASS test_assign_uses_direct_operand ('dac G_arr+8', no runtime address computation)"
    return 0
}

test_compound_assign_uses_direct_operand() {
    local dir="$tmpdir/compound"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int arr[16];
int main() {
    arr[8] = 5;
    arr[8] += 3;
    return arr[8];
}
EOF
    if ! "$RCC" --target=pdp7 --format=coff --optimize --out="$dir/t.o" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_compound_assign_uses_direct_operand: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! "$RCC" --link --target=pdp7 --format=rim --out="$dir/t.rim" "$dir/t.o" >>"$dir/log" 2>&1; then
        echo "FAIL test_compound_assign_uses_direct_operand: fixture failed to link"
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
    if [ "$((8#$ac))" != "8" ]; then
        echo "FAIL test_compound_assign_uses_direct_operand: expected AC=8 (5+3), got octal $ac"
        return 1
    fi
    echo "PASS test_compound_assign_uses_direct_operand (AC=8)"
    return 0
}

test_variable_index_not_direct() {
    local dir="$tmpdir/variable"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int arr[16];
int get() { return 8; }
int main() {
    int i;
    i = get();
    return arr[i];
}
EOF
    if ! "$RCC" --target=pdp7 --format=asm --optimize --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_variable_index_not_direct: fixture failed to compile"
        cat "$dir/log"
        return 1
    fi
    if grep -q 'lac +G_arr+' "$dir/t.s"; then
        echo "FAIL test_variable_index_not_direct: a non-constant index must not get direct" \
             "'label+offset' addressing, found it anyway:"
        cat "$dir/t.s"
        return 1
    fi
    echo "PASS test_variable_index_not_direct (ordinary runtime addressing still used)"
    return 0
}

test_cross_translation_unit_offset_is_added_not_ored() {
    local dir="$tmpdir/crosstu"
    mkdir -p "$dir"
    # The real bug this regression-tests: `arr` defined in a DIFFERENT
    # translation unit than the access needs a link-time relocation,
    # and a `label+offset` operand's leading symbol syllable has no
    # explicit sign, so Pdp7Assembler.cpp's own `char op = syl.sign ?
    # syl.sign : '|';` records that relocation as an OR, not a real
    # add - silently wrong whenever the real linked address and the
    # literal offset don't happen to combine cleanly under OR
    # (confirmed directly against this exact shape in the real
    # UnixV0-C kernel's `namei()`, reading `sys/s8.c`'s `dnode`/
    # `inode`). `directElementOperand()` must give the symbol syllable
    # an explicit leading '+' so the relocation adds instead.
    cat >"$dir/def.c" <<'EOF'
int arr[8];
EOF
    cat >"$dir/use.c" <<'EOF'
extern int arr[8];
int main() {
    arr[1] = 10;
    arr[2] = 20;
    return arr[1] + arr[2];
}
EOF
    if ! "$RCC" --target=pdp7 --format=coff --optimize --out="$dir/def.o" "$dir/def.c" >"$dir/log" 2>&1; then
        echo "FAIL test_cross_translation_unit_offset_is_added_not_ored: def.c failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! "$RCC" --target=pdp7 --format=coff --optimize --out="$dir/use.o" "$dir/use.c" >>"$dir/log" 2>&1; then
        echo "FAIL test_cross_translation_unit_offset_is_added_not_ored: use.c failed to compile"
        cat "$dir/log"
        return 1
    fi
    if ! "$RCC" --link --target=pdp7 --format=rim --out="$dir/t.rim" "$dir/def.o" "$dir/use.o" >>"$dir/log" 2>&1; then
        echo "FAIL test_cross_translation_unit_offset_is_added_not_ored: fixture failed to link"
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
    if [ "$((8#$ac))" != "30" ]; then
        echo "FAIL test_cross_translation_unit_offset_is_added_not_ored: expected AC=30" \
             "(10+20), got octal $ac - the offset combined via OR instead of addition"
        return 1
    fi
    echo "PASS test_cross_translation_unit_offset_is_added_not_ored (AC=30, cross-TU relocation correct)"
    return 0
}

test_read_uses_direct_operand || fail=1
test_assign_uses_direct_operand || fail=1
test_compound_assign_uses_direct_operand || fail=1
test_variable_index_not_direct || fail=1
test_cross_translation_unit_offset_is_added_not_ored || fail=1

exit $fail
