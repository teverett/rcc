#include "fileformat/dfs/DfsFormat.h"

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

// A hand-built catalogue from the Acorn DFS spec itself (see DfsFormat.h's own header comment
// for the byte layout this is taken from): a 3-byte program loaded/executed at $1900 (the real
// BBC Micro's own DFS `PAGE`), named "CODE" in the root directory, on a 400-sector (40-track)
// image. Checked byte-for-byte, not just round-tripped through the writer, so this test doesn't
// just agree with the implementation.
void testWriterMatchesHandBuiltCatalogue() {
    rcc::ObjectFile obj = makeObject(0x1900, 3);
    rcc::DfsFormatWriter writer;
    std::string bytes = writer.write(obj);

    require(bytes.size() == 400 * 256, "a small program should produce the 400-sector (40-track) disc size");

    // Sector 0: title (spaces), then entry 0's name/directory byte.
    std::string sector0 = bytes.substr(0, 256);
    require(sector0.substr(0, 8) == "        ", "sector 0's title should be blank");
    require(sector0.substr(8, 7) == "CODE   ", "entry 0's file name should be \"CODE\", space-padded to 7 characters");
    require(static_cast<uint8_t>(sector0[15]) == '$', "entry 0's directory byte should be '$' with the locked bit clear");
    for (size_t i = 16; i < 256; i++) {
        require(sector0[i] == '\0', "every unused catalogue-0 entry byte should be zero");
    }

    // Sector 1: title tail, cycle number 0, one file (8), boot option 0 / disc size high bits,
    // disc size low byte, then entry 0's load/exec/length/start-sector fields.
    std::string sector1 = bytes.substr(256, 256);
    require(sector1.substr(0, 4) == "    ", "sector 1's title tail should be blank");
    require(static_cast<uint8_t>(sector1[4]) == 0, "cycle number should be 0");
    require(static_cast<uint8_t>(sector1[5]) == 8, "file count byte should be 8*1 for one file");
    require(static_cast<uint8_t>(sector1[6]) == 0x01, "boot option 0 plus disc-size bits 8-9 of 400 (0b01)");
    require(static_cast<uint8_t>(sector1[7]) == 0x90, "disc size low byte of 400 ($190) should be $90");
    require(static_cast<uint8_t>(sector1[8]) == 0x00, "load address low byte of $1900");
    require(static_cast<uint8_t>(sector1[9]) == 0x19, "load address high byte of $1900");
    require(static_cast<uint8_t>(sector1[10]) == 0x00, "execution address should equal the load address");
    require(static_cast<uint8_t>(sector1[11]) == 0x19, "execution address should equal the load address");
    require(static_cast<uint8_t>(sector1[12]) == 3, "length low byte of a 3-byte program");
    require(static_cast<uint8_t>(sector1[13]) == 0, "length high byte of a 3-byte program");
    require(static_cast<uint8_t>(sector1[14]) == 0, "every packed high-bit extension should be 0");
    require(static_cast<uint8_t>(sector1[15]) == 2, "start sector should be 2, the first sector past the catalogue");

    // The program's own 3 bytes sit at sector 2 (absolute offset 512), with everything after
    // zero-filled out to the end of the 400-sector image.
    require(bytes.substr(512, 3) == std::string(reinterpret_cast<const char *>(obj.textBytes.data()), 3),
            "program bytes should start exactly at sector 2");
    for (size_t i = 515; i < bytes.size(); i++) {
        require(bytes[i] == '\0', "every sector past the program's own data should stay zero-filled");
    }
}

void testRoundTrip() {
    rcc::ObjectFile obj = makeObject(0x1900, 1000);
    rcc::DfsFormatWriter writer;
    std::string bytes = writer.write(obj);

    rcc::DfsFormatReader reader;
    rcc::ObjectFile back = reader.read(bytes);
    require(back.base == obj.base, "round trip lost the load address");
    require(back.textBytes == obj.textBytes, "round trip lost the program bytes");
}

// An empty program is still a valid image: one zero-length catalogue entry, still 400 sectors.
void testEmptyProgram() {
    rcc::ObjectFile obj = makeObject(0x1900, 0);
    rcc::DfsFormatWriter writer;
    std::string bytes = writer.write(obj);
    require(bytes.size() == 400 * 256, "an empty program should still produce a 400-sector image");
    require(static_cast<uint8_t>(bytes[256 + 12]) == 0 && static_cast<uint8_t>(bytes[256 + 13]) == 0,
            "an empty program's length field should be 0");

    rcc::DfsFormatReader reader;
    rcc::ObjectFile back = reader.read(bytes);
    require(back.base == obj.base, "empty-program round trip lost the load address");
    require(back.textBytes.empty(), "empty-program round trip should read back zero payload bytes");
}

// A single byte - the smallest nonempty program, and the smallest possible data-sector count (1).
void testSingleByte() {
    rcc::ObjectFile obj = makeObject(0x2000, 1);
    rcc::DfsFormatWriter writer;
    std::string bytes = writer.write(obj);
    require(bytes.size() == 400 * 256, "a 1-byte program should still fit the 400-sector image");

    rcc::DfsFormatReader reader;
    rcc::ObjectFile back = reader.read(bytes);
    require(back.textBytes == obj.textBytes, "1-byte round trip lost the program byte");
}

// Exactly one sector's worth of data (256 bytes) - the boundary where the data-sector count is
// still exactly 1.
void testExactlyOneSector() {
    rcc::ObjectFile obj = makeObject(0x1900, 256);
    rcc::DfsFormatWriter writer;
    std::string bytes = writer.write(obj);
    require(bytes.size() == 400 * 256, "an exactly-one-sector program should still fit the 400-sector image");

    rcc::DfsFormatReader reader;
    require(reader.read(bytes).textBytes == obj.textBytes, "exactly-one-sector round trip lost data");
}

