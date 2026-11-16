#pragma once

#include <memory>

#include "linker/Linker.h"

namespace rcc {

// Builds a GenericLinker for the Z8000, with `kZ8000RelocAbs16BE` patched via the shared
// RelocKinds::patchAbs16BE (big-endian - see Z8000Assembler.h's own header comment), plus a
// synthesized `_start` entry object assembled like any other object.
std::unique_ptr<TargetLinker> makeZ8000Linker();

}  // namespace rcc
