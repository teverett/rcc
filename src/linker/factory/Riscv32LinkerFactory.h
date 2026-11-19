#pragma once

#include <memory>

#include "linker/Linker.h"

namespace rcc {

// Builds a GenericLinker for riscv32 (RV32I+M), with local patchers for the
// split `lui`/`addi` absolute-address load and `jal`'s 20-bit PC-relative
// field, plus a synthesized `_start` entry object assembled like any other
// object.
std::unique_ptr<TargetLinker> makeRiscv32Linker();

}  // namespace rcc
