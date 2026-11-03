// src/tz_rule.h against glibc, whose tzset takes POSIX rules whole (spec 036): every rule the
// settings page gives a time zone, and hand-written ones, every 15 minutes of 2026-2027,
// local time, daylight saving and the abbreviation. Linux only (glibc).
//     g++ -O2 -std=c++17 -I src tests/tz/tz_rule_tests.cpp -o tz && ./tz rules.txt
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include "tz_rule.h"

static int failures = 0;

// rule: what tz_rule.h parses; theirs: what glibc gets, the same rule except where glibc's
// defaults differ from newlib's (then the rule newlib's default stands for).
static bool same(const char* label, const char* rule, const char* theirs = nullptr) {
  TzRule r;
  if (!tzParse(rule, r)) { printf("FAIL %s: not parsed: %s\n", label, rule); return false; }
  setenv("TZ", theirs ? theirs : rule, 1);
  tzset();
  for (time_t t = 1767225600; t < 1830297600; t += 900) {   // 2026-01-01 to 2028-01-01 UTC
    struct tm want, got;
    const char* abbr;
    localtime_r(&t, &want);
    tzLocal(r, t, &got, &abbr);
    if (want.tm_year != got.tm_year || want.tm_yday != got.tm_yday || want.tm_hour != got.tm_hour ||
        want.tm_min != got.tm_min || want.tm_sec != got.tm_sec || want.tm_mon != got.tm_mon ||
        want.tm_mday != got.tm_mday || want.tm_wday != got.tm_wday || (want.tm_isdst > 0) != (got.tm_isdst > 0) ||
        std::string(want.tm_zone) != abbr) {
      printf("FAIL %s (%s) at %ld: glibc %04d-%02d-%02d %02d:%02d %s dst %d, ours %04d-%02d-%02d %02d:%02d %s dst %d\n",
             label, rule, (long)t, want.tm_year + 1900, want.tm_mon + 1, want.tm_mday, want.tm_hour, want.tm_min,
             want.tm_zone, want.tm_isdst, got.tm_year + 1900, got.tm_mon + 1, got.tm_mday, got.tm_hour,
             got.tm_min, abbr, got.tm_isdst);
      return false;
    }
  }
  return true;
}

int main(int argc, char** argv) {
  int zones = 0;
  if (argc > 1) {
    FILE* f = fopen(argv[1], "r");
    char zone[96], rule[96];
    while (f && fscanf(f, "%95s %95s", zone, rule) == 2) { zones++; if (!same(zone, rule)) failures++; }
  }
  if (zones < 300) { printf("FAIL only %d zones read\n", zones); failures++; }
  // Hand-written rules: newlib's defaults and every form POSIX allows.
  const char* rules[] = {
    "UTC0", "<+0545>-5:45", "<-03>3", "<+1030>-10:30<+11>-11,M10.1.0,M4.1.0",
    "CET-1CEST,M3.5.0,M10.5.0/3", "AEST-10AEDT,M10.1.0,M4.1.0/3", "NZST-12NZDT,M9.5.0,M4.1.0/3",
    "<-02>2<-01>,M3.5.0/-1,M10.5.0/0", "IST-2IDT,M3.4.4/26,M10.5.0", "XST3XDT,J60,J300/1:30",
    "YST3YDT,59,300/04:05:06", "ZST3ZDT,J60/0,J300/25", "ABC+3:30DEF+2:30,M4.1.6/23:59:59,M10.5.6/1",
  };
  for (const char* r : rules) if (!same(r, r)) failures++;
  // No dates: newlib's default, the US rule since 2007 (glibc reads its posixrules file instead).
  if (!same("EST5EDT", "EST5EDT", "EST5EDT,M3.2.0,M11.1.0")) failures++;
  if (!same("EST5EDT4", "EST5EDT4", "EST5EDT4,M3.2.0,M11.1.0")) failures++;
  // Refused, as newlib refuses them.
  const char* bad[] = {"", "E5", "EST", "EST5EDT,M13.1.0,M11.1.0", "EST5EDT,M3.6.0,M11.1.0",
                       "EST5EDT,M3.2.7,M11.1.0", "<+0545-5:45", "EST5EDT,M3.2.0", "EST25"};
  for (const char* b : bad) {
    TzRule r;
    if (tzParse(b, r)) { printf("FAIL parsed a bad rule: \"%s\"\n", b); failures++; }
  }
  printf("%s: tz_rule.h against glibc, %d zones and %zu hand-written rules\n", failures ? "FAIL" : "PASS",
         zones, sizeof rules / sizeof *rules);
  return failures ? 1 : 0;
}
