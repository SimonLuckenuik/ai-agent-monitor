// Crash records and crash-loop protection (spec 021).
//
// The core calls custom_crash_callback() on any crash (exception, watchdog, abort,
// stack smash) before the restart. It keeps what is needed to find the cause in RTC
// memory, which survives the restart: the core's reason and exception registers, the
// words on the stack that look like code addresses, the face being drawn. The next boot
// moves the record into /crashes.bin (the last 8; they survive power cuts) and counts
// crash loops: a crash within 10 minutes of boot counts, 10 minutes up clears the count.
// After 3 in a row custom faces are off; after 2 more, safe mode draws no face at all,
// so the settings page and the API stay reachable. Resume clears it.
// The log is binary records, served as they are by GET /api/crashes: JSON here would cost
// several KB of flash. scripts/decode_crash.py turns the addresses into file:line.
#include "crash.h"
#include <LittleFS.h>
#include <user_interface.h>
#include "build_info.h"

int crashFace = -1;
const char* crashCustom = nullptr;

namespace {

constexpr uint32_t CRASH_MAGIC = 0x48535243;   // "CRSH"
constexpr uint32_t LOG_MAGIC = 0x474c5243;     // "CRLG", version 1 of the file layout
constexpr int CRASH_SLOT = 40;                 // RTC user memory, 4-byte slots; the boot marker is at 32
constexpr int ADDRS = 16, LOG_MAX = 8;
constexpr uint32_t LOOP_WINDOW_S = 600;
constexpr int OFF_AT = 3, SAFE_AT = 5;
const char* const LOG_PATH = "/crashes.bin";
const char* const LOG_TEMP = "/crashes.new";

struct Record {
  uint32_t magic, build, reason, exccause, epc1, epc2, epc3, excvaddr, depc;
  uint32_t uptime, heap, stackFree;
  int32_t face;
  char custom[24];
  uint32_t nAddrs, addrs[ADDRS];
};
static_assert(CRASH_SLOT * 4 + sizeof(Record) <= 512, "RTC user memory is 512 bytes");

struct Entry { Record r; uint32_t at; char version[12]; };   // a record as logged
struct Header { uint32_t magic, count, loop, level, n; };   // then n entries, newest first
// GET /api/crashes serves the file as it is: the settings page and
// scripts/decode_crash.py read this layout (little-endian; docs/specifications/integrations-api.md).
static_assert(sizeof(Record) == 144 && sizeof(Entry) == 160 && sizeof(Header) == 20, "the file layout others read");

uint32_t hashOf(const char* s) {
  uint32_t h = 2166136261u;
  while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
  return h;
}

Record crashRec;                        // static, not on the stack: the callback runs on what is left of it
uint32_t buildId = hashOf(FW_BUILD);    // tells a record from another firmware's
Header head{LOG_MAGIC, 0, 0, 0, 0};
bool tickCleared = false;

// Code lives in IRAM (0x40100000) and in flash mapped at 0x40200000.
bool codeAddress(uint32_t a) {
  return (a >= 0x40100000 && a < 0x40110000) || (a >= 0x40201000 && a < 0x40300000);
}

const char* reasonText(uint32_t r) {
  switch (r) {
    case 1: return "hardware watchdog";
    case 2: return "exception";
    case 3: return "software watchdog";
    case 253: return "stack overflow";
    case 254: return "panic";
    default: return "crash";
  }
}

bool readEntry(File& f, int i, Entry& e) {
  return f.seek(sizeof(Header) + i * sizeof(Entry)) && f.read(reinterpret_cast<uint8_t*>(&e), sizeof(e)) == sizeof(e);
}

void saveHead() {
  File f = LittleFS.open(LOG_PATH, "r+");
  if (!f) f = LittleFS.open(LOG_PATH, "w");
  if (!f) return;
  f.write(reinterpret_cast<const uint8_t*>(&head), sizeof(head));
  f.close();
}

}  // namespace

