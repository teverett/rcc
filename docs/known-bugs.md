# Known bugs

Defects that are understood but not yet fixed. Each entry gives the
affected code, the trigger, and the wrong behavior.

## `--target=z80-coleco` programs that multiply/divide/modulo/shift-by-variable, or deref through a pointer that spills, write through compiler-internal scratch cells placed in the same ROM-mapped cartridge image as code

Affects `src/codegen/z80/Z80Codegen.cpp`'s `declareTemp()`-based scratch
cells (`ac`/`_bc`/`cx`/`dx`/`ptr`, plus `wideScratch()`'s `a*`/`b*`/
`ret*`/`wacc` - see `genWideMul()`/`genWideDivMod()`/`genWideLoad()`/
`genWideStore()`, all of which read/write these), and
`src/linker/factory/ColecoLinkerFactory.cpp`'s/`Z80LinkerFactory.cpp`'s
shared "lay everything out in one address space starting at
`baseAddress`" model (`GenericLinker.cpp`'s `merge()`, no Harvard
code/data split wired up for this backend - contrast
`GenericLinkerConfig::dataBaseAddress`, which `pic16`/`pic18`/`avr` do
use).

Every Z80 C object - `Z80Assembler.cpp`'s `declareTemp()` emits each
scratch cell as an ordinary `lbl: db 0, 0` inside that same object's own
assembled bytes, not a separately-addressed data segment - gets linked
into the cartridge image at `$8000` and up, same as its own code. On a
real TRS-80 (`z80` target) that's fine: `$8000` is ordinary, writable
RAM there. On a real ColecoVision, `$8000`-`$FFFF` is the cartridge's
own read-only ROM - a write through one of these scratch cells (any
16-bit multiply/divide/modulo/shift-by-a-variable-amount, or a spilled
pointer dereference - see the `Z80Codegen.cpp` function references above)
is a no-op on real hardware instead of actually updating the value,
silently producing a wrong result rather than a crash. `tests/
run_coleco_rt_tests.sh`'s own z80-python-based harness can't catch this
either: its synthetic memory array has no concept of read-only regions,
so a scratch-cell write there "succeeds" even though the same write
would do nothing on real hardware.

Not yet fixed - would need either a real Harvard code(ROM)/data(RAM)
split for this backend (wiring `dataBaseAddress` through
`Z80Codegen.cpp`'s own global/temp placement the way `pic16`/`pic18`/
`avr` already do for their own Flash/RAM split) or some other way to
keep these specific scratch cells out of the cartridge image.
`src/rt/coleco/coleco.c`'s own functions
avoid every operation that would trigger this (no multiply/divide/
modulo/variable-shift, and no pointer spills past a single array
index), so they're themselves unaffected - this only bites a *caller's*
own C code once it does any of those things.

## `--target=z80` programs that write to real memory within roughly 1-2KB of `$FFFF` (while any function is still on the call stack) can silently corrupt their own return addresses/locals and hang

Affects `src/codegen/z80/Z80Codegen.cpp`'s `kStackTop` (hardcoded at
`$FF00` for every z80 platform - `--target=z80`/`z80-coleco` alike;
confirmed no `Variant`-style mechanism exists to relocate it the way
`M6502Codegen.h`'s `frameStackBase()` does for `atari2600`).

The Z80's hardware stack (`CALL`/`RET`/`PUSH`/`POP`, and every Local's/
Param's own `IX`-relative frame slot - see `Z80Codegen.h`'s own header
comment) starts at `$FF00` and grows downward from there, with no fixed
lower bound: at the moment any given instruction runs, the real
in-use stack region is `[SP, $FF00)`, and how far below `$FF00` that
reaches depends entirely on the current call depth and how many
Locals/Params are in scope - there's no compile-time constant a program
could check against. A program that writes to an address close enough
to `$FF00` (confirmed empirically: as little as ~15-250 bytes below it,
scaling with call depth) overwrites its own return address and/or
local variables instead of whatever it meant to write, with no crash or
diagnostic - just silently wrong behavior, typically an infinite loop
once the corrupted return address or loop-counter local takes effect.
`src/rt/trs80/`'s video RAM (`$3C00`-`$3FFF`) and `src/rt/coleco/`'s VDP
(behind ports, not direct memory addresses) sit nowhere near this, so
neither is affected; `src/rt/cpc/`'s screen RAM genuinely does reach
`$FFFF` on real hardware, so `cpc_cls()`/`cpc_set_pixel()` deliberately
leave the last 512 bytes (`$FE00`-`$FFFF`) of that 16KB window
untouched - a comfortable, but still empirical rather than proven,
margin above the observed danger zone - see `src/rt/cpc/cpc.h`'s own
header comment.

