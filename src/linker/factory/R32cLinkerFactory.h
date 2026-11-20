#pragma once

#include <memory>

#include "linker/Linker.h"

namespace rcc {

// Builds a GenericLinker for the R32C, with a synthesized `_start` entry
// object assembled like any other object so `jsr fn_main` resolves as
// an ordinary external relocation.
std::unique_ptr<TargetLinker> makeR32cLinker();

}  // namespace rcc
