#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <map>
#include <new>
#include <string>
#include <vector>
#include "firmware_validation.h"
#include "update_bundle.h"
struct String : std::string {
  using std::string::string;
  String(const std::string& s) : std::string(s) {}
  size_t length() const { return size(); }
};
String operator+(const String& a, const char* b) { return String(std::string(a) + b); }
String operator+(const String& a, char b) { return String(std::string(a) + b); }
String operator+(const String& a, const String& b) { return String(std::string(a) + std::string(b)); }
enum { U_FLASH, HTTP_POST, UPLOAD_FILE_START, UPLOAD_FILE_WRITE,
       UPLOAD_FILE_END, UPLOAD_FILE_ABORTED };
struct HTTPUpload { int status; uint8_t* buf; size_t currentSize; String filename; };
struct MockServer {
  HTTPUpload up{};
  int code = 0;
  String message;
  HTTPUpload& upload() { return up; }
  void sendHeader(const char*, const char*) {}
  void send(int status, const char*, const String& value) { code = status; message = value; }
} server;
struct MockESP {
  unsigned restarts = 0;
  uint32_t freeSpace = 1500000;
  uint32_t getFreeSketchSpace() { return freeSpace; }
  void restart() { ++restarts; }
} ESP;
struct MockUpdate {
  bool running = false, committed = false, failBegin = false, failWrite = false, failEnd = false;
  size_t expected = 0;
  std::vector<uint8_t> staged;
  bool begin(size_t size, int command) {
    assert(command == U_FLASH);
    if (failBegin) return false;
    assert(!running); running = true; expected = size; staged.clear(); return true;
  }
  size_t write(uint8_t* data, size_t size) {
    if (!running || failWrite) return 0;
    assert(staged.size() + size <= expected);
    staged.insert(staged.end(), data, data + size); return size;
  }
  bool end(bool allowPartial) {
    assert(!allowPartial);
    committed = running && staged.size() == expected && !failEnd;
    running = false; return committed;
  }
  bool isRunning() { return running; }
} Update;
// Storage: files by path, directories implied by them.
struct FSInfo { size_t totalBytes = 0, usedBytes = 0; };
struct MockFS {
  std::map<std::string, std::string> files;
  size_t total = 1 << 20;
  bool failRename = false;
  size_t used() const { size_t n = 0; for (auto& f : files) n += f.second.size(); return n; }
} fs;
struct File {
  std::string path; size_t pos = 0; bool open = false, writing = false;
  explicit operator bool() const { return open; }
  size_t write(const uint8_t* p, size_t n) { if (!open || !writing) return 0; fs.files[path].append((const char*)p, n); return n; }
  size_t print(const char* s) { return write((const uint8_t*)s, strlen(s)); }
  size_t readBytes(char* out, size_t n) { auto& d = fs.files[path]; size_t k = std::min(n, d.size() - pos); memcpy(out, d.data() + pos, k); pos += k; return k; }
  void close() { open = false; }
};
struct Dir {
  std::vector<std::string> names; size_t at = 0; std::string current;
  bool next() { if (at >= names.size()) return false; current = names[at++]; return true; }
  String fileName() { return current; }
};
struct MockLittleFS {
  File open(const String& path, const char* mode) {
    File f; f.path = path;
    if (*mode == 'w') { fs.files[path] = ""; f.open = f.writing = true; }
    else f.open = fs.files.count(path) > 0;
    return f;
  }
  bool remove(const String& path) { return fs.files.erase(path) > 0; }
  bool mkdir(const char*) { return true; }
  bool rmdir(const char*) { return true; }
  bool rename(const String& from, const String& to) {
    if (fs.failRename || !fs.files.count(from)) return false;
    fs.files[to] = fs.files[from]; fs.files.erase(from); return true;
  }
  bool info(FSInfo& i) { i.totalBytes = fs.total; i.usedBytes = fs.used(); return true; }
  Dir openDir(const char* dir) {
    Dir d; std::string prefix = std::string(dir) + "/";
    for (auto& f : fs.files) if (f.first.rfind(prefix, 0) == 0 && f.first.find('/', prefix.size()) == std::string::npos) d.names.push_back(f.first.substr(prefix.size()));
    return d;
  }
} LittleFS;
bool storageReady = true, recoveryMode = false;
#define FILES_HASH 0x6109c34du   // replaced by the runner with the build's own
#define F(s) s       // flash strings are plain ones on the host
#define FPSTR(s) s
#define snprintf_P snprintf
using __FlashStringHelper = char;
#define PSTR(s) s
#define FS_PHYS_ADDR 0x100000u   // 4m3m: the filesystem starts at 1 MB
void delay(unsigned) {}
void yield() {}
void safeRestart() { ESP.restart(); }
bool authOk = true;   // what requestAuthorized() answers for the request
bool requestAuthorized() { return authOk; }

