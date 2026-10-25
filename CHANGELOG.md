# Changelog

All notable SmartClock-OSS changes are recorded here. Versions follow semantic
versioning. `/version` on the device reports the version it was built as; a
`-dev` suffix marks a build from an unreleased checkout.

## Unreleased

### Added

- Agent messages: `POST /api/message` (and MQTT `<base>/message`) puts a title, a
  message, one of 8 built-in icons (info, success, warning, error, waiting, running,
  bell, done) and an optional progress bar on the built-in Message face. No sign-in.
  Up to 4 at a time, kept in RAM with a ttl; each is a turn of its own in the face
  rotation; `show` puts one on screen now; warning and error light the backlight fully.
- The clock's language: English or French (without accents), on the Display card or
  `/api/set?key=lang&value=fr`. Days, months, weather words and the faces' labels
  follow it; the API, MQTT and the settings page stay in English.
- Accented and typographic characters in messages and face variables are folded to
  the fonts' ASCII.

- Custom faces: any string field takes placeholders, and number and colour fields
  take expressions (`+ - * / %`, comparisons, `&& || !`, `?:`, `sin cos min max abs
  round`). Every item takes `show`, a condition. Faces take `fps` (1 to 20) and the
  `{ms}` placeholder for animation.
- Custom faces: a `path` item draws SVG path data (all commands, arcs included) from a
  viewBox, filled with anti-aliased edges and/or stroked with round caps and joins, so
  Lucide or Material Symbols icons can be used as they are.
- Custom faces: linear gradients (2 to 4 stops, any angle) for the face background,
  rectangles, circles and paths.
- Custom faces: `text` wraps with `w`, `lines` and `lh`.
- `/status` reports the DHCP hostname as `host`.
- Face variables hold up to 160 characters (was 40), enough for a short message.

### Removed

- The Dial face. Its id (3) stays reserved so the other faces keep their stock
  numbers; a clock set to it shows Classic.

### Changed

- The hostname (DHCP, and the default MQTT topic base) is now `agentmonitor-XXXXXX`
  (was `smartclock-XXXXXX`). With MQTT on, Home Assistant sees new topics: remove the
  old device there. The MQTT device is named "AI Agent Monitor XXXXXX".
- The setup network is now **AI Agent Monitor XXXXXX** (was Smart Weather Clock), with
  the hostname's suffix so clocks side by side do not share a name; `/status` reports
  it as `apSsid`. The settings page's project link and update check point to
  github.com/SimonLuckenuik/ai-agent-monitor.
- mDNS is now a build option (`pio run -e esp12e_mdns`): the default build leaves it
  out, which frees ~20 KB of the update budget. `/status` reports `mdns` only when it
  is built in.
- Custom faces redraw only items whose resolved content changed; a change in position,
  size or visibility redraws the face.

## 1.3.2 - 2026-10-17

### Added

