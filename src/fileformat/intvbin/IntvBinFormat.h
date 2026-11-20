#pragma once

#include "fileformat/FileFormat.h"

namespace rcc {

// `--format=intvbin` (link-output only, same group as Raw/ColecoRom/VectrexRom - see
// FileFormat.h): a bare Mattel Intellivision cartridge ROM dump, the same bytes `raw` writes for
// `--target=cp1610-intv` (CP1610IntvLinkerFactory.cpp's own entry object already starts every
// image at word address $5000, the conventional unexpanded-Intellivision cartridge ROM base -
// confirmed against MAME's own `src/devices/bus/intv/slot.cpp` cartridge loader, which falls
// back to loading a headerless .bin exactly there when no metadata is supplied). Each CP1610
// word is written as two bytes, low byte first - also confirmed against that same MAME loader
// (`fread(&low_byte, 1)` before `fread(&high_byte, 1)` for every word), the one format fact this
// project could verify from a primary, readable source; no other header, trailer or checksum
// exists in this format at all - it's the same flat word dump CP1610Assembler.cpp's own
// `wordsToBytes()` already produces for plain `--target=cp1610`.
//
// This is deliberately the plain `.bin` half of the real toolchain's `.bin`+`.cfg` pair, not the
// pair itself: the `.cfg` side's own grammar has no primary-source specification this project
// could find (not even MAME's own Intellivision driver parses one - see
// `docs/known-bugs.md`), so rcc never emits one. A plain `.bin` fixed at $5000 is still a real,
// loadable cartridge image for an unexpanded console (MAME's and jzIntv's own default mapping
// when no `.cfg`/hashfile metadata is given).
//
// What this format checks that plain `raw` doesn't: the image is an even number of bytes (a
// whole number of CP1610 words) and is no bigger than the 8192-word ($5000-$6FFF) non-bankswitched
// ROM window real, unexpanded Intellivision hardware decodes a cartridge into.
class IntvBinFormatWriter : public FileFormatWriter {
   public:
    std::string write(const ObjectFile &obj) const override;
};

class IntvBinFormatReader : public FileFormatReader {
   public:
    ObjectFile read(const std::string &bytes) const override;
};

}  // namespace rcc
