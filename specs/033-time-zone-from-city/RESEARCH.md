# 033 Research

- smalltv-pomodoro ships its own table (`web/tz.json`, IANA → POSIX), generated from
  tzdata by `web/tzgen.sh` and republished when they rebuild; it ages between releases.
- Better sources, both kept current by others:
  - **The browser's ICU time zone data** (`Intl.supportedValuesOf("timeZone")`,
    `Intl.DateTimeFormat`): updated with the browser and operating system. It gives
    offsets at any instant and short names, not POSIX rules, so the page derives the rule
    from sampled offsets; zones with no fixed yearly rule (e.g. Morocco's Ramadan
    changes) cannot be expressed as POSIX and are refused.
  - **Open-Meteo's geocoding API** (`timezone` per result, from GeoNames): already what the
    clock uses for its weather city, and callable from a browser.
  - A maintained table such as nayarsystems/posix_tz_db (zones.json, MIT) exists, but it
    is one more copy to trust and refresh.
- A TZif file (version 2+) ends with the POSIX TZ string for times after its last
  transition; the page's derived rule should equal it for regular zones.
- The clock takes a POSIX rule in `tz` (`configTime`).
