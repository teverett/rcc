#!/usr/bin/env bash
# Unit tests for --debug-map under real separate compilation (--format=coff
# + --link), the gap documented in docs/debug-map.md's own "Examples"
# section: a compile step forced into an object format by --platform (or an
# explicit --format=coff) previously made --debug-map a guaranteed no-op,
# since a relocatable object's addresses aren't final until --link runs and
# --link itself had no access to the original source text at all.
#
# The fix: a compile step with --debug-map also writes a pre-link sidecar
# (`<obj>.dbgpre` - see Backend::debugMapPreLink()'s own comment in
# Codegen.h), carrying the exact FILE/SYM/LINE text relative to that
# object's own org-0 .text start; --link reads back any sidecar that exists
# (readDebugMapPreLink() in main.cpp) and GenericLinker::merge() shifts its
# addresses by that object's own final placement offset - the same one it
# already computes for ordinary relocation patching - producing the final
# build/<out>.dbgmap.
#
# Five checks, all against --target=6502 (byte-addressed, exercises the
# common case every library-linked example hits):
#   1. Two separately-compiled objects, both with --debug-map, linked
#      together: the combined .dbgmap has two FILE records and correct
#      SYM/LINE records for both objects' own functions, addresses shifted
#      into the final linked image's own range.
#   2. A "library" object compiled WITHOUT --debug-map (no sidecar) still
#      links cleanly with --debug-map on the other object; the combined
#      .dbgmap only ever reports the one object that actually has debug
#      info.
#   3. Linking without --debug-map produces no .dbgmap file at all, even
#      though both objects' own sidecars exist on disk.
#   4. --debug-map's own sidecar/merge logic doesn't change the real linked
#      program: byte-identical --format=raw output with and without it.
#   5. A single object's own compile+debugMapPreLink output itself is a
#      byte-identical --format=coff object with and without --debug-map
#      (the sidecar is a side file, never spliced into the object itself).
#
# Usage: tests/run_debugmap_link_tests.sh [path/to/rcc]

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

A_SRC='extern int helper(int x);
int g_from_a;
int main() {
    int r;
    r = helper(5);
    g_from_a = r;
    return r;
}
'
B_SRC='int helper(int x) {
    int y;
    y = x + 1;
    return y;
}
'

ok=1
mkdir -p "$tmpdir/build"
printf '%s' "$A_SRC" >"$tmpdir/a.c"
printf '%s' "$B_SRC" >"$tmpdir/b.c"

run() { (cd "$tmpdir" && "$RCC" "$@") >"$tmpdir/log" 2>&1; }

# --- Check 1: both objects carry debug info ---
if ! run --target=6502 --format=coff --debug-map --out=build/a.o a.c; then
    echo "FAIL check1: compile error (a.c)"; cat "$tmpdir/log"; ok=0
fi
if ! run --target=6502 --format=coff --debug-map --out=build/b.o b.c; then
    echo "FAIL check1: compile error (b.c)"; cat "$tmpdir/log"; ok=0
fi
if [ ! -f "$tmpdir/build/a.o.dbgpre" ] || [ ! -f "$tmpdir/build/b.o.dbgpre" ]; then
    echo "FAIL check1: expected both a.o.dbgpre and b.o.dbgpre to exist"; ok=0
fi
if ! run --link --target=6502 --format=raw --debug-map --out=build/prog1.bin build/a.o build/b.o; then
    echo "FAIL check1: link error"; cat "$tmpdir/log"; ok=0
fi
dbg1="$tmpdir/build/prog1.dbgmap"
if [ ! -s "$dbg1" ]; then
    echo "FAIL check1: no non-empty build/prog1.dbgmap produced"; ok=0
else
    if ! grep -q '^FILE 1 a.c$' "$dbg1" || ! grep -q '^FILE 2 b.c$' "$dbg1"; then
        echo "FAIL check1: expected 'FILE 1 a.c' and 'FILE 2 b.c', got:"; cat "$dbg1"; ok=0
    fi
    if ! grep -qE '^SYM [0-9]+ main FUNC$' "$dbg1" || ! grep -qE '^SYM [0-9]+ helper FUNC$' "$dbg1"; then
        echo "FAIL check1: expected SYM records for both main and helper, got:"; cat "$dbg1"; ok=0
    fi
    if ! grep -qE '^LINE [0-9]+ 1 [0-9]+$' "$dbg1" || ! grep -qE '^LINE [0-9]+ 2 [0-9]+$' "$dbg1"; then
        echo "FAIL check1: expected LINE records against both file id 1 and 2, got:"; cat "$dbg1"; ok=0
    fi
    # 1536 (0x0600) is M6502LinkerFactory's own default --format=raw base address (no --base
    # given here) - the same one every other 6502 debug-map test in this suite uses.
    size="$(wc -c <"$tmpdir/build/prog1.bin" | tr -d ' ')"
    base=1536
    bad="$(awk -v base="$base" -v top="$((base + size))" \
        '($1=="SYM"||$1=="LINE"){addr=$2; if (addr<base||addr>=top) print}' "$dbg1")"
    if [ -n "$bad" ]; then
        echo "FAIL check1: record(s) outside the linked image's own [$base,$((base+size))) range:"; echo "$bad"; ok=0
    fi
