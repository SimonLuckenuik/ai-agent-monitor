# 029 Research

## smalltv-pomodoro (GPL-3.0, commit 2f6836d)

- **Wi-Fi** (`main.cpp`, `wifiHarden`, `wifiGuardBegin`): sets
  `station_config.threshold.authmode = AUTH_WPA_PSK` on a saved network with a password
  (and `open_and_wep_mode_disable` on SDK 3); `WiFi.onStationModeAuthModeChanged` flags a
  weaker mode and the loop calls `WiFi.reconnect()` (`disconnect()` would erase the saved
  password).
- **Host** (`hostOk`, `panelOk`): IPv4 (`safeIpv4Host`: four parts up to 255, optional
  port) or its exact `.local` names; every action also needs an `X-SmallTV: 1` header,
  which a cross-site page cannot send without a CORS preflight the clock never grants.
  The Alexa API, the setup portal and recovery stay open.
- **Time** (`TimeGuard` in `security.h`, `timeTrusted`): SNTP time counts for its history
  only once Open-Meteo's local time (over TCP) agrees, or two syncs agree without weather;
  a jump not confirmed is undone.

## Ours

- Session cookie: `HttpOnly; SameSite=Strict` (`src/main.cpp`). API key: `X-Api-Key`
  header or `key=` parameter. The `key=` parameter in a URL can be sent cross-site, but
  the attacker would need the key.
- Unauthenticated: `/status`, `/config`, `/api/set` (face, brightness), `GET /api/faces`
  when the faces API is on, the Home Assistant compatibility routes.
- Time: `configTime()` with a POSIX TZ rule; no check of SNTP replies. `millis()` drives
  `$ttl`; the crash-loop window counts boots.
- Home Assistant may reach the clock by a router DNS name (`clock.lan`), hence the
  setting for other names.
