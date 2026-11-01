# 029 Plan

1. **Wi-Fi:** `wifiHarden()` at boot, the auth-mode handler and the reconnect in the loop;
   `wifiDowngrades` in `/status`. The emulator cannot change auth modes: a unit test of
   the rank logic, and the owner checks a normal connection on the clock.
2. **Host:** `hostAllowed()` in one place, called by the routes table and the remaining
   handlers before anything else on the station interface; the "Other names" setting
   (`hostnames`, comma list) on the settings page. Safety test: every route goes through
   it. Integration cases with `Host:` headers.
3. **Time:** a time guard around the SNTP callback (`settimeofday_cb`) and the weather
   reply's `Date` header; `timeHeld` in `/status`. Integration case: the emulator's
   `/time` endpoint fakes an SNTP jump.
4. Flash measured before and after. Docs: settings, integrations API (`/status` fields,
   the 403), CHANGELOG, STATUS.
