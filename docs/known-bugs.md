# Known bugs

Defects that are understood but not yet fixed. Each entry gives the
affected code, the trigger, and the wrong behavior.

## No verified M32C simulator/emulator is wired into `tests/run_tests.sh`

Affects `tests/run_tests.sh`'s `check_m32c()`. No pip-installable Python M32C
core, qemu M32C target, or other emulator was found for this project, so
`check_m32c()` only compiles+assembles every `tests/*.c`/`tests/m32c/*.c`
file and reports a compile result - it never executes the output or checks
a test's own `// EXPECT: N` value. `--target=m32c` program correctness
(beyond "the compiler and assembler accept it") is unverified by this
project's own test suite.

## No verified R32C simulator/emulator is wired into `tests/run_tests.sh`

Affects `tests/run_tests.sh`'s `check_r32c()`. No pip-installable Python R32C
core, qemu R32C target, or other emulator was found for this project, so
`check_r32c()` only compiles+assembles every `tests/*.c`/`tests/r32c/*.c`
file and reports a compile result - it never executes the output or checks
a test's own `// EXPECT: N` value. `--target=r32c` program correctness
(beyond "the compiler and assembler accept it") is unverified by this
project's own test suite - same situation as the `m32c` entry just above,
whose codegen this backend's own `R32cCodegen.cpp` otherwise mirrors.

## `--target=m16c`/`--target=m32c`/`--target=r32c` array elements wider than one word read/write only their first word

Affects `src/codegen/m16c/M16cCodegen.cpp`'s/`src/codegen/m32c/M32cCodegen.cpp`'s/
`src/codegen/r32c/R32cCodegen.cpp`'s `genIndex()`/`genIndexAddr()`: an array element's effective address is
correctly scaled by its real element width, but the actual load/store
through that address always reads/writes exactly one word (`ld`/`st`) or
one byte (`ldb`/`stb`), regardless of the element type's width. A
`long`/`long long` *array* element (not a plain scalar - those go through
the unaffected `Var`/`Assign` word-count-generic path) silently reads/
writes only its low word; the other words are never touched.

Not yet fixed - would need `genIndex()`/`genIndexAddr()` to loop over
`wordCountOf(elementType)` words the way the scalar `Var`/`Assign`/
dereference paths already do.

## `--target=m32c`/`--target=r32c` can't multiply or divide two `long long` values

Affects `src/codegen/m32c/M32cCodegen.cpp`'s/`src/codegen/r32c/R32cCodegen.cpp`'s
`genWideMul()`/`genWideDivMod()`: `long`/`unsigned long` (32 bits) multiply/divide/modulo
use the real `MULL`/`DIVUL`/`DIVXL` 32-bit-pair-register instructions, but
`long long` (64 bits) has no corresponding hardware, so it's a hard
compile error - the same gap `src/codegen/m16c/M16cCodegen.cpp` already has
for every `long`/`long long` multiply/divide (M16C has no 32-bit hardware
multiply/divide at all). `tests/run_tests.sh`'s `is_known_unsupported()`
excludes `types_longlong_divmod.c`/`types_longlong_divmod_repeated.c` for
`m32c`/`r32c` accordingly.

Not yet fixed - would need a hand-written 64-bit multiply/divide routine
(built from 32-bit pieces) verified against a real M32C/R32C simulator,
which isn't available (see this file's own M32C/R32C-simulator entries
above).

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

## PDP-7 codegen still pays the full sign-dispatch cost inline for a variable comparison operand outside the idioms it can prove non-negative

Affects `src/codegen/pdp7/Pdp7Codegen.cpp`'s `emitSignedLtCells()`/
`genCompareCells()`.

`src/optimizers/induction_nonneg/InductionNonNegative.cpp` proves
non-negativity for two narrow, common idioms - a Local slot initialized to
a non-negative literal and only ever incremented by a positive literal
inside the one loop that owns it (the `for(i=0; i<N; i=i+1)` desugaring),
or decremented by a positive literal step whose owning loop's guard
(`i>C`/`i>=C`) already proves `i` stays high enough to absorb that step
without crossing zero (the `for(i=N; i>0; i=i-1)` desugaring) - with no
other write or `&slot` anywhere in the function - letting
`genCompareCells()`'s `ready()` treat such a slot's `Var` read the same
as a literal operand. Separately, `genCompareCells()`'s own
`hasStaticSign()` also recognizes a boolean-valued *other* operand (a
comparison/logical operator, or `!`, which always evaluates to 0 or 1) as
good enough to justify applying an already-known non-negative sign to its
sibling, without needing `simpleCell()` to turn the boolean expression
itself into a ready cell.

