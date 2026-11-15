#include "VectrexLinkerFactory.h"

#include "linker/GenericLinker.h"
#include "codegen/m6809/M6809Assembler.h"

namespace rcc {

namespace {
// $0000: the Vectrex maps the cartridge there, not RAM - unlike kM6809BaseAddress (0x3000),
// which is a CoCo RAM convention.
constexpr uint32_t kVectrexBaseAddress = 0x0000;

// The GCE Vectrex cartridge header the BIOS reads at $0000 before running anything else (see
// VectrexFormat.h's own comment for the one field - the "g GCE" marker at offset 0 - this
// project actually verifies, and the best-effort caveat on the rest). Field layout and the
// example values below both come from the `cartridge_t` struct documented in gtoal.com's vbcc
// porting notes (https://gtoal.com/vectrex/vide-pi5-cli/vbcc-notes.txt):
//   offset  0, 11 bytes: copyright string, must start "g GCE" and end $80
//   offset 11,  2 bytes: pointer to title music (0 = BIOS default tune)
//   offset 13,  1 byte : title height (signed)
//   offset 14,  1 byte : title width
//   offset 15,  1 byte : title y position (signed)
//   offset 16,  1 byte : title x position (signed)
//   offset 17,  N bytes: title string, ending $80 then $00
// Every rcc-built Vectrex program shares this one boilerplate header/title - there is no way for
// a C program to customize it (same limitation as ColecoLinkerFactory.cpp's own fixed
// sprite-table pointers in the ColecoVision header).
constexpr const char *kEntryAsm =
    "org 0\n"
    "header:\n"
    // "g GCE 2024", terminated by $80
    "    db 0x67, 0x20, 0x47, 0x43, 0x45, 0x20, 0x32, 0x30, 0x32, 0x34, 0x80\n"
    "    dw 0\n"          // music pointer: 0 = BIOS default tune
    "    db 0xF8\n"       // title height: -8
    "    db 0x50\n"       // title width: 80
    "    db 0xF0\n"       // title y: -16
    "    db 0xB8\n"       // title x: -72
    // "RCC", terminated by $80, $00
    "    db 0x52, 0x43, 0x43, 0x80, 0x00\n"
    "_start:\n"
    "    lds 53247\n"
    "    jsr [fn_main]\n"
    "    std [ac]\n"
    "halt:\n"
    "    jmp [halt]\n"
    "ac: dw 0\n"
    "_bc: dw 0\n"
    "cx: dw 0\n"
    "dx: dw 0\n"
    "ptr: dw 0\n";
}  // namespace

std::unique_ptr<TargetLinker> makeVectrexLinker() {
    GenericLinkerConfig config;
    config.baseAddress = kVectrexBaseAddress;
    config.machineType = kM6809CoffMachine;
    config.entryObject = assembleM6809Object(kEntryAsm);
    config.relocPatchers[kM6809RelocAbs16] = RelocKinds::patchAbs16BE;
    config.outputFormats = {"raw", "coff", "elf", "aout", "vectrex"};
    config.addressUnitBytes = 1;
    config.outputUnitBits = 8;
    return std::make_unique<GenericLinker>(std::move(config));
}

}  // namespace rcc
