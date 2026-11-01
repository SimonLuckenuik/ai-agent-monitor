# 029 Progress

- 2026-10-31: proposed, from smalltv-pomodoro's Wi-Fi, host and time guards.
- 2026-11-01: built.
  - **Wi-Fi:** the core's `WiFi.begin()` with a password already sets the WPA threshold
    and turns off open and WEP modes (`ESP8266WiFiSTA.cpp`), and the clock never
    connects from SDK-saved credentials, so goal 1's first point needed nothing. An
    `onStationModeAuthModeChanged` handler flags a weaker mode; `serviceSta()` drops
    the link and joins again; `/status` counts them (`wifiDowngrades`). There is no
    event log to write to: the count stands in. Not testable in the emulator.
  - **Host:** `hostAllowed()` in `main.cpp`, called by the routes table (`routes.h`)
    before a handler and before an upload's body, and by the not-found handler. The
    web server's hook runs before the headers are read, so it could not do this. The
    setting `hostnames` is under WiFi on the settings page. Integration case
    `request_host`.
  - **Time:** `src/time_guard.h` (no Arduino code; 23 unit checks in
    `tests/unit/guard_tests.cpp`, the Date parser included). `settimeofday_cb` in
    `setup()` keeps the clock's own time on a jump over 2 minutes; the weather
    reply's Date header (`weather.cpp`) confirms it. Without weather, a jump repeated
    by every SNTP reply for 6 hours is taken, so the clock cannot stay wrong for good.
    Chip only: the emulator's host core has no SNTP.
