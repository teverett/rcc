#pragma once

#include "fileformat/FileFormat.h"

namespace rcc {

// `--format=msxrom` (link-output only, same group as Raw/ColecoRom - see FileFormat.h): the same
// bytes `raw` writes, padded with 0xFF up to the nearest standard MSX cartridge ROM size (8/16/32
// KiB - the three sizes every source consulted, including the MSX2 Technical Handbook and the
// msx.org wiki, agrees a plain, unbanked cartridge comes in). Reading validates the "AB" ROM
// signature (see MsxLinkerFactory.cpp's own header comment - that linker's synthesized entry
// object already writes it, the same "header belongs to the entry stub, this format only pads/
// validates" split ColecoRomFormat.h uses) at offset 0 and that the file is one of those three
// sizes - the two things this format checks that plain `raw` doesn't.
class MsxRomFormatWriter : public FileFormatWriter {
   public:
    std::string write(const ObjectFile &obj) const override;
};

class MsxRomFormatReader : public FileFormatReader {
   public:
    ObjectFile read(const std::string &bytes) const override;
};

}  // namespace rcc
