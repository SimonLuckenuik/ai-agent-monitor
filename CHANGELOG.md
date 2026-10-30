# Changelog

All notable AI Agent Monitor changes are recorded here; versions up to 1.3.2 are
SmartClock-OSS, which it is forked from. Versions follow semantic
versioning. `/version` on the device reports the version it was built as; a
`-dev` suffix marks a build from an unreleased checkout.

## Unreleased

### Upgrading

- **Settings move over by themselves.** The three colours become `accent1`-`accent3`
  of the new day palette, the rotation's order keeps its built-in faces, and the
  night face is kept. The settings keys `faceorder` and `color1`-`color3` are still
  accepted, for older scripts and backups.
- **A few default colours moved to their part** (spec: `docs/specifications/settings.md`):
  Classic's day of the week is now in the date colour (amber, was blue); a day
  tile's low temperature is grey and its chance of rain blue; the Message face's
  `waiting` icon is orange (was yellow) and `running` blue (was light blue).
- **Custom faces without `bg` or `color`** now take the clock's background and text
  colours (black and white unless the user changes them).

### Added

- **Emulator: Scenarios panel.** The screen page mocks the time (any date, stopped or up
  to 3600× real pace), the weather (a form for the current conditions and four forecast
  days, plus presets from sunny to blizzard) and the values agents push (variables,
  messages, any MQTT topic, handled by the firmware as if from the broker), and loads
  whole scenarios or any example face with its sample values. New emulator endpoints:
  `GET /time` and `local`/`add`/`speed` on `POST /time`, `GET /weather`, `POST /mqtt`,
  `GET /vars`, `POST /facesapi`, `GET /presets`, `GET /examples/…`.
- **Palettes and roles.** Named colours in palettes (day, night, and up to 8), the
  palette in use by day and at night, and 18 roles, the parts of the faces (time,
  date, highlights, weather icons, temperatures, rain, sun times, message colours,
  background, text, labels, cards), each bound to a colour, another role, or a rule
  (`{now.temp}<0?@blue:@orange`). The built-in faces draw with the roles; custom faces
  reach them as `@time`, `@bg`, `@warn` …, and `{theme.<role>}`. A Colours card on the
  settings page edits them. The built-in night palette has the day's colours, dimmer;
  it is used while night mode is on and "Night color" is off.
- **One rotation list.** Custom faces join the built-in ones in the rotation's chips:
  any order, on or off, marked as custom (and `when` when they have a rule).
- **A face's own time on screen** (⋯ on its chip), for built-in and custom faces.
- **Custom faces at night** ("At night show") and in the Display face picker.
- **`"font": "big"`** in custom faces: the Big tiles' line font at any size, with
  punctuation and the symbols `° ↑ ↓ ← → ✓ ✗ • ♥ ⚡`.
