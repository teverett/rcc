#include "IntvBinFormat.h"

#include <stdexcept>

namespace rcc {

namespace {
// 8192 words ($5000-$6FFF inclusive), 2 bytes per word.
constexpr size_t kMaxBytes = 8192 * 2;
}  // namespace

std::string IntvBinFormatWriter::write(const ObjectFile &obj) const {
    std::string image(reinterpret_cast<const char *>(obj.textBytes.data()), obj.textBytes.size());
    if (image.size() % 2 != 0) {
        throw std::runtime_error("IntvBinFormat: program is " + std::to_string(image.size()) +
                                  " bytes, not a whole number of CP1610 words");
    }
    if (image.size() > kMaxBytes) {
        throw std::runtime_error("IntvBinFormat: program (" + std::to_string(image.size() / 2) +
                                  " words) is bigger than the $5000-$6FFF cartridge ROM window "
                                  "(8192 words) - link against --target=cp1610, not cp1610-intv, "
                                  "for a larger freestanding image");
    }
    return image;
}

ObjectFile IntvBinFormatReader::read(const std::string &bytes) const {
    if (bytes.size() % 2 != 0) {
        throw std::runtime_error("IntvBinFormat: " + std::to_string(bytes.size()) +
                                  " bytes isn't a whole number of CP1610 words");
    }
    if (bytes.size() > kMaxBytes) {
        throw std::runtime_error("IntvBinFormat: " + std::to_string(bytes.size() / 2) +
                                  " words is bigger than the $5000-$6FFF cartridge ROM window (8192 words)");
    }
    ObjectFile obj;
    obj.base = 0x5000;
    obj.unitBits = 16;
    obj.textBytes.assign(bytes.begin(), bytes.end());
    return obj;
}

}  // namespace rcc
