// Unit tests for src/mdns_packet.cpp (spec 027): queries we answer and ignore, malformed,
// truncated and looping packets, and the answers we write, read back record by record.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "../../src/mdns_packet.h"

static int failures = 0, checks = 0;
#define CHECK(c)                                                         \
  do {                                                                   \
    ++checks;                                                            \
    if (!(c)) { ++failures; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } \
  } while (0)

typedef std::vector<uint8_t> Bytes;

static void name(Bytes& b, const char* dotted) {
  std::string s(dotted);
  size_t at = 0;
  while (at < s.size()) {
    size_t dot = s.find('.', at);
    if (dot == std::string::npos) dot = s.size();
    b.push_back(uint8_t(dot - at));
    for (size_t i = at; i < dot; ++i) b.push_back(uint8_t(s[i]));
    at = dot + 1;
  }
  b.push_back(0);
}

static void u16(Bytes& b, uint16_t v) { b.push_back(uint8_t(v >> 8)); b.push_back(uint8_t(v)); }

static Bytes query(std::vector<std::pair<const char*, uint16_t>> qs, uint16_t cls = 1, uint16_t flags = 0) {
  Bytes b;
  u16(b, 0x1234); u16(b, flags); u16(b, uint16_t(qs.size())); u16(b, 0); u16(b, 0); u16(b, 0);
  for (auto& q : qs) { name(b, q.first); u16(b, q.second); u16(b, cls); }
  return b;
}

static const char* HOST = "agentmonitor-adec8a";

static uint8_t want(const Bytes& b, bool* unicast = nullptr) {
  MdnsQuery q;
  bool ok = mdnsReadQuery(b.data(), b.size(), HOST, q);
  if (unicast) *unicast = q.unicast;
  return ok ? q.want : 0;
}

static void testQueries() {
  CHECK(want(query({{"agentmonitor-adec8a.local", 1}})) == MDNS_A);
  CHECK(want(query({{"AgentMonitor-ADEC8A.LOCAL", 1}})) == MDNS_A);   // names ignore case
  CHECK(want(query({{"agentmonitor-adec8a.local", 255}})) == MDNS_A);
  CHECK(want(query({{"agentmonitor-adec8a.local", 28}})) == 0);       // AAAA: we have none
  CHECK(want(query({{"other-clock.local", 1}})) == 0);
  CHECK(want(query({{"agentmonitor-adec8a.local.evil", 1}})) == 0);
  CHECK(want(query({{"x.agentmonitor-adec8a.local", 1}})) == 0);
  CHECK(want(query({{"_http._tcp.local", 12}})) == MDNS_PTR);
  CHECK(want(query({{"_services._dns-sd._udp.local", 12}})) == MDNS_SERVICES);
  CHECK(want(query({{"agentmonitor-adec8a._http._tcp.local", 33}})) == MDNS_SRV);
  CHECK(want(query({{"agentmonitor-adec8a._http._tcp.local", 16}})) == MDNS_TXT);
  CHECK(want(query({{"agentmonitor-adec8a._http._tcp.local", 255}})) == (MDNS_SRV | MDNS_TXT));
  CHECK(want(query({{"_ipp._tcp.local", 12}, {"agentmonitor-adec8a.local", 1}})) == MDNS_A);
  bool uni = false;
  CHECK(want(query({{"agentmonitor-adec8a.local", 1}}, 0x8001), &uni) == MDNS_A && uni);
  CHECK(want(query({{"agentmonitor-adec8a.local", 1}}, 1), &uni) == MDNS_A && !uni);
  CHECK(want(query({{"agentmonitor-adec8a.local", 1}}, 3)) == 0);      // class CH
  CHECK(want(query({{"agentmonitor-adec8a.local", 1}}, 1, 0x8400)) == 0);   // a reply
  CHECK(want(query({{"agentmonitor-adec8a.local", 1}}, 1, 0x2800)) == 0);   // an update, not a query
}