Still open: a sign proven across an `if` guard rather than a loop's own
increment/decrement (e.g. `if (x >= 0) { ... use x ... }`), and any value
range narrower than "always non-negative from zero" that isn't one of the
two loop shapes or a boolean-valued expression above, still pay the full
dynamic sign dispatch via the shared `__cmp_lt` subroutine. Closing that
remaining general case would need a real value-range-analysis pass, which
still doesn't exist anywhere in this codebase (no `ValueRange`/range-
tracking code in `src/ir` or `src/codegen`, and no control-flow-sensitive
fact tracking either - only the slot-level `knownNonNegative` flag the two
loop idioms above set) - a substantial undertaking on its own, not
attempted here. Still PDP-7-specific: the PDP-7 has no condition-code/
flags register, so every signed `<` needs sign-based machinery at all,
unlike i386/6502 which get a correct comparison for free from one native
compare instruction regardless of either operand's sign.

## `r10000`/`r4000`'s `long long` support has two distinct, now-root-caused bugs: the divide/modulo loop and a wide call argument's word order

Both were root-caused while porting the identical algorithms to the new
`riscv32` backend (see `src/codegen/riscv32/Riscv32Codegen.cpp`, where
both are fixed) and confirmed by running the fixed and unfixed version
of each against the same inputs; neither is fixed in `MipsCodegen.cpp`
itself here, as that's out of scope for the `riscv32` port this was
found during.

1. `genWideDivMod()`'s own loop counter ("cnt") is initialized to 32
   (`addiu t0,zero,32`), but the bit-serial shift/compare/subtract
   division it implements needs one iteration per bit of the 64-bit
   dividend/divisor pair it's dividing - 64 iterations, not 32. The
   existing comment next to that initialization ("cnt = 32 (per-word,
   so 2*32=64 total)") describes the intent but the code doesn't follow
   through: the loop actually decrements and exits after exactly 32
   passes, so every `long long` `/`/`%` only ever shifts the low 32
   bits of the numerator through the remainder accumulator - any case
   whose result depends on the numerator's or divisor's upper 32 bits
   is wrong. Fix: `addiu t0,zero,32` -> `addiu t0,zero,64`.