- **5 example faces**: big-time, big-date and big-weather (the Big tiles as JSON, in
  the clock's colours) and scoreboard, in the big font; alarm, which flashes between
  two colour sets every second while an alarm is set.
- **The display's own features:** a custom face's `invert` (the panel inverts every
  colour, with no redraw, to flash it); a contrast curve (gamma 2.2, 1.8, 2.5 or 1.0)
  by day and at night; and "Screen off" at night, which turns the panel off, not only
  its backlight (warnings and errors still wake it). The emulator models all three.
- The settings page says "Use night mode" (was "Dim at night"): the switch turns on all
  of night mode, not only the dimmer backlight.
- **Screen orientation**: turn the picture 90, 180 or 270 degrees (Display card), done
  by the display controller, for a clock on its side or upside down.
- Settings: `orientation`, `gamma`, `nightgamma`, `nightoff`, `order`, `customoff`, `turns`, `customface`, `nightface` (any face),
  `palettes`, `palette`, `nightpalette`, `roles` (`docs/specifications/settings.md`).

## 1.5.0 - 2026-10-29

A richer custom face model: grouped placeholders, modifiers, maps, palettes, sprites,
sparklines, repeats, borders, faces that join the rotation only when they have
something to show, variable options, up to 40 faces and 40 variables, and 32 example
faces.

### Upgrading

- **From 1.4.0:** install `AIAgentMonitor_v1.5.0.bin.gz` from the Firmware update
  card. Settings, saved faces and photos are kept.
- **A new clock** (stock firmware, or SmartClock-OSS 1.3.x or earlier): install
  `SDPro_AIAgentMonitor_v1.4.0_nomqtt.bin` from the v1.4.0 release first, then
  `AIAgentMonitor_v1.5.0.bin.gz`.
- **Placeholders have new names** (`{time}` is now `{time.hm}`, `{temp}` is
  `{now.temp}`, …): custom faces saved on a clock show the old ones as empty until they
  are sent again with the new names. The example faces and the plugin's faces are
  updated. Old to new: `time` `time.hm`, `hh` `time.hour`, `mm` `time.min`, `ss`
  `time.sec`, `ms` `time.ms`, `ampm` `time.ampm`, `dow` `date.wday`, `dow3`
  `date.wday3`, `day` `date.day`, `month` `date.month`, `mon3` `date.month3`, `year`
  `date.year`, `date` `date.text`, `temp` `now.temp`, `feels` `now.feels`, `hi`
  `day0.high`, `lo` `day0.low`, `hum` `now.hum`, `wind` `now.wind`, `unit`
  `units.temp`, `wunit` `units.wind`, `desc` `now.text`, `main` `now.kind`, `city`
  `loc.city`, `icon` `now.icon`, `sunrise` `sun.rise`, `sunset` `sun.set`.
- **Variable names** are now 1-15 of `A-Z a-z 0-9 _` (no dots); other names are
  refused.
- **Releases have only the `.bin.gz`.** A new clock first installs
  `SDPro_AIAgentMonitor_v1.4.0_nomqtt.bin` from the v1.4.0 release, then the latest
  `.bin.gz`. The firmware is no longer held to the size of a plain image.

### Added

- **Placeholders** in groups: `time.` (also `epoch`, `daymin`, `zone`), `date.` (also
  `monthnum`, `yday`, `week`, `wdaynum`, `mdays`), `now.` (also `press`, `clouds`,
  `code`, `age`, `err`), `day0.` to `day3.` (the forecast), `sun.` (`up`, `next`,
  `until_rise`, `until_set`, `daylen`), `moon.` (`phase`, `lit`, `age`), `units.`,
  `loc.city`, `theme.` (`color1` to `color3`, `night`), `msg.count`, `net.` (`ip`,
  `host`, `ssid`, `rssi`, `mqtt`), `sys.` (`version`, `uptime`, `heap`, `lang`) and
  `age.<variable>`. Sunrise and sunset follow the 12/24 h setting.
- **Modifiers** (`{title:u}`, `{n:03}`, `{name:12}`, `{load:.1}`, `{delta:+}`),
  **maps** that turn a value into a colour or a label (`{build:state}`), and
  **expressions in text** (`"Build {{build}==fail?FAILED:OK}"`), with `rand(n)`.
  Expressions compute in double precision, so Unix times subtract to the second.
- **Palettes**: `"@name"` colours, and a palette chosen by an expression
  (`"palette": "{sun.up}?day:night"`).
- **Items**: `sprite` (pixel art, animated by frames), `spark` (a chart of a variable's
  history), `repeat` (items per forecast day, agent message, or number), and `border`
  and `bw` on rectangles and circles.
- **Rotation**: `when` (a face joins the rotation only while a condition holds) and
  `duration` (its own time on screen).
- **Variable options**: `$ttl` (expire), `$persist` (kept across restarts), `$history`
  (for `spark`), `$add` (counters); `null` deletes a variable.
- **32 example faces** in `examples/faces/` (26 new: agent inbox, CI pipeline,
  heartbeat watchdog, pomodoro, countdown, commit streak, KPI, homelab, battery, pixel
  pet, Claude mascot, calendar, binary clock, retro watch, forecast strip, weather
  poster, sun arc, daylight, moon phase, aquarium, night light, what to wear, sky
  window, golden hour, block world, retro runner), with `samples.json` and a gallery
  (`examples/faces/README.md`).
- **The plugin (0.4.0)**: `reference/face.schema.json`, the JSON Schema of a face;
  `clock.py check` validates faces and their placeholder names, and `push` checks
  first; `clock.py vars` takes `--ttl`, `--persist`, `--history`, `--add`.
  `scripts/gen_face_format.py` keeps the skill's face format in step with the
  specification (checked in CI).
- Weather: pressure and cloud cover.

### Changed

- **Up to 40 custom faces** on the clock (was 12), and up to 40 variables (was 24).

### Fixed

- A custom face item whose `show` condition uses a changing value (such as
  `{at}-{time.epoch}>0`) no longer redraws the whole face every second (countdown
  flickered); the face is redrawn only when the item appears or disappears.
- An animated `sprite` with a `bg` no longer blinks when its frame changes: each pixel
  is painted once, instead of clearing its box first.

### Removed

- The `esp12e_nomqtt` build and the plain first-install image from future releases.

## 1.4.0 - 2026-10-28

The first AI Agent Monitor release: messages and custom faces for AI agents, the Big
tiles and Day tiles, Weather Icons, French, night color, and compressed updates.

### Upgrading

- **From the stock firmware, or SmartClock-OSS 1.3.x or earlier:** install
  `SDPro_AIAgentMonitor_v1.4.0_nomqtt.bin` first (everything but MQTT, small enough
  for a plain update), then `AIAgentMonitor_v1.4.0.bin.gz` from the Firmware update
  card to add MQTT. Only the first has the `SDP` prefix the stock update page asks
  for: the stock firmware cannot unpack the compressed image.
- **From then on:** install the `.bin.gz`. The full firmware is too big for a plain
  update.
- The setup network, hostname and MQTT topics change names (see Changed): with MQTT
  on, remove the old device in Home Assistant.

### Added

- mDNS is back in the full firmware: `http://agentmonitor-XXXXXX.local/` from the same
  network, with an HTTP service record (model, version, API). It fits now that updates
  are compressed; the first-install `_nomqtt.bin` still leaves it out.
- Example custom faces in `examples/faces/`: bauhaus (an analog clock), night-lake,
  orbit, seaside (with the live weather icon), synthwave and terminal.
- Integration tests (`tests/integration/run_integration_tests.py`): the firmware in its
  own emulator container, driven over HTTP with a fixed clock and weather, through every
  face, language and unit setting, extreme values, night mode, messages, the Agents
  board, custom faces and the API's limits; each screen is saved and compared with the
  previous run.
- `scripts/screenshots.py` regenerates the README's images (clock screens, custom faces,
  agent screens and the settings page, the latter through Playwright).
