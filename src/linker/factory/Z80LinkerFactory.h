#pragma once

#include <cstdint>
#include <memory>

#include "codegen/z80/Z80Assembler.h"
#include "linker/Linker.h"

namespace rcc {

// Builds a GenericLinker for Z80: Abs16 relocations, and a synthesized entry object (set up the
// stack, `call fn_main`, halt) that is also the only place the backend's shared pseudo-registers
// (ac/_bc/cx/dx/ptr) are defined; every user object only references them via external relocation.
//
// A non-default `baseAddress` links at somewhere other than the ordinary `kZ80BaseAddress`
// ($8000) - needed for `--format=sms`/`--format=gg` (see src/fileformat/smsrom/SmsRomFormat.h),
// since a Master System/Game Gear cartridge's ROM window starts at $0000, not $8000. Safe at any
// address, since this backend's shared pseudo-registers/frame storage live in the entry object's
// own `.data`, placed by the linker wherever the rest of the program lands - there's no fixed
// zero-page-style region baked into the codegen the way `M6502Codegen.h`'s is.
std::unique_ptr<TargetLinker> makeZ80Linker(uint32_t baseAddress = kZ80BaseAddress);

}  // namespace rcc
