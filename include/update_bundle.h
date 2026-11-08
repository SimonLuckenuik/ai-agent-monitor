#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// An update (spec 048): the files the firmware reads from storage, then the firmware
// image (.bin.gz) as the release ships it, in one file the update page takes.
//
//   0   "AMUP"
//   4   u8 format (1), u8 file count (1-16), u16 0
//   8   u32 files hash: /sys/manifest records it; the image it carries has its own copy
//  12   per file: u8 name length (1-31), the name ([a-z0-9._-], no leading '.'),
//       u32 size (at most 256 KB), u32 CRC of the data (crc32 below), the data
//       then the image, which FirmwareValidation checks as a .bin.gz or .bin
//
// Numbers are little-endian. scripts/make_update.py writes it; tests/unit read it back.
// No Arduino here: the host unit tests use it as it is. The firmware defines
// BUNDLE_TEXT(s) as PSTR(s) first, so the messages stay in flash (read them with FPSTR).
#ifndef BUNDLE_TEXT
#define BUNDLE_TEXT(s) s
#endif
struct BundleReader {
  enum Event : uint8_t { NONE, FILE_START, FILE_DATA, FILE_END, IMAGE, FAILED };
  static constexpr uint8_t MAX_FILES = 16, NAME_MAX = 31;
  static constexpr uint32_t FILE_MAX = 256 * 1024;

  // The ESP8266 core's crc32() (and elf2bin's): MSB first, 0x04c11db7, no final xor.
  static uint32_t crc32(const uint8_t* p, size_t n, uint32_t crc = 0xffffffff) {
    while (n--) {
      crc ^= uint32_t(*p++) << 24;
      for (int bit = 0; bit < 8; ++bit) crc = (crc << 1) ^ ((crc & 0x80000000) ? 0x04c11db7 : 0);
    }
    return crc;
  }
  static bool isBundle(const uint8_t* p, size_t n) { return n >= 1 && p[0] == 'A' && (n < 4 || !memcmp(p, "AMUP", 4)); }

  uint32_t hash = 0;          // after the header
  uint8_t count = 0, done = 0;
  char name[NAME_MAX + 1] = {};
  uint32_t size = 0;          // the current file's
  const uint8_t* data = nullptr;   // FILE_DATA and IMAGE: the bytes
  size_t len = 0;
  const char* error = nullptr;

  // Reads from p (n bytes) and says what it found. Returns how many bytes it used;
  // call it again with the rest, and once more after the last byte while pending().
  size_t next(const uint8_t* p, size_t n, Event& ev) {
    ev = NONE;
    data = nullptr; len = 0;
    if (state == S_FAILED) { ev = FAILED; return n; }
    if (state == S_IMAGE) { data = p; len = n; ev = n ? IMAGE : NONE; return n; }
    if (state == S_DATA) {
      if (!left) {   // the file is complete
        if (crc != expectCrc) return fail(BUNDLE_TEXT("A file in the update is damaged (CRC)"), ev, n);
        ++done;
        state = done == count ? S_IMAGE : S_LEN;
        need = 1; got = 0;
        ev = FILE_END;
        return 0;
      }
      size_t take = n < left ? n : left;
      crc = crc32(p, take, crc);
      left -= take;
      data = p; len = take;
      ev = take ? FILE_DATA : NONE;
      return take;
    }
    // Fixed-size parts arrive in pieces: collect them in buf.
    size_t take = need - got < n ? need - got : n;
    memcpy(buf + got, p, take);
    got += take;
    if (got < need) return take;
    got = 0;
    if (state == S_HEAD) {
      if (memcmp(buf, "AMUP", 4)) return fail(BUNDLE_TEXT("Not an update file"), ev, take);
      if (buf[4] != 1) return fail(BUNDLE_TEXT("Update format not supported: update the clock with its .bin.gz first"), ev, take);
      count = buf[5];
      if (!count || count > MAX_FILES) return fail(BUNDLE_TEXT("Invalid update file"), ev, take);
      hash = word(buf + 8);
      state = S_LEN; need = 1;
    } else if (state == S_LEN) {
      if (!buf[0] || buf[0] > NAME_MAX) return fail(BUNDLE_TEXT("Invalid update file"), ev, take);
      state = S_NAME; need = buf[0];
    } else if (state == S_NAME) {
      for (size_t i = 0; i < need; ++i) {
        char c = buf[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!ok || (i == 0 && c == '.')) return fail(BUNDLE_TEXT("Invalid file name in the update"), ev, take);
        name[i] = c;
      }
      name[need] = 0;
      state = S_SIZES; need = 8;
    } else {   // S_SIZES
      size = word(buf); expectCrc = word(buf + 4);
      if (size > FILE_MAX) return fail(BUNDLE_TEXT("A file in the update is too big"), ev, take);
      left = size; crc = 0xffffffff;
      state = S_DATA;
      ev = FILE_START;
    }
    return take;
  }
  // A file's last byte was read and its FILE_END not yet reported.
  bool pending() const { return state == S_DATA && !left; }
  bool inImage() const { return state == S_IMAGE; }

 private:
  enum : uint8_t { S_HEAD, S_LEN, S_NAME, S_SIZES, S_DATA, S_IMAGE, S_FAILED };
  uint8_t state = S_HEAD;
  uint8_t buf[NAME_MAX + 1] = {};
  size_t need = 12, got = 0;
  uint32_t left = 0, crc = 0, expectCrc = 0;

  static uint32_t word(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
  }
  size_t fail(const char* why, Event& ev, size_t used) {
    error = why; state = S_FAILED; ev = FAILED;
    return used;
  }
};
