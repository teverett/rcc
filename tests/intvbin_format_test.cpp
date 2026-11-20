#include "fileformat/intvbin/IntvBinFormat.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

rcc::ObjectFile makeObject(size_t wordCount) {
    rcc::ObjectFile obj;
    obj.textBytes.resize(wordCount * 2);
    for (size_t i = 0; i < wordCount; i++) {
        uint16_t w = static_cast<uint16_t>(0x1234 + i);
        obj.textBytes[i * 2] = static_cast<uint8_t>(w & 0xFF);
        obj.textBytes[i * 2 + 1] = static_cast<uint8_t>((w >> 8) & 0xFF);
    }
    return obj;
}

// An empty program writes an empty image - no header/trailer bytes exist in this format at all.
void testEmptyProgramWritesEmptyImage() {
    rcc::ObjectFile obj = makeObject(0);
    rcc::IntvBinFormatWriter writer;
    std::string bytes = writer.write(obj);
    require(bytes.empty(), "an empty program should write zero bytes");
}

// A single word is written as exactly two bytes, low byte first - the one fact this project
// could verify from a primary source (MAME's own intv cartridge loader) - and nothing else.
void testSingleWordIsLowByteFirst() {
    rcc::ObjectFile obj;
    obj.textBytes = {0x34, 0x12};  // word 0x1234, already low-byte-first in textBytes
    rcc::IntvBinFormatWriter writer;
    std::string bytes = writer.write(obj);
    require(bytes.size() == 2, "a one-word program should write exactly two bytes");
    require(static_cast<uint8_t>(bytes[0]) == 0x34, "the low byte should come first in the file");
    require(static_cast<uint8_t>(bytes[1]) == 0x12, "the high byte should come second in the file");
}

// Byte-exact output for several words: the writer is a pure passthrough of textBytes, which is
// already little-endian-per-word (see CP1610Assembler.cpp's own wordsToBytes()).
void testByteExactMultiWordOutput() {
    rcc::ObjectFile obj = makeObject(3);
    rcc::IntvBinFormatWriter writer;
    std::string bytes = writer.write(obj);
    std::string expected(reinterpret_cast<const char *>(obj.textBytes.data()), obj.textBytes.size());
    require(bytes == expected, "the writer should pass textBytes through unchanged");
}

// Exactly 8192 words (the full $5000-$6FFF window) is the largest size this format accepts.
void testMaxSizeIsAccepted() {
    rcc::ObjectFile obj = makeObject(8192);
    rcc::IntvBinFormatWriter writer;
    std::string bytes = writer.write(obj);
    require(bytes.size() == 8192 * 2, "exactly 8192 words should be written in full, unpadded");
}

// One word over the $5000-$6FFF window is rejected outright - it would run off the end of the
// non-bankswitched cartridge ROM space real, unexpanded hardware decodes.
void testOverMaxSizeIsRejected() {
    rcc::ObjectFile obj = makeObject(8193);
    rcc::IntvBinFormatWriter writer;
    bool threw = false;
    try {
        writer.write(obj);
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "a program bigger than 8192 words should be rejected");
}

// An odd byte count can't be a whole number of CP1610 words, so it's rejected rather than
// silently truncated or zero-padded.
void testOddByteCountIsRejected() {
    rcc::ObjectFile obj;
    obj.textBytes = {0x01, 0x02, 0x03};
    rcc::IntvBinFormatWriter writer;
    bool threw = false;
    try {
        writer.write(obj);
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "an odd number of bytes should be rejected");
}

// A writer-to-reader round trip reproduces the original image exactly, and the reader reports
// the fixed $5000 base this format's own convention always loads at.
void testWriterToReaderRoundTrip() {
    rcc::ObjectFile obj = makeObject(100);
    rcc::IntvBinFormatWriter writer;
    std::string bytes = writer.write(obj);

    rcc::IntvBinFormatReader reader;
    rcc::ObjectFile back = reader.read(bytes);
    require(back.base == 0x5000, "reading back an image should report the fixed $5000 base");
    require(back.unitBits == 16, "reading back an image should report 16-bit addressable units");
    require(back.textBytes == obj.textBytes, "a round trip should reproduce the original image exactly");
}

// The reader rejects the same malformed input the writer would never produce: an odd byte count,
// and a file bigger than the 8192-word window.
void testReadRejectsMalformedInput() {
    rcc::IntvBinFormatReader reader;

    std::string oddSize(5, '\0');
    bool threw = false;
    try {
        reader.read(oddSize);
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "reading an odd-sized file should be rejected");

    std::string tooBig(8192 * 2 + 2, '\0');
    threw = false;
    try {
        reader.read(tooBig);
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "reading a file bigger than 8192 words should be rejected");
}

}  // namespace

int main() {
    try {
        testEmptyProgramWritesEmptyImage();
        testSingleWordIsLowByteFirst();
        testByteExactMultiWordOutput();
        testMaxSizeIsAccepted();
        testOverMaxSizeIsRejected();
        testOddByteCountIsRejected();
        testWriterToReaderRoundTrip();
        testReadRejectsMalformedInput();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
