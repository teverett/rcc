#include "CP1610IntvLinkerFactory.h"

#include "linker/GenericLinker.h"
#include "codegen/cp1610/CP1610Assembler.h"

namespace rcc {

namespace {
// $5000: the conventional unexpanded-Intellivision cartridge ROM base - confirmed against
// MAME's own `src/devices/bus/intv/slot.cpp` cartridge loader, which falls back to loading a
// headerless .bin exactly there (see IntvBinFormat.h's own header comment).
constexpr uint32_t kCP1610IntvBaseAddress = 0x5000;

// R6 (the hardware stack pointer) starts at 864 decimal ($0360): one word past System RAM's own
// confirmed top ($0200-$035F per the Intellivision memory map), growing down into the
// $02F0-$035F range still free once BACKTAB ($0200-$02EF, the console's 20x12 background card
// table - see src/rt/intv/intv.h) is excluded. Deliberately NOT inside Scratchpad RAM
// ($0100-$01EF): this project couldn't independently verify from a primary source whether
// Scratchpad is full 16 bits wide on real hardware, so the stack only ever uses the System RAM
// range this project did confirm is 16 bits wide - see docs/known-bugs.md. CP1610Assembler.h's
// own dialect takes decimal operands only, the same convention CP1610LinkerFactory.cpp's own
// kEntryAsm uses.
constexpr const char *kEntryAsm =
    "org 0\n"
    "_start:\n"
    "    mvii 864,r6\n"
    "    jsr r5,fn_main\n"
    "halt:\n"
    "    hlt\n";
}  // namespace

std::unique_ptr<TargetLinker> makeCP1610IntvLinker() {
    GenericLinkerConfig config;
    config.baseAddress = kCP1610IntvBaseAddress * 2;
    config.machineType = kCP1610CoffMachine;
    config.entryObject = assembleCP1610Object(kEntryAsm);
    config.relocPatchers[kCP1610RelocAbs16] = RelocKinds::patchAbs16;
    config.addressUnitBytes = 2;
    config.outputUnitBits = 16;
    config.outputFormats = {"raw", "coff", "elf", "aout", "intvbin"};
    return std::make_unique<GenericLinker>(std::move(config));
}

}  // namespace rcc
