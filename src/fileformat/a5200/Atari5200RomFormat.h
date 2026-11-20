#pragma once

#include "fileformat/FileFormat.h"

namespace rcc {

// `--format=a5200` (link-output only, same group as Raw/Rim/Ptr/A7out/List/Dos33/ProDos/Cmd/
// ColecoRom/Decb/Prg/AtariExe/Atari2600Rom/NesRom - see FileFormat.h): a bare Atari 5200
// cartridge ROM image. Unlike the Atari 2600 (whose 6507 fetches its reset vector straight out
// of the cartridge's own last six bytes), the 5200's 6502 vectors ($FFFA/$FFFC/$FFFE) live in
// the console's own 2KB System ROM, which jumps to the cartridge only after its own startup
// (enabling VBLANK/DLI NMIs along the way - see M6502Codegen.h's own `Atari5200` comment) via
// an indirect `jmp ($BFFE)` through the cartridge's own 2-byte entry vector at the very top of
// the cartridge window - this writer's `obj.base`.
//
// The cartridge occupies $4000-$BFFF (GTIA starts right above it at $C000), mapped so its last
// byte lands on $BFFF regardless of size - `obj.base` must be one of $B000/$A000/$8000/$4000
// (4K/8K/16K/32K cartridge respectively), any other value is rejected since this writer has no
// way to know which size image an unconventional base is meant to produce (same convention as
// Atari2600RomFormat). No bankswitching - a program bigger than the chosen size (minus the
// 24-byte header below) is rejected outright, not silently truncated or split across banks.
//
// The last 24 bytes of the image are the 5200's own cartridge header, read by the console's
// real BIOS to show a splash screen before running the cartridge (verified against cc65's
// `cfg/atari5200.cfg`, the standard 5200 homebrew toolchain's linker config, which places the
// same three fields at the same addresses):
//   - $BFE8-$BFFB (20 bytes): cartridge name, shown on the splash screen. This writer has no
//     name to put here (ObjectFile carries no such field) and fills it with ASCII spaces.
//   - $BFFC-$BFFD (2 bytes): two ASCII digits, the cartridge's "year". The BIOS treats a second
//     byte of $FF as a "diagnostic cartridge" flag that skips the splash screen and most
//     startup initialization - this writer always writes ASCII "00" so that flag is never set.
//   - $BFFE-$BFFF (2 bytes): CARTENTRY, the cartridge's own entry point - `obj.base` itself,
//     the address the BIOS jumps to once it's done.
//
// Padding (between the end of `obj.textBytes` and the header) is `$FF`, the same convention
// Atari2600RomFormat/ColecoRomFormat already use.
class Atari5200RomFormatWriter : public FileFormatWriter {
   public:
    std::string write(const ObjectFile &obj) const override;
};

class Atari5200RomFormatReader : public FileFormatReader {
   public:
    ObjectFile read(const std::string &bytes) const override;
};

}  // namespace rcc
