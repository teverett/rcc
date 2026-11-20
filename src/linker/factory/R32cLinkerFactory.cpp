#include "R32cLinkerFactory.h"

#include "linker/GenericLinker.h"
#include "codegen/r32c/R32cAssembler.h"

namespace rcc {

namespace {
// 57344 (0xE000) must match R32cCodegen.cpp's private kInitialSp.
constexpr const char *kEntryAsm =
    "org 0\n"
    "_start:\n"
    "    mov.w 57344,sp\n"
    "    jsr.w fn_main\n"
    "halt:\n"
    "    jmp halt\n";
}  // namespace

std::unique_ptr<TargetLinker> makeR32cLinker() {
    GenericLinkerConfig config;
    config.baseAddress = kR32cBaseAddress;
    config.machineType = kR32cCoffMachine;
    config.entryObject = assembleR32cObject(kEntryAsm);
    config.relocPatchers[kR32cRelocAbs32] = RelocKinds::patchAbs32;
    config.outputFormats = {"raw", "coff", "elf", "aout"};
    return std::make_unique<GenericLinker>(std::move(config));
}

}  // namespace rcc
