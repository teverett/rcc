#pragma once

#include "fileformat/FileFormat.h"

namespace rcc {

// `--format=sms`/`--format=gg` (link-output only, same group as Raw/ColecoRom/MsxRom - see
// FileFormat.h): the same bytes `raw` writes, padded with 0xFF up to the nearest of the three
// standard cartridge sizes whose header location is actually documented (8/16/32 KiB -
// smspower.org's own ROM Header page: "The header can be at offset $1ff0, $3ff0 or $7ff0 in the
// ROM" for those three sizes respectively - this writer never produces anything bigger, so the
// 48/64/128/256/512/1024 KiB sizes that same page separately marks "unused"/"buggy"/"rarely
// used" are deliberately not supported here), with the real 16-byte "TMR SEGA" header (same page)
// written at that size's own fixed offset:
//   +0..7   "TMR SEGA" signature (ASCII)
//   +8..9   reserved, written as $00 $00
//   +10..11 checksum, 16-bit little-endian: the plain sum of every byte from offset 0 up to (not
//           including) the header itself - the smspower.org page documents the field's own
//           offset/size/byte order but not the exact summed range; this matches the simple "sum
//           of all bytes up to the beginning of the signature" rule independently documented by
//           the Collapse OS project's own notes on this header, the only other source consulted
//           that states a concrete algorithm.
//   +12..13 product code (BCD, last 4 digits) - always $00 $00, this project has no product code
//   +14     high nibble: product code's 5th digit (hex) - always 0; low nibble: version - always 0
//   +15     high nibble: region code ($4 SMS Export / $6 GG Export - the two this writer
//           produces, picked with `isGameGear`); low nibble: ROM size code ($a 8KB / $b 16KB /
//           $c 32KB)
// Reading validates the "TMR SEGA" signature at the size-appropriate offset, that the file is one
// of the three supported sizes, and that the stored checksum matches.
class SmsRomFormatWriter : public FileFormatWriter {
   public:
    explicit SmsRomFormatWriter(bool isGameGear) : isGameGear_(isGameGear) {}
    std::string write(const ObjectFile &obj) const override;

   private:
    bool isGameGear_;
};

class SmsRomFormatReader : public FileFormatReader {
   public:
    ObjectFile read(const std::string &bytes) const override;
};

}  // namespace rcc