// One byte over a sector (257 bytes) - the boundary where the data-sector count becomes 2.
void testOneByteOverASector() {
    rcc::ObjectFile obj = makeObject(0x1900, 257);
    rcc::DfsFormatWriter writer;
    std::string bytes = writer.write(obj);
    require(bytes.size() == 400 * 256, "a one-byte-over-a-sector program should still fit the 400-sector image");

    rcc::DfsFormatReader reader;
    require(reader.read(bytes).textBytes == obj.textBytes, "one-byte-over-a-sector round trip lost data");
}

// The largest program that still fits a 400-sector (40-track) image: 398 data sectors
// (400 - 2 catalogue sectors) of 256 bytes each.
void testMaxSizeForSmallDisc() {
    rcc::ObjectFile obj = makeObject(0x1900, 398 * 256);
    rcc::DfsFormatWriter writer;
    std::string bytes = writer.write(obj);
    require(bytes.size() == 400 * 256, "a program that exactly fills every 40-track data sector should stay 40-track");

    rcc::DfsFormatReader reader;
    require(reader.read(bytes).textBytes == obj.textBytes, "max-size-for-40-track round trip lost data");
}

// One byte more than a 40-track image can hold: the writer should grow to the 80-track
// (800-sector) convention instead of failing or truncating.
void testGrowsTo80TrackPastSmallDiscCapacity() {
    rcc::ObjectFile obj = makeObject(0x1900, 398 * 256 + 1);
    rcc::DfsFormatWriter writer;
    std::string bytes = writer.write(obj);
    require(bytes.size() == 800 * 256, "a program one byte past 40-track capacity should grow to an 800-sector image");
    require(static_cast<uint8_t>(bytes[256 + 6]) == 0x03, "disc-size bits 8-9 of 800 (0b11) should be in sector 1 byte 6");
    require(static_cast<uint8_t>(bytes[256 + 7]) == 0x20, "disc size low byte of 800 ($320) should be $20");

    rcc::DfsFormatReader reader;
    require(reader.read(bytes).textBytes == obj.textBytes, "80-track round trip lost data");
}

// The largest program that fits an 80-track image: 798 data sectors.
void testMaxSizeForLargeDisc() {
    rcc::ObjectFile obj = makeObject(0x1900, 798 * 256);
    rcc::DfsFormatWriter writer;
    std::string bytes = writer.write(obj);
    require(bytes.size() == 800 * 256, "a program that exactly fills every 80-track data sector should stay 80-track");
}

// One byte more than even an 80-track image can hold: rejected outright, not truncated or split
// across multiple discs.
void testWriterRejectsOversizedProgram() {
    rcc::ObjectFile obj = makeObject(0x1900, 798 * 256 + 1);
    rcc::DfsFormatWriter writer;
    try {
        writer.write(obj);
    } catch (const std::runtime_error &) {
        return;
    }
    throw std::runtime_error("should have rejected a program too large for even an 80-track image");
}

void testWriterRejectsOversizedBase() {
    rcc::ObjectFile obj = makeObject(0x10000, 1);
    rcc::DfsFormatWriter writer;
    try {
        writer.write(obj);
    } catch (const std::runtime_error &) {
        return;
    }
    throw std::runtime_error("should have rejected a load address that doesn't fit in 16 bits");
}

void testReaderRejectsTruncatedCatalogue() {
    rcc::DfsFormatReader reader;
    try {
        reader.read(std::string(511, '\0'));
    } catch (const std::runtime_error &) {
        return;
    }
    throw std::runtime_error("should have rejected an image shorter than the two-sector catalogue");
}

void testReaderRejectsEmptyCatalogue() {
    rcc::DfsFormatReader reader;
    std::string image(400 * 256, '\0');  // every catalogue byte 0, including the file-count byte
    try {
        reader.read(image);
    } catch (const std::runtime_error &) {
        return;
    }
    throw std::runtime_error("should have rejected a catalogue with zero files");
}

// A file entry whose declared length runs past the end of the image - a corrupt or deliberately
// malformed disc image, not just an empty one.
void testReaderRejectsOverrunningLength() {
    rcc::ObjectFile obj = makeObject(0x1900, 10);
    rcc::DfsFormatWriter writer;
    std::string bytes = writer.write(obj);
    bytes[256 + 13] = static_cast<char>(0xFF);  // claim a huge length high byte instead of 0
    bytes[256 + 14] = static_cast<char>(0x30);  // and set the length's own high-bit extension (bits 4-5)

    rcc::DfsFormatReader reader;
    try {
        reader.read(bytes);
    } catch (const std::runtime_error &) {
        return;
    }
    throw std::runtime_error("should have rejected a file entry whose length runs past the end of the image");
}

}  // namespace

int main() {
    try {
        testWriterMatchesHandBuiltCatalogue();
        testRoundTrip();
        testEmptyProgram();
        testSingleByte();
        testExactlyOneSector();
        testOneByteOverASector();
        testMaxSizeForSmallDisc();
        testGrowsTo80TrackPastSmallDiscCapacity();
        testMaxSizeForLargeDisc();
        testWriterRejectsOversizedProgram();
        testWriterRejectsOversizedBase();
        testReaderRejectsTruncatedCatalogue();
        testReaderRejectsEmptyCatalogue();
        testReaderRejectsOverrunningLength();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
