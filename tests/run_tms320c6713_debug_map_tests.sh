#!/usr/bin/env bash
# Unit tests for --debug-map's port to tms320c6713 - see docs/debug-map.md's
# own "Implementation (the other 23: ...)" section for the shared pattern
# this follows (saturn/tms320c6713 were left out of that scripted batch
# for individual treatment - see this project's own symbol-line-map-design
# memory). This target has no relocatable object format at all (see
# Tms320C6713Codegen.h's own header comment), so there's no
# --format=coff-style exclusion check to make here.
#
# Four checks:
#   1. records correct - FILE/SYM/LINE present, every SYM/LINE address
#      inside the compiled --format=raw image's own [base, base+size)
#      range (no --format=list to cross-check against).
#   2. no flag -> no .dbgmap file, no stray "__line" marker in plain
#      --format=asm output.
#   3. byte-identical --format=raw output with/without --debug-map.
#   4. no internal per-param/per-local cell leaks into the SYM records.
#
# Usage: tests/run_tms320c6713_debug_map_tests.sh [path/to/rcc]

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

FIXTURE='int helper(int x, int y, int z) {
    int w;
    w = x + y + z;
    return w;
}
int g_counter;
int main() {
    int count;
    count = helper(1, 2, 3);
    g_counter = count;
    return count;
}
'

dir="$tmpdir/t"
mkdir -p "$dir/build"
printf '%s' "$FIXTURE" >"$dir/t.c"

if ! (cd "$dir" && "$RCC" --target=tms320c6713 --format=raw --debug-map --out="$dir/t.bin" "$dir/t.c") >"$dir/log" 2>&1; then
    echo "FAIL test_debug_map_written_with_correct_records: compile error"
    cat "$dir/log"
    fail=1
else
    ok=1
    map="$dir/build/t.dbgmap"
    if [ ! -f "$map" ]; then
        echo "FAIL test_debug_map_written_with_correct_records: $map was not written"
        ok=0
    else
        grep -q "^FILE 1 $dir/t.c\$" "$map" || { echo "FAIL: missing FILE record"; cat "$map"; ok=0; }
        grep -qE "^SYM [0-9]+ helper FUNC\$" "$map" || { echo "FAIL: missing SYM helper"; cat "$map"; ok=0; }
        grep -qE "^SYM [0-9]+ main FUNC\$" "$map" || { echo "FAIL: missing SYM main"; cat "$map"; ok=0; }
        grep -qE "^SYM [0-9]+ g_counter DATA\$" "$map" || { echo "FAIL: missing SYM g_counter"; cat "$map"; ok=0; }
        grep -qE "^LINE [0-9]+ 1 [0-9]+\$" "$map" || { echo "FAIL: missing LINE record"; cat "$map"; ok=0; }
        if grep -qE "^SYM [0-9]+ helper_" "$map"; then
            echo "FAIL test_internal_cells_not_listed_as_symbols: internal cell leaked:"
            grep -E "^SYM [0-9]+ helper_" "$map"
            ok=0
        fi
        size="$(wc -c <"$dir/t.bin" | tr -d ' ')"
        base=131072  # kBaseAddress (0x00020000)
        bad="$(awk -v base="$base" -v top="$((base + size))" \
            '($1=="SYM" || $1=="LINE") { addr=$2; if (addr<base || addr>=top) print }' "$map")"
        if [ -n "$bad" ]; then
            echo "FAIL: record(s) outside [$base,$((base+size))):"
            echo "$bad"
            ok=0
        fi
        [ "$ok" -eq 1 ] && echo "PASS test_debug_map_written_with_correct_records"
        [ "$ok" -eq 1 ] || fail=1
    fi
fi

rm -f "$dir/build/t.dbgmap"
if ! (cd "$dir" && "$RCC" --target=tms320c6713 --format=raw --out="$dir/nodbg.bin" "$dir/t.c") >"$dir/log2" 2>&1; then
    echo "FAIL test_no_debug_map_without_flag: compile error"
    cat "$dir/log2"
    fail=1
elif [ -f "$dir/build/t.dbgmap" ]; then
    echo "FAIL test_no_debug_map_without_flag: build/t.dbgmap written despite no --debug-map"
    fail=1
else
    (cd "$dir" && "$RCC" --target=tms320c6713 --format=asm --out="$dir/t.s" "$dir/t.c") >"$dir/log3" 2>&1
    if grep -q "__line" "$dir/t.s"; then
        echo "FAIL test_no_debug_map_without_flag: stray '__line' marker in plain asm output"
        fail=1
    else
        echo "PASS test_no_debug_map_without_flag"
    fi
fi

if ! cmp -s "$dir/nodbg.bin" "$dir/t.bin"; then
    echo "FAIL test_debug_map_identical_binary_output: --debug-map changed the compiled output"
    fail=1
else
    echo "PASS test_debug_map_identical_binary_output"
fi

exit $fail
