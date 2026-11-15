#include "VectrexFormat.h"

#include <array>
#include <cstdint>
#include <stdexcept>

namespace rcc {

namespace {
constexpr std::array<size_t, 4> kStandardSizes = {4096, 8192, 16384, 32768};
constexpr char kSignature[5] = {'g', ' ', 'G', 'C', 'E'};

bool hasSignature(const std::string &bytes) {
    if (bytes.size() < sizeof(kSignature)) return false;
    for (size_t i = 0; i < sizeof(kSignature); i++) {
        if (bytes[i] != kSignature[i]) return false;
    }
    return true;
}
}  // namespace

std::string VectrexRomFormatWriter::write(const ObjectFile &obj) const {
    std::string image(reinterpret_cast<const char *>(obj.textBytes.data()), obj.textBytes.size());
    if (!hasSignature(image)) {
        throw std::runtime_error(
            "VectrexRomFormat: program doesn't start with the cartridge copyright marker "
            "('g GCE') - link against --target=6809-vectrex, not plain 6809");
    }

    size_t padded = 0;
    for (size_t s : kStandardSizes) {
        if (image.size() <= s) {
            padded = s;
            break;
        }
    }
    if (padded == 0) {
        throw std::runtime_error("VectrexRomFormat: program (" + std::to_string(image.size()) +
                                  " bytes) is bigger than the largest non-bankswitched cartridge size "
                                  "(32768 bytes)");
    }

    image.append(padded - image.size(), static_cast<char>(0xFF));
    return image;
}

ObjectFile VectrexRomFormatReader::read(const std::string &bytes) const {
    if (!hasSignature(bytes)) {
        throw std::runtime_error("VectrexRomFormat: missing cartridge copyright marker ('g GCE') at offset 0");
    }
    bool knownSize = false;
    for (size_t s : kStandardSizes) {
        if (bytes.size() == s) knownSize = true;
    }
    if (!knownSize) {
        throw std::runtime_error("VectrexRomFormat: " + std::to_string(bytes.size()) +
                                  " bytes isn't one of the standard cartridge sizes (4096/8192/16384/32768)");
    }

    ObjectFile obj;
    obj.base = 0x0000;
    obj.textBytes.assign(bytes.begin(), bytes.end());
    return obj;
}

}  // namespace rcc
