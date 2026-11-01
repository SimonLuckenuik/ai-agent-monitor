// mDNS packets for the clock's own responder (spec 027): reading queries and writing
// answers, with no Arduino or network code, so the host unit tests can feed it anything.
// The parsing approach (bounded reads, compression pointers followed a few times and
// only backwards) follows smalltv-pomodoro's responder (github.com/davicorrea0/
// smalltv-pomodoro, GPL-3.0); the records and their layout are RFC 6762 and 6763.
#pragma once
#include <stddef.h>
#include <stdint.h>

// What a query asks of us (bits), and whether it wants the answer sent to it alone.
enum : uint8_t {
  MDNS_A = 1,          // <host>.local
  MDNS_PTR = 2,        // _http._tcp.local -> our instance
  MDNS_SRV = 4,        // <host>._http._tcp.local
  MDNS_TXT = 8,        // the same, its text
  MDNS_SERVICES = 16,  // _services._dns-sd._udp.local -> _http._tcp.local
};

struct MdnsQuery {
  uint8_t want = 0;     // MDNS_* bits
  bool unicast = false; // the QU bit on a question we answer
  uint16_t id = 0;
};

struct MdnsInfo {
  const char* host;     // "agentmonitor-1a2b3c", without ".local"
  uint8_t ip[4];
  uint16_t port;        // 80
  const char* const* txt;  // "key=value" strings
  uint8_t txtCount;
};

// Reads a query; false for a reply, a malformed packet, or nothing for us.
bool mdnsReadQuery(const uint8_t* p, size_t n, const char* host, MdnsQuery& q);

// Writes the answer for `want` (and the records that go with it as additional ones).
// ttlScale 0: a goodbye (TTL 0). legacy: a one-shot resolver (source port not 5353),
// which gets its id back and short TTLs. Returns the length, 0 when it does not fit.
size_t mdnsWriteAnswer(uint8_t* out, size_t cap, const MdnsInfo& info, uint8_t want,
                       uint16_t id, bool legacy, bool goodbye = false);
