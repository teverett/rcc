#include "MsxLinkerFactory.h"

#include "linker/GenericLinker.h"
#include "codegen/z80/Z80Assembler.h"

namespace rcc {

namespace {
// The MSX BIOS, during its own cartridge scan, looks for the "AB" signature at $4000 and $8000
// in every slot; a header without it is skipped, never called into at all - so this entry
// object's first bytes have to be that header, not code. Field order/offsets (ID, INIT,
// STATEMENT, DEVICE, TEXT - the last three left 0, meaning "none") match the msx.org wiki's
// cartridge-header page and the MSX2 Technical Handbook's own ROM header section; only INIT is
// used here. `0xF380`: a stack top several documented MSX homebrew examples use, safely below
// the BIOS/BASIC work area that starts at `0xF3BA` - unlike the ColecoVision/TRS-80 entry stubs,
// the MSX BIOS is still mapped and "live" (page 0's RAM work area, system variables, interrupt
// handlers) after a cartridge's own INIT routine is called, so this leaves that area alone
// rather than claiming all of page 3 for the stack the way a from-scratch target could.
constexpr const char *kEntryAsm =
    "org 0\n"
    "sig: db 65, 66\n"  // "AB"
    "init: dw _start\n"
    "statement: dw 0\n"
    "device: dw 0\n"
    "text: dw 0\n"
    "_start:\n"
    "    ld sp, 62336\n"  // 0xF380
    "    call fn_main\n"
    "    ld hl, [ac]\n"
    "halt:\n"
    "    jp halt\n"
    "ac: db 0, 0\n"
    "_bc: db 0, 0\n"
    "cx: db 0, 0\n"
    "dx: db 0, 0\n"
    "ptr: db 0, 0\n";
}  // namespace

std::unique_ptr<TargetLinker> makeMsxLinker() {
    GenericLinkerConfig config;
    config.baseAddress = 0x4000;
    config.machineType = kZ80CoffMachine;
    config.entryObject = assembleZ80Object(kEntryAsm);
    config.relocPatchers[kZ80RelocAbs16] = RelocKinds::patchAbs16;
    config.outputFormats = {"raw", "coff", "elf", "aout", "msxrom"};
    return std::make_unique<GenericLinker>(std::move(config));
}

}  // namespace rcc
