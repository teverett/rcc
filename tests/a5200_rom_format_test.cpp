#include "fileformat/a5200/Atari5200RomFormat.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

rcc::ObjectFile makeObject(uint32_t base, size_t textSize) {
    rcc::ObjectFile obj;
    obj.base = base;
    obj.textBytes.resize(textSize);
    for (size_t i = 0; i < textSize; i++) obj.textBytes[i] = static_cast<uint8_t>(i & 0xFF);
    return obj;
}

// A program based at $8000 (16K cartridge) pads to exactly 16384 bytes with $FF, then the
// 24-byte header (name/year/CARTENTRY) occupies the last 24 bytes, with CARTENTRY pointing at
// the program's own base.
void testRoundTrip16K() {
    rcc::ObjectFile obj = makeObject(0x8000, 300);
    rcc::Atari5200RomFormatWriter writer;
    std::string bytes = writer.write(obj);
    require(bytes.size() == 16384, "a 16K cartridge image must be exactly 16384 bytes");

    for (size_t i = 300; i < 16384 - 24; i++) {
        require(static_cast<uint8_t>(bytes[i]) == 0xFF, "unused space should be padded with $FF");
    }
    for (size_t i = 16384 - 24; i < 16384 - 4; i++) {
        require(static_cast<uint8_t>(bytes[i]) == ' ', "the 20-byte name field should be blank-filled");
    }
    require(bytes[16384 - 4] == '0' && bytes[16384 - 3] == '0', "the year field should be ASCII \"00\"");
    uint32_t entry = static_cast<uint8_t>(bytes[16384 - 2]) | (static_cast<uint8_t>(bytes[16384 - 1]) << 8);
    require(entry == 0x8000, "CARTENTRY should point at the program's own base");

    rcc::Atari5200RomFormatReader reader;
    rcc::ObjectFile back = reader.read(bytes);
    require(back.base == 0x8000, "round trip lost CARTENTRY's own base address");
    require(back.textBytes.size() == 16384, "reader should return the whole padded image, not trim it");
}

// Every standard cartridge size (4K/8K/16K/32K) maps to its own base address, ending at $BFFF.
void testEveryStandardSize() {
    struct {
        uint32_t base;
        size_t size;
    } cases[] = {{0xB000, 4096}, {0xA000, 8192}, {0x8000, 16384}, {0x4000, 32768}};
    for (const auto &c : cases) {
        rcc::ObjectFile obj = makeObject(c.base, 10);
        rcc::Atari5200RomFormatWriter writer;
        std::string bytes = writer.write(obj);
        require(bytes.size() == c.size, "wrong image size for this base address");
        require(c.base + bytes.size() == 0xC000, "cartridge image should end exactly at $C000 (GTIA)");
    }
}

void writerRejects(uint32_t base, size_t textSize, const char *why) {
    rcc::ObjectFile obj = makeObject(base, textSize);
    rcc::Atari5200RomFormatWriter writer;
    try {
        writer.write(obj);
    } catch (const std::runtime_error &) {
        return;
    }
    throw std::runtime_error(std::string("should have rejected: ") + why);
}

void testWriterRejections() {
    writerRejects(0x1000, 10, "a base address other than $B000/$A000/$8000/$4000");
    writerRejects(0x8000, 16384, "a program with no room left for the 24-byte header");
}

void readerRejects(const std::string &bytes, const char *why) {
    rcc::Atari5200RomFormatReader reader;
    try {
        reader.read(bytes);
    } catch (const std::runtime_error &) {
        return;
    }
    throw std::runtime_error(std::string("should have rejected: ") + why);
}

void testReaderRejections() {
    readerRejects(std::string(16383, '\0'), "a size that isn't 4096/8192/16384/32768 bytes");
    readerRejects(std::string(100, '\0'), "a tiny input far short of any cartridge size");
}

}  // namespace

int main() {
    try {
        testRoundTrip16K();
        testEveryStandardSize();
        testWriterRejections();
        testReaderRejections();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
