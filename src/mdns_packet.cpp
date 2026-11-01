// See mdns_packet.h.
#include "mdns_packet.h"

#include <string.h>

namespace {

constexpr uint16_t T_A = 1, T_PTR = 12, T_TXT = 16, T_SRV = 33, T_ANY = 255;
constexpr uint16_t CLASS_IN = 1, FLUSH = 0x8000;   // FLUSH: the cache-flush bit on our own records
constexpr int MAX_JUMPS = 8;                       // compression pointers followed per name

uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }

// The name at `pos`, dotted and lower case, into out[cap]. *next: where the record goes on
// (after the name in the packet, whatever pointers were followed).
bool readName(const uint8_t* p, size_t n, size_t pos, char* out, size_t cap, size_t* next) {
  size_t len = 0, at = pos, limit = pos;   // limit: a pointer must go before where it was read
  int jumps = 0;
  bool jumped = false;
  for (;;) {
    if (at >= n) return false;
    uint8_t l = p[at];
    if (l == 0) {
      if (!jumped) *next = at + 1;
      break;
    }
    if ((l & 0xC0) == 0xC0) {
      if (at + 1 >= n || ++jumps > MAX_JUMPS) return false;
      size_t to = size_t(l & 0x3F) << 8 | p[at + 1];
      if (to >= limit) return false;       // only backwards: no loops
      if (!jumped) *next = at + 2;
      jumped = true;
      at = limit = to;
      continue;
    }
    if (l & 0xC0) return false;            // the 01 and 10 label types are not used
    if (at + 1 + l > n || len + l + 2 > cap) return false;
    if (len) out[len++] = '.';
    for (size_t i = 0; i < l; ++i) {
      char c = char(p[at + 1 + i]);
      out[len++] = c >= 'A' && c <= 'Z' ? char(c + 32) : c;
    }
    at += 1 + l;
  }
  out[len] = 0;
  return true;
}

// "<prefix>.<suffix>" (prefix may be null) equals the dotted name, ignoring case in `prefix`.
bool sameName(const char* name, const char* prefix, const char* suffix) {
  if (prefix) {
    size_t l = strlen(prefix);
    for (size_t i = 0; i < l; ++i) {
      char c = prefix[i];
      if (c >= 'A' && c <= 'Z') c = char(c + 32);
      if (name[i] != c) return false;
    }
    if (name[l] != '.') return false;
    name += l + 1;
  }
  return strcmp(name, suffix) == 0;
}

struct Writer {
  uint8_t* o;
  size_t cap, len = 0;
  bool ok = true;
  void put(uint8_t b) { if (len < cap) o[len++] = b; else ok = false; }
  void put16(uint16_t v) { put(uint8_t(v >> 8)); put(uint8_t(v)); }
  void put32(uint32_t v) { put16(uint16_t(v >> 16)); put16(uint16_t(v)); }
  void label(const char* s, size_t l) {
    if (l == 0 || l > 63) { ok = false; return; }
    put(uint8_t(l));
    for (size_t i = 0; i < l; ++i) put(uint8_t(s[i]));
  }
  void labels(const char* dotted) {   // "a.b.c", no trailing zero
    while (*dotted) {
      const char* dot = strchr(dotted, '.');
      size_t l = dot ? size_t(dot - dotted) : strlen(dotted);
      label(dotted, l);
      dotted += l + (dot ? 1 : 0);
    }
  }
  void name(const char* prefix, const char* suffix) {
    if (prefix) labels(prefix);
    labels(suffix);
    put(0);
  }
  size_t mark() { put16(0); return len; }   // a length to fill in later
  void fill(size_t at) {
    if (!ok) return;
    size_t l = len - at;
    o[at - 2] = uint8_t(l >> 8);
    o[at - 1] = uint8_t(l);
  }
};

const char* const DOT_LOCAL = "local";
const char* const HTTP_TCP = "_http._tcp.local";
const char* const SD_SERVICES = "_services._dns-sd._udp.local";

