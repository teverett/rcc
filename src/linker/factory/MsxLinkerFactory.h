#pragma once

#include <memory>

#include "linker/Linker.h"

namespace rcc {

// Builds a GenericLinker for `--target=z80-msx`: same Z80 instruction set/relocations as
// `makeZ80Linker()` (see Z80LinkerFactory.h), but with an MSX cartridge ROM header - not a plain
// `ld sp,...`/`call fn_main` stub - as the synthesized entry object, since the MSX BIOS looks for
// a fixed "AB" signature and INIT vector at the very start of cartridge ROM space ($4000) before
// it will call into it at all. Same "one ISA, several platform conventions" shape as
// `z80-coleco`.
std::unique_ptr<TargetLinker> makeMsxLinker();

}  // namespace rcc
