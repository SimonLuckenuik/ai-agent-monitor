# AI Agent Monitor

Turn a cheap ESP8266 desk clock into a small screen your AI agents and scripts can use
to get your attention. Claude Code (or anything that can send an HTTP request or an
MQTT message) puts messages, progress, status and its own graphics on the clock, and
between messages it is a good-looking clock with weather and a forecast.

It is replacement firmware for the "Smart Weather Clock" sold as **SD Pro** (JUZIPi
SD_PRO, a GeekMagic-style clone with a 240×240 IPS screen). You install it from the
clock's own update page: no cables, soldering or opening the case. It is a fork of
[SmartClock-OSS](https://github.com/michaeldale/smartclock-oss), written from scratch
from a reverse-engineering of the stock firmware; no vendor code is reused.

![The clock faces](docs/images/faces.png)

## What it does

- **Messages from agents and scripts.** One HTTP call (no sign-in) shows a title, a
  message, one of 8 icons (info, success, warning, error, waiting, running, bell, done)
  and an optional progress bar. Up to 4 messages at a time, each with a time to live.
  Warning and error light the screen fully. The **Agents board** lists every active
  message at a glance.
- **Custom faces.** Agents (or you) design whole screens as JSON: text, shapes with
  borders, SVG paths (Lucide or Material icons as they are), gradients, gauges, pixel
  art, charts, lists, live values (time, weather, forecast, sun, moon, network, your
  own variables), expressions, palettes and animation. They are saved on the clock and
  join the rotation, or appear only when they have something to show. A JSON Schema
  checks them. [32 examples](examples/faces).
- **A Claude Code plugin** that teaches Claude the API, with examples. See
  [Claude Code plugin](#claude-code-plugin).
- **Clock faces:** Classic, Simple, Weather, Forecast, Photo, and the **Big tiles**:
  Big time, Big date, Big weather and four **Day tiles** (Weather today, then
  Weather forecast - Day 1, 2 and 3), each showing one thing as large as the screen
  allows. The Day tiles lead with the feels-like temperature, then wind, humidity,
  sunrise and sunset, low, high and precipitation; Weather today shows the current
  conditions. You choose which faces rotate, in what order and how often; faces with nothing to show are skipped.
- **Weather and a 4-day forecast with no API key**, from
  [Open-Meteo](https://open-meteo.com/), drawn with
  [Weather Icons](https://erikflowers.github.io/weather-icons/) (day and night).
- **Night mode:** dim the screen, draw everything in one dark color (dark gray by
  default) so it lights the room less, and show a single face all night (Big time, for
  example).
- **English or French** on the faces (days, months, weather, labels).
- **Correct local time all year:** time zones include daylight-saving rules.
- **Your photos:** the settings page crops and resizes them to 240×240 in your browser
  before uploading.
- **Home Assistant:** MQTT with discovery (backlight, face, weather sensors, messages),
  and the stock clock's API for
  [geekmagic-hacs](https://github.com/adrienbrault/geekmagic-hacs) dashboards. Both are
  off until you turn them on.
- **A settings page** for everything above, plus WiFi, password, backup and firmware
  updates. It works on phones, follows light or dark mode, and needs no internet on
  the clock's setup network.
- **Hard to brick:** updates are checked before anything is written, the setup network
  is always on, and there is a recovery mode reached by power cycling. See
  [Recovery](#recovery).
- **Private:** no cloud account, no tracking, and WiFi passwords are never exposed by
  the web API.

## Supported devices

| Device | Status |
|---|---|
| "Smart Weather Clock" **SD Pro**, stock firmware V1.0.6 | Tested: installed from the stock update page, two clocks in daily use |
| SD Pro, stock firmware V1.0.4 | Same flash layout as V1.0.6 (checked from the image); not tested on a device |
| GeekMagic SmallTV, SmallTV Ultra and other models | **Not supported.** Different boards and pin-outs |

The stock update page should say *"Please use the firmware of SD PRO"*. If it does not,
this firmware is not for your clock.

## Release files

Each [release](../../releases/latest) has these files. The clock's flash holds the
running firmware and the incoming update in the same 1 MB, so the firmware can only be
sent compressed, and only a clock running 1.4.0 or later can unpack it.

| File | What it is | Use it for |
|---|---|---|
| `AIAgentMonitor_v<version>.bin.gz` | The firmware, compressed | **Every install and update** on a clock running 1.4.0 or later, from this firmware's own Firmware update card |
| `SHA256SUMS.txt` | Checksums | `sha256sum -c SHA256SUMS.txt` |

**The first install** on a new clock uses one file from the
[v1.4.0 release](../../releases/tag/v1.4.0):
[`SDPro_AIAgentMonitor_v1.4.0_nomqtt.bin`](https://github.com/SimonLuckenuik/ai-agent-monitor/releases/download/v1.4.0/SDPro_AIAgentMonitor_v1.4.0_nomqtt.bin). It is a plain image, small enough for the stock
update page and for SmartClock-OSS before 1.4.0, without MQTT and mDNS. It is not built
for later releases, so their features are not limited by its size: once it runs,
install the latest `.bin.gz`.

Keep the file names. The stock update page only accepts names starting with `SDP`, so
only the first-install image has that prefix: the stock firmware cannot unpack the
compressed image, and its name keeps it from being tried there.

## Install

> **Read first.** Going back to the original firmware is not possible over WiFi. It
> needs a USB-serial adapter wired to the board, and a backup of the original flash made
> with one beforehand. Keep the clock powered during an update. If something goes wrong,
> see [Recovery](#recovery).

### From the stock firmware (a new clock)

1. Download **[`SDPro_AIAgentMonitor_v1.4.0_nomqtt.bin`](https://github.com/SimonLuckenuik/ai-agent-monitor/releases/download/v1.4.0/SDPro_AIAgentMonitor_v1.4.0_nomqtt.bin)** from the
   [v1.4.0 release](../../releases/tag/v1.4.0) (the first-install image; later releases
   have only the `.bin.gz`).
2. Open the clock's web page in a browser (its IP address is shown on the clock when it
   starts), and find **Firmware Update (OTA)**.
3. Choose the file and press **Start Update**. Confirm, then wait for "Update Success".
   The clock restarts by itself within about 30 seconds.
4. On your phone or computer, join the WiFi network **AI Agent Monitor XXXXXX** with the
   password shown on the clock, and open **http://192.168.4.1/**. Sign in with the same
   password.
5. In the **WiFi** card, pick your network, enter its password, and press
   **Connect & restart**. The clock shows its new address when it connects.
6. Open that address. In **Firmware update**, install
   **`AIAgentMonitor_v<version>.bin.gz`** from the
   [latest release](../../releases/latest): everything since 1.4.0, MQTT and mDNS.
7. Set your city and time zone, and choose your faces.

### From SmartClock-OSS

Clocks running SmartClock-OSS **1.3.x or earlier** only take plain images: install the
[`SDPro_AIAgentMonitor_v1.4.0_nomqtt.bin`](https://github.com/SimonLuckenuik/ai-agent-monitor/releases/download/v1.4.0/SDPro_AIAgentMonitor_v1.4.0_nomqtt.bin) from its Firmware update card first, then the latest
`.bin.gz` as in step 6 above. Settings and photos are kept.

### Updating

On a clock running 1.4.0 or later, install the new **`.bin.gz`** from the **Firmware
update** card. The settings page tells you when a new release is out. The card accepts
only this firmware's images and checks each one (size, checksum, layout) before writing
anything; a `.bin` that is too big for a plain update is refused with a pointer to the
`.gz`.

### Finding the clock

The clock names itself `agentmonitor-XXXXXX` (unique per clock) in your router's list
of devices, and its setup network **AI Agent Monitor XXXXXX** carries the same suffix.
The full firmware also answers at `http://agentmonitor-XXXXXX.local/` (mDNS, same
network only). The first-install image leaves mDNS out to stay small; it comes with the
`.bin.gz` update.

## Messages from agents and scripts

Anything on the network can put a message on the clock:

```bash
curl -X POST http://<clock>/api/message -d '{"id":"build","title":"Build","text":"All 42 tests passed.","icon":"success","show":true}'
```

| Field | Meaning |
|---|---|
| `id` | Messages with the same id replace each other (one per agent or task) |
| `title`, `text` | The heading and the message |
| `icon` | `info`, `success`, `warning`, `error`, `waiting`, `running`, `bell` or `done` |
| `color` | `"#rrggbb"`, instead of the icon's own color |
| `pct` | A progress bar, 0 to 100 |
| `ttl` | Seconds to keep it (600 by default, up to a day) |
| `show` | `true` puts it on screen now; otherwise it waits for its turn in the rotation |

Up to four messages are kept at once. Each one can be shown on its own (the Message
face, coloured by its icon) and all of them appear together on the **Agents** board,
one row per id. The [Claude Code plugin](#claude-code-plugin) also brings small status
faces of its own (attention, progress, done, error) that agents fill with variables:

![Messages, the Agents board and the plugin's status faces](docs/images/agent-screens.png)

`GET /api/message` lists the messages and `DELETE /api/message?id=build` clears one.
With MQTT on, the same JSON on `<base>/message` does the same. Details:
[Agent messages](docs/specifications/integrations-api.md#agent-messages).

## Custom faces

A face is a JSON list of items drawn in order: text, rectangles and circles (with
borders), lines, bars, rings, SVG paths, weather icons, photos, pixel-art **sprites**
(animated by frames), **sparklines** of a value's history, and **repeats** (items once
per forecast day, agent message, or number). Any string can hold placeholders: the
clock's own values (`{time.hm}`, `{date.wday}`, `{now.temp}`, `{day1.high}`,
`{sun.until_set}`, `{moon.phase}`, `{net.ip}`, …) and your **variables**, which agents
update over HTTP or MQTT: with an expiry (`$ttl`), kept across restarts (`$persist`),
added up (`$add`) or kept as a history for charts (`$history`). Number and colour fields
take expressions, and so does text in braces; **maps** turn values into colours or
labels, and **palettes** recolour a whole face (day and night, or by state). A face
joins the rotation, or only `when` a condition holds, for as long as its `duration`.

Turn them on in **Integrations → Custom faces**, which also gives the API key for
scripts:

```bash
curl -X POST "http://<clock>/api/faces?name=build&key=<api key>" -d @build.json
curl -X POST "http://<clock>/api/faces/vars?key=<api key>" -d '{"progress": 64, "build": "running", "$ttl": 900}'
```

The format is in
[docs/specifications/integrations-api.md](docs/specifications/integrations-api.md#custom-faces),
and `face.schema.json` in the [Claude Code plugin](#claude-code-plugin)'s skill is its
JSON Schema (`clock.py check face.json` validates a face and its placeholder names).

[`examples/faces/`](examples/faces) has 32 ready-made faces, each described with what
it demonstrates and the variables it reads: clocks (analog, binary, a 1980s watch, a
month calendar), weather (a forecast strip, the sun's path, the moon's phase, a
day/night beach, what to wear, a window on the sky, golden hour), faces for agents
and work (an inbox of agent messages, a CI pipeline, a heartbeat watchdog that
appears when an agent goes quiet, a pomodoro timer, a countdown, a commit streak, a
KPI with its trend, a homelab, a battery, a pixel pet, and an animated Claude
mascot), and two pixel-art games (a block world and a retro runner).

![The 32 example custom faces](docs/images/custom-faces.png)

```bash
curl -X POST "http://<clock>/api/faces?name=synthwave&show=1&key=<api key>" --data-binary @examples/faces/synthwave.json
```

## Claude Code plugin

This repository is also a Claude Code plugin marketplace. Its `ai-agent-monitor` skill
teaches Claude to put messages, progress, status and custom graphics on the clock or on
the [emulator](#emulator):

```
/plugin install ai-agent-monitor --marketplace SimonLuckenuik/ai-agent-monitor
```

Working in this repository, Claude Code offers the plugin by itself: the project's
`.claude/settings.json` lists the marketplace and enables it. Then give Claude the
clock's address and API key in `CLOCK_URL` and `CLOCK_API_KEY`.
Without them it uses the emulator at `http://127.0.0.1:8000`. The skill is in
[`plugins/ai-agent-monitor`](plugins/ai-agent-monitor/skills/ai-agent-monitor/SKILL.md):

| File | What it gives Claude |
|---|---|
| `SKILL.md` | When to use a message or a custom face, and the face model in short |
| `reference/api.md` | Every endpoint: messages, faces, variables and their options, MQTT, the emulator |
| `reference/face-format.md` | The face format in full (generated from the specification) |
| `reference/face.schema.json` | The JSON Schema of a face |
| `scripts/clock.py` | `message`, `push`, `vars`, `show`, `screenshot`, and `check` (schema and placeholder names) |
| `examples/` | Status faces for agents: attention, progress, done, error |

## Settings page

Everything is set from a web page on the clock: faces and their order, colors,
language, brightness, night mode, time zone, weather, WiFi, photos, integrations,
password, backup and firmware updates.

![Settings page: overview, faces and night mode](docs/images/settings-overview.png)

| On a phone | System: password, backup and reset |
|---|---|
| ![Settings page on a phone, dark mode](docs/images/settings-phone.png) | ![System card](docs/images/settings-system.png) |

![Integrations: custom faces, Home Assistant dashboards and MQTT](docs/images/settings-integrations.png)

## Recovery

- The **AI Agent Monitor XXXXXX** setup network stays on at all times, so you can
  always reach **http://192.168.4.1/**, even when your home WiFi is down or wrong.
- **Forgotten the password?** On the sign-in page, press **Show the password on the
  clock**, or use recovery mode below, which shows it on screen.
- **Recovery mode:** power the clock on for less than 10 seconds, twice, then power it
  on and leave it. The screen shows "Recovery". In recovery the setup network is open
  and no sign-in is needed. You can install firmware, and **Restart** returns to
  normal. The clock also enters recovery by itself if it crashes during startup.
- Settings and photos live in the clock's own storage and survive firmware updates.

More detail: [docs/MIGRATION.md](docs/MIGRATION.md).

## Known limitations

- The web page uses plain HTTP: someone already on your network can watch the traffic.
  The password stops them changing anything without it. Messages need no sign-in by
  design.
- Recovery mode (power cycling, or a crash during startup) is deliberately open.
- Animated GIFs are not played; the Photo face shows JPEGs.
- The faces' fonts have no accents: accented text is shown without them.
- There is no way back to the stock firmware without a USB-serial adapter.

## Emulator

The emulator runs the firmware source on your computer in Docker, with the clock's
real display driver on a simulated screen, so it draws exactly what the clock draws and
takes about as long:

```bash
docker compose -f emulator/docker-compose.yml up --build
```

The screen is at http://localhost:8080 (with a storage browser and power-cycle
buttons) and the clock's own web server at http://localhost:8000. See
[emulator/README.md](emulator/README.md).

## Building from source

Requires [PlatformIO](https://platformio.org/) and Python 3.

```bash
pio run
```

| Environment | Image |
|---|---|
| `esp12e` (default) | The firmware, with MQTT and mDNS: `.pio/build/esp12e/firmware.bin` and `firmware.bin.gz` |

The build reports how much of the update budget each image uses, and fails if an image
could not be replaced over the air. The CPU runs at 160 MHz (`board_build.f_cpu`).
Offline tests:

```bash
python tests/unit/run_unit_tests.py
```

```bash
python tests/run_safety_tests.py
```

```bash
python tests/face_preview/run_face_preview.py
```

The unit tests cover settings, expressions and text handling; the safety tests check
update validation, recovery and the flash layout; the face preview renders every face
to PNG and checks the screen-update logic.

With Docker, the integration tests run the firmware in its own emulator and drive it
over the real HTTP API:

```bash
python tests/integration/run_integration_tests.py
```

They fix the emulator's clock (Monday 5 October 2026, 10:09) and its weather, so every
run draws the same screens, then go through every built-in face, French, imperial
units, extreme cold, the sun-time phases, night mode, every message layout, a full
Agents board, the example faces and the API's limits and errors, and a restart. Each
screen is saved to `.pio/integration/` (with `sheet.png`), and a run lists the screens
that changed since the one before.

The images in this README come from the same emulator:

```bash
python scripts/screenshots.py
```

It needs Pillow, and Playwright (`pip install playwright`) for the settings page; it
uses an installed Edge or Chrome.

For your own builds you can compile in fallback WiFi details: copy
`secrets.example.ini` to `secrets.ini` (git-ignored). Release images are built with
`python scripts/make_release.py` (or by GitHub Actions on a push to `main` whose
`FW_VERSION` has no release yet: it publishes that version's release),
which ignores `secrets.ini` and refuses to finish if an image contains its values.
The weather icons are generated from the SVGs by `python scripts/gen_weather_icons.py`.

## Project documents

- [CHANGELOG.md](CHANGELOG.md): release history.
- [CLAUDE.md](CLAUDE.md): the project guide for working on the code (constraints,
  debugging, the emulator).
- [docs/specifications/integrations-api.md](docs/specifications/integrations-api.md):
  messages, custom faces, MQTT and the HTTP API.
- [docs/STATUS.md](docs/STATUS.md): what has been shown on hardware.
- [docs/specifications/hardware.md](docs/specifications/hardware.md): hardware and pin map.
- [docs/specifications/stock-firmware-analysis.md](docs/specifications/stock-firmware-analysis.md):
  stock firmware analysis.
- [docs/decisions](docs/decisions/README.md), [docs/issues](docs/issues/README.md) and
  [docs/plans](docs/plans/README.md): design records, known issues and plans.

## Licence

AI Agent Monitor is free software under the [GNU General Public License v3.0](LICENSE),
as is SmartClock-OSS, which it is forked from. Firmware images also contain third-party
components under their own licences: the ESP8266 Arduino core (LGPL-2.1), TFT_eSPI
(FreeBSD/MIT), ArduinoJson (MIT), PubSubClient (MIT), ChaN's TJpgDec (BSD-style), and
[Weather Icons](https://github.com/erikflowers/weather-icons) by Erik Flowers (SIL OFL
1.1). Weather data is provided by [Open-Meteo](https://open-meteo.com/) under CC BY 4.0.

This project is not affiliated with GeekMagic, JUZIPi or the makers of the SD Pro
clock.

## Repository layout

```
platformio.ini        build environments and panel configuration
scripts/              build options (size guard, compression), release, weather icons,
                      README screenshots
include/              pin map, update validation
src/main.cpp          boot, recovery, WiFi, web API, updates
src/faces.cpp         the clock faces, custom faces and the path renderer
src/integrations.cpp  messages, custom faces API, stock API, MQTT
src/weather.cpp       Open-Meteo geocoding, weather and forecast
src/config.h          settings stored on the clock (/config.json)
web/index.html        settings page (compressed into the firmware at build time)
emulator/             the Docker emulator
plugins/              the Claude Code plugin
examples/faces/       example custom faces, their sample values and a gallery
tests/                unit, safety, face preview and integration tests
docs/                 status, migration, specifications, decisions, issues, plans
```
