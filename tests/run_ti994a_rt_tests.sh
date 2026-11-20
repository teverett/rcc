#!/usr/bin/env bash
# Tests for src/rt/ti994a/ti994a.c's keyboard-matrix functions
# (ti_key_select_column()/ti_key_row()/ti_any_key_down()). No pip-installable
# Python TMS9900 core exists (see docs/platforms.md's own "tms9900" entry and
# docs/known-bugs.md), so unlike tests/run_coleco_rt_tests.sh this can't run
# the compiled program under a real CPU core - instead, like
# tests/run_register_tests.sh, it compiles with --format=asm and checks the
# emitted assembly text directly for the exact CRU-bit instruction sequence
# ti994a.h's own header comment documents (column select via SBO/SBZ at CRU
# base 36/$0024, row read via TB at CRU base 6/$0006), which
# tests/tms9900_assembler_test.cpp separately confirms assembles to the
# correct real TMS9900 opcodes.
#
# Usage: tests/run_ti994a_rt_tests.sh [path/to/rcc]

set -eu

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
RCC="$ROOT_DIR/build/bin/rcc"
for arg in "$@"; do
    case "$arg" in
        *) RCC="$arg" ;;
    esac
done
RCC="$(cd "$(dirname "$RCC")" && pwd)/$(basename "$RCC")"

if [ ! -x "$RCC" ]; then
    echo "rcc binary not found at $RCC - build it first ('make')" >&2
    exit 1
fi

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
cd "$tmpdir"

fail=0

cp "$ROOT_DIR/src/rt/ti994a/ti994a.h" "$ROOT_DIR/src/rt/ti994a/ti994a.c" .

# ti_key_select_column(col) writes the 3 column-select bits (P2-P4) one at a
# time via SBO/SBZ at CRU base 36 ($0024) - never via LDCR (see ti994a.h's
# own header comment for why). Since `col` is a runtime value, the compiler
# emits both the SBO and SBZ branch for each of the 3 bits (one taken, one
# not, depending on `col`'s actual value at runtime) - 3 bits x 2 branches
# each = 6 of each.
cat >select_main.c <<'EOF'
#include "ti994a.h"
int main() {
    ti_key_select_column(5);
    return 0;
}
EOF
"$RCC" --target=tms9900 --format=coff select_main.c >/dev/null
"$RCC" --target=tms9900 --format=coff ti994a.c >/dev/null
"$RCC" --link --target=tms9900 --format=raw --out=select.flat build/select_main.o build/ti994a.o

# --format=asm needs a whole program (a 'main'), not just a separately-
# compiled module - append one that calls both functions, so neither is
# left out of a build that happens to eliminate unreachable functions.
cat ti994a.c >ti994a_with_main.c
cat >>ti994a_with_main.c <<'EOF'
int main() {
    ti_key_select_column(5);
    return ti_key_row();
}
EOF
asm="$("$RCC" --target=tms9900 --format=asm ti994a_with_main.c)"
python3 - "$asm" <<'PY' || fail=1
import re
import sys

asm = sys.argv[1]
func = asm.split("fn_ti_key_select_column:", 1)[1].split("fn_ti_key_row:", 1)[0]
# Each of the 3 column bits must be written through its own "li r12,36" (36
# decimal = $0024, the CRU address ti994a.h's header comment cites for P2)
# immediately before a single-bit SBO or SZB write.
assert func.count("li r12, 36") == 6, f"expected 6 'li r12, 36' (2 branches x 3 column bits), got {func.count('li r12, 36')}"
assert func.count("sbo ") == 3, f"expected exactly 3 SBO instructions (one per column bit's 'bit set' branch), got {func.count('sbo ')}"
assert func.count("sbz ") == 3, f"expected exactly 3 SBZ instructions (one per column bit's 'bit clear' branch), got {func.count('sbz ')}"
assert {m for m in re.findall(r"sbo (\d+)", func)} == {"0", "1", "2"}, "expected SBO at displacements 0, 1, 2 (P2, P3, P4)"
assert {m for m in re.findall(r"sbz (\d+)", func)} == {"0", "1", "2"}, "expected SBZ at displacements 0, 1, 2 (P2, P3, P4)"
assert "ldcr" not in func, "ti_key_select_column() must not use LDCR - see ti994a.h's own header comment on its unconfirmed bit order"
print("PASS ti_key_select_column_uses_sbo_sbz_at_cru_base_36")
PY

# ti_key_row() reads the 8 row bits (INT3-INT10) one at a time via TB at CRU
# base 6 ($0006) - never via STCR (same "unconfirmed bit order" reason).
python3 - "$asm" <<'PY' || fail=1
import sys

asm = sys.argv[1]
func = asm.split("fn_ti_key_row:", 1)[1]
func = func.split("\nfn_", 1)[0]
assert func.count("li r12, 6") == 8, f"expected 8 'li r12, 6' (one per row bit), got {func.count('li r12, 6')}"
assert func.count("tb ") == 8, f"expected exactly 8 TB instructions (one per row bit), got {func.count('tb ')}"
for n in range(8):
    assert f"tb {n}\n" in func, f"missing 'tb {n}' for row bit {n}"
assert "stcr" not in func, "ti_key_row() must not use STCR - see ti994a.h's own header comment on its unconfirmed bit order"
print("PASS ti_key_row_uses_tb_at_cru_base_6_for_all_8_rows")
PY

exit $fail
