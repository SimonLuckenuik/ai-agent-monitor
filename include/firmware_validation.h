#pragma once
#include <stddef.h>
#include <stdint.h>

// Two image formats, told apart by the first byte:
//
// - Plain (0xE9): elf2bin.py writes a length and a CRC into ESP8266 Arduino images;
//   the CRC covers the entire file with the length/CRC fields zeroed.
// - Compressed (0x1F, gzip): scripts/build_options.py writes a gzip file whose header
//   carries an extra field "AM" (12 bytes: the unpacked size, the file size and a CRC
//   of everything after the 28-byte header, same CRC as above). The core's updater
//   stages the file as it is, and the bootloader (eboot) unpacks it into place,
//   taking the unpacked size from the gzip trailer, which must match the "AM" field.
//   gunzip still reads the file: the extra field is standard gzip.
struct FirmwareValidation {
  static constexpr size_t prefixSize = 4120;
  static constexpr size_t gzHeaderSize = 28;   // 10 base + 2 XLEN + 4 subfield + 12 data
  uint8_t prefix[prefixSize] = {};
  size_t received = 0;
  uint32_t expectedSize = 0, expectedCrc = 0, crc = 0xffffffff, rawSize = 0, tail = 0;
  uint8_t lastByte = 0;

  void reset() {
    received = expectedSize = expectedCrc = rawSize = tail = 0;
    crc = 0xffffffff;
    lastByte = 0;
  }

  static uint32_t word(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
  }
  bool compressed() const { return prefix[0] == 0x1f; }

  // After prefixSize bytes: nullptr when the image may be staged, else why not.
  // capacity: the free sketch space for the staged file. area: where the filesystem
  // starts (FS_PHYS_ADDR): a compressed image unpacks from address 0 up to the staged
  // copy, which the updater puts right below that.
  const char* check(size_t capacity, size_t area) {
    if (compressed()) {
      // Restrict to the project's own compressed format: deflate, only FEXTRA set.
      if (prefix[1] != 0x8b || prefix[2] != 8 || prefix[3] != 4 || prefix[10] != 16 || prefix[11] != 0 ||
          prefix[12] != 'A' || prefix[13] != 'M' || prefix[14] != 12 || prefix[15] != 0)
        return "Unsupported compressed image";
      rawSize = word(prefix + 16);
      expectedSize = word(prefix + 20);
      expectedCrc = word(prefix + 24);
      size_t staged = (expectedSize + 0xfff) & ~size_t(0xfff), raw = (rawSize + 0xfff) & ~size_t(0xfff);
      if (expectedSize <= prefixSize || rawSize <= prefixSize) return "Invalid compressed image length";
      if (expectedSize > capacity || staged > area || raw > area - staged)
        return "Compressed image too big for this clock";
      return nullptr;
    }
    expectedSize = word(prefix + 4112);
    expectedCrc = word(prefix + 4116);
    // Restrict OTA to the project's raw 4MB/DIO/40MHz Arduino image format.
    if (!(prefix[0] == 0xe9 && prefix[2] == 2 && prefix[3] == 0x40 &&
          prefix[4096] == 0xe9 && prefix[4098] == 2 && prefix[4099] == 0x40 &&
          prefix[4097] == 5 && word(prefix + 4104) == 0x40201010 &&
          expectedSize > prefixSize && word(prefix + 4108) <= expectedSize - prefixSize))
      return "Unsupported image or invalid image length";
    if (expectedSize > capacity) return "Too big to update uncompressed: upload the .bin.gz";
    return nullptr;
  }
  void consume(const uint8_t* data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
      if (received < prefixSize) prefix[received] = data[i];
      bool gz = compressed();
      if (!gz || received >= gzHeaderSize) {
        uint8_t value = !gz && received >= 4112 && received < 4120 ? 0 : data[i];
        crc ^= uint32_t(value) << 24;
        for (int bit = 0; bit < 8; ++bit)
          crc = (crc << 1) ^ ((crc & 0x80000000) ? 0x04c11db7 : 0);
      }
      tail = (tail >> 8) | (uint32_t(data[i]) << 24);   // the last 4 bytes: gzip's ISIZE
      lastByte = data[i];
      ++received;
    }
  }
  bool complete() const {
    return received == expectedSize && crc == expectedCrc && (!compressed() || tail == rawSize);
  }
};
