#!/usr/bin/env bash
# Unit tests for Phase 2's --debug-map port to the plain 6502 backend (also
# covers atari2600/2a03/5200, which share M6502Codegen via Variant - see
# tests/run_pdp7_debug_map_tests.sh for the Phase 1/pdp7 original, and this
# project's own symbol-line-map-design memory/docs/debug-map.md for the
# shared design). This target differs from pdp7/pdp1 in two ways that show
# up below: it's byte-oriented (M6502Assembler.cpp tracks `orgBase_+offset_`
# directly, no RELATIVE-flag/base-shift to worry about), and it has no
# `--format=list`-style label dump at all (M6502Codegen::assembledFormats()
# lists only `raw` plus five real object formats - see
# test_debug_map_written_with_correct_records below for how this test
# copes with that gap), using py65 for its real-hardware check instead of
# open-simh.
#
# Five tests (pdp7's own sixth test, which exercises `--format=coff`'s own
# exclusion, is covered here too since this target's own object-format set
# is larger - see test_no_debug_map_for_object_formats):
#   test_debug_map_written_with_correct_records - a plain two-function,
#       one-global program - expects a build/<name>.dbgmap with the right
#       FILE/SYM/LINE records; since there's no --format=list to cross-
#       check SYM addresses against, this instead confirms every SYM
#       address falls strictly inside the compiled --format=raw image's
#       own [base, base+size) range - a cheap sanity check that doesn't
#       need a second independent source of addresses.
#   test_no_debug_map_without_flag - the same program compiled without
#       --debug-map - expects no .dbgmap file, and no stray "__line"
#       pseudo-ops in the plain --format=asm output.
#   test_debug_map_identical_binary_output - compiling the same program
#       with and without --debug-map produces byte-identical --format=raw
#       output.
#   test_debug_map_program_still_runs_correctly - runs the --debug-map
#       build on the real py65 simulator and checks the result.
#   test_no_debug_map_for_object_formats - `--format=coff --debug-map`
#       (real separate compilation - one of this target's five object
#       formats) produces no .dbgmap.
#   test_internal_cells_not_listed_as_symbols - a function with several
#       params/locals - expects exactly one SYM record for the function
#       itself, not one per internal per-local cell too.
#
# Usage: tests/run_m6502_debug_map_tests.sh [path/to/rcc]

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

# Runs `binfile` (a compiled --format=raw image) on the real py65
# simulator, the same way tests/run_tests.sh's own check_6502() does:
# loaded at $0600, single-stepped until a self-targeting JMP (the halt
# loop), result read from zero-page cells 2/3 (little-endian, sign-
# adjusted). Prints the result, or "TIMEOUT"/"NO_PY65".
run_on_simulator() {
    local binfile="$1"
    if ! python3 -c "import py65" >/dev/null 2>&1; then
        echo "NO_PY65"
        return 0
    fi
    python3 - "$binfile" <<'PY'
import sys
from py65.devices.mpu6502 import MPU

with open(sys.argv[1], "rb") as f:
    data = f.read()

base = 0x0600
mpu = MPU()
mpu.memory[base:base + len(data)] = data
mpu.pc = base

for _ in range(2_000_000):
    pc_before = mpu.pc
    mpu.step()
    if mpu.pc == pc_before:
        break
else:
    print("TIMEOUT")
    sys.exit(0)

lo = mpu.memory[2]
hi = mpu.memory[3]
val = lo | (hi << 8)
if val >= 0x8000:
    val -= 0x10000
print(val)
PY
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
    if ! (cd "$dir" && "$RCC" --target=6502 --format=raw --debug-map --out="$dir/t.bin" "$dir/t.c") >"$dir/log" 2>&1; then
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
    if ! grep -qE "^LINE [0-9]+ 1 [0-9]+\$" "$dir/build/t.dbgmap"; then
        echo "FAIL test_debug_map_written_with_correct_records: expected at least one LINE record, got:"
        cat "$dir/build/t.dbgmap"
        return 1
    fi
    # No --format=list to cross-check against on this target (unlike
    # pdp7/pdp1) - instead confirm every SYM/LINE address lands strictly
    # inside the compiled image's own [0x0600, 0x0600+size) range.
    local size base
    size="$(wc -c <"$dir/t.bin" | tr -d ' ')"
    base=1536  # 0x0600
    local bad
    bad="$(awk -v base="$base" -v top="$((base + size))" \
        '($1=="SYM" || $1=="LINE") { addr=$2; if (addr<base || addr>=top) print }' "$dir/build/t.dbgmap")"
    if [ -n "$bad" ]; then
        echo "FAIL test_debug_map_written_with_correct_records: record(s) outside the compiled image's" \
             "own [$base,$((base+size))) address range:"
        echo "$bad"
        return 1
    fi
    echo "PASS test_debug_map_written_with_correct_records (FILE/SYM/LINE records present, addresses in range)"
    return 0
}