Not yet fixed - would need `kStackTop` to become relocatable (the same
kind of `Variant` mechanism `M6502Codegen.h` already has for
`atari2600`'s own zero-page/frame-storage relocation) so a future z80
platform whose own hardware also reaches high memory could move the
stack somewhere genuinely clear of it, or some other way to reserve a
provably-safe margin instead of an empirically-chosen one.

## PDP-7 codegen still pays the full sign-dispatch cost inline for a variable comparison operand outside the one idiom it can prove non-negative

Affects `src/codegen/pdp7/Pdp7Codegen.cpp`'s `emitSignedLtCells()`/
`genCompareCells()`.

`src/optimizers/induction_nonneg/InductionNonNegative.cpp` now proves
non-negativity for one narrow, common idiom - a Local slot initialized to
a non-negative literal and only ever incremented by a positive literal
inside the one loop that owns it, with no other write or `&slot` anywhere
in the function (the `for(i=0; i<N; i=i+1)` desugaring) - letting
`genCompareCells()`'s `ready()` treat such a slot's `Var` read the same
as a literal operand. Every other variable whose sign a human reader can
see never goes negative but doesn't match that exact shape (a decrementing
loop, a sign proven across an `if`, a value range narrower than "always
non-negative from zero") still pays the full dynamic sign dispatch via the
shared `__cmp_lt` subroutine. Closing that general case would need a real
value-range-analysis pass, which still doesn't exist anywhere in this
codebase (no `ValueRange`/range-tracking code in `src/ir` or
`src/codegen` - only the one slot-level `knownNonNegative` flag the narrow
pass above sets) - a substantial undertaking on its own, not attempted
here. Still PDP-7-specific: the PDP-7 has no condition-code/flags
register, so every signed `<` needs sign-based machinery at all, unlike
i386/6502 which get a correct comparison for free from one native compare
instruction regardless of either operand's sign.

## A packed struct can still land at a misaligned absolute address on `mips`/`r10000`

Affects `src/codegen/mips/MipsCodegen.cpp`'s frame-offset/global-placement
handling of a `@packed`-struct-typed slot (see `docs/language.md`'s own
"Structs" section for what `@packed` means).

`IRBuilder.cpp`'s `checkPackedAlignmentSafety()` only rejects a packed
layout where one member's byte offset *relative to the struct's own
start* isn't a multiple of that member's own alignment - it says nothing
about whether the struct's start itself, once placed in a stack frame or
as a global, ends up at an address whose own alignment the backend's
plain `lw`/`sw` can actually use. A packed struct's own `structAlign` is
1 (no padding), so nothing currently pads its frame offset up before
subtracting its exact byte size from `fp`: confirmed via the emitted
asm for `tests/struct_packed_basic.c`'s `struct Mixed @packed { int b;
char a; char c; }` (size 6) - its one local lands at `addiu t2,fp,-6`,
2 bytes short of a 4-byte boundary, and the `int b` member's `stind`
(a `sw`) at that address is a genuine MIPS misaligned-word trap, not
just a wrong value. The same compounds for `tests/struct_packed_array.c`:
even a base address padded to 4 bytes would still put element 1 at
`base+6`, itself 2 bytes off, since nothing pads the packed *size*
either - fixing this for real means either forcing a packed struct's
size up to its natural alignment when it's used as an array element
(which would take back some of what `@packed` is for), or giving MIPS
real unaligned-access codegen for a struct member load/store the way
`pdp7`/`pdp10`/`pdp11` already compose multi-byte access from smaller
pieces rather than trusting a single hardware instruction.

Confirmed via `tests/struct_packed_basic.c` and `tests/struct_packed_array.c`
failing on `r10000`/`mips` with a qemu monitor crash (not a wrong
value - a real trap), while every other simulator-verified backend
(`i386`, `vax`, `pdp7`, `pdp10`, `pdp11`, `armv7a`, `armv8a`/`arm64`,
`armv7a-m`/`cortex-m3`, `powerpc`, `alpha`, `saturn`, `tms320c6713`)
tolerates the same misalignment, either in hardware or via this
codegen's own byte-composed load/store. Not yet fixed; both tests are
skip-listed for `r10000` in `tests/run_tests.sh`'s `is_known_unsupported()`
rather than left as an unexplained failure.

## `tests/run_tests.sh --target=8086`/`8088` has no working test execution

Affects `tests/run_tests.sh`'s `8086|8088)` prerequisites case,
`check_8086()`, and `tests/qemu_wait.py`'s `--8086-image` handling.

`I8086LinkerFactory.cpp`'s entry stub and `I8086Codegen::generate()`'s
single-file entry sequence emit a plain flat binary loaded/entered at
`kI8086BaseAddress` (`0x1000`), with no boot-sector framing of any kind -
`--format=raw` is just `obj.textBytes`, the same convention `flat` uses
everywhere else. `check_8086()` has no way to boot that image under
`qemu-system-i386 -fda` (no `0x55 0xAA` boot signature), so
`tests/run_tests.sh --target=8086`/`8088` exits immediately with an
explanatory message instead of running (and failing) every test. Fixing
it needs a replacement way to get the flat binary running under
`qemu-system-i386` without rcc itself ever constructing a boot sector -
e.g. a tiny bootloader built and owned by the test harness (not rcc) that
chain-loads this binary via `INT 13h` and jumps to `kI8086BaseAddress`,
or switching to a different execution/verification method entirely. Not
yet fixed.

## `--target=6809` and its CoCo runtime have no automated execution test

Affects the whole `6809` backend (`src/codegen/m6809/M6809Codegen.cpp`,
`M6809Assembler.cpp`) and `src/rt/coco/`: no verified pure-Python 6809
CPU core has been found on PyPI (unlike `z80-python`/`py65` for the
Z80/6502 families - see `tests/run_tests.sh`'s own `z80`/`6502` tool-
check arms), so `tests/run_tests.sh --target=6809` deliberately fails
loudly with a clear message instead of silently skipping or faking a
pass - see its `6809)` tool-check arm. `check_6809()` in that script is
scaffolded (same shape as `check_z80()`, reading the result out of `D`
instead of `HL` since the 6809 is big-endian) but not wired into the
dispatch `case` below it, since it can't actually run yet.

Everything else about this target has been verified as far as possible
without execution: `make build`'s own build is clean, `tests/
m6809_assembler_test.cpp` asserts exact opcode bytes against the real
MC6809 instruction encoding, `tests/m6809_link_test.py` covers object-
mode linking, and the generated assembly for the shared `tests/*.c`
battery has been read by hand and looks correct - but none of this is
the same as running real 6809 machine code. Remove this entry once a
suitable Python 6809 emulator package is found and `check_6809()` is
wired into the dispatch.

## `--target=6809-vectrex` programs that multiply/divide/modulo/shift-by-variable, or deref through a pointer that spills, write through compiler-internal scratch cells placed in the same ROM-mapped cartridge image as code

Affects `src/codegen/m6809/M6809Codegen.cpp`'s `declareTemp()`-based
scratch cells (`ac`/`_bc`/`cx`/`dx`/`ptr`, the same ones `VectrexLinkerFactory.cpp`'s
entry object declares right after its own `halt:` loop) and
`GenericLinker.cpp`'s shared "lay everything out in one address space
starting at `baseAddress`" model - the exact same root cause as this
file's own `z80-coleco` entry above, just on the 6809 side.

`6809-vectrex`'s `baseAddress` is `$0000`, the address the Vectrex maps
its cartridge ROM at, so every scratch cell - and every global the linker
places after it - ends up inside that same read-only cartridge image. A
real Vectrex can't write through any of them: any 16-bit multiply/divide/
modulo/shift-by-a-variable-amount, or a spilled pointer dereference (the
operations that use these scratch cells - see `M6809Codegen.cpp`'s own
`genWideMul()`/`genWideDivMod()`/`genWideLoad()`/`genWideStore()`), is a
no-op on real hardware instead of updating the value, silently producing
a wrong result rather than a crash. The plain `6809` target doesn't have
this problem - its `baseAddress` ($3000) is ordinary CoCo RAM.

Not yet fixed - same fix shape as the `z80-coleco` entry: a real Harvard
code(ROM)/data(RAM) split for this backend (wiring
`GenericLinkerConfig::dataBaseAddress` through `M6809Codegen.cpp`'s own
global/temp placement, pointed at the Vectrex's $C800-$CFFF RAM window,
the way `pic16`/`pic18`/`avr` already do for their own Flash/RAM split).
`src/rt/vectrex/vectrex.c`'s own functions don't hit this themselves (no
multiply/divide/variable-shift/spilled-pointer code in them), but any
game logic that does will.

## `--target=6809-vectrex` and its runtime have no automated execution test

Affects the whole `6809-vectrex` target (shares `src/codegen/m6809/
M6809Codegen.cpp`/`M6809Assembler.cpp` with plain `6809` - see that
target's own "no automated execution test" entry above for why no
verified Python 6809 CPU core exists) and `src/rt/vectrex/`: there is no
verified Vectrex emulator wired into this project either (unlike, say,
`py65` for 6502 targets), so `tests/run_tests.sh --target=6809-vectrex`
is compile-only and `examples/6809-vectrex`'s own `make test` only
confirms the cartridge image was produced, not that it does anything
real hardware or an emulator would agree with.

Verified as far as possible without execution: `make build`'s own build
is clean; `tests/vectrex_format_test.cpp` checks the cartridge header/
padding/signature logic byte-for-byte against the documented header
layout; the BIOS call addresses `src/rt/vectrex/vectrex.h` wraps were
cross-checked against two independent community sources where noted
there - but none of this is the same as running real 6809 machine code
against a real (or emulated) Vectrex BIOS. Remove this entry once a
verified Vectrex emulator is found and wired into `run_tests.sh`.

## `--target=6809-vectrex`'s runtime has no text printing, sound, or line drawing

Affects `src/rt/vectrex/vectrex.h`/`vectrex.c`: the BIOS's vector-font
text-printing routines (`Print_Str_*`) need a character-encoding table
this project has no independently verified source for; the AY-3-8912
sound-chip init/play routines need register-level PSG documentation this
project doesn't have verified either; and the low-level timed vector-list
line-drawing routines (`Mov_Draw_VLc_b`/`Mov_Draw_VLcs`) have addresses
confirmed by two sources but a calling convention (the vector-list's own
byte layout) that isn't. Only the single-dot-plot (`Dot_d`), beam-move
(`Moveto_d`), intensity, recalibration/recenter, and random-number BIOS
calls are exposed, plus a best-effort button-read whose exact bit layout
also isn't independently confirmed.

Not a bug in what's implemented - a deliberate scope cut, logged here per
this project's own documentation policy rather than silently omitted.
Fixing it means finding/verifying the missing BIOS calling conventions
(a real GCE technical reference, or a disassembly of the real BIOS ROM,
would resolve all three at once) and extending `vectrex.h`/`vectrex.c`
accordingly.

## `--target=tms9900` has no automated execution test

Affects the whole `tms9900` backend (`src/codegen/tms9900/
TMS9900Codegen.cpp`, `TMS9900Assembler.cpp`). No pip-installable Python
TMS9900 CPU core exists (searched directly - nothing on PyPI, unlike
`z80-python`/`py65` for the Z80/6502 families), so `tests/run_tests.sh
--target=tms9900` deliberately fails loudly with a clear message
instead of silently skipping or faking a pass - see its `tms9900)`
tool-check arm. There's no `check_tms9900()` scaffolded at all (unlike
`check_6809()` above), since no real core exists yet to eventually wire
one in against.

Verified as far as possible without a real, independent, checked-in
emulator: `make build`'s own build is clean with no warnings;
`tests/tms9900_assembler_test.cpp` asserts exact opcode bytes for every
one of the TMS9900's 69 real instructions against its own documented
opcode table (cross-checked against MAME's own `tms9900.cpp` decode
table); and, going a step further than `6809`'s "read by hand" level, a
small from-scratch TMS9900 interpreter (not checked into this project -
built purely as a development aid, covering only the instruction forms
this backend's codegen actually emits) was used to run real test
programs exercising int arithmetic/comparisons (signed and unsigned),
multiply/divide (the real hardware `MPY`/`DIV`), shifts, bitwise
operators (including the `SZC`+`INV` synthesis this architecture's
missing plain-AND instruction needs), pointers, arrays, recursion, and
ternary/logical operators - all producing correct results. Two real
bugs were found and fixed this way (a signed `<=`/`>=` swapped-operand
mix-up in `TMS9900Codegen.cpp`'s own `genCompare()`), along with two
bugs in that disposable interpreter itself (not in this project's own
code). `long`/`long long` isn't implemented at all (a deliberate scope
cut, not a bug - see `TMS9900Codegen.h`'s own header comment); every
code path that would need it throws a clear error instead of emitting
wrong code. Remove this entry once a suitable Python TMS9900 emulator
package is found (or written) and a real `check_tms9900()` is wired
into the dispatch.

## `--target=z8000` has no automated execution test

Affects the whole `z8000` backend (`src/codegen/z8000/Z8000Codegen.cpp`,
`Z8000Assembler.cpp`). No pip-installable Python Z8000 CPU core exists
(searched directly - nothing on PyPI). GDB's own built-in simulator
*can* run Z8000 code (`target sim`, selecting between the unsegmented
Z8002 and segmented Z8001 from the object code itself), but only when
GDB is built from source with that target configured in - not a
package `tests/run_tests.sh` can simply install the way it does
`z80-python`/`py65` for the Z80/6502 families - so this script
deliberately fails loudly with a clear message instead of silently
skipping or faking a pass - see its `z8000)` tool-check arm. There's no
`check_z8000()` scaffolded at all (unlike `check_6809()` above), since
no real core exists yet to eventually wire one in against.

Verified as far as possible without a real, independent, checked-in
emulator: `make build`'s own build is clean with no warnings;
`tests/z8000_assembler_test.cpp` asserts exact opcode bytes for every
instruction this backend's dialect uses, cross-checked against MAME's
own `z8000tbl.hxx`/`8000dasm.cpp` decode tables; and, the same way
`tms9900`'s own entry above describes, a small from-scratch Z8000
interpreter (not checked into this project - built purely as a
development aid, covering only the instruction forms this backend's
codegen actually emits) was used to run real test programs exercising
int arithmetic/comparisons (signed and unsigned), multiply/divide (the
real hardware `MULT`/`DIV` register-pair convention), variable-count
shifts (`rlc`/`rrc`-through-carry), bitwise operators, `char`
signed/unsigned loads and stores (`ldb`/`extsb`), pointers, arrays,
structs (including pointer-to-struct member access), global/local
`long` arithmetic, function pointers and computed calls, and
recursion with a deep local-variable frame - all producing correct
results (`ctests/test1`/`test2`/`test3` and `tests/z8000/*.c`). Several
real register-allocation/operand-order bugs were found and fixed this
way during development (the Z8000's own `op dst,src` convention,
backwards from the PDP-11 dialect's `op src,dst` this backend's design
was initially copied from; and `MULT`/`DIV`'s source operand needing a
register outside the destination pair, unlike this backend's first
draft). `long`/`long long` multiply/divide isn't implemented at all (a
deliberate scope cut, not a bug - see `Z8000Codegen.cpp`'s own
`genWideMul()`/`genWideDivMod()` comment, matching the PDP-11 backend's
own identical cut for the same reason); every code path that would need
it throws a clear error instead of emitting wrong code. Remove this
entry once a suitable Python Z8000 emulator package is found (or
written) and a real `check_z8000()` is wired into the dispatch.

## `tests/hex_literal.c` fails on `--target=8051`/`8052`/`8031`/`8032`/`pdp1`, missing from each target's long-arithmetic skip-list

Affects `tests/run_tests.sh`'s `is_known_unsupported()`. MCS51 deliberately
has no `long`/`long long` arithmetic at all (`MCS51Codegen.cpp`'s own
`err(e.line, "'long'/'long long' arithmetic isn't supported on this
target")`), and `is_known_unsupported()` already skip-lists about twenty
other `tests/*.c` cases for `8051` on exactly that basis (`types_arrays.c`,
`test_long_arith.c`, `unsigned_literal_above_long_max.c`, etc.) - but not
`tests/hex_literal.c`, whose `unsigned long b = 0XFFUL;` hits the same
restriction. Confirmed directly: `./tests/run_tests.sh --target=8051`
fails it with `'long'/'long long' arithmetic isn't supported on this
target`, the same error every skip-listed case above gets, just without
the skip entry. Not yet fixed - needs an `8051:hex_literal.c) return 0
;;` arm alongside the existing ones (and, since the shared MCS51 backend
is identical across part numbers, the same gap presumably applies if
`8052`/`8031`/`8032` are ever added to that function's own `$TARGET`
matching instead of all falling under `8051`'s arms as they do today).

`pdp1` has the identical shape of gap for a different reason:
`Pdp1Codegen.h`'s own class comment explains it has no `long`/`long long`
support either (this one's-complement machine has no link/carry bit at
all, so a correct multi-word carry chain would need its own from-scratch
algorithm, unlike `pdp7`'s two's-complement-with-link architecture), and
`is_known_unsupported()` has no `pdp1` arm for `hex_literal.c` either.
Confirmed directly: `./tests/run_tests.sh --target=pdp1` fails
`tests/hex_literal.c` with `'long'/'long long' local variable isn't
supported on the pdp1 backend: ...`. Not yet fixed - needs a
`pdp1:hex_literal.c) return 0 ;;` arm alongside the `8051` one above.

## `make test`'s Apple II/TRS-80/ColecoVision runtime test scripts break when `$(TARGET)` is a relative path

Affects `tests/run_apple2_format_tests.sh`, `tests/run_apple2_rom_wrapper_tests.sh`,
`tests/run_trs80_rt_tests.sh`, and `tests/run_coleco_rt_tests.sh` - all
four share the same `RCC="$ROOT_DIR/build/bin/rcc"; for arg in "$@"; do
... RCC="$arg" ...; done` pattern, which takes a `path/to/rcc` argument
and stores it verbatim with no absolute-path resolution, then later does
`cd "$tmpdir"` before ever invoking `"$RCC"`. The root `Makefile`'s own
`test` target calls every one of these four with exactly `$(TARGET)` =
`build/bin/rcc` (relative, see `BIN_DIR`/`TARGET` near its top) - so once
the script's own `cd` lands it in a fresh `mktemp -d` directory, that
relative path no longer resolves to anything, and the script dies with
a plain `build/bin/rcc: No such file or directory` (bash's own
"couldn't exec" message, not one of the script's own) before running
any real check.

Confirmed directly: `./tests/run_apple2_format_tests.sh build/bin/rcc`
and `./tests/run_apple2_rom_wrapper_tests.sh build/bin/rcc`, run from the
repo root exactly the way `make test` runs them, both fail this way
(exit 127); `run_trs80_rt_tests.sh`/`run_coleco_rt_tests.sh` take the
same argument the same way and `cd` the same way, but exit earlier on
their own `z80-python not found` check in this environment, so this
wasn't directly confirmed for those two, just inferred from the
identical code shape. This is presumably why it's gone unnoticed: per
the "doesn't run `make test`/`run_tests.sh` in CI" entry above, nothing
in CI ever calls these scripts with a relative path (or at all) to
catch it. Not yet fixed - each script's `for arg in "$@"` loop needs to
resolve `$arg` to an absolute path (e.g. `RCC="$(cd "$(dirname "$arg")"
&& pwd)/$(basename "$arg")"`) before the later `cd "$tmpdir"`, the same
way `ROOT_DIR`/`SCRIPT_DIR` already resolve themselves a few lines
above.

## `src/generated/CParser.cpp`'s `condExprSempred()` triggers `-Wunused-parameter`

Affects `src/generated/CParser.cpp`'s `condExprSempred(CondExprContext
*_localctx, size_t predicateIndex)` - ANTLR's C++ target emits this
helper (one of the generated semantic-predicate dispatchers for
`condExpr`'s operator-precedence alternatives) with a `_localctx`
parameter the generated body never reads, so `-Wall -Wextra` flags it.
Pre-existing in the checked-in `src/generated/CParser.cpp` (reproduces
identically from the parser grammar as it stood before the `const`
qualifier was added - confirmed by compiling that earlier commit's own
checked-in copy standalone), not something any grammar change
introduced; it simply went unnoticed because nothing had forced
`CParser.cpp` to recompile in a while, and `make`'s incremental
object-file caching meant the warning wasn't actually re-surfacing
each build. Not fixed here: `src/generated/` is machine output from
`scripts/generate-grammars.sh` (see the root `Makefile`'s own header
comment), never hand-edited, so the real fix has to be either an
upstream ANTLR C++-target template change or a build-level
`-Wno-unused-parameter` carve-out scoped to `src/generated/` alone -
out of scope for whatever grammar change happens to be the one that
next forces a rebuild of this file.
