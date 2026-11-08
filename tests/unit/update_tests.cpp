// The update file reader (spec 048, include/update_bundle.h) against files written by
// scripts/make_update.py (run_unit_tests.py writes them and passes their paths).
//   update_tests <good.update> <files hash, hex>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "update_bundle.h"

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

struct Result {
  std::vector<std::pair<std::string, std::string>> files;
  std::string image, error;
  bool inImage = false;
  uint32_t hash = 0;
};

// Feeds data in pieces of `piece` bytes, as an upload or a download would.
static Result read(const std::string& data, size_t piece) {
  Result r;
  BundleReader b;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(data.data());
  size_t left = data.size();
  while (left || b.pending()) {
    size_t n = left < piece ? left : piece;
    const uint8_t* q = p;
    size_t m = n;
    while (m || b.pending()) {
      BundleReader::Event ev;
      size_t used = b.next(q, m, ev);
      q += used; m -= used;
      if (ev == BundleReader::FAILED) { r.error = b.error ? b.error : "?"; return r; }
      if (ev == BundleReader::FILE_START) r.files.push_back({b.name, ""});
      if (ev == BundleReader::FILE_DATA) r.files.back().second.append(reinterpret_cast<const char*>(b.data), b.len);
      if (ev == BundleReader::IMAGE) r.image.append(reinterpret_cast<const char*>(b.data), b.len);
      if (!used && ev == BundleReader::NONE) break;
    }
    p += n; left -= n;
  }
  r.inImage = b.inImage();
  r.hash = b.hash;
  return r;
}

static std::string load(const char* path) {
  FILE* f = std::fopen(path, "rb");
  if (!f) { std::printf("cannot open %s\n", path); std::exit(2); }
  std::string s;
  char buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
  std::fclose(f);
  return s;
}

int main(int argc, char** argv) {
  if (argc < 3) { std::printf("usage: update_tests <good.update> <hash>\n"); return 2; }
  std::string good = load(argv[1]);
  uint32_t hash = std::strtoul(argv[2], nullptr, 16);
  // The fixture (run_unit_tests.py): index.html.gz = 3000 bytes of i*7, empty.txt = "",
  // notes.md = "hello", then an "image" of 5000 bytes of i*13.
  std::string page, image;
  for (int i = 0; i < 3000; ++i) page += char(i * 7);
  for (int i = 0; i < 5000; ++i) image += char(i * 13);

  for (size_t piece : {size_t(1), size_t(2), size_t(7), size_t(12), size_t(13), size_t(100), size_t(1460), size_t(4096), good.size()}) {
    Result r = read(good, piece);
    CHECK(r.error.empty());
    CHECK(r.files.size() == 3);
    if (r.files.size() == 3) {
      CHECK(r.files[0].first == "index.html.gz" && r.files[0].second == page);
      CHECK(r.files[1].first == "empty.txt" && r.files[1].second.empty());
      CHECK(r.files[2].first == "notes.md" && r.files[2].second == "hello");
    }
    CHECK(r.inImage && r.image == image);
    CHECK(r.hash == hash);
  }
  CHECK(BundleReader::isBundle(reinterpret_cast<const uint8_t*>("AMUP"), 4));
  CHECK(!BundleReader::isBundle(reinterpret_cast<const uint8_t*>("\x1f\x8b"), 2));
  CHECK(BundleReader::crc32(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0x0376E6E7);   // CRC-32/MPEG-2

  std::string bad = good;
  bad[12 + 1 + 13 + 8 + 100] ^= 1;   // a byte of index.html.gz's data
  CHECK(read(bad, 512).error.find("CRC") != std::string::npos);
  bad = good; bad[0] = 'X';
  CHECK(read(bad, 512).error == "Not an update file");
  bad = good; bad[4] = 2;
  CHECK(read(bad, 512).error.find("not supported") != std::string::npos);
  bad = good; bad[5] = 0;
  CHECK(!read(bad, 512).error.empty());
  bad = good; bad[12 + 1] = '.';   // a name starting with '.'
  CHECK(read(bad, 512).error.find("name") != std::string::npos);
  bad = good; bad[12 + 1 + 2] = '/';   // a path separator
  CHECK(read(bad, 512).error.find("name") != std::string::npos);
  bad = good; bad[12 + 1 + 13 + 2] = 0x10;   // index.html.gz's size: over 256 KB
  CHECK(read(bad, 512).error.find("too big") != std::string::npos);
  Result cut = read(good.substr(0, 12 + 1 + 13 + 8 + 50), 64);   // stops inside a file
  CHECK(cut.error.empty() && !cut.inImage && cut.image.empty());

  std::printf("%s: %d update file checks, %d failed\n", fails ? "FAIL" : "PASS", checks, fails);
  return fails ? 1 : 0;
}
