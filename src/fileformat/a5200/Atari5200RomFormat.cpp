#include "Atari5200RomFormat.h"

#include <array>
#include <cstdint>
#include <stdexcept>

namespace rcc {

namespace {
constexpr std::array<uint32_t, 4> kBases = {0xB000, 0xA000, 0x8000, 0x4000};
constexpr size_t kHeaderBytes = 24;      // name(20) + year(2) + CARTENTRY(2)
constexpr size_t kNameBytes = 20;
constexpr size_t kEntryOffset = 22;       // relative to header start

size_t cartSizeForBase(uint32_t base) {
    for (uint32_t b : kBases) {
        if (b == base) return 0xC000 - b;
    }
    throw std::runtime_error(
        "Atari5200RomFormat: base address must be $B000 (4K cartridge), $A000 (8K), $8000 (16K) "
        "or $4000 (32K), got " +
        std::to_string(base));
}
}  // namespace

std::string Atari5200RomFormatWriter::write(const ObjectFile &obj) const {
    size_t cartSize = cartSizeForBase(obj.base);
    if (obj.textBytes.size() > cartSize - kHeaderBytes) {
        throw std::runtime_error("Atari5200RomFormat: program (" + std::to_string(obj.textBytes.size()) +
                                  " bytes) doesn't fit in a " + std::to_string(cartSize) +
                                  "-byte cartridge once the 24-byte header is reserved - no bankswitching "
                                  "support");
    }
    std::string out;
    out.reserve(cartSize);
    out.append(reinterpret_cast<const char *>(obj.textBytes.data()), obj.textBytes.size());
    out.append(cartSize - kHeaderBytes - obj.textBytes.size(), static_cast<char>(0xFF));

    out.append(kNameBytes, ' ');                // cartridge name: blank
    out.push_back('0');                         // year, digit 1
    out.push_back('0');                         // year, digit 2 (never $FF - not a diagnostic cart)
    out.push_back(static_cast<char>(obj.base & 0xFF));
    out.push_back(static_cast<char>((obj.base >> 8) & 0xFF));
    return out;
}

ObjectFile Atari5200RomFormatReader::read(const std::string &bytes) const {
    bool knownSize = false;
    for (uint32_t b : kBases) {
        if (bytes.size() == 0xC000 - b) knownSize = true;
    }
    if (!knownSize) {
        throw std::runtime_error("Atari5200RomFormat: " + std::to_string(bytes.size()) +
                                  " bytes isn't a standard cartridge size (4096/8192/16384/32768)");
    }
    if (bytes.size() < kHeaderBytes) {
        throw std::runtime_error("Atari5200RomFormat: image too small to hold the 24-byte cartridge header");
    }

    size_t headerStart = bytes.size() - kHeaderBytes;
    size_t entryOffset = headerStart + kEntryOffset;
    ObjectFile obj;
    obj.base = static_cast<uint8_t>(bytes[entryOffset]) | (static_cast<uint8_t>(bytes[entryOffset + 1]) << 8);
    obj.textBytes.assign(bytes.begin(), bytes.end());
    return obj;
}

}  // namespace rcc
