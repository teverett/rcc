#include "VexLinkerFactory.h"

#include "codegen/vex/VexAssembler.h"
#include "codegen/vex/VexCodegen.h"
#include "linker/GenericLinker.h"

namespace rcc {

namespace {
// `vexRuntimeAsm()` (VexCodegen.cpp) always defines every shared scratch cell/runtime routine
// unconditionally here - a separately-compiled user object only ever references them, never
// defines them, since it can't know whether some other object linked alongside it needs them
// too (see that function's own header comment).
std::string entryAsm() {
    return "_start:\n"
           "    lit FN_main\n"
           "    cal\n"
           "    ret\n" +
           vexRuntimeAsm();
}
}  // namespace

std::unique_ptr<TargetLinker> makeVexLinker() {
    GenericLinkerConfig config;
    config.baseAddress = kVexBaseAddress;
    config.machineType = kVexCoffMachine;
    config.entryObject = assembleVexObject(entryAsm());
    config.relocPatchers[kVexRelocAbs32] = RelocKinds::patchAbs32BE;
    config.outputFormats = {"raw", "coff"};
    config.addressUnitBytes = 1;
    config.outputUnitBits = 8;
    return std::make_unique<GenericLinker>(std::move(config));
}

}  // namespace rcc
