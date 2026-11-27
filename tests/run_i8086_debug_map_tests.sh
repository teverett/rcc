#!/usr/bin/env bash
# Unit tests for --debug-map's port to i8086 (target "8086"; see
# docs/debug-map.md's own "Implementation (the other 23: ...)" section
# and this project's own symbol-line-map-design memory for the shared
# design this follows). i8086 already had --dump-asm's own perFileAsm_
# fixed (its own codeRanges/trailerRanges, unrelated to this port); this
# only adds the debug-map pseudo-op/side-table/debugSymbols_ plumbing on
# top. debugMap() uses a denylist ("coff" is the one real relocatable
# object here - see I8086Codegen::assembleInProcess()'s own comment on
# why "raw"/"elf" are both complete, final-address programs instead).
#
# Five tests, the same pattern tests/run_vex_debug_map_tests.sh uses:
#   test_debug_map_written_with_correct_records
#   test_no_debug_map_without_flag
#   test_debug_map_identical_binary_output
#   test_no_debug_map_for_coff_format
#   test_internal_cells_not_listed_as_symbols
#
# Usage: tests/run_i8086_debug_map_tests.sh [path/to/rcc]

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RCC="$SCRIPT_DIR/../build/bin/rcc"
[ $# -ge 1 ] && RCC="$1"

if [ ! -x "$RCC" ]; then
    echo "rcc binary not found at $RCC - build it first ('make')" >&2
    exit 1
fi
RCC="$(cd "$(dirname "$RCC")" && pwd)/$(basename "$RCC")"

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
fail=0

# kI8086BaseAddress (I8086Assembler.h).
BASE=4096

FIXTURE='int helper(int x) {
    int y;
    y = x + 1;
    return y;
}
int g_counter;
int main() {
    int i;
    int count;
    count = 0;
    for (i = 0; i < 5; i = i + 1) {
        count = helper(count);
    }
    g_counter = count;
    return count;
}
'

test_debug_map_written_with_correct_records() {
    local dir="$tmpdir/records"
    mkdir -p "$dir/build"
    printf '%s' "$FIXTURE" >"$dir/t.c"
    if ! (cd "$dir" && "$RCC" --target=8086 --format=raw --debug-map --out="$dir/t.bin" "$dir/t.c") >"$dir/log" 2>&1; then
        echo "FAIL test_debug_map_written_with_correct_records: compile error"
        cat "$dir/log"
        return 1
    fi
    local map="$dir/build/t.dbgmap"
    if [ ! -f "$map" ]; then
        echo "FAIL test_debug_map_written_with_correct_records: $map was not written"
        return 1
    fi
    local ok=1
    if ! grep -q "^FILE 1 $dir/t.c\$" "$map"; then
        echo "FAIL test_debug_map_written_with_correct_records: expected a FILE record, got:"
        cat "$map"
        ok=0
    fi
    for name in helper main g_counter; do
        if ! grep -qE "^SYM [0-9]+ $name (FUNC|DATA)\$" "$map"; then
            echo "FAIL test_debug_map_written_with_correct_records: no SYM record for $name, got:"
            cat "$map"
            ok=0
        fi
    done
    if ! grep -qE "^LINE [0-9]+ 1 [0-9]+\$" "$map"; then
        echo "FAIL test_debug_map_written_with_correct_records: expected at least one LINE record, got:"
        cat "$map"
        ok=0
    fi
    local size
    size="$(wc -c <"$dir/t.bin" | tr -d ' ')"
    local bad
    bad="$(awk -v base="$BASE" -v top="$((BASE + size))" \
        '($1=="SYM" || $1=="LINE") { addr=$2; if (addr<base || addr>=top) print }' "$map")"
    if [ -n "$bad" ]; then
        echo "FAIL test_debug_map_written_with_correct_records: record(s) outside [$BASE,$((BASE+size))):"
        echo "$bad"
        ok=0
    fi
    [ "$ok" -eq 1 ] && echo "PASS test_debug_map_written_with_correct_records"
    [ "$ok" -eq 1 ]
}