- WiFi setup: a failed join now also shows the disconnect code the clock
  received, such as `(15)` on the setup screen. The settings page explains it,
  for example "password handshake timed out, check the password" or
  "unsupported security mode, try WPA2"
  ([#2](https://github.com/michaeldale/smartclock-oss/issues/2)).

## 1.3.1 - 2026-10-16

### Fixed

- A clock that joined WiFi only through the build-time network in `secrets.ini`
  now saves it on the first join. Before, updating it to a release image (built
  without that network) left it on the setup screen with no network to join.
- WiFi setup: **Connect & restart** no longer asks for confirmation first. Phone
  captive-portal windows, where the setup page usually opens, block that dialog,
  so the button did nothing.

## 1.3.0 - 2026-10-16

### Added

- Photos: a **Show the time** switch on the Photos card (on by default). Turn
  it off so the time does not cover Home Assistant dashboards pushed by
  geekmagic-hacs ([#1](https://github.com/michaeldale/smartclock-oss/issues/1)).
- WiFi setup: when the clock cannot join the saved network, the setup screen
  and the settings page say why (network not found, wrong password, connection
  refused, or no answer) instead of only showing the setup screen again
  ([#2](https://github.com/michaeldale/smartclock-oss/issues/2)).

## 1.2.0 - 2026-10-06

The version moves from 0.1.x to 1.2.0 with this release.

### Added

- **Integrations, all off by default** (settings page → Integrations; see
  [integrations-api.md](docs/specifications/integrations-api.md) and the
  [record](docs/decisions/2026-10-06-integrations.md)):
  - **Custom faces**: design faces as JSON (text, shapes, progress rings and
    bars, weather icons, photos) and send them over HTTP or MQTT. Placeholders
    such as `{time}` and `{temp}` stay live on the clock, and your own
    variables can be pushed separately. Signed-in page or API key.
  - **Home Assistant dashboards**: the stock SD Pro photo API, so the
    geekmagic-hacs integration can push rendered dashboards. These endpoints
    work without the password while on, and only reach photos, face and
    brightness.
  - **MQTT** with Home Assistant discovery: backlight light, face select,
    weather sensors, update button; custom faces and variables on topics.
- Photos: per-photo slideshow switch (the eye on each photo) and seconds per photo.
  A single photo is no longer re-decoded at every interval, and full-screen
  photos draw without a black flash (geekmagic-hacs uses a 1 s interval).
- The System card links to the project on GitHub
  (https://github.com/michaeldale/smartclock-oss).

### Changed

- ArduinoJson built without 64-bit integers and doubles (4.4 KB smaller).

## 0.1.2 - 2026-10-06

### Upgrading from 0.1.1

After this update the settings page asks for the clock's password, and the
**Smart Weather Clock** setup network needs it too. To find it, press **Show the
password on the clock** on the sign-in page, then read it off the clock. You can
change it under System → Password.

### Added

- **A device password** for the settings page and the setup network (WPA2). It
  is generated on first boot (10 characters) and kept outside the file system so
  recovery can read it. The clock shows it on its setup and recovery screens, or
  on request from the sign-in page until you sign in (at most 60 s). Sessions use an `HttpOnly`,
  `SameSite=Strict` cookie, and five wrong attempts lock sign-in for a minute.
  Recovery mode stays open, so a forgotten password cannot lock you out
  ([record](docs/decisions/2026-10-06-passwords-and-flash-savings.md)).
- **Update available** banner. The settings page checks GitHub for a newer
  release; the clock itself needs no internet access for this.
- **Night mode from sunset to sunrise**, from Open-Meteo's daily sun times.
- **Backup and restore** of settings as a JSON file, plus **Forget WiFi** and
  **Reset all settings**. Backups contain no WiFi details or password.

### Changed

- **34 KB of flash freed** without removing features: float printf/scanf no
  longer forced into the link, lwIP without IP forwarding/fragmentation
  features, no unused serial driver, and no `strftime`. Image 472 KB (92%).
- Brightness changes respect night dimming immediately instead of at the next
  minute.

## 0.1.1 - 2026-10-06

### Fixed

- **The clock showed standard time during daylight saving, an hour behind.**
  The time was set with a fixed minute offset (`timezone`, +600 for Sydney)
  and no DST rule, so it stayed on AEST after Sydney moved to AEDT on
  2026-10-04. The time zone is now a POSIX TZ rule (`tz`), so libc switches
  at each DST change. The default is `AEST-10AEDT,M10.1.0,M4.1.0/3`
  ([issue](docs/issues/2026-10-05-clock-one-hour-behind-during-dst.md)).
  Flashed through our own updater; the rule is active on the device
  ([record](docs/decisions/2026-10-05-first-update-through-own-ota.md)).
- Numeric settings are range-checked (theme, brightness, hours, interval).

### Removed

- **OpenWeatherMap.** Open-Meteo replaces it entirely, saving ~5 KB of flash
  that the OTA budget needs. The `weatherkey` setting (stock's too) is no
  longer accepted, and a saved key is dropped at the next settings save.
- Only power-on and external resets count toward the three-power-cycle recovery
  gesture; crashes, watchdogs and restarts clear it.
- mDNS starts after the network connects and only with 28 KB of free heap; a
  weather fetch waits while free heap is under 16 KB.
- Weather was not fetched until 10 minutes after boot; it is now fetched at once.
- The city is URL-encoded in weather requests (a space broke them).
- `/api/photos/delete` removes any listed file in `/photo`, so files the stock
  firmware left (GIFs, names with spaces) can be deleted. The page marks
  non-JPEG files "Not shown".
- Photos whose names contain spaces or other URL-encoded characters showed as
  broken thumbnails: `/photo/` was served by `serveStatic()`, which matches the
  raw URI. A dedicated handler now decodes the name and serves only files
  directly in `/photo`.

### Added

- **Weather without an API key, from Open-Meteo.** The city ("Sydney" or
  "Sydney, AU") is geocoded once and cached, and one ~1 KB request every 15
  minutes gives current conditions and the 4-day forecast
  ([record](docs/decisions/2026-10-05-open-meteo-reset-reason-and-discovery.md)).
- **Sync now** on the Weather card (`POST /api/weather/sync`, at most once per
  10 s) fetches immediately, reports the result, and restarts the 15-minute
  timer; the card shows when the weather last updated (`weatherAge`).
- **A per-unit hostname**, `smartclock-<chip id>`, used for mDNS and as the DHCP
  hostname so routers list the clock by name. mDNS TXT records give model,
  version, board, API and path.
- `/status` reports `resetReason`, `heapLow`, `weatherSource`, `weatherError`
  and the resolved `location`.
- **All seven clock faces, redesigned.** Classic (date, large time, seconds bar,
  weather line), Dial (anti-aliased hands and ticks, date and temperature),
  Weather (large icon and temperature, high/low, feels-like, humidity, wind),
  Forecast (four day cards with icon, high/low and chance of rain), Flip
  (split-flap cards with a fold animation), Simple (the time and date) and
  Photo (your photos with the time on top). Weather icons are drawn, not
  stored, at any size: sun, moon, clouds, rain, storm, snow and mist. A
  "Setting the time" screen replaces 1970 before NTP. Faces redraw only what
  changed ([record](docs/decisions/2026-10-05-clock-faces-without-a-framebuffer.md)).
- **Rotation that skips empty faces.** Rotation and the button move to the
  next face that has something to show, from a chosen set (`faces` bitmask,
  default all). A rotated face is not saved, so the chosen face stays yours.
- **4-day forecast** with daily high, low, icon and chance of rain. A failed
  fetch retries after a minute instead of waiting the full interval.
- **Photos.** `/api/photos` (list and storage use), `/api/photos/upload`
  (JPEG, up to 200 KB, keeps 128 KB free) and `/api/photos/delete`. The
  settings page crops and resizes photos to 240x240 in the browser before
  upload, so a phone photo arrives as a ~10-30 KB baseline JPEG.
- **Wind in km/h or mph** (`mile`), and new default colours (amber, soft blue,
  mint) for units with no saved colours.
- **`tests/face_preview`** renders every face on the PC with the real fonts and
  JPEG decoder, and checks incremental redraws against full redraws.
- **A new settings page.** Overview tiles (device time with zone, network and
  signal, weather, uptime), face picker, brightness and colour pickers, night
  mode, time and region, weather, a WiFi scan with signal strength, a
  drag-and-drop firmware update with progress, and system info. Changes save
  as you make them. Light and dark themes, phone to desktop width, and no
  external resources, so it works on the offline setup network. Recovery mode
  locks the settings and says why. The page lives in `web/index.html` and is
  gzipped into flash at build time (about 30 KB to 10 KB).
- `/status` reports version, uptime, free memory, network, local time,
  the resolved weather location, and the latest weather. It never includes
  secrets.
- `/scanwifi?detail=1` adds signal strength and security; without it the
  response keeps the stock shape.
- `/api/set` accepts `hour12`; setting the city fetches
  weather at once.
- `/api/set?key=tz&value=<POSIX TZ>` and a **Timezone** list on the web page.
  `/config` reports `tz`. The stock `timezone` key still works and sets a
  fixed offset with no DST, as stock did. Changing `tz`, `timezone` or `ntp`
  takes effect without a reboot.

## 0.1.0 - 2026-10-05

The first release, and the first firmware flashed over the stock SD Pro
V1.0.6 firmware. Tag `v0.1.0`, commit `6d94d08`. Flashed through the stock
update page on the unit at 10.0.1.118, where it came up in normal mode with
the stock LittleFS volume mounted
([record](docs/decisions/2026-10-05-first-flash-over-stock.md)).

### Flashing over stock

- **The flash layout matches stock (4m3m).** Both stock images mount LittleFS
  at `0x40300000`-`0x405FA000` with 256 B pages and 8 KB blocks. The image
  carries the same constants, so the stock volume mounts after OTA
  ([record](docs/decisions/2026-10-05-flash-layout-matches-stock.md)).
- **OTA size budget.** Sketch and staged update share 1 MB, so the build fails
  above 0x7E000 bytes and deletes the image. 0.1.0 is 444,496 bytes (86%).
  Unused TFT fonts were dropped to make room.
- **The stock update page only accepts filenames starting with `SDP`.** It is
  a browser-side check; renaming the image is enough. See
  [MIGRATION.md](docs/MIGRATION.md).

### Recovery without a button

- **Three quick power cycles enter recovery** (on for less than 10 s twice,
  then leave it on). The count lives in the EEPROM sector, and stock bytes
  there are preserved. Confirmed working on the device
  ([record](docs/decisions/2026-10-05-recovery-without-a-button.md)).
- A crash in the first 30 s, or a filesystem that will not mount, also enters
  recovery. Every deliberate reboot clears both triggers (`safeRestart()`).
- Recovery shows its state on the panel, and offers an explicit **Format
  storage** only after a mount has failed. Nothing formats automatically.
- GPIO4 is never read to choose recovery.

### OTA validation

- Firmware-only OTA: header, declared length and whole-image CRC are checked
  before the final byte is written. Truncated, corrupt, oversized, aborted and
  multi-file uploads are rejected without a reboot.

### Networking

- The setup AP, **Smart Weather Clock** at http://192.168.4.1/, is always on.
  The STA side retries in 30 s windows every 5 minutes so the AP stays usable
  when the saved network is missing.
- Optional build-time WiFi from a gitignored `secrets.ini`, used only when
  nothing is saved, and in recovery.
- mDNS as `smartclock.local`.

### Clock

- Classic and Dial themes, NTP, OpenWeatherMap, night dimming, and the
  stock-compatible web API (`/config`, `/api/set`, `/scanwifi`, `/connect`,
  `/restart`, `/version`, `/update_ota`). `/config` does not expose WiFi or
  API secrets, unlike stock.

### Known issues

- The time is an hour behind during daylight saving (fixed in 0.1.1).
- The AP is open and OTA is unauthenticated
  ([issue](docs/issues/2026-10-05-ap-and-ota-unauthenticated.md)).
- You cannot OTA back to the stock firmware; that needs UART.
