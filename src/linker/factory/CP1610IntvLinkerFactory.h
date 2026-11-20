#pragma once

#include <memory>

#include "linker/Linker.h"

namespace rcc {

// Builds a GenericLinker for `--target=cp1610-intv`: the same CP1610Codegen/CP1610Assembler as
// plain `cp1610` (see CP1610LinkerFactory.h), but based at the real Mattel Intellivision
// cartridge ROM window ($5000, not the bare target's own disposable $1000 convention) with a
// stack pointer initialized into real console RAM, plus the `intvbin` output format.
std::unique_ptr<TargetLinker> makeCP1610IntvLinker();

}  // namespace rcc
