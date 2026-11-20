#include "fileformat/msxrom/MsxRomFormat.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

rcc::ObjectFile makeObject(size_t textSize) {
    rcc::ObjectFile obj;
    obj.base = 0x4000;
    obj.textBytes.resize(textSize);
    obj.textBytes[0] = 'A';
    obj.textBytes[1] = 'B';
    for (size_t i = 2; i < textSize; i++) obj.textBytes[i] = static_cast<uint8_t>(i & 0xFF);
    return obj;
}

// A program smaller than the smallest standard cartridge size (8192 bytes) is padded with 0xFF
// up to exactly that size, with every original byte preserved at the front.
void testPadsToSmallestStandardSize() {
    rcc::ObjectFile obj = makeObject(100);
    rcc::MsxRomFormatWriter writer;
    std::string bytes = writer.write(obj);

    require(bytes.size() == 8192, "a 100-byte program should pad up to the 8192-byte cartridge size");
    require(std::string(bytes.begin(), bytes.begin() + 100) ==
                std::string(reinterpret_cast<const char *>(obj.textBytes.data()), 100),
            "padding should leave the original bytes untouched");
    for (size_t i = 100; i < bytes.size(); i++) {
        require(static_cast<uint8_t>(bytes[i]) == 0xFF, "the padding itself should be 0xFF");
    }

    rcc::MsxRomFormatReader reader;
    rcc::ObjectFile back = reader.read(bytes);
    require(back.base == 0x4000, "reading back a cartridge image should report the fixed $4000 base");
    require(back.textBytes.size() == 8192, "reading back should keep the padded size");
}

// A program that already exactly fills one standard size needs no padding at all.
void testExactStandardSizeNeedsNoPadding() {
    rcc::ObjectFile obj = makeObject(16384);
    rcc::MsxRomFormatWriter writer;
    std::string bytes = writer.write(obj);
    require(bytes.size() == 16384, "a program that already fills 16384 bytes shouldn't grow");
}

// A program bigger than the largest standard cartridge size (32768 bytes) is rejected outright.
void testOversizedProgramIsRejected() {
    rcc::ObjectFile obj = makeObject(32769);
    rcc::MsxRomFormatWriter writer;
    bool threw = false;
    try {
        writer.write(obj);
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "a program bigger than 32768 bytes should be rejected");
}

// Writing rejects a program that doesn't start with the "AB" cartridge signature - it isn't this
// format's job to add one, only MsxLinkerFactory's own entry object is.
void testWriteRejectsMissingSignature() {
    rcc::ObjectFile obj = makeObject(100);
    obj.textBytes[0] = 0x00;
    obj.textBytes[1] = 0x00;
    rcc::MsxRomFormatWriter writer;
    bool threw = false;
    try {
        writer.write(obj);
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "a program missing the cartridge signature should be rejected");
}

// Reading rejects both a missing signature and a size that isn't one of the three standard ones.
void testReadRejectsMalformedInput() {
    rcc::MsxRomFormatReader reader;

    std::string noSignature(8192, '\0');
    bool threw = false;
    try {
        reader.read(noSignature);
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "reading a file with no cartridge signature should be rejected");

    std::string wrongSize(1000, '\0');
    wrongSize[0] = 'A';
    wrongSize[1] = 'B';
    threw = false;
    try {
        reader.read(wrongSize);
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "reading a file whose size isn't one of the three standard sizes should be rejected");
}

}  // namespace

int main() {
    try {
        testPadsToSmallestStandardSize();
        testExactStandardSizeNeedsNoPadding();
        testOversizedProgramIsRejected();
        testWriteRejectsMissingSignature();
        testReadRejectsMalformedInput();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
