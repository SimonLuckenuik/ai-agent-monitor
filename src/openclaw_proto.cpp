#include "openclaw_proto.h"

namespace {
char b64char(unsigned v) {   // the base64url alphabet, without a 64-byte table in RAM
  return v < 26 ? 'A' + v : v < 52 ? 'a' + v - 26 : v < 62 ? '0' + v - 52 : v == 62 ? '-' : '_';
}
int b64value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '-' || c == '+') return 62;
  if (c == '_' || c == '/') return 63;
  return -1;
}
}  // namespace

size_t b64urlEncode(char* out, const uint8_t* in, size_t n) {
  size_t o = 0;
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < n ? in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
    size_t chars = n - i >= 3 ? 4 : n - i + 1;
    for (size_t k = 0; k < chars; ++k) out[o++] = b64char(v >> (18 - 6 * k) & 63);
  }
  out[o] = 0;
  return o;
}

int b64urlDecode(uint8_t* out, size_t max, const char* in, size_t n) {
  uint32_t acc = 0;
  int bits = 0;
  size_t o = 0;
  for (size_t i = 0; i < n; ++i) {
    char c = in[i];
    if (c == '=' || c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
    int v = b64value(c);
    if (v < 0) return -1;
    acc = acc << 6 | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (o >= max) return -1;
      out[o++] = (uint8_t)(acc >> bits);
    }
  }
  return (int)o;
}

size_t wsTextHeader(uint8_t h[8], size_t len, const uint8_t mask[4]) {
  size_t n = 2;
  h[0] = 0x81;   // FIN, text
  if (len < 126) h[1] = 0x80 | (uint8_t)len;
  else { h[1] = 0x80 | 126; h[2] = (uint8_t)(len >> 8); h[3] = (uint8_t)len; n = 4; }   // frames stay under 64 KB
  for (int i = 0; i < 4; ++i) h[n++] = mask[i];
  return n;
}

void wsMask(uint8_t* data, size_t n, const uint8_t mask[4], size_t offset) {
  for (size_t i = 0; i < n; ++i) data[i] ^= mask[(offset + i) & 3];
}
