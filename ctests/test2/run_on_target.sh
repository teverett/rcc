#!/usr/bin/env bash
# Compiles this directory's one test*.c for one --target and runs it
# under that target's own hardware simulator, by reusing
# ../../tests/run_tests.sh's own check_<target>() functions verbatim:
# a temporary copy of everything in that script up to (not including)
# its own test-discovery loop is sourced fresh on every run, so this
# never drifts from the project's one proven simulator-invocation
# implementation per target. Identical across every ctests/test*/
# directory - copy it as-is into a new one.
#
# The temp copy is created *inside* tests/ (not /tmp) so that script's
# own $SCRIPT_DIR (computed from ${BASH_SOURCE[0]}) still resolves to
# the real tests/ directory once sourced - check_vex() and the qemu-
# based checks locate tests/sim/vex/vex.c and tests/qemu_wait.py
# through that same variable.
#
# Usage: run_on_target.sh <target> [path/to/rcc]
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET="${1:?usage: run_on_target.sh <target> [path/to/rcc]}"
RCC="${2:-$HERE/../../build/bin/rcc}"
SRC="$(ls "$HERE"/test*.c 2>/dev/null | head -1)"
if [ -z "$SRC" ]; then
    echo "FAIL: no test*.c found in $HERE" >&2
    exit 1
fi
NAME="$(basename "$SRC")"
TESTS_DIR="$(cd "$HERE/../../tests" && pwd)"
RUN_TESTS_SH="$TESTS_DIR/run_tests.sh"

expect="$(grep -m1 -oE 'EXPECT: *-?[0-9]+' "$SRC" | grep -oE -- '-?[0-9]+$')"
if [ -z "$expect" ]; then
    echo "FAIL $NAME: no EXPECT comment found in $SRC" >&2
    exit 1
fi

funcs_only="$(mktemp "$TESTS_DIR/.run_on_target_funcs.XXXXXX")"
trap 'rm -f "$funcs_only"' EXIT
awk '/^target_sources=\(\)/ { exit } { print }' "$RUN_TESTS_SH" > "$funcs_only"

# shellcheck disable=SC1090
source "$funcs_only" "$RCC" "--target=$TARGET"

# The sourced script sets its own EXIT trap (to remove its $tmpdir/
# $sockdir), replacing ours above - fold $funcs_only into it too.
trap 'rm -rf "$tmpdir" "$sockdir" "$funcs_only"' EXIT

case "$TARGET" in
    pdp1)        check_pdp1        "$SRC" "$NAME" ;;
    pdp7)        check_pdp7        "$SRC" "$NAME" ;;
    pdp11)       check_pdp11       "$SRC" "$NAME" ;;
    pdp10)       check_pdp10       "$SRC" "$NAME" ;;
    vax)         check_vax         "$SRC" "$NAME" ;;
    avr)         check_avr         "$SRC" "$NAME" ;;
    alpha)       check_alpha       "$SRC" "$NAME" ;;
    saturn)      check_saturn      "$SRC" "$NAME" ;;
    tms320c6713) check_tms320c6713 "$SRC" "$NAME" ;;
    r10000)      check_r10000      "$SRC" "$NAME" ;;
    powerpc)     check_powerpc     "$SRC" "$NAME" ;;
    cortex-m3)   check_cortex_m3   "$SRC" "$NAME" ;;
    6502)        check_6502        "$SRC" "$NAME" ;;
    65c02)       check_65c02       "$SRC" "$NAME" ;;
    68000)       check_68000       "$SRC" "$NAME" ;;
    mc68328)     check_mc68328     "$SRC" "$NAME" ;;
    8080)        check_8080        "$SRC" "$NAME" ;;
    z80)         check_z80         "$SRC" "$NAME" ;;
    8008)        check_8008        "$SRC" "$NAME" ;;
    8051)        check_8051        "$SRC" "$NAME" ;;
    stm32)       check_stm32       "$SRC" "$NAME" ;;
    i386)        check_i386        "$SRC" "$NAME" ;;
    armv7a)      check_armv7a      "$SRC" "$NAME" ;;
    armv8a)      check_armv8a      "$SRC" "$NAME" ;;
    vex)         check_vex         "$SRC" "$NAME" ;;
    *)
        echo "FAIL $NAME: no simulator wired up here for --target=$TARGET" >&2
        exit 1
        ;;
esac
