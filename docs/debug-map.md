# `--debug-map`

A lightweight, plain-text address↔symbol/address↔line debug map, written
to `build/<name>.dbgmap` alongside the compiled program - not real DWARF
(see `docs/known-bugs.md`'s own entry on why DWARF is a poor fit for most
of rcc's targets: most backends emit a platform-native cartridge/ROM/tape
format with no ELF container for DWARF to live in, and those platforms'
real emulators/debuggers don't consume DWARF anyway). This is exactly the
altitude of information those tools actually want: `address <-> line`,
`address <-> symbol`.

38 of rcc's 40 backends implement this today - `pdp7`, `pdp1`, `pdp10`,
`pdp11`, `6502` (also covers `atari2600`/`2a03`/`5200`, which share
`M6502Codegen` via `Variant`), `65c02`, `z80`, `z8000`, `i386`,
`arm`/`arm64`/`armm`, `m68k`/`mc68328`, `mips`, `riscv32`/`riscv64`,
`powerpc`, `sparc`, `stm8`, `mcs51`, `tms9900`, `cp1610`, `i8008`/`i8080`,
`alpha`, `vax`, `avr`, `m16c`/`m32c`/`r32c`, `pic16`, `pic18`, `stm32`,
`vex`, `i8086`, `saturn`, `tms320c6713`, `m6809` (covers both `6809` and
`6809-vectrex`) - for a whole-program-final `--format` (`rim` for
pdp7/pdp1, `raw` for everything else except `pic16`/`pic18`'s own `hex`),
same as always, **and** now also under real separate compilation
(`--format=coff` + `--link`) - see "`--debug-map` under real separate
compilation" below for how a relocatable object's own not-yet-final
addresses get resolved once `--link` actually runs. `pdp1` has no
object/`--link` format at all, so every format it supports gets a debug
map the first way.

Not yet done: `parrotvm` and `moarvm`, both investigated and found to
need more than a port - see "Why not parrotvm/moarvm" below.

## Format

Three record types, one per line:

```
FILE <file-id> <path>
SYM <addr> <name> <kind>      ; kind = FUNC | DATA
LINE <addr> <file-id> <line>  ; in address order
```

`<addr>` is a decimal word address. Today there is always exactly one
`FILE` record (`rcc` compiles one source file per invocation - see
`src/main.cpp`'s own `compileOneFile()` comment), so every `LINE` record's
own `<file-id>` is always `1`.

Example, for a small two-function, one-global program:

```
FILE 1 t.c
SYM 4098 helper FUNC
SYM 4114 main FUNC
SYM 4151 g_counter DATA
LINE 4109 1 3
LINE 4112 1 4
LINE 4119 1 10
```

A `LINE` record's address is where the *first* instruction attributable
to that source line lands - a line whose own statement generates no code
(an uninitialized local declaration, say) simply has no record of its
own; the next line's own marker, emitted right after it in program order
with no instruction in between, resolves to that same address instead
(not a bug - there's genuinely no code in between to attribute).

## Implementation (pdp7)

- `Pdp7Codegen::emitSourceLine()` emits a `__line N` pseudo-instruction
  (not a real pdp7 opcode) immediately before the first instruction of
  each statement, when `--debug-map` was given (`setDebugMapEnabled()`,
  called from `main.cpp` before `generate()` runs) - deduplicated so a
  run of statements sharing one line number (or a statement that itself
  emits no code) doesn't repeat the marker.
- `Pdp7Codegen::debugSymbols_` collects the exact `(label, isFunc)` pairs
  for every real `Function`/`Global` `generate()` emits - deliberately not
  derived from a prefix guess over every label the assembler ever sees,
  since `Pdp7Codegen.h`'s own `label()` helpers prefix plenty of purely
  internal per-function cells (`_P0`, `_LF2`, `_fast`, ...) with that same
  function's own `FN_<name>` label.
- `Pdp7Assembler.cpp`'s `Segment::DebugLine` (the `__line N` pseudo-op,
  intercepted in `buildSegment()` the same way the existing `__root`
  linker pseudo-op is) records `(current address, N)` into the
  assembler's own `debugLines_` table during its real stage-2 pass,
  without storing a word or advancing the program counter - reusing the
  exact same address-resolution logic (`Assembler::resolveLocation()`,
  factored out of `storeWord()`) that every real instruction's own
  address comes from.
- `assemblePdp7DebugMap()` re-assembles the already-generated text (the
  same text every other `--format` assembles) and formats the result.

Since the `__line` markers are pure text with no `storeWord()` call, they
are a verified no-op on the real compiled output: compiling the same
program with and without `--debug-map` produces byte-identical
`--format=rim` bytes (see `tests/run_pdp7_debug_map_tests.sh`'s own
`test_debug_map_identical_binary_output`).

## Implementation (pdp1, 6502, z80)

Same pattern as pdp7, with differences dictated by each target's own
assembler shape (none of these needed a new design decision - just
following `docs/debug-map.md`'s own "Extending to another target"
section below, each one on the first try):

- **pdp1**: no `__root`-style pseudo-op precedent existed (this target has
  no object/`--link` support at all, so nothing marks a `--gc-sections`
  root either) - `Segment::DebugLine` is the first pseudo-op `Pdp1Assembler.cpp`
  intercepts. No `resolveLocation()`-equivalent is needed: this target has
  no RELATIVE-flag/base-shift machinery (every format is whole-program-
  final), so `symbols_.at(".")` is already the real address. `debugMap()`
  has no format to exclude - see `Pdp1Codegen.h`'s own comment.
- **6502**/**z80**: both byte-oriented, two-pass assemblers with no
  `Segment` struct (a `Line::Kind` enum plays that role) and no existing
  pseudo-op precedent either - `__line N` is intercepted in `buildLine()`
  next to `org`/`db`/`dw` (6502 also has `dblo`/`dbhi`). The running
  address is `orgBase_ + offset_`, computed inline wherever needed (no
  `resolveLocation()`-equivalent exists on either, so the debug-line
  handler just reads it directly). Both have five real object formats
  (`coff`/`elf`/`macho`/`aout`/`objraw`) vs. pdp7's one (`coff`), so
  `debugMap()` uses an allowlist (`if (format != "raw") return
  std::nullopt;`) rather than pdp7's denylist. Neither has a
  `--format=list`-style label dump to cross-check a test's SYM addresses
  against (see `tests/run_m6502_debug_map_tests.sh`/
  `tests/run_z80_debug_map_tests.sh`'s own
  `test_debug_map_written_with_correct_records` for how they cope - every
  SYM/LINE address is checked to fall inside the compiled image's own
  address range instead). `6502`'s label-naming convention
  (`"fn_"+name`/`"g_"+name` for Function/Global, with Params/Locals never
  sharing that prefix) means the exact prefix-collision bug pdp7 hit
  doesn't reproduce there - `debugSymbols_` collects the exact list
  anyway, per this file's own "Extending to another target" rule below,
  not because a prefix guess happened to be unsafe on these two
  specifically.
- **2a03**/**atari2600**/**5200**: no extra work - all three share
  `M6502Codegen` with plain `6502` via `M6502Codegen::Variant`, so the
  6502 implementation above covers them; their own `raw`-equivalent base
  address (`$8000` for 2a03's NROM window, etc.) just flows through the
  same `orgBase_`/`labels()` machinery unchanged.

## Implementation (the other 23: pdp10/pdp11/65c02/i386/arm family/m68k family/z8000/mips/riscv32/riscv64/powerpc/sparc/stm8/mcs51/tms9900/cp1610/i8008/i8080/alpha/vax/avr/m16c/m32c/r32c)

Every one of these turned out to share the 6502/z80 `Line::Kind`/
`orgBase_+offset_` assembler shape - confirmed by grep across all of them
for the exact literal text each anchor below needed, not assumed -
letting the whole port (both the assembler-side pseudo-op/side-table/
`writeDebugMap()` plumbing and the codegen-side `debugSymbols_`/
`emitSourceLine()` plumbing) run as two small scripted transformations
instead of 23 one-off ports:

- The `"__line"` interception goes right before each assembler's own
  `line.kind = Line::Instr;` fallback (the trailing `line.mnemonic =
  ...` assignment right after it isn't a safe anchor - `m16c`'s own
  `stripSizeSuffix(mnemonic)` differs from the literal `mnemonic` every
  other target uses there).
- The `DebugLine` enum value is inserted right before the enum's own
  closing `} kind = None;`, not assumed to always follow `Instr` -
  `mcs51`'s own `Xdata` kind is declared after `Instr`.
- The `processLine()` switch's own `case Line::Instr:` body varies
  (`encodeInstr(line.mnemonic, line.operands)` on most, bare
  `encodeInstr(line)` on `pdp10`/`pdp11`), so the new `case
  Line::DebugLine:` is spliced in by matching any single statement
  between `case Line::Instr:` and its own `return;`, not a literal
  string.
- `i386` has no `offset_` member at all (it tracks `bytes_.size()`
  directly via its own `currentAddr()` helper) - the one target out of
  26 that needed a one-line manual fix after the scripted pass, instead
  of `orgBase_ + offset_`.
- `mcs51`/`avr` both route a Global's own `"xdata N"`/`"ram N"`
  reservation through a *second* symbol table (`dataLabels()`) distinct
  from the ordinary code-address `labels()` every `FUNC` symbol (and
  every other target's own Global) resolves through - `writeDebugMap()`
  on both checks `labels()` first, falling back to `dataLabels()`, since
  the exact same `(label, isFunc)` list can resolve through either table
  depending on the target.
- Every one of these 23 has several real object formats
  (`coff`/`elf`/`macho`/`aout`/`objraw`, or a subset), so `debugMap()`
  uses the same `if (format != "raw") return std::nullopt;` allowlist
  6502/z80 do.
- None has a `--format=list`-style label dump, so
  `tests/run_debugmap_allbackends_tests.sh` (one shared script covering
  all 23, rather than 23 near-identical files) checks every SYM/LINE
  address falls inside the compiled `--format=raw` image's own
  `[realBase, realBase+size)` range instead - `realBase` looked up from
  each target's own `kXBaseAddress` constant (`Assembler.h`), not
  assumed to be 0.

## Extending to another target

No single hook covers every backend - each target's own in-process
assembler does its own address bookkeeping. The pattern to repeat,
per target:

1. A debug-map-only pseudo-op analogous to `__line N`, intercepted by
   that target's own assembler the same way an existing linker-only
   pseudo-op (if it has one) already is - or, if none exists (true for
   every target ported after pdp7 so far), added as the first one, next
   to whatever other pseudo-ops (`org`/`db`/...) that assembler already
   intercepts.
2. A side table recorded during that assembler's real address-assignment
   pass - not a separate pass, just one more thing recorded alongside the
   real program-counter bookkeeping it already does. Only word-oriented,
   RELATIVE-flag targets like pdp7 need a `resolveLocation()`-style helper
   to get the real address; a plain `orgBase_+offset_`-tracking assembler
   (pdp1/6502/z80) can read the address directly.
3. `Backend::debugMap()`/`Backend::setDebugMapEnabled()` overrides on that
   target's own `Codegen` class, following `Pdp7Codegen`'s as a template -
   `debugMap()`'s own format check is a denylist when the target has one
   object format (pdp7's `coff`) or none at all (pdp1), an allowlist when
   it has several (6502/z80's five).
4. Collect the exact `(label, isFunc)` symbol list as `generate()` emits
   each real Function/Global - never a prefix guess over every label the
   assembler ever sees, even on a target whose naming convention doesn't
   obviously collide the way pdp7's did (see 6502/z80's own note above).

## Implementation (pic16/pic18/stm32/vex/i8086/saturn/tms320c6713/m6809)

All eight share the `Line::Kind`/running-address assembler shape, so the
port followed this file's own "Extending to another target" section
above directly - no new design needed, just targets that happened to be
scripted-batch outliers (`pic16`/`pic18`/`stm32`, blocked on their own
`--dump-asm`/`perFileAsm_` fix landing first) or individual-treatment
cases (`i8086`/`saturn`/`tms320c6713`, each flagged with a divergent
assembler signature) or simply not yet attempted (`vex`, `m6809` - the
latter missed entirely in the first round through this file, caught only
once every other "standard-shape" backend had already been covered).

- **pic16**/**pic18**/**stm32**: each already had its own hand-rolled
  `--dump-asm`/`perFileAsm_` fix (see `docs/known-bugs.md`'s own former
  entry, now removed) built around a `globalInit`/`ramDecls_`-style side
  stream distinct from `code_` - the debug-map port reused that same
  split: `debugSymbols_` collection sits in the same places the
  `perFileAsm_` fix's own `codeRanges`/`initRanges` tracking does, and
  `pic18`'s inlined object-mode `fn_main` branch (bypassing the shared
  `genFunction(fb, extraPrologue)` helper, for the same reason the
  `perFileAsm_` fix inlined it - to bracket `extraPrologue`'s own splice
  precisely) gets its own `debugSymbols_` push rather than relying on
  `genFunction()`'s.
- **i8086**'s own multi-format `assembleI8086()` turned out not to be a
  real obstacle once read closely - `debugMap()`'s format check is a
  denylist (`coff` is i8086's one real relocatable object; `raw`/`elf`
  are both whole-program-final already, per `assembleInProcess()`'s own
  comment), not the allowlist most multi-format targets use.
- **saturn**'s own `assembleSaturn(asmText, allowExternal)` signature
  likewise wasn't an obstacle: `assembleSaturnDebugMap()` just always
  passes `allowExternal=false`, since a debug map is whole-program-only
  by definition.
- **tms320c6713**'s suspected missing `labels()`/`relocations()`
  accessors turned out to be half right: `labels_` existed as a private
  member with no public accessor (one was added); `relocations()`
  genuinely doesn't apply, since this target has no relocatable object
  format at all (`assembledFormats()` returns only `raw`) - so
  `debugMap()` needs no format check beyond what `main.cpp`'s own
  `isObjectFormat()` gate already provides.
- **saturn**/**m6809** both use the uppercase `"G_"`/`"FN_"` label-prefix
  convention (not every target's lowercase `"g_"`/`"fn_"`) - `writeDebugMap()`'s
  own `substr(isFunc?3:2)` strip works unchanged either way, since the
  prefix *lengths* (not case) are what it depends on.
- **vex** matched the standard shape on the first attempt, no surprises.

Tests: one dedicated script per target (`tests/run_vex_debug_map_tests.sh`,
`tests/run_i8086_debug_map_tests.sh`, `tests/run_saturn_debug_map_tests.sh`,
`tests/run_tms320c6713_debug_map_tests.sh`, `tests/run_m6809_debug_map_tests.sh`)
plus a shared one for the three Harvard-style targets
(`tests/run_debugmap_harvard_tests.sh`, covering `pic16`/`pic18`/`stm32`
with the same 4-check pattern `tests/run_debugmap_allbackends_tests.sh`
uses). Full regression: zero real failures anywhere in this round.

## `--debug-map` under real separate compilation (`--format=coff` + `--link`)

`examples/`'s normal `all`/`test` flow compiles each example's `main.c`
with `--platform=<name>` and no `--link` yet, and `--platform`'s own
resolution (`src/main.cpp`) forces that compile step to the `coff` object
format whenever the target has one - needed so the later `--link` step can
merge it with the runtime library's own `.o` files. `Backend::debugMap()`
only ever produces a file for a whole-program, non-object compile, since a
relocatable object's addresses aren't final until `--link` runs - so
simply appending `--debug-map` to that existing compile line used to be a
silent no-op in every example that links against a runtime library, which
is nearly all of them.

This is now a real, working path instead of a limitation:

- **Compile** (`--format=coff` + `--debug-map`): `Backend::debugMapPreLink()`
  (`Codegen.h`) produces the exact same FILE/SYM/LINE text
  `Backend::debugMap()` does, just relative to this object's own org-0
  `.text` start (pre-link addresses) instead of a final whole-program
  address, and allowing external references (the object may legitimately
  call into a runtime library it hasn't seen yet). `main.cpp` writes this
  to a sidecar file, `<obj>.o.dbgpre`, next to the `.o` - never embedded in
  the real COFF/ELF/etc. bytes, so no file-format reader/writer needed
  changing.
- **Link** (`--link` + `--debug-map`): `main.cpp` reads back any `.dbgpre`
  that exists next to an input `.o` (`readDebugMapPreLink()`) - an object
  compiled without `--debug-map`, e.g. a prebuilt runtime library, simply
  has none, and contributes nothing. `GenericLinker::merge()`
  (`src/linker/GenericLinker.cpp`) shifts every one of that object's own
  SYM/LINE addresses by the exact same per-object/per-section final
  placement offset it already computes for ordinary relocation patching,
  and assigns each debug-info-carrying object its own `FILE` id. The
  result is written to `build/<out>.dbgmap`, formatted by
  `formatLinkedDebugMap()` in `main.cpp`.
- `ObjectFile` (`src/fileformat/ObjectFile.h`) carries this as plain data
  (`debugSourceFile`/`debugFiles`, `debugLines` as `ObjDebugLine{offset,
  line, fileId}`, `debugSymbolNames`) - never populated by any
  `FileFormatReader`, only by `readDebugMapPreLink()` before `merge()`
  runs.
- `--gc-sections` is handled correctly, not specially: a debug line or
  symbol whose own section got dropped is simply left out of the merged
  debug map, the same way `merge()` already drops that section's bytes
  and symbol-table entries.
- Every example's Makefile now passes `--debug-map` on both its compile
  step and its main `--link` step (an alternate-format `--link` variant a
  few examples also build, e.g. `main.raw` alongside `main.prg`, does not
  get it, to avoid two link invocations racing to write the same
  `build/<out-stem>.dbgmap`).

`hosted-hello` and `moarvm-hello` are the two exceptions, for unrelated
reasons: a hosted target's `--link` goes through `linkHosted()`
(`src/hosted/`), not `GenericLinker`, so there's no sidecar-reading hook to
attach to; `moarvm` has no `debugMap()`/`debugMapPreLink()` at all (see
"Why not parrotvm/moarvm" below).

Tests: `tests/run_debugmap_link_tests.sh` (5 checks, `--target=6502`) -
two debug-map-enabled objects linked together, a library object with no
sidecar contributing nothing, no `.dbgmap` without `--debug-map` at link
time even though sidecars exist on disk, and byte-identical output (both
the linked program and the `--format=coff` object itself) with and
without `--debug-map`.

## Why not parrotvm/moarvm

Both were actually investigated this round, not just deferred by
assumption:

- **parrotvm**: `ParrotVmCodegen::assembleInProcess()` for its one format
  (`raw`) is a pure passthrough of the generated PIR text - there is no
  assembler, no address-resolution pass, and so no linear address space
  at all for a `SYM`/`LINE` record's own `<addr>` field to mean anything.
  Parrot interprets PIR directly from source text; whatever internal
  bytecode address Parrot itself assigns is never exposed to rcc. The
  whole FILE/SYM/LINE design assumes a resolved address a debugger steps
  through - that assumption simply doesn't hold here, not a gap this
  backend's own port could close.
- **moarvm**: unlike parrotvm, `MoarVmAssembler` genuinely does lay out
  real per-frame byte offsets (`beginFrame()`/`newLabel()`/
  `placeLabel()`/`finish()`), so a debug map is technically possible -
  but every existing port leans on parsing a `"__line N"` text pseudo-op
  inside a real two-pass text assembler, and this target has no text
  assembly step at all (`MoarVmCodegen` drives `MoarVmAssembler`
  programmatically, method call by method call). Supporting it would
  mean designing a new, bespoke hook (a direct `markSourceLine()`-style
  API call from `MoarVmCodegen` into `MoarVmAssembler`, resolved against
  each frame's own final file offset in `finish()`) rather than porting
  the existing mechanism - a from-scratch feature, not a port - and no
  known external `.moarvm` debugger exists to consume the result even if
  built. Deferred pending an explicit request to design that from
  scratch.
