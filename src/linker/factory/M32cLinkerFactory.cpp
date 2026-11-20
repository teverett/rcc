#include "M32cLinkerFactory.h"

#include "linker/GenericLinker.h"
#include "codegen/m32c/M32cAssembler.h"

namespace rcc {

namespace {
// 57344 (0xE000) must match M32cCodegen.cpp's private kInitialSp.
constexpr const char *kEntryAsm =
    "org 0\n"
    "_start:\n"
    "    mov.w 57344,sp\n"
    "    jsr.w fn_main\n"
    "halt:\n"
    "    jmp halt\n";
}  // namespace

std::unique_ptr<TargetLinker> makeM32cLinker() {
    GenericLinkerConfig config;
    config.baseAddress = kM32cBaseAddress;
    config.machineType = kM32cCoffMachine;
    config.entryObject = assembleM32cObject(kEntryAsm);
    config.relocPatchers[kM32cRelocAbs16] = RelocKinds::patchAbs16;
    config.outputFormats = {"raw", "coff", "elf", "aout"};
    return std::make_unique<GenericLinker>(std::move(config));
}

}  // namespace rcc
