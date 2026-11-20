#include "DfsFormat.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace rcc {

namespace {
constexpr size_t kSectorSize = 256;
constexpr uint32_t kCatalogSectors = 2;  // track 0, sectors 0-1
constexpr uint32_t kDataStartSector = kCatalogSectors;
constexpr uint32_t kSmallDiscSectors = 400;  // 40 tracks * 10 sectors/track
constexpr uint32_t kLargeDiscSectors = 800;  // 80 tracks * 10 sectors/track
// This writer's own load-address limit: every address a 6502 program can actually be entered at
// fits in 16 bits, so a bigger `obj.base` is rejected outright rather than silently truncated.
constexpr uint32_t kMax16Bit = 0xFFFF;
// The catalogue's load/execution-address and length fields are each 16 bits plus a 2-bit
// extension (18 bits total - see DfsFormat.h's own header comment), wide enough to describe a
// data file bigger than a plain 64KB 6502 address space could ever hold resident at once (DFS
// itself places no such limit on a file's own length, only on what `*RUN` could do with it
// afterward), so the length check below uses this wider limit rather than `kMax16Bit`.
constexpr uint32_t kMax18Bit = 0x3FFFF;

const char kFileName[8] = {'C', 'O', 'D', 'E', ' ', ' ', ' ', '\0'};  // 7 chars + the directory
                                                                        // byte written separately
constexpr char kDirectory = '$';
}  // namespace

std::string DfsFormatWriter::write(const ObjectFile &obj) const {
    if (obj.base > kMax16Bit) {
        throw std::runtime_error("DfsFormat: load address " + std::to_string(obj.base) +
                                  " doesn't fit this writer's 16-bit load-address field");
    }
    uint32_t len = static_cast<uint32_t>(obj.textBytes.size());
    if (len > kMax18Bit) {
        throw std::runtime_error("DfsFormat: program length " + std::to_string(len) +
                                  " doesn't fit the catalogue's 18-bit length field");
    }

    uint32_t dataSectors = static_cast<uint32_t>((len + kSectorSize - 1) / kSectorSize);
    uint32_t sectorsNeeded = kCatalogSectors + dataSectors;
    uint32_t discSectors;
    if (sectorsNeeded <= kSmallDiscSectors) {
        discSectors = kSmallDiscSectors;
    } else if (sectorsNeeded <= kLargeDiscSectors) {
        discSectors = kLargeDiscSectors;
    } else {
        throw std::runtime_error("DfsFormat: program (" + std::to_string(len) +
                                  " bytes) doesn't fit even an 80-track DFS disc image");
    }

    std::string image(static_cast<size_t>(discSectors) * kSectorSize, '\0');
    auto put = [&](size_t offset, uint8_t value) { image[offset] = static_cast<char>(value); };

    // Sector 0: title (left as spaces) and the one file-name entry.
    for (size_t i = 0; i < 8; i++) put(i, ' ');
    for (size_t i = 0; i < 7; i++) put(8 + i, static_cast<uint8_t>(kFileName[i]));
    put(15, static_cast<uint8_t>(kDirectory));  // bit 7 (locked) stays clear

    // Sector 1: title tail, cycle number, file count, boot option/disc size, then entry 0.
    for (size_t i = 0; i < 4; i++) put(kSectorSize + i, ' ');
    put(kSectorSize + 4, 0);  // cycle number
    put(kSectorSize + 5, 8);  // 8 * number of files (one file)
    put(kSectorSize + 6, static_cast<uint8_t>((discSectors >> 8) & 0x3));  // boot option 0, in bits 4-5
    put(kSectorSize + 7, static_cast<uint8_t>(discSectors & 0xFF));
    put(kSectorSize + 8, static_cast<uint8_t>(obj.base & 0xFF));
    put(kSectorSize + 9, static_cast<uint8_t>((obj.base >> 8) & 0xFF));
    put(kSectorSize + 10, static_cast<uint8_t>(obj.base & 0xFF));         // execution address == load address
    put(kSectorSize + 11, static_cast<uint8_t>((obj.base >> 8) & 0xFF));
    put(kSectorSize + 12, static_cast<uint8_t>(len & 0xFF));
    put(kSectorSize + 13, static_cast<uint8_t>((len >> 8) & 0xFF));
    // Packed high-bit extension: start sector bits 8-9 (bits 0-1), load address bits 16-17
    // (bits 2-3), length bits 16-17 (bits 4-5), execution address bits 16-17 (bits 6-7). Only
    // length's own extension can ever be nonzero here: `obj.base` is checked above to fit 16
    // bits, so its own and the (identical) execution address's bits 16-17 are always 0, and
    // `kDataStartSector` (2) never needs bits 8-9.
    uint8_t packedHigh = static_cast<uint8_t>(((len >> 16) & 0x3) << 4);
    put(kSectorSize + 14, packedHigh);
    put(kSectorSize + 15, static_cast<uint8_t>(kDataStartSector & 0xFF));

    size_t dataOffset = static_cast<size_t>(kDataStartSector) * kSectorSize;
    std::copy(obj.textBytes.begin(), obj.textBytes.end(), image.begin() + static_cast<long>(dataOffset));
    return image;
}

ObjectFile DfsFormatReader::read(const std::string &bytes) const {
    if (bytes.size() < 2 * kSectorSize) {
        throw std::runtime_error("DfsFormat: input is shorter than the two-sector catalogue");
    }
    auto byteAt = [&](size_t offset) { return static_cast<uint8_t>(bytes[offset]); };

    uint8_t numFiles = static_cast<uint8_t>(byteAt(kSectorSize + 5) / 8);
    if (numFiles == 0) {
        throw std::runtime_error("DfsFormat: catalogue has no files");
    }

    uint8_t loadLo = byteAt(kSectorSize + 8);
    uint8_t loadHi = byteAt(kSectorSize + 9);
    uint8_t lenLo = byteAt(kSectorSize + 12);
    uint8_t lenHi = byteAt(kSectorSize + 13);
    uint8_t packed = byteAt(kSectorSize + 14);
    uint8_t startLo = byteAt(kSectorSize + 15);

    uint32_t startSector = (static_cast<uint32_t>(packed & 0x3) << 8) | startLo;
    uint32_t loadAddr = (static_cast<uint32_t>((packed >> 2) & 0x3) << 16) | (static_cast<uint32_t>(loadHi) << 8) | loadLo;
    uint32_t length = (static_cast<uint32_t>((packed >> 4) & 0x3) << 16) | (static_cast<uint32_t>(lenHi) << 8) | lenLo;

    size_t dataOffset = static_cast<size_t>(startSector) * kSectorSize;
    if (dataOffset + length > bytes.size()) {
        throw std::runtime_error("DfsFormat: entry 0's data runs past the end of the disc image");
    }

    ObjectFile obj;
    obj.base = loadAddr;
    obj.textBytes.assign(bytes.begin() + static_cast<long>(dataOffset), bytes.begin() + static_cast<long>(dataOffset + length));
    return obj;
}

}  // namespace rcc
