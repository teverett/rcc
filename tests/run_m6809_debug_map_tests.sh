#!/usr/bin/env bash
# Unit tests for --debug-map on m6809 (target names "6809"/"6809-vectrex",
# both the same codegen/assembler) - a backend missed in every earlier round
# of the --debug-map rollout until a later audit caught it (see
# docs/debug-map.md and this project's own symbol-line-map-design memory).
# Same 5-check pattern as tests/run_pdp1_debug_map_tests.sh /
# tests/run_saturn_debug_map_tests.sh.
#
# Usage: tests/run_m6809_debug_map_tests.sh [path/to/rcc]

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
printf '%s' "$FIXTURE" >"$tmpdir/t.c"
mkdir -p "$tmpdir/build"

test_debug_map_written_with_correct_records() {
    if ! (cd "$tmpdir" && "$RCC" --target=6809 --format=raw --debug-map --out="$tmpdir/t.bin" "$tmpdir/t.c") >"$tmpdir/log" 2>&1; then
        echo "FAIL test_debug_map_written_with_correct_records: compile error"
        cat "$tmpdir/log"
        return 1
    fi
    local map="$tmpdir/build/t.dbgmap"
    if [ ! -f "$map" ]; then
        echo "FAIL test_debug_map_written_with_correct_records: $map was not written"
        return 1
    fi
    local ok=1
    if ! grep -q "^FILE 1 $tmpdir/t.c\$" "$map"; then
        echo "FAIL test_debug_map_written_with_correct_records: expected a FILE record"
        ok=0
    fi
    for name in helper main g_counter; do
        if ! grep -qE "^SYM [0-9]+ $name (FUNC|DATA)\$" "$map"; then
            echo "FAIL test_debug_map_written_with_correct_records: no SYM record for $name"
            ok=0
        fi
    done
    if ! grep -qE "^LINE [0-9]+ 1 [0-9]+\$" "$map"; then
        echo "FAIL test_debug_map_written_with_correct_records: expected at least one LINE record"
        ok=0
    fi
    local bad
    bad="$(awk -v base=12288 -v top=$((12288 + 65536)) '($1=="SYM" || $1=="LINE") { addr=$2; if (addr<base || addr>=top) print }' "$map")"
    if [ -n "$bad" ]; then
        echo "FAIL test_debug_map_written_with_correct_records: record(s) outside the sane address range:"
        echo "$bad"
        ok=0
    fi
    [ "$ok" -eq 1 ] && echo "PASS test_debug_map_written_with_correct_records"
    [ "$ok" -eq 1 ]
}

test_no_debug_map_without_flag() {
    rm -f "$tmpdir/build/t.dbgmap"
    if ! (cd "$tmpdir" && "$RCC" --target=6809 --format=raw --out="$tmpdir/nodbg.bin" "$tmpdir/t.c") >"$tmpdir/log2" 2>&1; then
        echo "FAIL test_no_debug_map_without_flag: compile error"
        cat "$tmpdir/log2"
        return 1
    fi
    if [ -f "$tmpdir/build/t.dbgmap" ]; then
        echo "FAIL test_no_debug_map_without_flag: build/t.dbgmap was written despite no --debug-map"
        return 1
    fi
    if ! (cd "$tmpdir" && "$RCC" --target=6809 --format=asm --out="$tmpdir/t.s" "$tmpdir/t.c") >"$tmpdir/log3" 2>&1; then
        echo "FAIL test_no_debug_map_without_flag: compile error (--format=asm)"
        cat "$tmpdir/log3"
        return 1
    fi
    if grep -q "__line" "$tmpdir/t.s"; then
        echo "FAIL test_no_debug_map_without_flag: found a stray '__line' marker without the flag"
        return 1
    fi
    echo "PASS test_no_debug_map_without_flag"
}

test_debug_map_identical_binary_output() {
    if ! cmp -s "$tmpdir/nodbg.bin" "$tmpdir/t.bin"; then
        echo "FAIL test_debug_map_identical_binary_output: --debug-map changed the compiled --format=raw output"
        return 1
    fi
    echo "PASS test_debug_map_identical_binary_output"
}

test_no_debug_map_for_coff_format() {
    rm -f "$tmpdir/build/t.dbgmap"
    if ! (cd "$tmpdir" && "$RCC" --target=6809 --format=coff --debug-map --out="$tmpdir/t.o" "$tmpdir/t.c") >"$tmpdir/log4" 2>&1; then
        echo "FAIL test_no_debug_map_for_coff_format: compile error"
        cat "$tmpdir/log4"
        return 1
    fi
    if [ -f "$tmpdir/build/t.dbgmap" ]; then
        echo "FAIL test_no_debug_map_for_coff_format: build/t.dbgmap was written for --format=coff"
        return 1
    fi
    echo "PASS test_no_debug_map_for_coff_format"
}

test_internal_cells_not_listed_as_symbols() {
    local map="$tmpdir/build/t.dbgmap"
    if ! (cd "$tmpdir" && "$RCC" --target=6809 --format=raw --debug-map --out="$tmpdir/t2.bin" "$tmpdir/t.c") >"$tmpdir/log5" 2>&1; then
        echo "FAIL test_internal_cells_not_listed_as_symbols: compile error"
        cat "$tmpdir/log5"
        return 1
    fi
    if grep -qE "^SYM [0-9]+ helper_" "$map"; then
        echo "FAIL test_internal_cells_not_listed_as_symbols: an internal per-local cell leaked into SYM records:"
        grep -E "^SYM [0-9]+ helper_" "$map"
        return 1
    fi
    local funcSymCount
    funcSymCount="$(grep -c "^SYM [0-9]* helper FUNC\$" "$map")"
    if [ "$funcSymCount" -ne 1 ]; then
        echo "FAIL test_internal_cells_not_listed_as_symbols: expected exactly 1 SYM for helper, found $funcSymCount"
        return 1
    fi
    echo "PASS test_internal_cells_not_listed_as_symbols"
}

test_debug_map_written_with_correct_records || fail=1
test_no_debug_map_without_flag || fail=1
test_debug_map_identical_binary_output || fail=1
test_no_debug_map_for_coff_format || fail=1
test_internal_cells_not_listed_as_symbols || fail=1

exit $fail
