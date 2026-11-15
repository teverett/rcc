#include "fileformat/vectrex/VectrexFormat.h"

#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

std::string makeHeader() {
    std::string h;
    h.push_back(0x67);
    h.push_back(0x20);
    h.push_back(0x47);
    h.push_back(0x43);
    h.push_back(0x45);
    h.push_back(0x20);
    h.push_back('2');
    h.push_back('0');
    h.push_back('2');
    h.push_back('4');
    h.push_back(static_cast<char>(0x80));
    h.push_back(0x00);
    h.push_back(0x00);  // music ptr
    h.push_back(static_cast<char>(0xF8));
    h.push_back(0x50);
    h.push_back(static_cast<char>(0xF0));
    h.push_back(static_cast<char>(0xB8));
    h.push_back('R');
    h.push_back('C');
    h.push_back('C');
    h.push_back(static_cast<char>(0x80));
    h.push_back(0x00);
    return h;
}

rcc::ObjectFile makeObject(size_t codeSize) {
    rcc::ObjectFile obj;
    obj.base = 0x0000;
    std::string header = makeHeader();
    obj.textBytes.assign(header.begin(), header.end());
    for (size_t i = 0; i < codeSize; i++) obj.textBytes.push_back(static_cast<uint8_t>(i & 0xFF));
    return obj;
}

// A program with the real header round-trips byte for byte and pads to the smallest standard
// cartridge size with $FF, matching ColecoRomFormat/Atari2600RomFormat's own padding convention.
void testRoundTripAndPadding() {
    rcc::ObjectFile obj = makeObject(100);
    rcc::VectrexRomFormatWriter writer;
    std::string bytes = writer.write(obj);
    require(bytes.size() == 4096, "a small program should pad up to the smallest standard size (4096)");
    require(bytes.compare(0, obj.textBytes.size(),
                           std::string(reinterpret_cast<const char *>(obj.textBytes.data()), obj.textBytes.size())) == 0,
            "the program's own bytes must come first, unpadded");
    for (size_t i = obj.textBytes.size(); i < bytes.size(); i++) {
        require(static_cast<uint8_t>(bytes[i]) == 0xFF, "padding bytes must be $FF");
    }

    rcc::VectrexRomFormatReader reader;
    rcc::ObjectFile back = reader.read(bytes);
    require(back.base == 0x0000, "the cartridge is mapped at $0000");
    require(back.textBytes.size() == bytes.size(), "the reader keeps every byte, including padding");
}

// Exercises every standard cartridge size boundary: exactly at a size, and one byte over it
// (which must bump up to the next size, or fail past the largest one).
void testSizeBoundaries() {
    {
        rcc::ObjectFile obj = makeObject(4096 - makeHeader().size());
        rcc::VectrexRomFormatWriter writer;
        require(writer.write(obj).size() == 4096, "exactly 4096 bytes of program should not pad further");
    }
    {
        rcc::ObjectFile obj = makeObject(4096 - makeHeader().size() + 1);
        rcc::VectrexRomFormatWriter writer;
        require(writer.write(obj).size() == 8192, "4096+1 bytes must bump up to the next standard size (8192)");
    }
    {
        rcc::ObjectFile obj = makeObject(32768 - makeHeader().size());
        rcc::VectrexRomFormatWriter writer;
        require(writer.write(obj).size() == 32768, "exactly 32768 bytes of program should not pad further");
    }
    {
        rcc::ObjectFile obj = makeObject(32768 - makeHeader().size() + 1);
        rcc::VectrexRomFormatWriter writer;
        bool threw = false;
        try {
            writer.write(obj);
        } catch (const std::runtime_error &) {
            threw = true;
        }
        require(threw, "a program bigger than the largest standard size (32768) must be rejected, not truncated");
    }
}

// The writer refuses a program that doesn't start with the real "g GCE" cartridge marker,
// instead of silently shipping a cartridge the BIOS would reject.
void testWriterRejectsMissingSignature() {
    rcc::ObjectFile obj;
    obj.base = 0x0000;
    obj.textBytes = {0, 1, 2, 3, 4, 5};
    rcc::VectrexRomFormatWriter writer;
    bool threw = false;
    try {
        writer.write(obj);
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "missing the 'g GCE' marker must be rejected");
}

// The reader rejects a truncated marker, a corrupted marker, and a file whose length isn't one
// of the standard cartridge sizes.
void testReaderRejectsMalformedInput() {
    rcc::VectrexRomFormatReader reader;

    {
        bool threw = false;
        try {
            reader.read("g GC");  // truncated, only 4 of the 5 marker bytes
        } catch (const std::runtime_error &) {
            threw = true;
        }
        require(threw, "a truncated marker must be rejected");
    }
    {
        std::string bad = "X GCE";
        bad.resize(4096, '\xFF');
        bool threw = false;
        try {
            reader.read(bad);
        } catch (const std::runtime_error &) {
            threw = true;
        }
        require(threw, "a corrupted marker byte must be rejected");
    }
    {
        std::string wrongSize = makeHeader();
        wrongSize.resize(5000, '\xFF');  // not one of 4096/8192/16384/32768
        bool threw = false;
        try {
            reader.read(wrongSize);
        } catch (const std::runtime_error &) {
            threw = true;
        }
        require(threw, "a length that isn't a standard cartridge size must be rejected");
    }
}

// A byte sequence taken directly from the cartridge header layout documented in gtoal.com's vbcc
// porting notes (see VectrexLinkerFactory.cpp's own header comment for the field layout this
// mirrors), not merely re-deriving whatever the writer itself produces.
void testAgainstDocumentedHeaderLayout() {
    std::string header = makeHeader();
    require(header.size() == 22, "11-byte copyright + 2-byte music ptr + 4 title position/size bytes + 5-byte title");
    require(static_cast<uint8_t>(header[0]) == 0x67 && header[1] == ' ' && header[2] == 'G' && header[3] == 'C' &&
                header[4] == 'E',
            "the first five bytes must be the literal 'g GCE' marker MAME's own cartridge-slot code checks for");
    require(static_cast<uint8_t>(header[10]) == 0x80, "the copyright field ends with $80");
    require(static_cast<uint8_t>(header[20]) == 0x80 && static_cast<uint8_t>(header[21]) == 0x00,
            "the title field ends with $80 then $00");
}

}  // namespace

int main() {
    testRoundTripAndPadding();
    testSizeBoundaries();
    testWriterRejectsMissingSignature();
    testReaderRejectsMalformedInput();
    testAgainstDocumentedHeaderLayout();
    return 0;
}
