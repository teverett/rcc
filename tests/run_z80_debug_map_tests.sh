#!/usr/bin/env bash
# Unit tests for Phase 2's --debug-map port to the plain z80 backend (see
# tests/run_pdp7_debug_map_tests.sh for the Phase 1/pdp7 original, and this
# project's own symbol-line-map-design memory/docs/debug-map.md for the
# shared design). Same shape as tests/run_m6502_debug_map_tests.sh - no
# `--format=list` to cross-check SYM addresses against, so this test
# instead confirms every SYM/LINE address falls strictly inside the
# compiled image's own address range - using z80-python (via
# tests/run_tests.sh's own check_z80()) for the real-hardware check.
#
# Six tests:
#   test_debug_map_written_with_correct_records - a plain two-function,
#       one-global program - expects a build/<name>.dbgmap with the right
#       FILE/SYM/LINE records, every address inside the compiled
#       --format=raw image's own [0x8000, 0x8000+size) range.
#   test_no_debug_map_without_flag - the same program compiled without
#       --debug-map - expects no .dbgmap file, and no stray "__line"
#       pseudo-ops in the plain --format=asm output.
#   test_debug_map_identical_binary_output - compiling the same program
#       with and without --debug-map produces byte-identical --format=raw
#       output.
#   test_debug_map_program_still_runs_correctly - runs the --debug-map
#       build on the real z80-python simulator and checks the result.
#   test_no_debug_map_for_object_formats - `--format=coff --debug-map`
#       (real separate compilation) produces no .dbgmap.
#   test_internal_cells_not_listed_as_symbols - a function with several
#       params/locals - expects exactly one SYM record for the function
#       itself, not one per internal per-local cell too.
#
# Usage: tests/run_z80_debug_map_tests.sh [path/to/rcc]

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

# Runs `binfile` (a compiled --format=raw image) on the real z80-python
# simulator, the same way tests/run_tests.sh's own check_z80() does:
# loaded at $8000, single-stepped until a self-targeting JP (the halt
# loop), result read from HL (sign-adjusted). Prints the result, or
# "TIMEOUT"/"NO_Z80PYTHON".
run_on_simulator() {
    local binfile="$1"
    if ! python3 -c "import z80_python" >/dev/null 2>&1; then
        echo "NO_Z80PYTHON"
        return 0
    fi
    python3 - "$binfile" <<'PY'
import sys
from z80_python import Z80CPU

with open(sys.argv[1], "rb") as f:
    data = f.read()

base = 0x8000
memory = bytearray(0x10000)
memory[base:base + len(data)] = data

cpu = Z80CPU(memory.__getitem__, memory.__setitem__)
cpu.pc = base

for _ in range(2_000_000):
    pc_before = cpu.pc
    cpu.step()
    if cpu.pc == pc_before:
        break
else:
    print("TIMEOUT")
    sys.exit(0)

val = (cpu.h << 8) | cpu.l
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
    if ! (cd "$dir" && "$RCC" --target=z80 --format=raw --debug-map --out="$dir/t.bin" "$dir/t.c") >"$dir/log" 2>&1; then
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
    local size base
    size="$(wc -c <"$dir/t.bin" | tr -d ' ')"
    base=32768  # 0x8000
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
    if ! (cd "$dir" && "$RCC" --target=z80 --format=raw --out="$dir/t.bin" "$dir/t.c") >"$dir/log" 2>&1; then
        echo "FAIL test_no_debug_map_without_flag: compile error"
        cat "$dir/log"
        return 1
    fi
    if [ -f "$dir/build/t.dbgmap" ]; then
        echo "FAIL test_no_debug_map_without_flag: build/t.dbgmap was written despite no --debug-map"
        return 1
    fi
    if ! (cd "$dir" && "$RCC" --target=z80 --format=asm --out="$dir/t.sz80" "$dir/t.c") >"$dir/log2" 2>&1; then
        echo "FAIL test_no_debug_map_without_flag: compile error (--format=asm)"
        cat "$dir/log2"
        return 1
    fi
    if grep -q "__line" "$dir/t.sz80"; then
        echo "FAIL test_no_debug_map_without_flag: found a stray '__line' pseudo-op in --format=asm" \
             "output despite no --debug-map:"
        grep "__line" "$dir/t.sz80"
        return 1
    fi
    echo "PASS test_no_debug_map_without_flag (no .dbgmap file, no '__line' markers in plain asm)"
    return 0
}

test_debug_map_identical_binary_output() {
    local dir="$tmpdir/identical"
    mkdir -p "$dir/build"
    printf '%s' "$FIXTURE" >"$dir/t.c"
    if ! (cd "$dir" && "$RCC" --target=z80 --format=raw --out="$dir/nodbg.bin" "$dir/t.c") >"$dir/log1" 2>&1; then
        echo "FAIL test_debug_map_identical_binary_output: compile error (no --debug-map)"
        cat "$dir/log1"
        return 1
    fi
    if ! (cd "$dir" && "$RCC" --target=z80 --format=raw --debug-map --out="$dir/withdbg.bin" "$dir/t.c") >"$dir/log2" 2>&1; then
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
    if ! (cd "$dir" && "$RCC" --target=z80 --format=raw --debug-map --out="$dir/t.bin" "$dir/t.c") >"$dir/log" 2>&1; then
        echo "FAIL test_debug_map_program_still_runs_correctly: compile error"
        cat "$dir/log"
        return 1
    fi
    local result
    result="$(run_on_simulator "$dir/t.bin")"
    if [ "$result" = "NO_Z80PYTHON" ]; then
        echo "SKIP test_debug_map_program_still_runs_correctly: z80-python not installed"
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
    if ! (cd "$dir" && "$RCC" --target=z80 --format=coff --debug-map --out="$dir/t.o" "$dir/t.c") >"$dir/log" 2>&1; then
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
    if ! (cd "$dir" && "$RCC" --target=z80 --format=raw --debug-map --out="$dir/t.bin" "$dir/t.c") >"$dir/log" 2>&1; then
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
