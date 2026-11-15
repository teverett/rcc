#pragma once

#include <memory>

#include "linker/Linker.h"

namespace rcc {

// Builds a GenericLinker for `--target=6809-vectrex`: same 6809 instruction set, calling
// convention and Abs16 (big-endian) relocation as the plain `6809` target (M6809LinkerFactory.h),
// but with a different entry object - one that starts with the GCE Vectrex cartridge header the
// BIOS requires at $0000 (see VectrexFormat.h's own header comment) before falling through into
// the usual `lds`/`jsr fn_main`/halt sequence - and a $0000 base address, since the cartridge is
// mapped there rather than loaded into RAM.
std::unique_ptr<TargetLinker> makeVectrexLinker();

}  // namespace rcc