// Simulated EEPROM sector: persists across "boots", counts real flash commits.
constexpr size_t SPI_FLASH_SEC_SIZE = 4096;
struct MockEEPROM {
  std::vector<uint8_t> flash = std::vector<uint8_t>(SPI_FLASH_SEC_SIZE, 0xff), ram;
  bool dirty = false; unsigned commits = 0;
  void begin(size_t size) { assert(size == SPI_FLASH_SEC_SIZE); ram = flash; dirty = false; }
  uint8_t read(int a) { return ram.at(a); }
  void write(int a, uint8_t v) { if (ram.at(a) != v) { ram[a] = v; dirty = true; } }
  void end() { if (dirty) { flash = ram; ++commits; } ram.clear(); }
} EEPROM;

// The runner inserts the power-cycle counter from src/main.cpp here.
// CYCLE_IMPLEMENTATION

// src/update.h, without Arduino.h.
void updateBegin(); bool updateFeed(const uint8_t*, size_t); bool updateFinish(); void updateFail(const String&);
const String& updateError(); const String& updateNote(); bool updateBusy(); uint32_t updateReceived();
void handleOtaUpload(); void finishOta(); const __FlashStringHelper* filesState(); bool filesOk(); void filesCheck();
// The runner inserts the actual update implementation from src/update.cpp here.
// OTA_IMPLEMENTATION

void reset() {
  assert(!Update.running);
  server = MockServer{}; ESP = MockESP{}; Update = MockUpdate{};
  assert(!firmware && !bundle);   // freed after every upload, accepted or not
  assert(!updateBusy());
}
void event(int status, uint8_t* data = nullptr, size_t size = 0) {
  server.up.status = status; server.up.buf = data; server.up.currentSize = size;
  handleOtaUpload();
}
void upload(std::vector<uint8_t>& image, size_t chunk = 2048) {
  event(UPLOAD_FILE_START);
  for (size_t offset = 0; offset < image.size(); offset += chunk)
    event(UPLOAD_FILE_WRITE, image.data() + offset, std::min(chunk, image.size() - offset));
  event(UPLOAD_FILE_END);
}
void rejected() {
  finishOta();
  assert(server.code == 400 && !Update.committed && !Update.running && ESP.restarts == 0);
}
void accepted(std::vector<uint8_t>& image) {
  upload(image); finishOta();
  assert(server.code == 200 && Update.committed && ESP.restarts == 1 && Update.staged == image);
}
std::vector<uint8_t> load(const char* path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), {}};
}
void put32(std::vector<uint8_t>& v, size_t at, uint32_t x) { for (int i = 0; i < 4; ++i) v[at + i] = uint8_t(x >> (8 * i)); }

// Compressed .bin.gz (firmware.bin.gz from scripts/build_options.py).
void compressedCases(std::vector<uint8_t>& gz) {
  assert(gz.size() > FirmwareValidation::prefixSize && gz[0] == 0x1f);
  for (size_t chunk : {size_t(1), size_t(2048), gz.size()}) {
    reset(); upload(gz, chunk); assert(updateBusy() && Update.staged.size() == gz.size() - 1);
    finishOta(); assert(server.code == 200 && Update.committed && Update.staged == gz);
  }
  for (size_t length : {size_t(27), size_t(4120), gz.size() - 1}) {
    reset(); auto partial = gz; partial.resize(length); upload(partial); rejected();
  }
  reset(); auto corrupt = gz; corrupt[5000] ^= 1; upload(corrupt); rejected();            // CRC
  reset(); auto trailer = gz; trailer.back() ^= 1; upload(trailer); rejected();           // CRC
  reset(); auto longer = gz; longer.push_back(0); upload(longer); rejected();             // length
  // Header fields outside the CRC: each still has a check.
  reset(); auto raw = gz; put32(raw, 16, FirmwareValidation::word(&gz[16]) + 1); upload(raw); rejected();   // != ISIZE
  reset(); auto size = gz; put32(size, 20, uint32_t(gz.size() - 1)); upload(size); rejected();
  reset(); auto crc = gz; crc[24] ^= 1; upload(crc); rejected();
  reset(); auto tag = gz; tag[12] = 'X'; upload(tag); rejected();
  assert(server.message == "Unsupported compressed image");
  reset(); auto flags = gz; flags[3] = 4 | 8; upload(flags); rejected();                 // FNAME too
  // Too big: no room to stage it, or the unpacked image would reach the staged copy.
  reset(); ESP.freeSpace = uint32_t(gz.size()); upload(gz); rejected();
  assert(server.message == "Compressed image too big for this clock" && Update.staged.empty());
  reset(); auto huge = gz; put32(huge, 16, 0xC0000); upload(huge); rejected();
  assert(server.message == "Compressed image too big for this clock" && Update.staged.empty());
  std::cout << "PASS: compressed: complete/chunked, truncated, corrupt, trailer, oversized, header fields "
               "(unpacked size, length, CRC, tag, flags), no room, would overlap\n";
}