test_no_debug_map_without_flag() {
    local dir="$tmpdir/noflag"
    mkdir -p "$dir/build"
    printf '%s' "$FIXTURE" >"$dir/t.c"
    if ! (cd "$dir" && "$RCC" --target=6502 --format=raw --out="$dir/t.bin" "$dir/t.c") >"$dir/log" 2>&1; then
        echo "FAIL test_no_debug_map_without_flag: compile error"
        cat "$dir/log"
        return 1
    fi
    if [ -f "$dir/build/t.dbgmap" ]; then
        echo "FAIL test_no_debug_map_without_flag: build/t.dbgmap was written despite no --debug-map"
        return 1
    fi
    if ! (cd "$dir" && "$RCC" --target=6502 --format=asm --out="$dir/t.s65" "$dir/t.c") >"$dir/log2" 2>&1; then
        echo "FAIL test_no_debug_map_without_flag: compile error (--format=asm)"
        cat "$dir/log2"
        return 1
    fi
    if grep -q "__line" "$dir/t.s65"; then
        echo "FAIL test_no_debug_map_without_flag: found a stray '__line' pseudo-op in --format=asm" \
             "output despite no --debug-map:"
        grep "__line" "$dir/t.s65"
        return 1
    fi
    echo "PASS test_no_debug_map_without_flag (no .dbgmap file, no '__line' markers in plain asm)"
    return 0
}

test_debug_map_identical_binary_output() {
    local dir="$tmpdir/identical"
    mkdir -p "$dir/build"
    printf '%s' "$FIXTURE" >"$dir/t.c"
    if ! (cd "$dir" && "$RCC" --target=6502 --format=raw --out="$dir/nodbg.bin" "$dir/t.c") >"$dir/log1" 2>&1; then
        echo "FAIL test_debug_map_identical_binary_output: compile error (no --debug-map)"
        cat "$dir/log1"
        return 1
    fi
    if ! (cd "$dir" && "$RCC" --target=6502 --format=raw --debug-map --out="$dir/withdbg.bin" "$dir/t.c") >"$dir/log2" 2>&1; then
        echo "FAIL test_debug_map_identical_binary_output: compile error (--debug-map)"
        cat "$dir/log2"
        return 1
    fi
    if ! cmp -s "$dir/nodbg.bin" "$dir/withdbg.bin"; then
        echo "FAIL test_debug_map_identical_binary_output: --debug-map changed the compiled .bin output" \
             "- the '__line' markers must be a true no-op on the real emitted program"
        return 1
    fi
    echo "PASS test_debug_map_identical_binary_output (byte-identical .bin with and without --debug-map)"
    return 0
}

test_debug_map_program_still_runs_correctly() {
    local dir="$tmpdir/runs"
    mkdir -p "$dir/build"
    printf '%s' "$FIXTURE" >"$dir/t.c"
    if ! (cd "$dir" && "$RCC" --target=6502 --format=raw --debug-map --out="$dir/t.bin" "$dir/t.c") >"$dir/log" 2>&1; then
        echo "FAIL test_debug_map_program_still_runs_correctly: compile error"
        cat "$dir/log"
        return 1
    fi
    local result
    result="$(run_on_simulator "$dir/t.bin")"
    if [ "$result" = "NO_PY65" ]; then
        echo "SKIP test_debug_map_program_still_runs_correctly: py65 not installed"
        return 0
    fi
    if [ "$result" = "TIMEOUT" ]; then
        echo "FAIL test_debug_map_program_still_runs_correctly: simulator never reached the halt loop"
        return 1
    fi
    if [ "$result" -ne 5 ]; then
        echo "FAIL test_debug_map_program_still_runs_correctly: expected 5, got $result"
        return 1
    fi
    echo "PASS test_debug_map_program_still_runs_correctly (real-hardware check, result=$result)"
    return 0
}

test_no_debug_map_for_object_formats() {
    local dir="$tmpdir/coff"
    mkdir -p "$dir/build"
    printf '%s' "$FIXTURE" >"$dir/t.c"
    if ! (cd "$dir" && "$RCC" --target=6502 --format=coff --debug-map --out="$dir/t.o" "$dir/t.c") >"$dir/log" 2>&1; then
        echo "FAIL test_no_debug_map_for_object_formats: compile error"
        cat "$dir/log"
        return 1
    fi
    if [ -f "$dir/build/t.dbgmap" ]; then
        echo "FAIL test_no_debug_map_for_object_formats: build/t.dbgmap was written for --format=coff" \
             "(addresses aren't final until --link runs - out of scope here)"
        return 1
    fi
    echo "PASS test_no_debug_map_for_object_formats (no .dbgmap for a real separate-compilation object)"
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
    if ! (cd "$dir" && "$RCC" --target=6502 --format=raw --debug-map --out="$dir/t.bin" "$dir/t.c") >"$dir/log" 2>&1; then
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
test_no_debug_map_for_object_formats || fail=1
test_internal_cells_not_listed_as_symbols || fail=1

exit $fail
