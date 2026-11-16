#include "Z8000LinkerFactory.h"

#include "linker/GenericLinker.h"
#include "codegen/z8000/Z8000Assembler.h"

namespace rcc {

namespace {
// 0xE000 must match Z8000Codegen.cpp's private kInitialSp.
constexpr const char *kEntryAsm =
    "org 0\n"
    "_start:\n"
    "    ld r15,57344\n"
    "    call fn_main\n"
    "halt:\n"
    "    halt\n";
}  // namespace

std::unique_ptr<TargetLinker> makeZ8000Linker() {
    GenericLinkerConfig config;
    config.baseAddress = kZ8000BaseAddress;
    config.machineType = kZ8000CoffMachine;
    config.entryObject = assembleZ8000Object(kEntryAsm);
    config.relocPatchers[kZ8000RelocAbs16BE] = RelocKinds::patchAbs16BE;
    config.outputFormats = {"raw", "coff", "elf", "aout"};
    return std::make_unique<GenericLinker>(std::move(config));
}

}  // namespace rcc
