#include "fileformat/smsrom/SmsRomFormat.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

rcc::ObjectFile makeObject(size_t textSize) {
    rcc::ObjectFile obj;
    obj.base = 0;
    obj.textBytes.assign(textSize, 0x00);
    return obj;
}

template <typename Fn>
bool throws(Fn fn) {
    try {
        fn();
    } catch (const std::runtime_error &) {
        return true;
    }
    return false;
}

// A 2-byte program pads to the smallest supported size (8192 bytes, header included), with the
// header written at that size's own fixed offset ($1ff0 - smspower.org's "ROM Header" page:
// "The header can be at offset $1ff0, $3ff0 or $7ff0 in the ROM"). The checksum below (0xce15) is
// computed independently (not by this project's own writer): the plain sum, mod 65536, of every
// byte from offset 0 up to $1ff0 - two content bytes (0x01, 0x02) plus 8174 bytes of 0xFF padding.
void testSmallProgramHeaderAndChecksum() {
    rcc::ObjectFile obj = makeObject(2);
    obj.textBytes[0] = 0x01;
    obj.textBytes[1] = 0x02;
    rcc::SmsRomFormatWriter writer(/*isGameGear=*/false);
    std::string bytes = writer.write(obj);

    require(bytes.size() == 8192, "a tiny program should pad to the smallest supported size (8192)");
    require(bytes.compare(0x1FF0, 8, "TMR SEGA") == 0, "the signature should sit at the 8KB header offset, $1ff0");
    require(static_cast<uint8_t>(bytes[0x1FF0 + 10]) == 0x15 && static_cast<uint8_t>(bytes[0x1FF0 + 11]) == 0xCE,
            "the checksum should be the independently-computed 0xce15, little-endian");
    require(static_cast<uint8_t>(bytes[0x1FF0 + 15]) == 0x4A,
            "the region/size byte should be $4 (SMS Export) in the high nibble, $a (8KB) in the low nibble");
}

// The same program written for Game Gear gets the same layout/checksum, but a different region
// code ($6, GG Export, vs $4 SMS Export) in the header's last byte high nibble.
void testGameGearRegionCode() {
    rcc::ObjectFile obj = makeObject(2);
    obj.textBytes[0] = 0x01;
    obj.textBytes[1] = 0x02;
    rcc::SmsRomFormatWriter writer(/*isGameGear=*/true);
    std::string bytes = writer.write(obj);
    require(static_cast<uint8_t>(bytes[0x1FF0 + 15]) == 0x6A,
            "Game Gear's region/size byte should be $6 (GG Export) in the high nibble, $a (8KB) low nibble");
}

// A program just over 8176 bytes (8192 - 16-byte header) needs the next size up, with its header
// at that size's own offset ($3ff0 for 16KB).
void testStepsUpToNextSizeWhenHeaderWouldntFit() {
    rcc::ObjectFile obj = makeObject(8177);
    rcc::SmsRomFormatWriter writer(/*isGameGear=*/false);
    std::string bytes = writer.write(obj);
    require(bytes.size() == 16384, "a program that doesn't leave room for the 8KB header should step up to 16KB");
    require(bytes.compare(0x3FF0, 8, "TMR SEGA") == 0, "the signature should sit at the 16KB header offset, $3ff0");
}

// The largest size this writer supports (32KB) places its header at $7ff0, the only offset
// smspower.org says is actually used in known commercial software.
void testLargestSupportedSize() {
    rcc::ObjectFile obj = makeObject(32752);  // exactly fills 32768 - 16
    rcc::SmsRomFormatWriter writer(/*isGameGear=*/false);
    std::string bytes = writer.write(obj);
    require(bytes.size() == 32768, "a program that exactly fills 32768 - 16 bytes shouldn't grow further");
    require(bytes.compare(0x7FF0, 8, "TMR SEGA") == 0, "the signature should sit at the 32KB header offset, $7ff0");
}

// A program too big for the largest supported size (32768 bytes, header included) is rejected -
// this writer deliberately doesn't support the 48KB+ sizes smspower.org itself flags as
// unused/buggy/rarely used (see SmsRomFormat.h's own header comment).
void testOversizedProgramIsRejected() {
    rcc::ObjectFile obj = makeObject(32753);
    rcc::SmsRomFormatWriter writer(/*isGameGear=*/false);
    require(throws([&] { writer.write(obj); }), "a program bigger than 32768 - 16 bytes should be rejected");
}

// Linking at any base other than $0 produces a ROM whose header/checksum would be byte-correct
// but whose code would run at the wrong address on real hardware (the SMS/GG cartridge window
// starts at $0000) - this format refuses it outright instead of silently writing a broken image.
void testNonZeroBaseIsRejected() {
    rcc::ObjectFile obj = makeObject(2);
    obj.base = 0x8000;
    rcc::SmsRomFormatWriter writer(/*isGameGear=*/false);
    require(throws([&] { writer.write(obj); }), "a program linked at a non-zero base should be rejected");
}

// A writer-to-reader round trip reproduces the original image exactly.
void testRoundTrip() {
    rcc::ObjectFile obj = makeObject(100);
    for (size_t i = 0; i < 100; i++) obj.textBytes[i] = static_cast<uint8_t>(i);
    rcc::SmsRomFormatWriter writer(/*isGameGear=*/false);
    std::string bytes = writer.write(obj);

    rcc::SmsRomFormatReader reader;
    rcc::ObjectFile back = reader.read(bytes);
    require(back.textBytes.size() == bytes.size(), "reading back should keep the padded size");
    require(std::string(reinterpret_cast<const char *>(back.textBytes.data()), 100) ==
                std::string(reinterpret_cast<const char *>(obj.textBytes.data()), 100),
            "reading back should reproduce every original content byte");
}

// The reader rejects a missing signature, a bad checksum, and a size that isn't one of the three
// supported ones.
void testReadRejectsMalformedInput() {
    rcc::SmsRomFormatReader reader;

    std::string noSignature(8192, '\0');
    require(throws([&] { reader.read(noSignature); }), "reading a file with no signature should be rejected");

    rcc::ObjectFile obj = makeObject(2);
    rcc::SmsRomFormatWriter writer(/*isGameGear=*/false);
    std::string bytes = writer.write(obj);
    bytes[0x1FF0 + 10] = static_cast<char>(static_cast<uint8_t>(bytes[0x1FF0 + 10]) ^ 0xFF);
    require(throws([&] { reader.read(bytes); }), "reading a file with a corrupted checksum should be rejected");

    std::string wrongSize(1000, '\0');
    require(throws([&] { reader.read(wrongSize); }),
            "reading a file whose size isn't one of the three supported sizes should be rejected");
}

}  // namespace

int main() {
    try {
        testSmallProgramHeaderAndChecksum();
        testGameGearRegionCode();
        testStepsUpToNextSizeWhenHeaderWouldntFit();
        testLargestSupportedSize();
        testOversizedProgramIsRejected();
        testNonZeroBaseIsRejected();
        testRoundTrip();
        testReadRejectsMalformedInput();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
