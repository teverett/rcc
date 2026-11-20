#include "MsxRomFormat.h"

#include <array>
#include <cstdint>
#include <stdexcept>

namespace rcc {

namespace {
constexpr std::array<size_t, 3> kStandardSizes = {8192, 16384, 32768};

bool hasSignature(const std::string &bytes) {
    return bytes.size() >= 2 && bytes[0] == 'A' && bytes[1] == 'B';
}
}  // namespace

std::string MsxRomFormatWriter::write(const ObjectFile &obj) const {
    std::string image(reinterpret_cast<const char *>(obj.textBytes.data()), obj.textBytes.size());
    if (!hasSignature(image)) {
        throw std::runtime_error(
            "MsxRomFormat: program doesn't start with the \"AB\" cartridge signature - link against "
            "--target=z80-msx, not plain z80");
    }

    size_t padded = 0;
    for (size_t s : kStandardSizes) {
        if (image.size() <= s) {
            padded = s;
            break;
        }
    }
    if (padded == 0) {
        throw std::runtime_error("MsxRomFormat: program (" + std::to_string(image.size()) +
                                  " bytes) is bigger than the largest standard cartridge size (32768 bytes)");
    }

    image.append(padded - image.size(), static_cast<char>(0xFF));
    return image;
}

ObjectFile MsxRomFormatReader::read(const std::string &bytes) const {
    if (!hasSignature(bytes)) {
        throw std::runtime_error("MsxRomFormat: missing \"AB\" cartridge signature at offset 0");
    }
    bool knownSize = false;
    for (size_t s : kStandardSizes) {
        if (bytes.size() == s) knownSize = true;
    }
    if (!knownSize) {
        throw std::runtime_error("MsxRomFormat: " + std::to_string(bytes.size()) +
                                  " bytes isn't one of the standard cartridge sizes (8192/16384/32768)");
    }

    ObjectFile obj;
    obj.base = 0x4000;
    obj.textBytes.assign(bytes.begin(), bytes.end());
    return obj;
}

}  // namespace rcc