extern "C" void custom_crash_callback(struct rst_info* info, uint32_t stack, uint32_t stack_end) {
  Record& r = crashRec;
  memset(&r, 0, sizeof(r));
  r.magic = CRASH_MAGIC;
  r.build = buildId;
  if (info) {
    r.reason = info->reason; r.exccause = info->exccause;
    r.epc1 = info->epc1; r.epc2 = info->epc2; r.epc3 = info->epc3;
    r.excvaddr = info->excvaddr; r.depc = info->depc;
  }
  r.uptime = millis() / 1000;
  r.heap = ESP.getFreeHeap();
  r.stackFree = ESP.getFreeContStack();
  r.face = crashFace;
  if (crashCustom) strncpy(r.custom, crashCustom, sizeof(r.custom) - 1);
  for (uint32_t a = stack; a + 4 <= stack_end && r.nAddrs < ADDRS; a += 4) {
    uint32_t w = *reinterpret_cast<const uint32_t*>(a);
    if (codeAddress(w)) r.addrs[r.nAddrs++] = w;
  }
  ESP.rtcUserMemoryWrite(CRASH_SLOT, reinterpret_cast<uint32_t*>(&r), sizeof(r));
}

void crashBoot(const char* version) {
  File f = LittleFS.open(LOG_PATH, "r");
  if (f && (f.read(reinterpret_cast<uint8_t*>(&head), sizeof(head)) != sizeof(head) || head.magic != LOG_MAGIC))
    head = Header{LOG_MAGIC, 0, 0, 0, 0};
  head.n = std::min<uint32_t>(head.n, LOG_MAX);
  Entry fresh;
  if (!ESP.rtcUserMemoryRead(CRASH_SLOT, reinterpret_cast<uint32_t*>(&fresh.r), sizeof(fresh.r)) ||
      fresh.r.magic != CRASH_MAGIC) {
    if (f) f.close();
    return;
  }
  // Logged: a later restart does not count it again.
  uint32_t zero = 0;
  ESP.rtcUserMemoryWrite(CRASH_SLOT, &zero, sizeof(zero));
  time_t now = time(nullptr);
  fresh.at = now > 1600000000 ? (uint32_t)now : 0;   // known only once the clock has the time
  memset(fresh.version, 0, sizeof(fresh.version));
  strncpy(fresh.version, version, sizeof(fresh.version) - 1);

  head.count++;
  head.loop = fresh.r.uptime < LOOP_WINDOW_S ? head.loop + 1 : 1;
  if (head.loop >= SAFE_AT) head.level = 2;
  else if (head.loop >= OFF_AT && head.level < 1) head.level = 1;

  // The new log, through a second file one entry at a time (no RAM to hold the log):
  // this entry, then the older ones (the oldest falls off).
  File w = LittleFS.open(LOG_TEMP, "w");
  if (!w) { if (f) f.close(); return; }
  uint32_t old = head.n;
  head.n = 1;
  w.write(reinterpret_cast<const uint8_t*>(&head), sizeof(head));
  w.write(reinterpret_cast<const uint8_t*>(&fresh), sizeof(fresh));
  for (uint32_t i = 0; f && i < old && head.n < (uint32_t)LOG_MAX; ++i) {
    if (!readEntry(f, i, fresh)) break;
    w.write(reinterpret_cast<const uint8_t*>(&fresh), sizeof(fresh));
    head.n++;
  }
  if (f) f.close();
  w.seek(0);
  w.write(reinterpret_cast<const uint8_t*>(&head), sizeof(head));
  w.close();
  LittleFS.remove(LOG_PATH);
  LittleFS.rename(LOG_TEMP, LOG_PATH);
}

void crashTick() {
  if (tickCleared || !head.loop || millis() < LOOP_WINDOW_S * 1000) return;
  tickCleared = true;
  head.loop = 0;   // 10 minutes up: not a loop; the level stays until Resume
  saveHead();
}

int crashLevel() { return head.level; }
int crashLoopCount() { return head.loop; }

void crashStatus(JsonDocument& doc) {
  if (!head.n && !head.level) return;
  doc["crashes"] = head.count;
  doc["crashLoop"] = head.loop;
  doc["safeMode"] = head.level;
  File f = LittleFS.open(LOG_PATH, "r");
  Entry e;
  if (!f || !head.n || !readEntry(f, 0, e)) return;
  f.close();
  JsonObject o = doc["lastCrash"].to<JsonObject>();
  o["reason"] = reasonText(e.r.reason);
  if (e.at) o["at"] = e.at;
  o["uptime"] = e.r.uptime;
  o["face"] = e.r.face;
  if (e.r.custom[0]) o["custom"] = String(e.r.custom);
}

const char* crashLogPath() { return LittleFS.exists(LOG_PATH) ? LOG_PATH : nullptr; }

void crashResume() {
  head.loop = 0;
  head.level = 0;
  saveHead();
}

void crashClear() {
  head = Header{LOG_MAGIC, 0, 0, 0, 0};
  LittleFS.remove(LOG_PATH);
}
