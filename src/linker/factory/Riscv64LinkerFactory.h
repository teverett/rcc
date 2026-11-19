#pragma once

#include <memory>

#include "linker/Linker.h"

namespace rcc {

// Builds a GenericLinker for riscv64 (RV64I+M), with local patchers for the
// split `lui`/`addi` absolute-address load, `jal`'s 20-bit PC-relative
// field, and the 64-bit data-word relocation `dq` can produce, plus a
// synthesized `_start` entry object assembled like any other object.
std::unique_ptr<TargetLinker> makeRiscv64Linker();

}  // namespace rcc
