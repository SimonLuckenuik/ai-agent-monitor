// Unit tests for src/time_guard.h (spec 029): forged SNTP jumps held until confirmed, and
// the HTTP Date header read as a Unix time.
#include <stdio.h>

#include "../../src/time_guard.h"

static int failures = 0, checks = 0;
#define CHECK(c)                                                         \
  do {                                                                   \
    ++checks;                                                            \
    if (!(c)) { ++failures; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } \
  } while (0)

static void testDates() {
  CHECK(parseHttpDate("Sun, 01 Nov 2026 01:23:45 GMT") == 1793496225u);
  CHECK(parseHttpDate("Thu, 29 Feb 2024 23:59:59 GMT") == 1709251199u);
  CHECK(parseHttpDate("Fri, 01 Jan 2027 00:00:00 GMT") == 1798761600u);
  CHECK(parseHttpDate("01 Nov 2026 01:23:45 GMT") == 1793496225u);   // no weekday
  CHECK(parseHttpDate("") == 0);
  CHECK(parseHttpDate(nullptr) == 0);
  CHECK(parseHttpDate("Sun, 01 Foo 2026 01:23:45 GMT") == 0);
  CHECK(parseHttpDate("Sun, 01 Nov 2026 25:23:45 GMT") == 0);
  CHECK(parseHttpDate("Sun, 01 Nov 1994 01:23:45 GMT") == 0);         // before any clock of ours
  CHECK(parseHttpDate("Sun, 01 Nov 2026 0123:45") == 0);
  CHECK(parseHttpDate("garbage") == 0);
}

static void testGuard() {
  const uint32_t T = 1793496225u;
  TimeGuard g;
  CHECK(g.sntp(T, 1000) == T && !g.held());                        // the first time is taken
  CHECK(g.sntp(T + 60 + 30, 61000) == T + 90 && !g.held());       // 30 s of drift: fine
  // A forged jump of a year: held, the clock keeps its own time.
  uint32_t fake = T + 365 * 86400u;
  CHECK(g.sntp(fake, 120000) == T + 90 + 59 && g.held());
  CHECK(g.expected(180000) == T + 90 + 119);
  // The weather's Date header disagrees with the jump: nothing changes.
  CHECK(g.confirm(T + 200, 180000) == 0 && g.held());
  // The real time comes back from SNTP: the held jump is dropped.
  CHECK(g.sntp(T + 90 + 120, 181000) == T + 210 && !g.held());

  // A real jump (the clock was wrong): held, then the Date header agrees.
  TimeGuard h;
  h.sntp(T, 0);
  CHECK(h.sntp(T + 7200, 10000) == T + 10 && h.held());
  CHECK(h.confirm(T + 7200 + 50, 60000) == T + 7250 && !h.held());
  CHECK(h.expected(70000) == T + 7260);
  CHECK(h.confirm(T, 80000) == 0);                                  // alone, it never moves the clock

  // Without weather: the same jump repeated for 6 hours is taken.
  TimeGuard k;
  k.sntp(T, 0);
  uint32_t ms = 0;
  bool taken = false;
  for (int i = 1; i <= 8 && !taken; ++i) {
    ms = i * 3600000u;
    uint32_t shown = k.sntp(T + 3600 + ms / 1000, ms);
    taken = shown == T + 3600 + ms / 1000;
  }
  CHECK(taken && ms == 7 * 3600000u && !k.held());   // first heard at 1 h, taken 6 h later

  // millis() wraps after 49 days: the arithmetic is unsigned.
  TimeGuard w;
  w.sntp(T, 0xFFFF0000u);
  CHECK(w.sntp(T + 0x20000 / 1000, 0x00010000u) == T + 0x20000 / 1000 && !w.held());
}

int main() {
  testDates();
  testGuard();
  printf("%s: %d guard checks, %d failed\n", failures ? "FAIL" : "PASS", checks, failures);
  return failures ? 1 : 0;
}
