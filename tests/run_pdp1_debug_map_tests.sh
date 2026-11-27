#!/usr/bin/env bash
# Unit tests for Phase 2's --debug-map port to pdp1 (see
# tests/run_pdp7_debug_map_tests.sh for the Phase 1/pdp7 original, and
# this project's own symbol-line-map-design memory/docs/debug-map.md for
# the shared design). pdp1 is simpler than pdp7 in two ways that show up
# below: it has no object/`--link` format at all (every format is whole-
# program-final, so there's no format to exclude the way pdp7 excludes
# "coff"), and its address bookkeeping has no RELATIVE-flag/base-shift to
# worry about (see Pdp1Assembler.cpp's own Segment::DebugLine comment).
#
# Five tests (pdp7's own sixth test, --format=coff exclusion, doesn't
# apply here - see above):
#   test_debug_map_written_with_correct_records - a plain two-function,
#       one-global program - expects a build/<name>.dbgmap with the right
#       FILE/SYM/LINE records; "main"'s own SYM address is cross-checked
#       against --format=list's own label dump (only "main" is safe to
#       compare this way - see the FILE/SYM/LINE comment on
#       test_internal_cells_not_listed_as_symbols below for why "helper"'s
#       own longer label can't be, on this target either).
#   test_no_debug_map_without_flag - the same program compiled without
#       --debug-map - expects no .dbgmap file, and no stray "__line"
#       pseudo-ops in the plain --dump-asm output.
#   test_debug_map_identical_binary_output - compiling the same program
#       with and without --debug-map produces byte-identical --format=rim
#       output.
#   test_debug_map_program_still_runs_correctly - runs the --debug-map
#       build on the real simulator (via its paper-tape-reader bootstrap,
#       not pdp7's load-at-fixed-address convention) and checks the
#       result.
#   test_internal_cells_not_listed_as_symbols - a function with several
#       params (so Pdp1Codegen.h's own leaf-fast-path-equivalent cells
#       exist) - expects exactly one SYM record for the function itself.
#
# Usage: tests/run_pdp1_debug_map_tests.sh [path/to/rcc]

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RCC="$SCRIPT_DIR/../build/bin/rcc"
[ $# -ge 1 ] && RCC="$1"

if [ ! -x "$RCC" ]; then
    echo "rcc binary not found at $RCC - build it first ('make')" >&2
    exit 1
fi
# Absolute, since several tests below `cd` into a per-test tmpdir first (so
# writeToBuildDir()'s own cwd-relative "build/" lands there, not here).
RCC="$(cd "$(dirname "$RCC")" && pwd)/$(basename "$RCC")"

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
    if ! (cd "$dir" && "$RCC" --target=pdp1 --format=rim --debug-map --out="$dir/t.rim" "$dir/t.c") >"$dir/log" 2>&1; then
        echo "FAIL test_debug_map_written_with_correct_records: compile error"
        cat "$dir/log"
        return 1
    fi
    if [ ! -f "$dir/build/t.dbgmap" ]; then
        echo "FAIL test_debug_map_written_with_correct_records: build/t.dbgmap was not written"
        return 1
    fi
    if ! grep -q "^FILE 1 $dir/t.c\$" "$dir/build/t.dbgmap"; then
        echo "FAIL test_debug_map_written_with_correct_records: expected a FILE record for $dir/t.c, got:"
        cat "$dir/build/t.dbgmap"
        return 1
    fi
    for name in helper main g_counter; do
        if ! grep -qE "^SYM [0-9]+ $name (FUNC|DATA)\$" "$dir/build/t.dbgmap"; then
            echo "FAIL test_debug_map_written_with_correct_records: no SYM record for $name, got:"
            cat "$dir/build/t.dbgmap"
            return 1
        fi
    done
    # Cross-check "main"'s own SYM address against --format=list's own
    # independent label dump (octal). Only "main" is safe this way:
    # writeList() (Pdp1Assembler.cpp) truncates every label to 8
    # characters, same as pdp7's, so "FN_helper"'s own longer label
    # collides there with "FN_helper_ra" (both truncate to "FN_helpe") -
    # "FN_main" is short enough not to collide with "FN_main_ra".
    (cd "$dir" && "$RCC" --target=pdp1 --format=list --out="$dir/t.lst" "$dir/t.c") >/dev/null 2>&1
    local symAddrDec listAddrOctal listAddrDec
    symAddrDec="$(grep -E '^SYM [0-9]+ main FUNC$' "$dir/build/t.dbgmap" | awk '{print $2}')"
    listAddrOctal="$(grep '^FN_main ' "$dir/t.lst" | awk '{print $2}' | tr -d '#')"
    listAddrDec=$((8#$listAddrOctal))
    if [ "$symAddrDec" -ne "$listAddrDec" ]; then
        echo "FAIL test_debug_map_written_with_correct_records: main's SYM address $symAddrDec" \
             "doesn't match --format=list's own $listAddrDec (octal $listAddrOctal)"
        return 1
    fi
    if ! grep -qE "^LINE [0-9]+ 1 [0-9]+\$" "$dir/build/t.dbgmap"; then
        echo "FAIL test_debug_map_written_with_correct_records: expected at least one LINE record, got:"
        cat "$dir/build/t.dbgmap"
        return 1
    fi
    echo "PASS test_debug_map_written_with_correct_records (FILE/SYM/LINE records present and address-correct)"
    return 0
}

test_no_debug_map_without_flag() {
    local dir="$tmpdir/noflag"
    mkdir -p "$dir/build"
    printf '%s' "$FIXTURE" >"$dir/t.c"
    if ! (cd "$dir" && "$RCC" --target=pdp1 --format=rim --dump-asm --out="$dir/t.rim" "$dir/t.c") >"$dir/log" 2>&1; then
        echo "FAIL test_no_debug_map_without_flag: compile error"
        cat "$dir/log"
        return 1
    fi
    if [ -f "$dir/build/t.dbgmap" ]; then
        echo "FAIL test_no_debug_map_without_flag: build/t.dbgmap was written despite no --debug-map"
        return 1
    fi
    if [ ! -f "$dir/build/t.s" ]; then
        echo "FAIL test_no_debug_map_without_flag: --dump-asm's own build/t.s was not written"
        return 1
    fi
    if grep -q "__line" "$dir/build/t.s"; then
        echo "FAIL test_no_debug_map_without_flag: found a stray '__line' pseudo-op in --dump-asm" \
             "output despite no --debug-map:"
        grep "__line" "$dir/build/t.s"
        return 1
    fi
    echo "PASS test_no_debug_map_without_flag (no .dbgmap file, no '__line' markers in plain asm)"
    return 0
}

test_debug_map_identical_binary_output() {
    local dir="$tmpdir/identical"
    mkdir -p "$dir/build"
    printf '%s' "$FIXTURE" >"$dir/t.c"
    if ! (cd "$dir" && "$RCC" --target=pdp1 --format=rim --out="$dir/nodbg.rim" "$dir/t.c") >"$dir/log1" 2>&1; then
        echo "FAIL test_debug_map_identical_binary_output: compile error (no --debug-map)"
        cat "$dir/log1"
        return 1
    fi
    if ! (cd "$dir" && "$RCC" --target=pdp1 --format=rim --debug-map --out="$dir/withdbg.rim" "$dir/t.c") >"$dir/log2" 2>&1; then
        echo "FAIL test_debug_map_identical_binary_output: compile error (--debug-map)"
        cat "$dir/log2"
        return 1
    fi
    if ! cmp -s "$dir/nodbg.rim" "$dir/withdbg.rim"; then
        echo "FAIL test_debug_map_identical_binary_output: --debug-map changed the compiled .rim output" \
             "- the '__line' markers must be a true no-op on the real emitted program"
        return 1
    fi
    echo "PASS test_debug_map_identical_binary_output (byte-identical .rim with and without --debug-map)"
    return 0
}

test_debug_map_program_still_runs_correctly() {
    local dir="$tmpdir/runs"
    mkdir -p "$dir/build"
    if ! command -v pdp1 >/dev/null 2>&1; then
        echo "SKIP test_debug_map_program_still_runs_correctly: pdp1 (open-simh) not found on PATH"
        return 0
    fi
    printf '%s' "$FIXTURE" >"$dir/t.c"
    if ! (cd "$dir" && "$RCC" --target=pdp1 --format=rim --debug-map --out="$dir/t.rim" "$dir/t.c") >"$dir/log" 2>&1; then
        echo "FAIL test_debug_map_program_still_runs_correctly: compile error"
        cat "$dir/log"
        return 1
    fi
    local simfile="$dir/t.simh" outfile="$dir/t.out"
    cat >"$simfile" <<SIMH
attach ptr $dir/t.rim
boot ptr
e ac
SIMH
    run_with_timeout 10 bash -c "pdp1 '$simfile' </dev/null >'$outfile' 2>/dev/null"
    local ac_octal
    ac_octal="$(grep '^AC:' "$outfile" | awk '{print $2}')"
    if [ -z "$ac_octal" ]; then
        echo "FAIL test_debug_map_program_still_runs_correctly: simulator produced no AC (timed out or crashed)"
        return 1
    fi
    local ac_dec
    ac_dec="$(to_signed $((8#$ac_octal)) 18)"
    if [ "$ac_dec" -ne 5 ]; then
        echo "FAIL test_debug_map_program_still_runs_correctly: expected 5, got $ac_dec (AC octal: $ac_octal)"
        return 1
    fi
    echo "PASS test_debug_map_program_still_runs_correctly (real-hardware check, AC=$ac_dec)"
    return 0
}

test_internal_cells_not_listed_as_symbols() {
    local dir="$tmpdir/internalcells"
    mkdir -p "$dir/build"
    cat >"$dir/t.c" <<'EOF'
int add3(int a, int b, int c) {
    int t;
    t = a + b + c;
    return t;
}
int main() {
    return add3(1, 2, 3);
}
EOF
    if ! (cd "$dir" && "$RCC" --target=pdp1 --format=rim --debug-map --out="$dir/t.rim" "$dir/t.c") >"$dir/log" 2>&1; then
        echo "FAIL test_internal_cells_not_listed_as_symbols: compile error"
        cat "$dir/log"
        return 1
    fi
    local funcSymCount
    funcSymCount="$(grep -c "^SYM [0-9]* add3 FUNC\$" "$dir/build/t.dbgmap")"
    if [ "$funcSymCount" -ne 1 ]; then
        echo "FAIL test_internal_cells_not_listed_as_symbols: expected exactly 1 SYM record for add3," \
             "found $funcSymCount - got:"
        cat "$dir/build/t.dbgmap"
        return 1
    fi
    if grep -qE "^SYM [0-9]+ add3_" "$dir/build/t.dbgmap"; then
        echo "FAIL test_internal_cells_not_listed_as_symbols: an internal per-param/per-local cell" \
             "leaked into the SYM records:"
        grep -E "^SYM [0-9]+ add3_" "$dir/build/t.dbgmap"
        return 1
    fi
    echo "PASS test_internal_cells_not_listed_as_symbols (exactly 1 SYM record for add3, no internal cells)"
    return 0
}

test_debug_map_written_with_correct_records || fail=1
test_no_debug_map_without_flag || fail=1
test_debug_map_identical_binary_output || fail=1
test_debug_map_program_still_runs_correctly || fail=1
test_internal_cells_not_listed_as_symbols || fail=1

exit $fail