- Emulator: `POST /time?epoch=N` stops the clock and `POST /weather` replaces the
  Open-Meteo fetch with a given reply, both kept across restarts.
- The repository's `.claude/settings.json` enables the Claude Code plugin; the plugin
  (0.3.0) knows the Agents board, mDNS addresses and the example faces.
- Release files: a no-MQTT plain image (`esp12e_nomqtt`, `SMARTCLOCK_MQTT=0`) as the
  stepping stone from plain-only updaters, and the full firmware as `.bin.gz`.
  `scripts/make_release.py` builds both; CI checks that the no-MQTT image fits a plain
  update.
- Weather icons: Erik Flowers' Weather Icons (SIL OFL 1.1) on every face, chosen from the
  WMO code with the companion app's table (day and night variants). `scripts/gen_weather_icons.py`
  turns the SVGs into polygons (`src/weather_icons.h`, 7.5 KB) that the path filler draws at
  any size. `{icon}` is now a name such as `wi-day-rain`; the icon item takes a `color`.
  The plain image is now over its limit: update with the `.bin.gz`.
- Day tiles (faces 10-13): Weather today, then Weather forecast - Day 1, Day 2 and
  Day 3 (their names on the settings page and in Home Assistant), one day per screen
  in the Big tiles' style. Each shows the day's name, its icon and, as the big number,
  the feels-like temperature: what it is like to go out. Under it a line of facts:
  wind, humidity and two sun times (W 22 km/h | H 74% | sunrise | sunset), then the
  low, high and chance of precipitation. Weather today is not a forecast: its icon,
  feels-like and wind and humidity are the current ones, the actual temperature is
  under the big number (NOW -1), and its sun times are the next two (before sunrise:
  sunrise, sunset; after: sunset, tomorrow's sunrise; after sunset: tomorrow's two).
  The forecast now also fetches each day's feels-like high, highest wind, mean
  humidity, sunrise and sunset. Custom faces moved to id 14 and messages to 15
  (`facesv` 4), and `faceorder` writes ids 10-13 as `a`-`d`.
