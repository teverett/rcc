#!/usr/bin/env bash
# Unit tests for Phase 2's --debug-map rollout to the remaining "standard-
# shape" backends (see docs/debug-map.md's own "Implementation (pdp1,
# 6502, z80)" section, and this project's own symbol-line-map-design
# memory): every target below shares the identical byte-oriented,
# Line::Kind-based assembler shape (no Segment/RELATIVE-flag machinery),
# the same "fn_"/"g_" label convention, and `raw` as its one whole-
# program-final format - the exact pattern tests/run_m6502_debug_map_tests.sh
# and tests/run_z80_debug_map_tests.sh already validate per-backend. This
# script applies that same four-check pattern across all of them in one
# place, rather than duplicating a near-identical file 27 times.
#
# Four checks per target (`check_target` below), run for every target in
# $TARGETS:
#   1. --debug-map produces build/<name>.dbgmap with the right FILE/SYM/LINE
#      records, every address inside the compiled --format=raw image's own
#      [0, size) byte range relative to its own base (sanity check - most of
#      these targets have no --format=list to cross-check against the way
#      pdp7/pdp1 do).
#   2. No --debug-map -> no .dbgmap file, and no stray "__line" marker in
#      plain --format=asm output.
#   3. Byte-identical --format=raw output with and without --debug-map (the
#      markers are a true no-op on the real emitted program).
#   4. --format=coff --debug-map (a real separate-compilation object)
#      produces no .dbgmap.
#
# Usage: tests/run_debugmap_allbackends_tests.sh [path/to/rcc]

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

# target:baseAddress (decimal) - baseAddress only used to sanity-check SYM/
# LINE addresses fall inside [base, base+size).
TARGETS="
m65c02:65c02:1536
pdp10:pdp10:4096
pdp11:pdp11:4096
i386:i386:1048576
arm:armv7a:4096
arm64:armv8a:1074266112
armm:cortex-m3:4096
m68k:68000:4096
mc68328:mc68328:0
z8000:z8000:4096
mips:r10000:2147549184
riscv32:riscv32:2147483648
riscv64:riscv64:2147483648
powerpc:powerpc:1048576
sparc:sparc:16384
stm8:stm8:256
mcs51:8051:0
tms9900:tms9900:8192
cp1610:cp1610:4096
i8008:8008:0
i8080:8080:256
alpha:alpha:1179648
vax:vax:16384
avr:avr:0
m16c:m16c:1024
m32c:m32c:1024
r32c:r32c:4096
"

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

check_target() {
    local target="$1"
    local dir="$tmpdir/$target"
    mkdir -p "$dir/build"
    printf '%s' "$FIXTURE" >"$dir/t.c"
    local ok=1

    if ! (cd "$dir" && "$RCC" --target="$target" --format=raw --debug-map --out="$dir/t.bin" "$dir/t.c") >"$dir/log" 2>&1; then
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
    local size base
    size="$(wc -c <"$dir/t.bin" | tr -d ' ')"
    base="$2"
    local bad
    bad="$(awk -v base="$base" -v top="$((base + size))" \
        '($1=="SYM" || $1=="LINE") { addr=$2; if (addr<base || addr>=top) print }' "$dir/build/t.dbgmap")"
    if [ -n "$bad" ]; then
        echo "FAIL $target: record(s) outside the compiled image's own [$base,$((base+size))) range:"
        echo "$bad"
        ok=0
    fi
    [ "$ok" -eq 1 ] && echo "PASS $target: check 1 (FILE/SYM/LINE records present, correct, addresses in range)"

    # Check 2: no flag -> no .dbgmap, no stray marker.
    rm -f "$dir/build/t.dbgmap"
    if ! (cd "$dir" && "$RCC" --target="$target" --format=raw --out="$dir/nodbg.bin" "$dir/t.c") >"$dir/log2" 2>&1; then
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

    # Check 3: byte-identical --format=raw with/without --debug-map.
    if ! cmp -s "$dir/nodbg.bin" "$dir/t.bin"; then
        echo "FAIL $target: --debug-map changed the compiled --format=raw output"
        ok=0
    fi
    [ "$ok" -eq 1 ] && echo "PASS $target: check 3 (byte-identical --format=raw with/without --debug-map, so far)"

    # Check 4: no .dbgmap for a real separate-compilation object. Skipped
    # for pdp10: a program with a Global already fails to compile under
    # --format=coff with no --debug-map involved at all ("CoffFormat:
    # relocation references unknown symbol '..'" - confirmed pre-existing
    # and unrelated on the unmodified checkout, see docs/known-bugs.md's
    # own entry), so there's no way to exercise this check on that target
    # with a fixture that also needs a Global for checks 1-3 above.
    if [ "$target" != "pdp10" ]; then
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
    fi

    if [ "$ok" -eq 1 ]; then
        echo "PASS $target (all 4 checks: records correct, no flag -> nothing, byte-identical, no map for coff)"
        return 0
    fi
    return 1
}

for entry in $TARGETS; do
    [ -z "$entry" ] && continue
    dir="${entry%%:*}"
    rest="${entry#*:}"
    target="${rest%%:*}"
    base="${rest#*:}"
    check_target "$target" "$base" || fail=1
done

exit $fail