void record(Writer& w, const MdnsInfo& info, uint8_t which, uint32_t ttlShort, uint32_t ttlLong, bool flush) {
  uint16_t own = CLASS_IN | (flush ? FLUSH : 0);
  switch (which) {
    case MDNS_A:
      w.name(info.host, DOT_LOCAL);
      w.put16(T_A); w.put16(own); w.put32(ttlShort); w.put16(4);
      for (uint8_t b : info.ip) w.put(b);
      break;
    case MDNS_PTR: {
      w.name(nullptr, HTTP_TCP);
      w.put16(T_PTR); w.put16(CLASS_IN); w.put32(ttlLong);
      size_t at = w.mark();
      w.name(info.host, HTTP_TCP);
      w.fill(at);
      break;
    }
    case MDNS_SRV: {
      w.name(info.host, HTTP_TCP);
      w.put16(T_SRV); w.put16(own); w.put32(ttlShort);
      size_t at = w.mark();
      w.put16(0); w.put16(0); w.put16(info.port);
      w.name(info.host, DOT_LOCAL);
      w.fill(at);
      break;
    }
    case MDNS_TXT: {
      w.name(info.host, HTTP_TCP);
      w.put16(T_TXT); w.put16(own); w.put32(ttlLong);
      size_t at = w.mark();
      for (uint8_t i = 0; i < info.txtCount; ++i) {
        size_t l = strlen(info.txt[i]);
        if (l > 255) { w.ok = false; break; }
        w.put(uint8_t(l));
        for (size_t k = 0; k < l; ++k) w.put(uint8_t(info.txt[i][k]));
      }
      if (!info.txtCount) w.put(0);   // an empty TXT is one zero byte
      w.fill(at);
      break;
    }
    case MDNS_SERVICES: {
      w.name(nullptr, SD_SERVICES);
      w.put16(T_PTR); w.put16(CLASS_IN); w.put32(ttlLong);
      size_t at = w.mark();
      w.name(nullptr, HTTP_TCP);
      w.fill(at);
      break;
    }
  }
}

}  // namespace

bool mdnsReadQuery(const uint8_t* p, size_t n, const char* host, MdnsQuery& q) {
  q = MdnsQuery();
  if (!p || n < 12) return false;
  uint16_t flags = rd16(p + 2), count = rd16(p + 4);
  if (flags & 0x8000) return false;           // a reply, not a query
  if (flags & 0x7800) return false;           // only the standard query opcode
  if (count == 0 || count > 32) return false;
  q.id = rd16(p);
  size_t pos = 12;
  char name[128];
  for (uint16_t i = 0; i < count; ++i) {
    size_t next = 0;
    if (!readName(p, n, pos, name, sizeof name, &next) || next + 4 > n) return false;
    uint16_t type = rd16(p + next), cls = rd16(p + next + 2);
    pos = next + 4;
    if ((cls & 0x7FFF) != CLASS_IN && (cls & 0x7FFF) != T_ANY) continue;
    uint8_t got = 0;
    if (sameName(name, host, DOT_LOCAL) && (type == T_A || type == T_ANY)) got = MDNS_A;
    else if (sameName(name, nullptr, HTTP_TCP) && (type == T_PTR || type == T_ANY)) got = MDNS_PTR;
    else if (sameName(name, host, HTTP_TCP))
      got = type == T_SRV ? MDNS_SRV : type == T_TXT ? MDNS_TXT : type == T_ANY ? MDNS_SRV | MDNS_TXT : 0;
    else if (sameName(name, nullptr, SD_SERVICES) && (type == T_PTR || type == T_ANY)) got = MDNS_SERVICES;
    if (got) {
      q.want |= got;
      if (cls & 0x8000) q.unicast = true;
    }
  }
  return q.want != 0;
}

size_t mdnsWriteAnswer(uint8_t* out, size_t cap, const MdnsInfo& info, uint8_t want,
                       uint16_t id, bool legacy, bool goodbye) {
  if (!out || !want) return 0;
  // What travels as additional records: what a browser needs next, unless already answered.
  uint8_t extra = 0;
  if (want & MDNS_PTR) extra |= MDNS_SRV | MDNS_TXT | MDNS_A;
  if (want & MDNS_SRV) extra |= MDNS_A;
  extra &= uint8_t(~want);
  uint32_t ttlShort = goodbye ? 0 : legacy ? 10 : 120, ttlLong = goodbye ? 0 : legacy ? 10 : 4500;
  bool flush = !legacy;
  static const uint8_t ORDER[] = {MDNS_PTR, MDNS_SERVICES, MDNS_SRV, MDNS_TXT, MDNS_A};
  uint16_t answers = 0, additional = 0;
  for (uint8_t r : ORDER) {
    if (want & r) answers++;
    if (extra & r) additional++;
  }
  Writer w{out, cap};
  w.put16(legacy ? id : 0);
  w.put16(0x8400);   // a reply, authoritative
  w.put16(0); w.put16(answers); w.put16(0); w.put16(additional);
  for (uint8_t r : ORDER) if (want & r) record(w, info, r, ttlShort, ttlLong, flush);
  for (uint8_t r : ORDER) if (extra & r) record(w, info, r, ttlShort, ttlLong, flush);
  return w.ok ? w.len : 0;
}