// Update files (spec 048): firmware.update from scripts/build_options.py, the files then
// the .bin.gz. The image is committed first; the files reach /sys only after it.
void updateFileCases(std::vector<uint8_t>& up, std::vector<uint8_t>& gz) {
  assert(up.size() > gz.size() && std::equal(gz.begin(), gz.end(), up.end() - gz.size()));
  std::string page;   // index.html.gz: the first file (scripts/sys_files.py)
  { BundleReader b; size_t at = 0; BundleReader::Event ev;
    while (at < up.size()) { at += b.next(up.data() + at, up.size() - at, ev);
      if (ev == BundleReader::FILE_DATA && !strcmp(b.name, "index.html.gz")) page.append((const char*)b.data, b.len);
      if (ev == BundleReader::IMAGE) break; } }
  char want[9]; snprintf(want, sizeof want, "%08x", (unsigned)FILES_HASH);
  for (size_t chunk : {size_t(1), size_t(1460), size_t(2048), up.size()}) {
    reset(); fs = MockFS{}; fs.files["/sys/index.html.gz"] = "old page"; fs.files["/sys/old.css"] = "x";
    fs.files["/sys/manifest"] = "00000000"; fs.files["/photo/a.jpg"] = "photo";
    filesCheck(); assert(!strcmp(filesState(), "stale"));
    upload(up, chunk);
    assert(fs.files["/sys/index.html.gz"] == "old page");   // nothing replaced before the commit
    finishOta();
    assert(server.code == 200 && server.message == "OK" && Update.committed && Update.staged == gz && ESP.restarts == 1);
    assert(fs.files["/sys/index.html.gz"] == page && fs.files["/sys/manifest"] == want);
    assert(!fs.files.count("/sys/old.css") && fs.files["/photo/a.jpg"] == "photo");   // only /sys is touched
    for (auto& f : fs.files) assert(f.first.rfind("/sys.new/", 0) != 0);
    filesCheck(); assert(!strcmp(filesState(), "ok") && filesOk());
  }
  // A damaged image: nothing committed, /sys as it was, the staging removed.
  reset(); fs = MockFS{}; fs.files["/sys/index.html.gz"] = "old page"; fs.files["/sys/manifest"] = "00000000";
  auto bad = up; bad[bad.size() - 3000] ^= 1; upload(bad); rejected();
  assert(fs.files["/sys/index.html.gz"] == "old page" && fs.files["/sys/manifest"] == "00000000" && fs.files.size() == 2);
  // A damaged file: refused before the image is staged.
  reset(); fs = MockFS{}; bad = up; bad[12 + 1 + 13 + 8 + 200] ^= 1; upload(bad); rejected();
  assert(Update.staged.empty() && fs.files.empty() && server.message.find("CRC") != std::string::npos);
  // Truncated inside the files.
  reset(); fs = MockFS{}; bad = up; bad.resize(5000); upload(bad); rejected(); assert(fs.files.empty());
  // Storage full: refused, with the reason.
  reset(); fs = MockFS{}; fs.total = 20000; upload(up); rejected();
  assert(server.message.find("Storage is full") == 0 && Update.staged.empty());
  // Recovery (and storage not mounted): the image installs, the files wait for the next update.
  for (int c = 0; c < 2; ++c) {
    reset(); fs = MockFS{}; (c ? storageReady : recoveryMode) = c ? false : true;
    upload(up); finishOta();
    assert(server.code == 200 && server.message.find("not its files") != std::string::npos && Update.committed && fs.files.empty());
    recoveryMode = false; storageReady = true;
  }
  // A rename that fails: the image is in, the reply says to install the update again.
  reset(); fs = MockFS{}; fs.failRename = true; upload(up); finishOta();
  assert(server.code == 200 && server.message.find("only in part") != std::string::npos && !fs.files.count("/sys/manifest"));
  filesCheck(); assert(!strcmp(filesState(), "missing"));
  storageReady = false; filesCheck(); assert(!strcmp(filesState(), "unmounted")); storageReady = true;
  // An update file inside an update file, or an image other than ours after the files.
  reset(); fs = MockFS{}; bad = up; bad.resize(up.size() - gz.size()); bad.insert(bad.end(), up.begin(), up.end());
  upload(bad); rejected(); assert(!Update.committed && !fs.files.count("/sys/manifest"));
  std::cout << "PASS: update files: chunked, files only after the commit, old files removed, photos kept, damaged image or file, "
               "truncated, storage full, recovery and unmounted (image only), failed rename, manifest states\n";
}

