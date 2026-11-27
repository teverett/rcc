#!/usr/bin/env bash
# Unit tests for --debug-map on the 3 remaining Harvard-style backends
# (pic16/pic18/stm32 - mcs51/8051 already got this port in an earlier round,
# see docs/debug-map.md) now that their own --dump-asm/perFileAsm_
# prerequisite is fixed. Same four-check pattern as
# tests/run_debugmap_allbackends_tests.sh, adapted for pic16/pic18's own
# --format=hex (Intel HEX text, not a raw binary, so there's no byte count
# to range-check SYM/LINE addresses against - a loose upper bound on the
# small GPR/flash address space they target is used instead).
#
# Usage: tests/run_debugmap_harvard_tests.sh [path/to/rcc]

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

# Checks 1 (records correct, address bound), 2 (no flag -> nothing), 3
# (byte-identical whole-program output), shared across all 3; check 4 (no
# map for a real object format) is skipped for pic16/pic18/stm32 only when
# that target has no such format exercised here (all three do, via coff).
check_target() {
    local target="$1" format="$2" maxaddr="$3"
    local dir="$tmpdir/$target"
    mkdir -p "$dir/build"
    printf '%s' "$FIXTURE" >"$dir/t.c"
    local ok=1

    if ! (cd "$dir" && "$RCC" --target="$target" --format="$format" --debug-map --out="$dir/t.bin" "$dir/t.c") >"$dir/log" 2>&1; then
        echo "FAIL $target: compile error (--debug-map)"
        cat "$dir/log"
        return 1
    fi
    if [ ! -f "$dir/build/t.dbgmap" ]; then
        echo "FAIL $target: build/t.dbgmap was not written"
        return 1
    fi
    if ! grep -q "^FILE 1 $dir/t.c\$" "$dir/build/t.dbgmap"; then
        echo "FAIL $target: expected a FILE record for $dir/t.c, got:"
        cat "$dir/build/t.dbgmap"
        return 1
    fi
    for name in helper main g_counter; do
        if ! grep -qE "^SYM [0-9]+ $name (FUNC|DATA)\$" "$dir/build/t.dbgmap"; then
            echo "FAIL $target: no SYM record for $name, got:"
            cat "$dir/build/t.dbgmap"
            ok=0
        fi
    done
    if ! grep -qE "^LINE [0-9]+ 1 [0-9]+\$" "$dir/build/t.dbgmap"; then
        echo "FAIL $target: expected at least one LINE record, got:"
        cat "$dir/build/t.dbgmap"
        ok=0
    fi
    local funcSymCount
    funcSymCount="$(grep -c "^SYM [0-9]* helper FUNC\$" "$dir/build/t.dbgmap")"
    if [ "$funcSymCount" -ne 1 ]; then
        echo "FAIL $target: expected exactly 1 SYM record for helper, found $funcSymCount - got:"
        cat "$dir/build/t.dbgmap"
        ok=0
    fi
    if grep -qE "^SYM [0-9]+ helper_" "$dir/build/t.dbgmap"; then
        echo "FAIL $target: an internal per-param/per-local cell leaked into the SYM records:"
        grep -E "^SYM [0-9]+ helper_" "$dir/build/t.dbgmap"
        ok=0
    fi
    local bad topDecimal
    topDecimal=$((maxaddr))
    bad="$(awk -v top="$topDecimal" '($1=="SYM" || $1=="LINE") { addr=$2; if (addr<0 || addr>=top) print }' "$dir/build/t.dbgmap")"
    if [ -n "$bad" ]; then
        echo "FAIL $target: record(s) outside the sane [0,$maxaddr) address bound:"
        echo "$bad"
        ok=0
    fi
    [ "$ok" -eq 1 ] && echo "PASS $target: check 1 (FILE/SYM/LINE records present, correct, addresses in bound)"

    # Check 2: no flag -> no .dbgmap, no stray marker.
    rm -f "$dir/build/t.dbgmap"
    if ! (cd "$dir" && "$RCC" --target="$target" --format="$format" --out="$dir/nodbg.bin" "$dir/t.c") >"$dir/log2" 2>&1; then
        echo "FAIL $target: compile error (no --debug-map)"
        cat "$dir/log2"
        return 1
    fi
    if [ -f "$dir/build/t.dbgmap" ]; then
        echo "FAIL $target: build/t.dbgmap was written despite no --debug-map"
        ok=0
    fi
    if ! (cd "$dir" && "$RCC" --target="$target" --format=asm --out="$dir/t.s" "$dir/t.c") >"$dir/log3" 2>&1; then
        echo "FAIL $target: compile error (--format=asm)"
        cat "$dir/log3"
        return 1
    fi
    if grep -q "__line" "$dir/t.s"; then
        echo "FAIL $target: found a stray '__line' pseudo-op in --format=asm output despite no --debug-map"
        ok=0
    fi
    [ "$ok" -eq 1 ] && echo "PASS $target: check 2 (no .dbgmap file, no '__line' markers without the flag, so far)"

    # Check 3: byte-identical whole-program output with/without --debug-map.
    if ! cmp -s "$dir/nodbg.bin" "$dir/t.bin"; then
        echo "FAIL $target: --debug-map changed the compiled $format output"
        ok=0
    fi
    [ "$ok" -eq 1 ] && echo "PASS $target: check 3 (byte-identical $format output with/without --debug-map, so far)"

    # Check 4: no .dbgmap for a real separate-compilation object.
    rm -f "$dir/build/t.dbgmap"
    if ! (cd "$dir" && "$RCC" --target="$target" --format=coff --debug-map --out="$dir/t.o" "$dir/t.c") >"$dir/log4" 2>&1; then
        echo "FAIL $target: compile error (--format=coff --debug-map)"
        cat "$dir/log4"
        return 1
    fi
    if [ -f "$dir/build/t.dbgmap" ]; then
        echo "FAIL $target: build/t.dbgmap was written for --format=coff"
        ok=0
    fi

    if [ "$ok" -eq 1 ]; then
        echo "PASS $target (all 4 checks: records correct, no flag -> nothing, byte-identical, no map for coff)"
        return 0
    fi
    return 1
}

check_target pic16 hex 0x8000 || fail=1
check_target pic18 hex 0x8000 || fail=1
check_target stm32 raw 0x30000000 || fail=1

exit $fail