- Emulator: runs the clock's real TFT_eSPI on a simulated ST7789 instead of a
  stand-in, so it draws exactly what the clock draws; draws take about the clock's
  time at the CPU frequency in `platformio.ini`, calibrated against the clock
  (`emulator/calibrate.py`, `GET :8080/timing`, `EMU_TIMING=0` for instant draws).
- `/status` reports `cpuMHz` and each face's drawing time (`drawMs`: the latest and
  the slowest draw in ms, by face id), to measure the line font and paths on the clock.
- Stroked paths (the line font, outline icons) yield every row, so a large glyph no
  longer trips the software watchdog (Big date reset the clock), and skip most of the
  float maths: only pixels near each segment are tested, and only edge pixels take a
  square root. Output is pixel-identical.
- The CPU runs at 160 MHz (was 80) to speed up the line font and paths.
- Message screens have a solid background in a dark shade of the message color,
  instead of a gradient to black.
- Faces: three **Big tiles** that each show one thing as large as the screen allows,
  in one modern monoline style (a monospaced stroke font that follows Roboto Mono's
  shapes and spacing, and outline icons, drawn by the path renderer): **Big date** (id 3: day of the week, the day, the month), **Big time** (7:
  hours above minutes) and **Big weather** (8: the conditions' icon above the
  temperature). And the **Agents board** (9, last: every active agent message with its
  icon, title, first line and time left; "All quiet" when there are none, in the
  rotation only while messages are active). Custom faces move from id 7 to 10 and
  messages to 11; a saved configuration is migrated, and the new faces join an
  existing rotation.
- Custom faces: `{name|fallback}` shows the fallback while a value is empty.
- **Compressed firmware updates:** every build writes `firmware.bin.gz` next to
  `firmware.bin` (about 70% of the size), and the Firmware update card takes either.
  The `.gz` carries its own check (unpacked size, file size, CRC in the gzip header),
  so a damaged or foreign file is refused before anything is written. The update
  budget becomes the image plus its `.gz` within 1 MB: about 610 KB instead of 516 KB.
  A `.bin` too big for a plain update is refused with a message pointing at the `.gz`.
  Releases publish both.
- **Classic and Simple** draw their time in the Big tiles' line font. **Flip** is gone
  and **font 8** left the build (3.6 KB of flash for new features): id 6 stays
  reserved and is skipped; custom faces asking for font 8 get font 6.
- **Rotation order** (Faces card, `faceorder`): drag the rotation chips to set the
  order the built-in faces come in; custom faces and messages follow them. The setting
  is the face ids as digits (`"7801234569"`); unknown ones are dropped and missing ones
  appended.
- **Night color** (Night mode card, `nighttint`, `nightcolor`): at night every face is
  drawn in one dim color, dark gray by default, so the screen lights the room less.
  Each color keeps its brightness; photos and urgent messages keep their colors.
- **At night show** (Night mode card, `nightface`): one face all night instead of the
  rotation, for example Big time. A message shown on purpose still comes through.

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

- The Dial face. Its id (3) now holds the Date face.

### Changed

- New clocks start on a 24-hour clock (km/h and °C, as before).

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
