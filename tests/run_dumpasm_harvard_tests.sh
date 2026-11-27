#!/usr/bin/env bash
# Unit tests for --dump-asm's perFileAsm_ fix on the 4 Harvard-architecture
# backends (mcs51/pic16/pic18/stm32) - see docs/known-bugs.md's own entry on
# why these were deferred past the other ~25 backends' own fix (a RAM/XDATA
# reservation table separate from the code stream, swapped/spliced in ways
# the generic scripted fix didn't cover) and this project's own
# symbol-line-map-design memory.
#
# Two checks per target (`check_target` below):
#   1. --dump-asm writes a non-empty build/t<ext> file containing both the
#      function's and the global's own label - perFileAsm_ was always empty
#      before this fix, so generate() silently never populated it.
#   2. No --dump-asm -> no build/t<ext> file at all.
#
# Usage: tests/run_dumpasm_harvard_tests.sh [path/to/rcc]

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

# target:format:sourceExtension
TARGETS="
8051:raw:.s51
pic16:hex:.pic16
pic18:hex:.pic18
stm32:raw:.s32
"

FIXTURE='int helper(int x) {
    int y;
    y = x + 1;
    return y;
}
int g_counter;
int main() {
    int count;
    count = helper(5);
    g_counter = count;
    return count;
}
'

check_target() {
    local target="$1" format="$2" ext="$3"
    local dir="$tmpdir/$target"
    mkdir -p "$dir/build"
    printf '%s' "$FIXTURE" >"$dir/t.c"
    local ok=1

    if ! (cd "$dir" && "$RCC" --target="$target" --format="$format" --dump-asm --out="$dir/t.bin" "$dir/t.c") >"$dir/log" 2>&1; then
        echo "FAIL $target: compile error (--dump-asm)"
        cat "$dir/log"
        return 1
    fi
    local dumped="$dir/build/t$ext"
    if [ ! -s "$dumped" ]; then
        echo "FAIL $target: $dumped was not written, or is empty"
        ok=0
    else
        if ! grep -q "fn_helper" "$dumped"; then
            echo "FAIL $target: $dumped is missing the 'fn_helper' function label:"
            cat "$dumped"
            ok=0
        fi
        if ! grep -q "g_counter" "$dumped"; then
            echo "FAIL $target: $dumped is missing the 'g_counter' global label:"
            cat "$dumped"
            ok=0
        fi
        if ! grep -q "fn_main" "$dumped"; then
            echo "FAIL $target: $dumped is missing the 'fn_main' function label:"
            cat "$dumped"
            ok=0
        fi
    fi
    [ "$ok" -eq 1 ] && echo "PASS $target: check 1 (--dump-asm writes a non-empty, correctly-populated $ext file)"

    rm -f "$dumped"
    if ! (cd "$dir" && "$RCC" --target="$target" --format="$format" --out="$dir/nodump.bin" "$dir/t.c") >"$dir/log2" 2>&1; then
        echo "FAIL $target: compile error (no --dump-asm)"
        cat "$dir/log2"
        return 1
    fi
    if [ -f "$dumped" ]; then
        echo "FAIL $target: $dumped was written despite no --dump-asm"
        ok=0
    else
        echo "PASS $target: check 2 (no $ext file without --dump-asm)"
    fi

    [ "$ok" -eq 1 ]
}

for entry in $TARGETS; do
    [ -z "$entry" ] && continue
    target="${entry%%:*}"
    rest="${entry#*:}"
    format="${rest%%:*}"
    ext="${rest#*:}"
    check_target "$target" "$format" "$ext" || fail=1
done

exit $fail