fi
[ "$ok" -eq 1 ] && echo "PASS check1 (two debug-map-enabled objects linked: two FILE records, correct SYM/LINE)"
[ "$ok" -eq 1 ] || fail=1

# --- Check 2: one object has no sidecar (simulates a runtime library compiled without --debug-map) ---
ok2=1
if ! run --target=6502 --format=coff --out=build/b_nodbg.o b.c; then
    echo "FAIL check2: compile error (b.c, no --debug-map)"; cat "$tmpdir/log"; ok2=0
fi
if [ -f "$tmpdir/build/b_nodbg.o.dbgpre" ]; then
    echo "FAIL check2: b_nodbg.o.dbgpre should not exist"; ok2=0
fi
if ! run --link --target=6502 --format=raw --debug-map --out=build/prog2.bin build/a.o build/b_nodbg.o; then
    echo "FAIL check2: link error"; cat "$tmpdir/log"; ok2=0
fi
dbg2="$tmpdir/build/prog2.dbgmap"
if [ ! -s "$dbg2" ]; then
    echo "FAIL check2: no non-empty build/prog2.dbgmap produced"; ok2=0
else
    if ! grep -q '^FILE 1 a.c$' "$dbg2" || grep -q '^FILE 2' "$dbg2"; then
        echo "FAIL check2: expected exactly one FILE record (a.c only), got:"; cat "$dbg2"; ok2=0
    fi
    if grep -qE 'helper (FUNC|DATA)$' "$dbg2"; then
        echo "FAIL check2: helper (no sidecar) leaked into the combined debug map:"; cat "$dbg2"; ok2=0
    fi
    if ! grep -qE '^SYM [0-9]+ main FUNC$' "$dbg2"; then
        echo "FAIL check2: expected a SYM record for main, got:"; cat "$dbg2"; ok2=0
    fi
fi
[ "$ok2" -eq 1 ] && echo "PASS check2 (object with no sidecar contributes nothing, link still succeeds)"
[ "$ok2" -eq 1 ] || fail=1

# --- Check 3: no --debug-map at link time -> no .dbgmap, even though sidecars exist ---
ok3=1
rm -f "$tmpdir/build/prog3.dbgmap"
if ! run --link --target=6502 --format=raw --out=build/prog3.bin build/a.o build/b.o; then
    echo "FAIL check3: link error"; cat "$tmpdir/log"; ok3=0
fi
if [ -f "$tmpdir/build/prog3.dbgmap" ]; then
    echo "FAIL check3: build/prog3.dbgmap was written despite no --debug-map at link time"; ok3=0
fi
[ "$ok3" -eq 1 ] && echo "PASS check3 (no --debug-map at link -> no .dbgmap, regardless of sidecars on disk)"
[ "$ok3" -eq 1 ] || fail=1

# --- Check 4: byte-identical linked program with and without --debug-map ---
ok4=1
if ! cmp -s "$tmpdir/build/prog1.bin" "$tmpdir/build/prog3.bin"; then
    echo "FAIL check4: --debug-map changed the linked --format=raw output"; ok4=0
fi
[ "$ok4" -eq 1 ] && echo "PASS check4 (byte-identical linked program with/without --debug-map)"
[ "$ok4" -eq 1 ] || fail=1

# --- Check 5: the .o itself is byte-identical with and without --debug-map (sidecar is a side file) ---
ok5=1
if ! run --target=6502 --format=coff --out=build/a_nodbg.o a.c; then
    echo "FAIL check5: compile error (a.c, no --debug-map)"; cat "$tmpdir/log"; ok5=0
fi
if ! cmp -s "$tmpdir/build/a.o" "$tmpdir/build/a_nodbg.o"; then
    echo "FAIL check5: --debug-map changed the compiled --format=coff object"; ok5=0
fi
[ "$ok5" -eq 1 ] && echo "PASS check5 (byte-identical --format=coff object with/without --debug-map)"
[ "$ok5" -eq 1 ] || fail=1

exit $fail
