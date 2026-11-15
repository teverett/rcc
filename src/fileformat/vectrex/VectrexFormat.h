#pragma once

#include "fileformat/FileFormat.h"

namespace rcc {

// `--format=vectrex` (link-output only, same group as Raw/ColecoRom/Atari2600Rom - see
// FileFormat.h): a GCE Vectrex cartridge ROM image. The 6809 maps the cartridge at $0000-$7FFF;
// on cold start, the BIOS (in its own $E000-$FFFF ROM, not part of this image) reads a fixed
// header at the very start of the cartridge before ever executing any of it, so - like
// ColecoRomFormat - that header has to be real bytes at the front of `obj.textBytes` already
// (written by VectrexLinkerFactory.cpp's own entry object), not something this writer adds.
//
// MAME's own Vectrex cartridge-slot code (src/devices/bus/vectrex/slot.cpp) rejects any image
// whose first five bytes aren't the literal ASCII "g GCE" copyright marker, which is the one part
// of the header this writer/reader actually checks; the rest of the header (music pointer, title
// position/size, title text) is free-form data the BIOS only uses for the title screen, so this
// format has no further opinion on it. No official GCE technical reference is bundled with this
// project to confirm the remaining header fields byte-for-byte - same best-effort, community-layout
// caveat as src/rt/trs80/'s own header comment.
//
// Pads `obj.textBytes` with $FF up to the smallest of the conventional non-bankswitched cartridge
// sizes (4/8/16/32 KiB) it fits in, the same convention ColecoRomFormat/Atari2600RomFormat use. No
// bankswitching support - a program bigger than 32768 bytes is rejected outright.
class VectrexRomFormatWriter : public FileFormatWriter {
   public:
    std::string write(const ObjectFile &obj) const override;
};

class VectrexRomFormatReader : public FileFormatReader {
   public:
    ObjectFile read(const std::string &bytes) const override;
};

}  // namespace rcc
