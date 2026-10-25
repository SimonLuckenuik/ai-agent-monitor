# Emulator

Runs the firmware **source** on your computer, in Docker, so changes can be tried
before they go near a clock. The clock's own web server (settings page, API, faces)
and a page that shows its screen are both reachable from the host.

```bash
docker compose -f emulator/docker-compose.yml up --build
```

| URL | What |
|---|---|
| http://localhost:8080 | The screen, streamed over a WebSocket as it changes (up to ~30 updates a second) and dimmed like the backlight, with Power cycle, Reset and Enter recovery buttons, and a **Storage** browser for the clock's LittleFS volume (JSON formatted, images shown, anything else as hex, each file downloadable) |
| http://localhost:8000 | The firmware's port 80: the settings page, `/status`, `/api/...` |

Both ports are published on `127.0.0.1` only: port 8080 can read everything in the
clock's storage. From scripts, use `127.0.0.1` rather than `localhost`: on Windows,
`localhost` tries IPv6 first and each connection waits ~200 ms before falling back.

The screen server's endpoints, for scripts:

| Endpoint | Returns |
|---|---|
| `GET /ws` | WebSocket. Binary messages: byte 0 = 1, byte 1 = backlight percent, bytes 2–3 = first row, bytes 4–5 = row count (little-endian), then those rows as RGB565 little-endian. The whole screen on connect, then only rows that changed, at most every 33 ms; a message with 0 rows reports a backlight change. Up to 4 viewers |
| `GET /fb` | The framebuffer: 240×240 RGB565, little-endian (115,200 bytes); headers `X-Backlight` (percent) and `X-Frame` |
| `GET /password` | The device password (plain text) |
| `GET /fs?path=/faces` | A directory: `{"path","total","used","entries":[{"name","dir","size"}]}` |
| `GET /fs/file?path=/config.json` | A file; `&download=1` as an attachment |
| `POST /power`, `POST /reset` | Power cycle, external reset |

Storage is read-only from here. Change it through the firmware's own API, as on the clock.

**Screen size:** the buttons under the screen show it at **Real** size (the 1.54" panel
is a 1.09" square, about 105 CSS pixels; how close that is to life depends on the
monitor), 1×, 1.5× or 2×. The choice is remembered.

**Signing in:** the **Settings page** button opens `http://localhost:8000/#password=…`.
The emulator's build of the settings page, and only that build, signs in with it,
removes it from the address bar and reloads. The part after `#` is never sent to the
server. The firmware built by PlatformIO has no such shortcut. Otherwise the device
password is on the emulated screen whenever the real clock would show it (setup and
recovery screens, or **Show the password on the clock** on the sign-in page).

## How it works

The firmware is built on the esp8266/Arduino core's own host emulation
([`tests/host`](https://github.com/esp8266/Arduino/tree/3.1.2/tests/host)), at the core
version PlatformIO uses (3.1.2) and the library versions `platformio.ini` resolves.
`src/` is compiled unchanged; the differences live in this directory:

| File | Does |
|---|---|
| `fw_main.cpp` | Includes `src/main.cpp` with `setup()`/`loop()` renamed, and reroutes what the host core lacks: `configTime()` (the core's is a stub that ignores the zone), `EEPROM.end()`'s return value, the backlight PWM |
| `emulator.cpp` | Wraps `setup()`/`loop()`, serves the screen on port 8080, and provides the RTC user memory, reset reason, hardware RNG and button interrupt |
| `run.sh` | The power supply: restarts the firmware when it reboots, with the matching reset reason |
| `screen.html` | The screen page |
| `build.sh`, `gen_headers.py` | The build, with the headers `scripts/build_options.py` would generate, plus the settings page's `#password=` sign-in |

The display is `tests/face_preview/mock/TFT_eSPI`, the same stand-in the face preview
uses, drawing the real TFT_eSPI fonts into a 240×240 RGB565 framebuffer.

| On the clock | In the emulator |
|---|---|
| WiFi | Always connected, through the container's network. The setup AP is not emulated |
| Time | The host's clock; the time zone from settings is applied |
| Weather, MQTT | Real network requests |
| Storage (LittleFS, 3 MB) | `/data/smartclock-littlefs3048KB` in the `clock-data` volume, formatted on first start (`EMU_NO_FORMAT=1` keeps a blank volume, to test the "storage not mounted" recovery) |
| EEPROM (password, boot counter) | `/data/eeprom` |
| RTC memory (crash marker) | `/data/rtc.bin`, cleared by **Power cycle** |
| Reboot (`ESP.restart()`) | The process exits and `run.sh` starts it again |
| Firmware update | The upload, its checks and the commit run; after the restart the emulator is still the same build |
| Free heap | Fixed at 30,000 bytes: memory limits are not emulated |

The flash budget is not checked here: `pio run` still decides whether a change fits.

## Reset reasons

| Action | Reset reason | Effect |
|---|---|---|
| Start, **Power cycle** | Power-on | Counts toward recovery; clears RTC memory |
| **Reset** | External (EN pin) | Counts toward recovery |
| `ESP.restart()` (OTA, Connect & restart, Restart) | Software | Clears the count |
| A crash | Exception | Next boot enters recovery if startup had not finished |

**Enter recovery** sends three power cycles 4 s apart: from a clock that has been up
for 10 s, the third boot enters recovery, as on the device.

## Starting over

```bash
docker compose -f emulator/docker-compose.yml down -v
```

`-v` deletes the `clock-data` volume: storage, password and settings.

## Iterating without rebuilding the image

The image's `deps` stage holds the core and libraries. Mount the repo into it and
build there; the core's objects stay cached between builds:

```bash
docker build -f emulator/Dockerfile --target deps -t smartclock-emu-deps .
docker run -d --name emu-dev -v "$PWD:/repo" smartclock-emu-deps sleep infinity
docker exec emu-dev sh /repo/emulator/build.sh /repo /tmp/smartclock
```