int main(int argc, char** argv) {
  assert(argc == 4);
  std::vector<uint8_t> image = load(argv[1]), gz = load(argv[2]), up = load(argv[3]);
  assert(image.size() > FirmwareValidation::prefixSize);
  for (size_t chunk : {size_t(1), size_t(511), size_t(2048), size_t(4096), image.size()}) {
    reset(); server.up.filename = "clock-lfs.bin"; upload(image, chunk);
    assert(updateBusy() && !Update.committed && ESP.restarts == 0);
    assert(Update.staged.size() == image.size() - 1);
    finishOta();
    assert(server.code == 200 && Update.committed && ESP.restarts == 1 && Update.staged == image);
  }
  for (size_t length : {size_t(0), size_t(100), size_t(4119), size_t(4120), image.size() - 1}) {
    reset(); auto partial = image; partial.resize(length); upload(partial); rejected();
  }
  reset(); auto corrupt = image; corrupt[5000] ^= 1; upload(corrupt); rejected();
  reset(); auto oversize = image; oversize.push_back(0); upload(oversize); rejected();
  reset(); auto wrongMode = image; wrongMode[2] = 0; upload(wrongMode); rejected();
  reset(); auto badLength = image; badLength[4115] = 0xff; upload(badLength); rejected();
  reset(); auto filesystem = image; filesystem[0] = 0; server.up.filename = "littlefs.bin";
  upload(filesystem); rejected();
  reset(); upload(image); event(UPLOAD_FILE_START); rejected();
  reset(); finishOta(); assert(server.code == 400 && ESP.restarts == 0);
  reset(); event(UPLOAD_FILE_START); event(UPLOAD_FILE_WRITE, image.data(), 8192);
  event(UPLOAD_FILE_ABORTED); assert(!Update.running && !Update.committed);
  upload(image); finishOta(); assert(Update.committed && ESP.restarts == 1);
  reset(); upload(image); event(UPLOAD_FILE_ABORTED);
  assert(!Update.running && !Update.committed && ESP.restarts == 0);
  upload(image); finishOta(); assert(Update.committed && ESP.restarts == 1);
  // Not signed in: rejected at the first chunk, nothing is staged.
  reset(); authOk = false; upload(image); assert(!Update.running && Update.staged.empty()); rejected();
  assert(server.message == "Sign in first"); authOk = true;
  // Too big for a plain update: refused before anything is staged, with the way out.
  reset(); ESP.freeSpace = uint32_t(image.size()); upload(image); rejected();
  assert(server.message == "Too big to update uncompressed: upload the .bin.gz" && Update.staged.empty());
  reset(); Update.failBegin = true; upload(image); rejected();
  reset(); Update.failWrite = true; upload(image); rejected();
  reset(); Update.failEnd = true; upload(image); rejected();
  compressedCases(gz);
  updateFileCases(up, gz);
  // Power-cycle recovery: boots shorter than the window count up; the third triggers.
  EEPROM = MockEEPROM{}; EEPROM.flash[0x10] = 0x5a;   // a stock byte elsewhere in the sector
  assert(!countPowerCycle() && !countPowerCycle() && countPowerCycle());
  assert(!countPowerCycle());                        // trigger resets the count
  clearPowerCycles(); assert(!countPowerCycle() && !countPowerCycle());
  clearPowerCycles(); unsigned commits = EEPROM.commits;
  clearPowerCycles(); assert(EEPROM.commits == commits); // no erase when already clear
  assert(!countPowerCycle()); clearPowerCycles();   // a normal boot never accumulates
  assert(!countPowerCycle()); clearPowerCycles(); assert(!countPowerCycle());
  EEPROM.flash[CYCLE_ADDR + 4] = 200;                // corrupt count is clamped, not looped
  assert(countPowerCycle());
  assert(EEPROM.flash[0x10] == 0x5a);                // stock byte preserved
  std::cout << "PASS: power-cycle counter (trigger on 3rd short boot, reset, clamp, no needless erase, sector preserved)\n";
  std::cout << "PASS: complete/chunked/renamed, truncated, corrupt, oversized, invalid header, filesystem, multiple-file, empty, abort/retry, too big, and updater failure cases\n";
}
