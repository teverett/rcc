#pragma once

#include <memory>

#include "linker/Linker.h"

namespace rcc {

// Builds a GenericLinker for the CP1610, with `addressUnitBytes = 2` (this is a word-addressed
// target - see CP1610Assembler.h's own header comment) and `kCP1610RelocAbs16` patched via the
// shared RelocKinds::patchAbs16, plus a synthesized `_start` entry object assembled like any
// other object.
std::unique_ptr<TargetLinker> makeCP1610Linker();

}  // namespace rcc
