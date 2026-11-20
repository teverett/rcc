#include "SmsRomFormat.h"

#include <array>
#include <cstdint>
#include <stdexcept>

namespace rcc {

namespace {
struct SizeInfo {
    size_t size;
    size_t headerOffset;
    uint8_t sizeCode;
};
constexpr std::array<SizeInfo, 3> kSizes = {{
    {8192, 0x1FF0, 0xA},
    {16384, 0x3FF0, 0xB},
    {32768, 0x7FF0, 0xC},
}};

uint16_t checksumUpTo(const std::string &bytes, size_t headerOffset) {
    uint32_t sum = 0;
    for (size_t i = 0; i < headerOffset; i++) sum += static_cast<uint8_t>(bytes[i]);
    return static_cast<uint16_t>(sum & 0xFFFF);
}
}  // namespace

std::string SmsRomFormatWriter::write(const ObjectFile &obj) const {
    if (obj.base != 0) {
        throw std::runtime_error(
            "SmsRomFormat: program is linked to run at $" + std::to_string(obj.base) +
            ", not $0 - link with --target=z80 --base=0 (the SMS/GG cartridge ROM window starts at "
            "$0000), not plain z80's own default $8000 base");
    }
    std::string image(reinterpret_cast<const char *>(obj.textBytes.data()), obj.textBytes.size());

    const SizeInfo *chosen = nullptr;
    for (const auto &s : kSizes) {
        if (image.size() + 16 <= s.size) {
            chosen = &s;
            break;
        }
    }
    if (!chosen) {
        throw std::runtime_error("SmsRomFormat: program (" + std::to_string(image.size()) +
                                  " bytes) is bigger than the largest supported cartridge size "
                                  "(32768 bytes, header included)");
    }

    image.append(chosen->size - image.size(), static_cast<char>(0xFF));

    size_t h = chosen->headerOffset;
    image.replace(h, 8, "TMR SEGA");
    image[h + 8] = static_cast<char>(0);
    image[h + 9] = static_cast<char>(0);
    uint16_t checksum = checksumUpTo(image, h);
    image[h + 10] = static_cast<char>(checksum & 0xFF);
    image[h + 11] = static_cast<char>((checksum >> 8) & 0xFF);
    image[h + 12] = static_cast<char>(0);  // product code (BCD), low byte
    image[h + 13] = static_cast<char>(0);  // product code (BCD), high byte
    image[h + 14] = static_cast<char>(0);  // high nibble: 5th product-code digit; low: version
    uint8_t regionCode = isGameGear_ ? 0x6 : 0x4;
    image[h + 15] = static_cast<char>(static_cast<uint8_t>(regionCode << 4) | chosen->sizeCode);

    return image;
}

ObjectFile SmsRomFormatReader::read(const std::string &bytes) const {
    const SizeInfo *chosen = nullptr;
    for (const auto &s : kSizes) {
        if (bytes.size() == s.size) chosen = &s;
    }
    if (!chosen) {
        throw std::runtime_error("SmsRomFormat: " + std::to_string(bytes.size()) +
                                  " bytes isn't one of the supported cartridge sizes (8192/16384/32768)");
    }

    size_t h = chosen->headerOffset;
    if (bytes.compare(h, 8, "TMR SEGA") != 0) {
        throw std::runtime_error("SmsRomFormat: missing \"TMR SEGA\" signature at offset " + std::to_string(h));
    }
    uint16_t storedChecksum =
        static_cast<uint8_t>(bytes[h + 10]) | (static_cast<uint8_t>(bytes[h + 11]) << 8);
    if (checksumUpTo(bytes, h) != storedChecksum) {
        throw std::runtime_error(
            "SmsRomFormat: checksum mismatch - bytes before the header don't sum to the stored checksum");
    }

    ObjectFile obj;
    obj.base = 0;
    obj.textBytes.assign(bytes.begin(), bytes.end());
    return obj;
}

}  // namespace rcc
