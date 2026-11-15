#pragma once

#include <memory>

#include "linker/Linker.h"

namespace rcc {

// Builds a GenericLinker for vex: Abs32 relocations (big-endian, patching the full 4-byte `LIT`
// field - see VexAssembler.h's own kVexRelocAbs32), and a synthesized entry object (`LIT fn_main
// CAL` then a bare `RET`, exactly VexCodegen.cpp's own whole-program entry sequence - see that
// file's class comment for why a bare `RET` here is this target's own normal-halt convention, not
// an omission).
std::unique_ptr<TargetLinker> makeVexLinker();

}  // namespace rcc
