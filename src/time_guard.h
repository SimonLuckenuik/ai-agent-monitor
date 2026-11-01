// The clock's time against forged SNTP replies (spec 029). SNTP takes any UDP reply, so one
// packet from anywhere on the network could move the clock. The first time after boot is
// taken as it comes; a later jump of more than JUMP_S is held (the clock keeps counting
// from what it had) until a second source agrees: the Date header of the weather reply,
// which comes over TCP (forging it takes a machine in the path). Without weather, a jump
// that every SNTP reply has repeated for HOLD_MAX_MS is taken, so a clock with the weather
// off cannot stay wrong for good. Plain C++, no Arduino: the unit tests drive it.
// The approach follows smalltv-pomodoro's TimeGuard (github.com/davicorrea0/smalltv-pomodoro,
// GPL-3.0).
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct TimeGuard {
  static constexpr int32_t JUMP_S = 120;                      // what counts as a jump
  static constexpr uint32_t HOLD_MAX_MS = 6UL * 3600 * 1000;  // repeated this long: taken

  uint32_t trusted = 0, trustedAt = 0;   // a Unix time we trust, and millis() then
  uint32_t pending = 0, pendingAt = 0, pendingSince = 0;   // a held jump; when it was first heard

  bool held() const { return pending != 0; }
  uint32_t expected(uint32_t ms) const { return trusted + (ms - trustedAt) / 1000; }

  // SNTP set the clock to t at ms: returns the time the clock should show.
  uint32_t sntp(uint32_t t, uint32_t ms) {
    if (!trusted || near(t, expected(ms))) return trust(t, ms);
    if (pending && near(t, pending + (ms - pendingAt) / 1000)) {   // the same jump again
      if (ms - pendingSince >= HOLD_MAX_MS) return trust(t, ms);
      pending = t; pendingAt = ms;
      return expected(ms);
    }
    pending = t; pendingAt = pendingSince = ms;   // a new jump: held
    return expected(ms);
  }

  // A second source says t at ms: returns the time to set, or 0 for no change. It confirms
  // a held jump; alone, it never moves the clock.
  uint32_t confirm(uint32_t t, uint32_t ms) {
    if (!t || !pending) return 0;
    uint32_t p = pending + (ms - pendingAt) / 1000;
    if (!near(t, p)) return 0;
    return trust(p, ms);
  }

 private:
  static bool near(uint32_t a, uint32_t b) { return labs((long)(int32_t)(a - b)) <= JUMP_S; }
  uint32_t trust(uint32_t t, uint32_t ms) {
    trusted = t; trustedAt = ms; pending = 0;
    return t;
  }
};

// An HTTP Date header ("Sun, 01 Nov 2026 01:23:45 GMT", RFC 7231) as a Unix time; 0 when it
// is not one.
inline uint32_t parseHttpDate(const char* s) {
  static const char MONTHS[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  if (!s) return 0;
  const char* comma = strchr(s, ',');
  if (comma) s = comma + 1;
  while (*s == ' ') ++s;
  char mon[4] = {0};
  int d, y, h, m, sec;
  if (strlen(s) < 20) return 0;
  d = atoi(s);
  const char* p = strchr(s, ' ');
  if (!p) return 0;
  memcpy(mon, p + 1, 3);
  const char* mp = strstr(MONTHS, mon);
  if (!mp || (mp - MONTHS) % 3) return 0;
  int month = int(mp - MONTHS) / 3 + 1;
  p = strchr(p + 1, ' ');
  if (!p) return 0;
  y = atoi(p + 1);
  p = strchr(p + 1, ' ');
  if (!p || strlen(p) < 9 || p[3] != ':' || p[6] != ':') return 0;
  h = atoi(p + 1); m = atoi(p + 4); sec = atoi(p + 7);
  if (d < 1 || d > 31 || y < 2020 || y > 2100 || h > 23 || m > 59 || sec > 60) return 0;
  // Days since 1970-01-01 (civil-from-days, Howard Hinnant's algorithm).
  int yy = y - (month <= 2);
  int era = yy / 400, yoe = yy - era * 400;
  int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = long(era) * 146097 + doe - 719468;
  return uint32_t(days * 86400L + h * 3600L + m * 60L + sec);
}
