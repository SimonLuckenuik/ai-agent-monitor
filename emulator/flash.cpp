// The chip's flash for ESP.flashRead/flashWrite/flashEraseSector, which the updater
// uses to stage an update (spec 048). The host core's versions drop every write, so a
// staged image never passed the updater's final check and no update could complete.
// build.sh takes the core's versions out. Storage (LittleFS) has its own emulated flash
// (flash_hal_mock.cpp) and is not touched here. The emulator keeps running its own
// binary after an "update": only the update path, the files it brings and the restart
// are emulated.
#include <Arduino.h>
#include <cstring>

extern "C" void* __libc_malloc(size_t);

namespace {
constexpr uint32_t SIZE = 4 * 1024 * 1024, SECTOR = 4096;
// The whole 4 MB chip, erased (0xff), from the host heap: it is not the clock's RAM
// (memory.cpp would count anything allocated while the firmware runs). Addresses wrap
// at 4 MB as on the chip: the host core puts the filesystem at 0, so the updater stages
// "below" it, near 3 GB.
uint8_t* chip() {
  static uint8_t* mem = nullptr;
  if (!mem) { mem = static_cast<uint8_t*>(__libc_malloc(SIZE)); memset(mem, 0xff, SIZE); }
  return mem;
}
uint8_t& at(uint32_t address) { return chip()[address & (SIZE - 1)]; }
}  // namespace

bool EspClass::flashEraseSector(uint32_t sector) {
  for (uint32_t i = 0; i < SECTOR; ++i) at(sector * SECTOR + i) = 0xff;
  return true;
}

bool EspClass::flashWrite(uint32_t offset, const uint8_t* data, size_t size) {
  for (size_t i = 0; i < size; ++i) at(offset + i) &= data[i];   // NOR flash: a write only clears bits
  return true;
}

bool EspClass::flashWrite(uint32_t offset, const uint32_t* data, size_t size) {
  return flashWrite(offset, reinterpret_cast<const uint8_t*>(data), size);
}

bool EspClass::flashRead(uint32_t offset, uint8_t* data, size_t size) {
  for (size_t i = 0; i < size; ++i) data[i] = at(offset + i);
  return true;
}

bool EspClass::flashRead(uint32_t offset, uint32_t* data, size_t size) {
  return flashRead(offset, reinterpret_cast<uint8_t*>(data), size);
}