static void testCompression() {
  // Two questions; the second's "local" is a pointer back into the first.
  Bytes b = query({{"_http._tcp.local", 12}});
  size_t localAt = 12 + 1 + 5 + 1 + 4;   // after "_http" and "_tcp" labels
  b[5] = 2;
  b.push_back(19); for (const char* c = HOST; *c; ++c) b.push_back(uint8_t(*c));
  b.push_back(0xC0); b.push_back(uint8_t(localAt));
  u16(b, 1); u16(b, 1);
  CHECK(want(b) == (MDNS_PTR | MDNS_A));

  // A pointer to itself, a pointer forward, and two pointing at each other.
  Bytes self;
  u16(self, 0); u16(self, 0); u16(self, 1); u16(self, 0); u16(self, 0); u16(self, 0);
  self.push_back(0xC0); self.push_back(12); u16(self, 1); u16(self, 1);
  CHECK(want(self) == 0);
  Bytes fwd = self;
  fwd[13] = 20;
  CHECK(want(fwd) == 0);
  Bytes pair;
  u16(pair, 0); u16(pair, 0); u16(pair, 1); u16(pair, 0); u16(pair, 0); u16(pair, 0);
  pair.push_back(1); pair.push_back('a'); pair.push_back(0xC0); pair.push_back(12);   // a -> itself via start
  u16(pair, 1); u16(pair, 1);
  CHECK(want(pair) == 0);

  // A name longer than any we answer, and labels with the reserved 01/10 types.
  Bytes longName;
  u16(longName, 0); u16(longName, 0); u16(longName, 1); u16(longName, 0); u16(longName, 0); u16(longName, 0);
  for (int i = 0; i < 5; ++i) { longName.push_back(60); for (int k = 0; k < 60; ++k) longName.push_back('x'); }
  longName.push_back(0); u16(longName, 1); u16(longName, 1);
  CHECK(want(longName) == 0);
  Bytes reserved = query({{"agentmonitor-adec8a.local", 1}});
  reserved[12] |= 0x40;
  CHECK(want(reserved) == 0);
}

static void testTruncatedAndRandom() {
  Bytes full = query({{"agentmonitor-adec8a.local", 1}});
  for (size_t n = 0; n < full.size(); ++n) {
    MdnsQuery q;
    CHECK(!mdnsReadQuery(full.data(), n, HOST, q));
  }
  CHECK(want(full) == MDNS_A);
  // Random packets, and random changes to a good one: no crash, no read past the end
  // (each buffer is exactly its length, so a sanitizer or the heap checks would catch it).
  srand(27);
  for (int i = 0; i < 200000; ++i) {
    size_t n = size_t(rand() % 300);
    uint8_t* p = (uint8_t*)malloc(n ? n : 1);
    if (i % 2) for (size_t k = 0; k < n; ++k) p[k] = uint8_t(rand());
    else {
      n = full.size();
      p = (uint8_t*)realloc(p, n);
      memcpy(p, full.data(), n);
      for (int k = 0; k < 3; ++k) p[rand() % n] = uint8_t(rand());
    }
    MdnsQuery q;
    mdnsReadQuery(p, n, HOST, q);
    free(p);
  }
  CHECK(true);
}

// Reads our answer back: record names, types, TTLs and data.
struct Rec { std::string name; uint16_t type, cls; uint32_t ttl; Bytes data; size_t dataAt; };

static bool readName(const Bytes& b, size_t& at, std::string& out) {
  out.clear();
  while (at < b.size() && b[at]) {
    if ((b[at] & 0xC0) == 0xC0) return false;   // we never compress
    size_t l = b[at];
    if (at + 1 + l > b.size()) return false;
    if (!out.empty()) out += '.';
    out.append((const char*)&b[at + 1], l);
    at += 1 + l;
  }
  ++at;
  return at <= b.size();
}

static bool parse(const Bytes& b, uint16_t& id, std::vector<Rec>& answers, std::vector<Rec>& extra) {
  if (b.size() < 12) return false;
  id = uint16_t(b[0] << 8 | b[1]);
  if ((b[2] << 8 | b[3]) != 0x8400 || b[4] || b[5] || b[8] || b[9]) return false;
  int an = b[6] << 8 | b[7], ar = b[10] << 8 | b[11];
  size_t at = 12;
  for (int i = 0; i < an + ar; ++i) {
    Rec r;
    if (!readName(b, at, r.name) || at + 10 > b.size()) return false;
    r.type = uint16_t(b[at] << 8 | b[at + 1]);
    r.cls = uint16_t(b[at + 2] << 8 | b[at + 3]);
    r.ttl = uint32_t(b[at + 4]) << 24 | uint32_t(b[at + 5]) << 16 | uint32_t(b[at + 6]) << 8 | b[at + 7];
    size_t len = size_t(b[at + 8] << 8 | b[at + 9]);
    at += 10;
    if (at + len > b.size()) return false;
    r.dataAt = at;
    r.data.assign(b.begin() + long(at), b.begin() + long(at + len));
    at += len;
    (i < an ? answers : extra).push_back(r);
  }
  return at == b.size();
}

