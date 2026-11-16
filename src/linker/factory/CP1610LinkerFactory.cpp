#include "CP1610LinkerFactory.h"

#include "linker/GenericLinker.h"
#include "codegen/cp1610/CP1610Assembler.h"

namespace rcc {

namespace {
// 0xE000 must match CP1610Codegen.cpp's private kInitialSp (in word units).
constexpr const char *kEntryAsm =
    "org 0\n"
    "_start:\n"
    "    mvii 57344,r6\n"
    "    jsr r5,fn_main\n"
    "halt:\n"
    "    hlt\n";
}  // namespace

std::unique_ptr<TargetLinker> makeCP1610Linker() {
    GenericLinkerConfig config;
    config.baseAddress = kCP1610BaseAddress * 2;
    config.machineType = kCP1610CoffMachine;
    config.entryObject = assembleCP1610Object(kEntryAsm);
    config.relocPatchers[kCP1610RelocAbs16] = RelocKinds::patchAbs16;
    config.addressUnitBytes = 2;
    config.outputUnitBits = 16;
    config.outputFormats = {"raw", "coff", "elf", "aout"};
    return std::make_unique<GenericLinker>(std::move(config));
}

}  // namespace rcc