test_no_debug_map_without_flag() {
    local dir="$tmpdir/noflag"
    mkdir -p "$dir/build"
    printf '%s' "$FIXTURE" >"$dir/t.c"
    if ! (cd "$dir" && "$RCC" --target=8086 --format=raw --out="$dir/t.bin" "$dir/t.c") >"$dir/log" 2>&1; then
        echo "FAIL test_no_debug_map_without_flag: compile error"
        cat "$dir/log"
        return 1
    fi
    if [ -f "$dir/build/t.dbgmap" ]; then
        echo "FAIL test_no_debug_map_without_flag: build/t.dbgmap was written despite no --debug-map"
        return 1
    fi
    if ! (cd "$dir" && "$RCC" --target=8086 --format=asm --out="$dir/t.s" "$dir/t.c") >"$dir/log2" 2>&1; then
        echo "FAIL test_no_debug_map_without_flag: compile error (--format=asm)"
        cat "$dir/log2"
        return 1
    fi
    if grep -q "__line" "$dir/t.s"; then
        echo "FAIL test_no_debug_map_without_flag: found a stray '__line' pseudo-op in --format=asm output"
        return 1
    fi
    echo "PASS test_no_debug_map_without_flag"
    return 0
}

test_debug_map_identical_binary_output() {
    local dir="$tmpdir/identical"
    mkdir -p "$dir/build"
    printf '%s' "$FIXTURE" >"$dir/t.c"
    if ! (cd "$dir" && "$RCC" --target=8086 --format=raw --debug-map --out="$dir/with.bin" "$dir/t.c") >"$dir/log1" 2>&1; then
        echo "FAIL test_debug_map_identical_binary_output: compile error (--debug-map)"
        cat "$dir/log1"
        return 1
    fi
    if ! (cd "$dir" && "$RCC" --target=8086 --format=raw --out="$dir/without.bin" "$dir/t.c") >"$dir/log2" 2>&1; then
        echo "FAIL test_debug_map_identical_binary_output: compile error (no --debug-map)"
        cat "$dir/log2"
        return 1
    fi
    if ! cmp -s "$dir/with.bin" "$dir/without.bin"; then
        echo "FAIL test_debug_map_identical_binary_output: --debug-map changed the compiled --format=raw output"
        return 1
    fi
    echo "PASS test_debug_map_identical_binary_output"
    return 0
}

test_no_debug_map_for_coff_format() {
    local dir="$tmpdir/coff"
    mkdir -p "$dir/build"
    printf '%s' "$FIXTURE" >"$dir/t.c"
    if ! (cd "$dir" && "$RCC" --target=8086 --format=coff --debug-map --out="$dir/t.o" "$dir/t.c") >"$dir/log" 2>&1; then
        echo "FAIL test_no_debug_map_for_coff_format: compile error"
        cat "$dir/log"
        return 1
    fi
    if [ -f "$dir/build/t.dbgmap" ]; then
        echo "FAIL test_no_debug_map_for_coff_format: build/t.dbgmap was written for --format=coff"
        return 1
    fi
    echo "PASS test_no_debug_map_for_coff_format"
    return 0
}

test_internal_cells_not_listed_as_symbols() {
    local dir="$tmpdir/internal"
    mkdir -p "$dir/build"
    cat >"$dir/t.c" <<'EOF'
int helper(int a, int b, int c, int d) {
    int x;
    int y;
    x = a + b;
    y = c + d;
    return x + y;
}
int main() {
    return helper(1, 2, 3, 4);
}
EOF
    if ! (cd "$dir" && "$RCC" --target=8086 --format=raw --debug-map --out="$dir/t.bin" "$dir/t.c") >"$dir/log" 2>&1; then
        echo "FAIL test_internal_cells_not_listed_as_symbols: compile error"
        cat "$dir/log"
        return 1
    fi
    local map="$dir/build/t.dbgmap"
    local count
    count="$(grep -c "^SYM [0-9]* helper FUNC\$" "$map")"
    if [ "$count" -ne 1 ]; then
        echo "FAIL test_internal_cells_not_listed_as_symbols: expected exactly 1 SYM record for helper, found $count:"
        cat "$map"
        return 1
    fi
    if grep -qE "^SYM [0-9]+ helper_" "$map"; then
        echo "FAIL test_internal_cells_not_listed_as_symbols: an internal cell leaked into the SYM records:"
        grep -E "^SYM [0-9]+ helper_" "$map"
        return 1
    fi
    echo "PASS test_internal_cells_not_listed_as_symbols"
    return 0
}

test_debug_map_written_with_correct_records || fail=1
test_no_debug_map_without_flag || fail=1
test_debug_map_identical_binary_output || fail=1
test_no_debug_map_for_coff_format || fail=1
test_internal_cells_not_listed_as_symbols || fail=1

exit $fail
