#!/usr/bin/env bash
# Unit tests for the 'register' storage-class keyword (grammar/CParser.g4's
# 'reg' token on param/simpleStmt's non-'static' alternatives,
# src/ast/BuildAst.cpp's Param::isRegister/Stmt::isRegister) - see
# docs/language.md's "Register declarations" section for exactly what this
# subset does (and doesn't - it's an accepted-but-unacted-on hint, same as
# real C compilers are free to treat it) with the keyword.
#
# Tests (i386, --format=asm, no simulator needed):
#   test_register_local_compiles             - a 'register' local compiles.
#   test_register_param_compiles             - a 'register' parameter
#       compiles.
#   test_register_with_const_compiles        - 'register' combined with
#       'const' (register first, same fixed order as the grammar's other
#       storage-class-specifier-then-'const' alternatives) compiles.
#   test_register_static_local_rejected      - 'register' combined with
#       'static' on the same local must be a compile error (real C's own
#       two storage-class specifiers are mutually exclusive).
#   test_register_global_rejected            - 'register' on a file-scope
#       global must be a compile error (real C only allows it on locals/
#       parameters).
#
# Usage: tests/run_register_tests.sh [path/to/rcc]

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RCC="$SCRIPT_DIR/../build/bin/rcc"
[ $# -ge 1 ] && RCC="$1"

if [ ! -x "$RCC" ]; then
    echo "rcc binary not found at $RCC - build it first ('make')" >&2
    exit 1
fi

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
fail=0

test_register_local_compiles() {
    local dir="$tmpdir/local"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int main() {
    register int x = 5;
    register int arr[2] = {1, 2};
    return x + arr[0] + arr[1];
}
EOF
    if ! "$RCC" --target=i386 --format=asm --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_register_local_compiles: expected this to compile, but it failed:"
        cat "$dir/log"
        return 1
    fi
    echo "PASS test_register_local_compiles"
    return 0
}

test_register_param_compiles() {
    local dir="$tmpdir/param"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int bump(register int x) {
    return x + 1;
}
int main() {
    return bump(1);
}
EOF
    if ! "$RCC" --target=i386 --format=asm --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_register_param_compiles: expected this to compile, but it failed:"
        cat "$dir/log"
        return 1
    fi
    echo "PASS test_register_param_compiles"
    return 0
}

test_register_with_const_compiles() {
    local dir="$tmpdir/withconst"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int scale(register const int factor, register const int base) {
    return factor * base;
}
int main() {
    register const int limit = 3;
    return scale(limit, 4);
}
EOF
    if ! "$RCC" --target=i386 --format=asm --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_register_with_const_compiles: expected this to compile, but it failed:"
        cat "$dir/log"
        return 1
    fi
    echo "PASS test_register_with_const_compiles"
    return 0
}

test_register_static_local_rejected() {
    local dir="$tmpdir/staticlocal"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
int main() {
    static register int x = 5;
    return x;
}
EOF
    if "$RCC" --target=i386 --format=asm --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_register_static_local_rejected: expected a compile error, but it compiled"
        return 1
    fi
    echo "PASS test_register_static_local_rejected"
    return 0
}

test_register_global_rejected() {
    local dir="$tmpdir/global"
    mkdir -p "$dir"
    cat >"$dir/t.c" <<'EOF'
register int g = 5;
int main() {
    return g;
}
EOF
    if "$RCC" --target=i386 --format=asm --out="$dir/t.s" "$dir/t.c" >"$dir/log" 2>&1; then
        echo "FAIL test_register_global_rejected: expected a compile error, but it compiled"
        return 1
    fi
    echo "PASS test_register_global_rejected"
    return 0
}

test_register_local_compiles || fail=1
test_register_param_compiles || fail=1
test_register_with_const_compiles || fail=1
test_register_static_local_rejected || fail=1
test_register_global_rejected || fail=1

exit $fail
