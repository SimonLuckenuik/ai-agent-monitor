#include "ascii.h"

String asciiFold(const String& s) {
  static const char LATIN1[] = "AAAAAA*CEEEEIIIIDNOOOOOxOUUUUYT*aaaaaa*ceeeeiiiidnooooo/ouuuuyty";
  String out;
  for (unsigned i = 0; i < s.length(); ++i) {
    uint8_t c = s[i];
    if (c < 0x80) { out += (char)c; continue; }
    uint8_t c1 = i + 1 < s.length() ? (uint8_t)s[i + 1] : 0, c2 = i + 2 < s.length() ? (uint8_t)s[i + 2] : 0;
    if (c == 0xC3 && c1 >= 0x80 && c1 <= 0xBF) {
      char a = LATIN1[c1 - 0x80];
      if (a != '*') out += a;
      else out += c1 == 0x86 ? "AE" : c1 == 0xA6 ? "ae" : "ss";
      ++i;
    } else if (c == 0xC5 && (c1 == 0x92 || c1 == 0x93)) {
      out += c1 == 0x92 ? "OE" : "oe";
      ++i;
    } else if (c == 0xE2 && c1 == 0x80 && c2) {
      out += c2 == 0x98 || c2 == 0x99 ? "'" : c2 == 0x9C || c2 == 0x9D ? "\"" : c2 == 0x93 || c2 == 0x94 ? "-" : c2 == 0xA6 ? "..." : "?";
      i += 2;
    } else {
      out += '?';   // and skip the rest of this UTF-8 sequence
      while (i + 1 < s.length() && ((uint8_t)s[i + 1] & 0xC0) == 0x80) ++i;
    }
  }
  return out;
}