2. The `Call` case's own wide-argument push (`stind sp,t1` / `stind
   sp,t0,4` - high word first, low word second) stores the high word at
   the *lower* address and the low word at the *higher* one, but
   `loadOperand()` (used to read any Param slot back, wide or not)
   expects the opposite: low word at the lower address (`ldind
   addrReg,t0` with no displacement), high word at the higher one
   (`ldind addrReg,t1,4`). A `long long` parameter therefore always
   reads back with its two words swapped. Fix: swap the push order to
   `stind sp,t0` / `stind sp,t1,4`.

`tests/run_tests.sh`'s own `is_known_unsupported()` already skip-lists
`types_longlong_divmod.c`/`types_longlong_divmod_repeated.c` (bug 1) and
`types_params_return.c` (bug 2) for `r10000`, attributing all three only
to "a wrong value" rather than to either specific cause above; removing
those three skip-list entries once both one-line fixes land in
`MipsCodegen.cpp` closes this out. `powerpc`'s own identical skip-list
entries (`PowerPCCodegen.cpp`'s wide-divide/wide-call code wasn't
checked against either root cause here) may or may not share one or
both bugs - not investigated.

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

## `--target=cp1610` has no automated execution test

Affects the whole `cp1610` backend (`src/codegen/cp1610/CP1610Codegen.cpp`,
`CP1610Assembler.cpp`). No pip-installable Python CP1610 (the
Intellivision's CPU) core exists. jzIntv, the real open-source
Intellivision emulator, needs copyrighted EXEC/GROM ROM images to boot
a real console - not something this project can legally bundle or
install - so `tests/run_tests.sh --target=cp1610` deliberately fails
loudly with a clear message instead of silently skipping or faking a
pass - see its `cp1610)` tool-check arm. There's no `check_cp1610()`
scaffolded at all, since no real core exists yet to eventually wire
one in against. There's also no example/runtime library for this
target (unlike most other platforms here) for the same reason: no
accessible, simulator-runnable Intellivision machine exists to target
one at.

Verified as far as possible without a real, independent, checked-in
emulator: `make build`'s own build is clean with no warnings;
`tests/cp1610_assembler_test.cpp` asserts exact opcode bytes for every
instruction this backend's dialect uses, cross-checked against MAME's
own `cp1610.cpp`/`1610dasm.cpp` decode tables; and, the same way the
`tms9900`/`z8000` entries elsewhere in this file describe, a small
from-scratch CP1610 interpreter (not checked into this project - built
purely as a development aid, covering only the instruction forms this
backend's codegen actually emits) was used to run real test programs
exercising int arithmetic/comparisons (signed and unsigned), the
software multiply/divide loops, variable-count shifts, bitwise
operators (including the De Morgan `|` synthesis this architecture's
missing `OR` instruction needs), `char` signed/unsigned loads and
stores, pointers, arrays (including a `long` array's own double-width
element stride), structs (including pointer-to-struct member access),
global/local `long` arithmetic, function pointers and computed calls
(built from a direct `r7` write, since this CPU has no register-
indirect call instruction), and recursion with a deep local-variable
frame - all producing correct results (`ctests/test1`/`test2`/`test3`
and `tests/cp1610/*.c`). Several real bugs were found and fixed this
way during development: `r5` turned out to be real hardware auto-
increment addressing (any `[r5]` access already advances `r5` by one
on its own), so this backend's first draft was double-advancing it
with an extra explicit `add 1,r5` between words of a wide load/store;
`widthOfType(t) == 1` can't tell a real `char` apart from a plain
`int` on this word-addressed target the way it does on a byte-
addressed backend (every scalar type up to `int` is 1 native word
here), so the first draft was silently re-truncating every `int`
(and pointer!) to 8 bits; `CMPR`/`SUBR`'s own real carry convention
turned out to be "carry = 1 means no borrow" (the opposite sense this
backend's unsigned comparison branches were first written assuming);
and a computed call's own return address needs to ride in `r5` (`JSR`'s
own real convention, which every prologue's `PSHR r5` already expects),
not be pushed by hand directly. `long`/`long long` multiply/divide
isn't implemented at all (a deliberate scope cut - see
`CP1610Codegen.cpp`'s own `genWideMul()`/`genWideDivMod()` comment,
even more justified here than on the Z8000/PDP-11 backends given this
CPU has no multiply/divide instruction even for a native-width value).
Remove this entry once a suitable Python CP1610 emulator package is
found (or written) and a real `check_cp1610()` is wired into the
dispatch.

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

## `tests/multidecl_array.c`/`tests/multidecl_scalar.c` fail on `--target=stm32`/`i386`/`cortex-m3`

Affects `src/codegen/stm32/STM32Codegen.cpp`, `src/codegen/i386/I386Codegen.cpp`, and
`src/codegen/armm/ArmMCodegen.cpp` (`cortex-m3`), discovered running `make test` after merging
in the comma-separated-declarator-list feature (`grammar/c/CParser.g4`/`src/ast/BuildAst.cpp`/
`src/ir/IRBuilder.cpp` - see these two tests' own header comments). Every other target exercised
(`6502`-family, `pdp1`/`pdp7`, `68000`, `mc68328`, `8080`, `z80`, `8051`, `r32c`/`m32c`,
`riscv32`, `z8000`, `6809`) passes both tests cleanly; only these three fail, and not with a
crash - `multidecl_array.c` returns 340 instead of 339 on `stm32` (one of the uninitialized
locals, `mid` or `s2`, comes back as 1 rather than 0; `cortex-m3` returns a different garbage
value, 1048915), and `multidecl_scalar.c` returns unrelated large garbage values on all three
(28801/28497/1048611 rather than 35), suggesting an uninitialized local or a frame-layout issue
specific to how these backends place a comma-separated declarator list's own several locals,
rather than anything about the shared parser/AST/IR change itself (which neither test exercises
per-target logic for) - and, per `cortex-m3`'s own 2026-11-20 confirmation (identical generated
assembly before and after an unrelated `emit()`-API codegen refactor), not something introduced
by any later per-backend change either. Not yet root-caused - excluded via
`is_known_unsupported()` in the meantime rather than left as an unexplained failure.

## `src/rt/bbc/`'s runtime has no OSARGS, no OSGBPB disc-title/directory functions, and no OSBYTE/OSWORD call whose own result returns in X or Y

Affects `src/rt/bbc/bbc.h`/`bbc.c` (the BBC Micro MOS runtime library for
`--target=6502`). This dialect's own 6502 assembler
(`src/codegen/m6502/M6502Assembler.cpp`) has no `TAX`/`TXA`/`TAY`/`TYA`/
`STX`/`STY` of any kind (only `ldxi`/`ldyi`/`ldxi_hi`/`ldyi_hi`,
compile-time-constant-or-label-only immediate loads, and the
absolute-addressing `ldx`/`ldy` added for this library's own dynamic
file-handle argument - see that file's own `encodeInstr()` comment), so
a value MOS returns in X and/or Y rather than A can never be read back
into C-visible state. OSARGS (`$FFDA`) and OSGBPB's own disc-title/
current-directory/current-library/directory-listing functions (A = 5-8)
all return their own real result that way; every OSBYTE "read a system
variable" call (OSHWM, HIMEM, the VDU variables, INKEY's own time-limited
read) does too. None of these are implemented - only the documented
OSBYTE calls whose effect is the call itself (Escape enable/acknowledge,
keyboard auto-repeat, buffer flushing, vertical sync) are, via
`bbc_osbyte()`'s own generic A/X/Y-in escape hatch.

Not a bug in what's implemented - a deliberate scope cut, logged here
per this project's own documentation policy rather than silently
omitted. Fixing it for real needs either a genuinely wider assembler
extension (real `TAX`/`TAY`/`STX`/`STY` support, well past the narrow,
purpose-built mnemonics every 6502 runtime library here has added so
far) or some other way to get an X/Y-held result into a byte this
dialect's `sta` can actually read.

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

## Memotech MTX has no `rcc` support - its I/O port map/BIOS calls aren't verified against any reachable source

No `--target`/runtime library exists for the Memotech MTX (a documented request: "SEGA Master
System / Game Gear, Memotech MTX, Camputers Lynx" as platforms to add). The VDP (TMS9929A),
SN76489A, and keyboard-matrix port numbers, and whether the machine offers any ROM/monitor call
table the way MSX's BIOS does, were not found in any page this session could reach: the
Wikipedia article and the MEMU emulator's own `docs/index.html` both describe the hardware in
prose (chip names, RAM size) with no port/address tables, a cited "MTX Technical Specifications"
PDF (`mtxinfo.de/TechInfo.pdf`, only reachable via an archive.org mirror) and `primrosebank.net`
(the MEMU docs' own "definitive source of information about Memotech computers") were both
unreachable (TLS error), and the MEMU source itself wasn't fetched. Writing a runtime library
from guessed port numbers would risk silently wrong hardware behavior with nothing in this
project to catch it. Add support once a primary source (the TechInfo PDF, a MEMU source
checkout, or the MTX ROM disassembly) is actually readable.

## Camputers Lynx has no `rcc` support - its video/keyboard/sound hardware isn't verified against any reachable source

No `--target`/runtime library exists for the Camputers Lynx (not the Atari Lynx handheld - same
platform request as the Memotech MTX entry above). Wikipedia names the chips (a Motorola 6845
for video, a 6-bit DAC for sound) but gives no port numbers, video RAM base address, or keyboard
matrix layout; retroisle.com's own "Lynx Board Information"/"Lynx Firmware" sections, the most
likely source for a pinout or ROM disassembly, returned only empty section-index pages to this
session's fetches. MAME's own cassette-format loader (`camplynx_cas.cpp`) describes the tape
block structure in general terms but the source file itself returned HTTP 403 from the mirror
tried. Add support once a primary source (the MAME source via a reachable mirror, or a Lynx
technical manual/disassembly) is actually readable.

## The C grammar rejects an assignment target wrapped in redundant parens

Affects the C grammar/parser (`grammar/c/CParser.g4`/`src/generated/CParser.cpp`), discovered
while writing `src/rt/atari5200/atari5200.c`. An assignment whose left-hand side is a
dereference wrapped in one redundant, otherwise no-op pair of parens -
`(*(unsigned char *)addr) = value;` - fails with "syntax errors, not compiling";
`*(unsigned char *)addr = value;` (no outer parens) parses fine. Reproducible with a two-line
`.c` file against any target.

`expr`'s assignment alternatives (`assignDerefExpr`/`assignIndexExpr`/`assignExpr`/
`assignMemberExpr`/`assignMemberPtrExpr`) each pattern-match the LHS's literal surface syntax
(a leading `*`, a bare `IDENT`, `base.name`, `base->name`) directly in the grammar rather than
accepting any `condExpr` and checking assignability afterward; wrapping the LHS in parens routes
it through `condExpr`'s own generic `'(' expr ')'` alternative instead, which never reconnects
to an `assignOp`. Fixing this needs a generic `lhs=condExpr op=assignOp val=expr` alternative
with the "is this actually an assignable shape" check (unwrapping any redundant parens, then
requiring a deref/index/member/plain-ident) moved into `BuildAst.cpp`, replacing the current
shape-specific alternatives - not yet done; worked around in `atari5200.c`'s own
hardware-register macros by never emitting the parenthesized shape.

## The 6502 backend corrupts a call made right after a `goto`-skipped local reads a global array

Discovered while building the BASIC frontend's Apple ][ example/runtime
(`examples/basic-6502-dos33/`, `src/rt/apple2/basic_apple2.c`): `GOSUB`/`RETURN`'s original
dispatch lowering used a `switch` statement (desugared by `IRBuilder` into a hidden local holding
the switched-on value), and the runtime call immediately following the `switch`'s own matching
`goto` received a corrupted pointer argument on `--target=6502` - e.g. a string literal address
passed to a print call came out pointing at unrelated memory, producing garbage text instead of
the right string. An `int` argument to a call in the same position looked fine, which is what
made this easy to miss at first (the bug is argument-content corruption at one specific call
site, not a structural control-flow bug - the `goto`s and comparisons themselves all land and
evaluate correctly).

Root cause is still open. What's been ruled out by direct reproduction (`--target=6502`,
`--format=raw`, via a two-global `int stack[N]; int sp;` test with no BASIC involved):
- Needs **all** of: a local variable, assigned from a global-array read with a variable index
  (`tmp = stack[sp];`, not a literal index or a literal value), inside a block only reached via a
  forward `goto` that skips over other code first - removing any one of those three (the local
  itself, the array read, or the `goto`-skip) makes the bug disappear.
- Needs **separate compilation**: the exact same source, `#include`-combined into one file and
  compiled directly to `--format=raw` (no object files, no `--link`), runs correctly every time.
  Only compiling it as its own `--format=coff` object and linking it against another
  separately-compiled object (e.g. `apple2.lib`) reproduces it - independent of `--base`/link
  address, so it isn't a base-address-dependent relocation bug in the obvious sense.
- Not a control-flow bug: `--dump-ir`/tracing confirms every `goto` lands on the right label and
  every `if` evaluates the right comparison. Not simply "any local after a `goto`": a local
  initialized from a literal (`tmp = 1;`) in the exact same `goto`-skipped position works fine.
  Not a general multi-call sequencing bug: an arbitrary sequence of plain calls with no local
  variables and no `goto` at all (`print_str(...); print_int(...); print_str(...); ...`) works
  fine, including many calls in a row.
- Inspected and ruled out as the direct cause: `src/ir/IRBuilder.cpp`'s `assignFrameOffsets`
  (frame-slot assignment walks the actual built tree correctly, including through this exact
  shape); `M6502Codegen.cpp`'s `genFunction`/`genEpilogue` (the software frame pointer/stack
  bump-allocator convention is self-consistent and call-balanced by inspection); 6502 label
  numbering (`newLabel()`/`irLabel()` share one counter, but resolve per-object in
  `M6502Assembler.cpp`, so can't collide across separately-linked objects); `GenericLinker.cpp`'s
  `".."` base-shift relocation (takes a trivial, address-independent path for a single-function
  object, which is what the repro uses).

Not fixed. Worked around in `BasicFrontend.cpp`: `GOSUB`/`RETURN`'s dispatch no longer uses
`switch` (which needed the hidden local) or any other local of its own, so it doesn't hit this -
see that file's `appendGosubDispatch` for the `if`/`goto` chain used instead. Revisit with a
from-scratch 6502-only minimal repro (no runtime library, hand-written globals) and a
relocation-table dump of the linked object if this needs root-causing for real.

## `tests/local_cse_convert.c` fails on `powerpc` even without `--optimize`

Discovered 2026-11-20 while independently re-verifying `tests/run_tests.sh --target=powerpc`
after an unrelated change (the `emit()`-API codegen refactor, which this bug predates and is
unrelated to - confirmed by reproducing the identical failure on the pre-refactor `powerpc`
backend via `git stash`). `local_cse_convert.c` (`// EXPECT: 1`) returns 0 instead. Despite the
test's own name/comment (written to exercise `LocalCSE` hoisting a repeated `(int)c + 1` into a
shared temp), the failure is NOT about `LocalCSE`: `tests/run_tests.sh`'s default invocation
(no `--optimize`) never runs `LocalCSE` at all (it's gated behind `--optimize` in
`src/main.cpp`'s `compileOneFile`), so whatever's wrong is in plain, unoptimized `PowerPCCodegen`
handling of an `ir::ExprKind::Convert` from `char` to `int` - not an optimizer-pass interaction.

Not root-caused yet. Not in `tests/run_tests.sh`'s `is_known_unsupported()` skip-list (unlike
`powerpc`'s three documented `long`/`long long` entries nearby in that function, which are a
different, already-triaged issue) - this one slipped through untracked until now. Revisit by
building down from the test's two back-to-back `(int)c + 1` conversions to the single-conversion
case and checking `PowerPCCodegen::genConvert`'s char-to-int sign-extension path under qemu.

## `parrotvm` local array initialization emits garbled/invalid PIR for a literal or symbol element

Discovered 2026-11-20 while converting `ParrotVmCodegen.cpp`'s instruction emission to the
shared `emit()` API (the same mechanical refactor every other text-emitting backend has gone
through) and noticing the mechanical conversion would have silently fixed this as a side effect
- reverted that one spot back to the original broken form specifically so this refactor stays a
pure mechanical one, with the real fix tracked here separately instead.

`genStmt()`'s `StmtKind::LocalDecl` array-initializer case, for element 0 specifically (`offset
== 0`) and separately for a symbol-reference element (`elem.slot`), builds its line as
`code_ << "   " << addr << " = " << slotAddress(...) << "\n";` (and the symbol-reference
equivalent) - `slotAddress()` has its own `code_`-emission side effects (it emits a real
instruction computing the address, then returns the register holding it), and it's used as a
*late operand* of the same `<<` chain that's already written this line's own unterminated
prefix (`"   $I0 = "`) directly into `code_`. C++17 guarantees left-to-right evaluation of
chained `operator<<`, but that's exactly what causes the bug: by the time `slotAddress()` is
evaluated as the chain's 4th operand, the first three have already been flushed into `code_`
unterminated, so `slotAddress()`'s own complete, newline-terminated instruction line gets
spliced into the middle of this still-open line, and the chain's remaining operand (the register
name `slotAddress()` returns) and final `"\n"` land on their own dangling, indentless, invalid
line right after. E.g. for `int a[3] = {1,2,3};`, this produces:
```
   $I0 =    $I1 = $I_fp
$I1
```
instead of the intended
```
   $I1 = $I_fp
   $I0 = $I1
```
Reproduced with `tests/arrays.c`-equivalent BAC source (any local array initializer with a
literal/symbol element, not just a struct/dispatch-table case) - likely emits unparseable PIR on
real Parrot for this shape today. Not fixed. Needs `slotAddress()`'s result captured into a
local `std::string` *before* building the line that uses it (the same fix this refactor's
`emit()`/`cat()` already gives every other converted call site in this file - `cat("= ",
slotAddress(...))` fully evaluates `slotAddress()`'s own side effects before building the
returned string, which is what naturally avoids this exact interleaving everywhere else in
`ParrotVmCodegen.cpp`), applied as its own standalone fix with its own tests, not bundled into
the mechanical refactor.

## `tests/struct_nonpow2_array_stride.c` fails on `avr`

Discovered 2026-11-20 while verifying the `emit()`-API conversion of `AVRCodegen.cpp` - confirmed
pre-existing and unrelated to that refactor (the generated assembly is byte-for-byte identical
before and after the conversion; `tests/run_tests.sh --target=avr` fails it the same way on the
unconverted original). `// EXPECT: 33`, gets 33 via `r16=58 r17=00` misread (wrong value entirely,
not just a wrong address read back consistently - see the test's own header comment on why it
checks the real pointer stride directly for exactly this reason). Likely the same family of
non-power-of-two (`sizeof(struct Three) == 3`) element-stride index-scaling issue other targets
have hit, but not yet confirmed to be the identical root cause - not root-caused for `avr`
specifically. Not in `tests/run_tests.sh`'s `is_known_unsupported()` skip-list yet.
