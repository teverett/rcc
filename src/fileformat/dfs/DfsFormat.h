#pragma once

#include "fileformat/FileFormat.h"

namespace rcc {

// `--format=ssd` (link-output only, same group as Raw/Rim/Ptr/A7out/List/Dos33/ProDos/Cmd/
// ColecoRom/Decb/Prg/AtariExe - see FileFormat.h): a real Acorn DFS single-sided disc image, the
// byte-for-byte ".ssd" convention every DFS-based BBC Micro emulator mounts directly. Unlike
// PrgFormat/AtariExeFormat (a bare load file with no directory of its own), this is a full disc
// image: two 256-byte catalogue sectors (track 0, sectors 0-1 - see the Acorn DFS catalogue
// layout this writer/reader implement below) followed by the file's own data starting at sector
// 2, the first sector past the catalogue.
//
// The catalogue always holds exactly one file, named "CODE" in the root directory ('$'),
// unlocked, with its load and execution address both set to `obj.base` (this convention has no
// separate entry address of its own, the same choice PrgFormat/AtariExeFormat make) - loading it
// on a real/emulated machine still needs an explicit `*LOAD CODE`/`*RUN CODE`, the same deliberate
// "data on a disc, not a disc that runs itself" shape as every other format in this project (no
// boot-option byte is ever set to anything but 0/"no action", so the disc itself does nothing on
// insertion).
//
// Sector 0 layout (file-name half of the catalogue):
//   0-7:    disc title, first 8 characters (always spaces here)
//   8-14:   entry 0's 7-character file name, space-padded ("CODE   ")
//   15:     entry 0's directory character in bits 0-6 ('$'), locked flag in bit 7 (always clear)
//   16-255: unused entries (zero-filled; this writer only ever has one file)
//
// Sector 1 layout (address/length half of the catalogue):
//   0-3:  disc title, last 4 characters (always spaces here)
//   4:    cycle number (BCD; always 0)
//   5:    8 * number of files (always 8, one file)
//   6:    bits 4-5 boot option (always 0, "no action"); bits 0-1 disc size bits 8-9
//   7:    disc size (total sectors on the image), bits 0-7
//   8-9:  entry 0's load address, bits 0-15
//   10-11: entry 0's execution address, bits 0-15
//   12-13: entry 0's length, bits 0-15
//   14:   packed high bits - bits 0-1 start sector bits 8-9; bits 2-3 load address bits 16-17;
//         bits 4-5 length bits 16-17; bits 6-7 execution address bits 16-17. This writer's load
//         address always fits 16 bits and its start sector is always 2, so only the length's own
//         extension (bits 4-5) is ever nonzero - a file can be longer than a 6502's own 64KB
//         address space can hold resident, since DFS places no such limit on a file's length,
//         only on what `*RUN` could do with it afterward.
//   15:   entry 0's start sector, bits 0-7 (always 2 - the first sector past the catalogue)
//
// A disc holds 400 sectors (40 tracks * 10 sectors/track, 100KB) unless the program needs more,
// in which case it grows to the 80-track convention (800 sectors, 200KB) instead; a program that
// still doesn't fit is rejected rather than silently truncated or split across multiple discs.
class DfsFormatWriter : public FileFormatWriter {
   public:
    std::string write(const ObjectFile &obj) const override;
};

class DfsFormatReader : public FileFormatReader {
   public:
    ObjectFile read(const std::string &bytes) const override;
};

}  // namespace rcc
