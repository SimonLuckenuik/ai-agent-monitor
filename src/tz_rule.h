// Local time from a POSIX TZ rule, without the C library's tzset (spec 036).
//
// newlib's tzset parses the rule with formatted input (about 5 KB of flash), and its localtime_r calls
// tzset on every call, so both stay linked as long as anything converts time through them.
// This header parses the rule once ("EST5EDT,M3.2.0,M11.1.0", "<+0545>-5:45",
// "AEST-10AEDT,M10.1.0,M4.1.0/3") and converts UTC to local time with gmtime_r, which
// never calls tzset. Header-only, so tests/unit/tz_rule_tests.cpp checks it against glibc.
#pragma once
#include <stdint.h>
#include <string.h>
#include <time.h>

struct TzEdge {
  char ch = 'M';          // 'M' month.week.day, 'J' day 1-365 without Feb 29, 'D' day 0-365
  int m = 0, n = 0, d = 0;
  long s = 7200;          // the local time of day of the change, seconds (POSIX: -167 to 167 h)
};

struct TzRule {
  long stdOff = 0, dstOff = 0;   // seconds east of UTC (POSIX writes them west)
  bool hasDst = false;
  char stdName[12] = "UTC", dstName[12] = "";
  TzEdge start, end;             // into and out of daylight saving time
};

namespace tzrule {

inline bool name(const char*& p, char* out) {   // "EST" or "<+0545>": 3 to 10 characters
  int n = 0;
  if (*p == '<') {
    const char* q = p + 1;
    while ((*q >= '0' && *q <= '9') || (*q >= 'A' && *q <= 'Z') || (*q >= 'a' && *q <= 'z') || *q == '+' || *q == '-') ++q;
    n = q - p - 1;
    if (*q != '>' || n < 3 || n > 10) return false;
    memcpy(out, p + 1, n);
    p = q + 1;
  } else {
    const char* q = p;
    while ((*q >= 'A' && *q <= 'Z') || (*q >= 'a' && *q <= 'z')) ++q;
    n = q - p;
    if (n < 3 || n > 10) return false;
    memcpy(out, p, n);
    p = q;
  }
  out[n] = 0;
  return true;
}

inline bool number(const char*& p, long& v) {
  if (*p < '0' || *p > '9') return false;
  v = 0;
  while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
  return true;
}

// [+-]hh[:mm[:ss]] in seconds, signed as written
inline bool hms(const char*& p, long& secs, long maxHours) {
  int sign = 1;
  if (*p == '+' || *p == '-') sign = *p++ == '-' ? -1 : 1;
  long h, m = 0, s = 0;
  if (!number(p, h) || h > maxHours) return false;
  if (*p == ':') { ++p; if (!number(p, m) || m > 59) return false; }
  if (*p == ':') { ++p; if (!number(p, s) || s > 59) return false; }
  secs = sign * (h * 3600 + m * 60 + s);
  return true;
}

inline bool edge(const char*& p, TzEdge& e) {
  long v;
  if (*p == 'M') {
    long m, n, d;
    ++p;
    if (!number(p, m) || *p++ != '.' || !number(p, n) || *p++ != '.' || !number(p, d)) return false;
    if (m < 1 || m > 12 || n < 1 || n > 5 || d > 6) return false;
    e.ch = 'M'; e.m = m; e.n = n; e.d = d;
  } else {
    e.ch = 'D';
    if (*p == 'J') { e.ch = 'J'; ++p; }
    if (!number(p, v) || v > 365 || (e.ch == 'J' && v < 1)) return false;
    e.d = v;
  }
  e.s = 7200;
  if (*p == '/') { ++p; if (!hms(p, e.s, 167)) return false; }
  return true;
}

inline bool leap(long y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

inline long daysFromCivil(long y, int m, int d) {   // days since 1970-01-01 (H. Hinnant)
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const long yoe = y - era * 400;
  const long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}

// The UTC time of a change in year y, given the offset in force before it.
inline long long changeAt(const TzEdge& e, long y, long offBefore) {
  long day;   // days since the epoch
  if (e.ch == 'M') {
    static const int len[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const long first = daysFromCivil(y, e.m, 1);
    const int wd = (int)(((first % 7) + 7 + 4) % 7);   // 1970-01-01 was a Thursday
    int dom = 1 + (e.d - wd + 7) % 7 + (e.n - 1) * 7;
    const int days = len[e.m - 1] + (e.m == 2 && leap(y));
    while (dom > days) dom -= 7;
    day = first + dom - 1;
  } else {
    day = daysFromCivil(y, 1, 1) + (e.ch == 'J' ? e.d - 1 + (leap(y) && e.d >= 60) : e.d);
  }
  return (long long)day * 86400 + e.s - offBefore;
}

}  // namespace tzrule

// Parses a POSIX TZ rule. False for anything newlib's tzset would not take whole; r is
// then UTC.
inline bool tzParse(const char* p, TzRule& r) {
  r = TzRule();
  TzRule t;
  long off;
  if (*p == ':') ++p;
  if (!tzrule::name(p, t.stdName) || !tzrule::hms(p, off, 24)) return false;
  t.stdOff = -off;
  if (*p) {
    if (!tzrule::name(p, t.dstName)) return false;
    t.hasDst = true;
    t.dstOff = t.stdOff + 3600;
    if (*p && *p != ',') { if (!tzrule::hms(p, off, 24)) return false; t.dstOff = -off; }
    if (*p == ',') {
      ++p;
      if (!tzrule::edge(p, t.start) || *p++ != ',' || !tzrule::edge(p, t.end)) return false;
    } else {   // newlib's default: the US rule since 2007
      t.start.ch = t.end.ch = 'M';
      t.start.m = 3; t.start.n = 2; t.start.d = 0;
      t.end.m = 11; t.end.n = 1; t.end.d = 0;
    }
    if (*p) return false;
  }
  r = t;
  return true;
}

// A fixed offset, as configTime(offset, 0) gives: no daylight saving time.
inline void tzFixed(TzRule& r, long eastSecs) {
  r = TzRule();
  r.stdOff = eastSecs;
  strcpy(r.stdName, "GMT");
}

inline bool tzIsDst(const TzRule& r, time_t t) {
  if (!r.hasDst) return false;
  struct tm g;
  time_t local = t + r.stdOff;
  gmtime_r(&local, &g);
  const long y = g.tm_year + 1900;
  const long long a = tzrule::changeAt(r.start, y, r.stdOff), b = tzrule::changeAt(r.end, y, r.dstOff);
  return a < b ? (t >= a && t < b) : !(t >= b && t < a);   // southern zones: summer spans the new year
}

// localtime_r for this rule; the zone's abbreviation, "EST" or "EDT", in *abbr.
inline struct tm* tzLocal(const TzRule& r, time_t t, struct tm* out, const char** abbr = nullptr) {
  const bool dst = tzIsDst(r, t);
  time_t local = t + (dst ? r.dstOff : r.stdOff);
  gmtime_r(&local, out);
  out->tm_isdst = dst;
  if (abbr) *abbr = dst ? r.dstName : r.stdName;
  return out;
}