static const Rec* find(const std::vector<Rec>& v, uint16_t type) {
  for (auto& r : v) if (r.type == type) return &r;
  return nullptr;
}

static void testAnswers() {
  static const char* TXT[] = {"model=AI Agent Monitor", "version=2.1.0", "path=/"};
  MdnsInfo info{HOST, {192, 168, 66, 229}, 80, TXT, 3};
  uint8_t buf[512];
  std::vector<Rec> an, ex;
  uint16_t id;

  size_t n = mdnsWriteAnswer(buf, sizeof buf, info, MDNS_A, 0x1234, false);
  Bytes b(buf, buf + n);
  CHECK(n > 0 && parse(b, id, an, ex) && id == 0 && an.size() == 1 && ex.empty());
  CHECK(an[0].name == "agentmonitor-adec8a.local" && an[0].type == 1 && an[0].cls == 0x8001 && an[0].ttl == 120);
  CHECK(an[0].data == Bytes({192, 168, 66, 229}));

  an.clear(); ex.clear();
  n = mdnsWriteAnswer(buf, sizeof buf, info, MDNS_PTR, 0, false);
  b.assign(buf, buf + n);
  CHECK(n > 0 && n < 400 && parse(b, id, an, ex) && an.size() == 1 && ex.size() == 3);
  CHECK(an[0].name == "_http._tcp.local" && an[0].type == 12 && an[0].cls == 1 && an[0].ttl == 4500);
  size_t at = an[0].dataAt;
  std::string target;
  CHECK(readName(b, at, target) && target == "agentmonitor-adec8a._http._tcp.local");
  const Rec* srv = find(ex, 33);
  CHECK(srv && srv->name == "agentmonitor-adec8a._http._tcp.local" && srv->data.size() > 6 &&
        (srv->data[4] << 8 | srv->data[5]) == 80);
  at = srv ? srv->dataAt + 6 : 0;
  CHECK(srv && readName(b, at, target) && target == "agentmonitor-adec8a.local");
  const Rec* txt = find(ex, 16);
  std::string t = txt ? std::string(txt->data.begin(), txt->data.end()) : "";
  CHECK(txt && t.find("model=AI Agent Monitor") != std::string::npos && t.find("version=2.1.0") != std::string::npos);
  CHECK(find(ex, 1) != nullptr);

  // Everything at once (an announcement), a goodbye, a legacy resolver, a buffer too small.
  an.clear(); ex.clear();
  uint8_t all = MDNS_A | MDNS_PTR | MDNS_SRV | MDNS_TXT | MDNS_SERVICES;
  n = mdnsWriteAnswer(buf, sizeof buf, info, all, 0, false);
  b.assign(buf, buf + n);
  CHECK(n > 0 && n < 400 && parse(b, id, an, ex) && an.size() == 5 && ex.empty());
  an.clear(); ex.clear();
  n = mdnsWriteAnswer(buf, sizeof buf, info, all, 0, false, true);
  b.assign(buf, buf + n);
  bool zero = parse(b, id, an, ex);
  for (auto& r : an) zero = zero && r.ttl == 0;
  CHECK(zero);
  an.clear(); ex.clear();
  n = mdnsWriteAnswer(buf, sizeof buf, info, MDNS_A, 0xBEEF, true);
  b.assign(buf, buf + n);
  CHECK(parse(b, id, an, ex) && id == 0xBEEF && an[0].ttl == 10 && an[0].cls == 1);
  for (size_t cap = 0; cap < 40; ++cap) CHECK(mdnsWriteAnswer(buf, cap, info, MDNS_A, 0, false) == 0);
  CHECK(mdnsWriteAnswer(buf, sizeof buf, info, 0, 0, false) == 0);
}

int main() {
  testQueries();
  testCompression();
  testTruncatedAndRandom();
  testAnswers();
  printf("%s: %d mDNS checks, %d failed\n", failures ? "FAIL" : "PASS", checks, failures);
  return failures ? 1 : 0;
}
