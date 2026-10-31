# Emulator

Runs the firmware **source** on your computer, in Docker, so changes can be tried
before they go near a clock. The clock's own web server (settings page, API, faces)
and a page that shows its screen are both reachable from the host.

```bash
docker compose -f emulator/docker-compose.yml up --build
```

| URL | What |
|---|---|
| http://localhost:8080 | The screen, streamed over a WebSocket as it changes (up to ~30 updates a second) and dimmed like the backlight, with Power cycle, Reset and Enter recovery buttons, a **Scenarios** panel (below), and a **Storage** browser for the clock's LittleFS volume (JSON formatted, images shown, anything else as hex, each file downloadable) |
| http://localhost:8000 | The firmware's port 80: the settings page, `/status`, `/api/...` |

Both ports are published on `127.0.0.1` only: port 8080 can read everything in the
clock's storage. From scripts, use `127.0.0.1` rather than `localhost`: on Windows,
`localhost` tries IPv6 first and each connection waits ~200 ms before falling back.

The screen server's endpoints, for scripts:

| Endpoint | Returns |
|---|---|
| `GET /ws` | WebSocket. Binary messages: byte 0 = 1, byte 1 = backlight percent, bytes 2–3 = first row, bytes 4–5 = row count (little-endian), then those rows as RGB565 little-endian. The whole screen on connect, then only rows that changed, at most every 33 ms; a message with 0 rows reports a backlight change. Up to 4 viewers |
| `GET /stats` | The device's statistics: `{"uptime","cpuMHz","cpuLoad" (% of the last second the modelled clock was busy drawing),"heap":{"size","free","low","maxBlock"},"stack":{"size","maxUsed"},"lastCrash"}`. `lastCrash` is null or `{"reason","detail","uptime","at" (Unix time),"trace":["function at file:line", ...]}`, kept in `/data/last-crash.json` until the next crash. Also sent on `/ws` as a text message each second |
| `GET /fb` | The framebuffer: 240×240 RGB565, little-endian (115,200 bytes); headers `X-Backlight` (percent) and `X-Frame` |
| `GET /password` | The device password (plain text) |
| `GET /fs?path=/faces` | A directory: `{"path","total","used","entries":[{"name","dir","size"}]}` |
| `GET /fs/file?path=/config.json` | A file; `&download=1` as an attachment |
| `POST /power`, `POST /reset` | Power cycle, external reset |
| `POST /panic` | The firmware panics in its next `loop()`: its crash record, the crash log and crash-loop protection (spec 021) run as on the clock. The screen page's **Crash** button. A panic in the first 30 s after boot starts recovery instead, as on the clock |
| `POST /time?epoch=N` | Sets the firmware's clock to Unix time N, stopped, across restarts (`0`: the real time again). `local=2026-10-05T07:45[:SS]` instead of `epoch` is a time in the clock's zone; `add=S` moves it by S seconds; `speed=X` runs it at X times real time (0 stops it; alone, it changes the pace and keeps the time) |
| `GET /time` | `{"epoch","local","zone","mocked","speed"}`: the time the firmware sees |
| `POST /weather` | An Open-Meteo forecast reply (the clock's own request: `current` and `daily`, plus an optional `"city"`) used instead of the network, and applied now; `DELETE /weather` goes back to fetching; `GET /weather` returns it (`null`: live) |
| `POST /mqtt?topic=vars` | The body as if the MQTT broker had sent it to `<base>/<topic>`: `vars`, `message`, `message/clear`, `face/set`, `custom/<name>`, `backlight/set`, `sync`. It goes through the firmware's MQTT handler; no broker or MQTT settings needed. 400 when Custom faces are off and the topic needs them |
| `GET /vars` | `{"facesApi","showing","faces","vars":{name:{"value","age","ttl","persist","history"}}}` |
| `POST /facesapi?on=1` | Turns Custom faces (Integrations) on, or off with `on=0` |
| `GET /presets` | `emulator/presets.json` |
| `GET /examples/index`, `GET /examples/<file>.json` | The list of `examples/faces`, and one of its files (`samples.json` included) |

Storage is read-only from here. Change it through the firmware's own API, as on the clock.

The fixed time and weather are for tests that need the same screens on every run
(`tests/integration/`, `scripts/screenshots.py`); both are files in the data volume.

### Scenarios

The **Scenarios** panel on the screen page mocks what the clock reacts to, to see every
screen in any condition:

- **Time:** a date and time in the clock's zone, stopped or running at 1× to 3600×
  (night mode, sun phases and `when` rules follow it), ±minute/hour/day buttons, and
  presets: sunrise, golden hour, midnight, New Year's Eve, the daylight saving changes,
  a leap day …
- **Weather:** the current conditions and four forecast days as a form (°C, km/h),
  applied as you type, and presets from sunny to blizzard, heatwave and "Extremes
  (widest numbers)". A fixed weather's forecast dates follow the mocked date.
- **Values and MQTT:** the pushed variables (edit, delete, push with `$history`,
  `$persist`, `$add`, `$ttl`), a message form, the face showing, and a raw publish to
  any topic. All of it goes through the firmware's MQTT handler (`POST /mqtt`).
- **Presets:** whole scenarios (time, weather, messages, face) from
  `emulator/presets.json`, and every example face of `examples/faces` with its sample
  values from `samples.json` (as `tests/integration/emulator.py` `apply_sample()` does).
  "Clear values first" removes the variables and messages pushed before.

Add presets to `emulator/presets.json`: weather fields are `open_meteo()`'s
(`tests/integration/emulator.py`, wind in m/s), and the integration test `scenarios`
checks that every preset applies. `dev-reload.sh` copies the page, the presets and the
example faces into the running emulator.

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
| `memory.cpp` | The clock's heap and stack (below): the allocator, the stack depth hook, the panic and its crash record |
| `emulator.cpp` | Wraps `setup()`/`loop()`, serves the screen on port 8080, and provides the RTC user memory, reset reason, hardware RNG and button interrupt |
| `run.sh` | The power supply: restarts the firmware when it reboots, with the matching reset reason |
| `screen.html` | The screen page |
| `build.sh`, `gen_headers.py` | The build, with the headers `scripts/build_options.py` would generate, plus the settings page's `#password=` sign-in, and `emu_build_flags.h`: `platformio.ini`'s build flags (the panel config) and `board_build.f_cpu` |
| `tft_host/` | TFT_eSPI's processor layer for the PC: `build.sh` swaps it in for the ESP8266 one (SPI registers) in its copy of the library |
| `panel.cpp` | The simulated ST7789 and the clock's drawing speed (below) |
| `calibrate.py` | Fits the drawing speed to times measured on the real clock |

## Display

The emulator runs the clock's real TFT_eSPI (the version `platformio.ini` resolves, with
its panel config), so it draws exactly what the clock draws: the same fonts, smooth
shapes, padding and pixel order. Only the last step differs: instead of the ESP8266's
SPI registers, each byte goes to `panel.cpp`, which decodes the ST7789 commands the
panel would receive (column/row window, memory write, colour inversion) into a 240×240
RGB565 framebuffer. The screen page shows that framebuffer, dimmed by the backlight.

What it cannot show is the glass itself: an IPS panel is brighter, more saturated and
has another gamma than a monitor.

## Drawing speed

Draws take about as long as on the clock, at the CPU frequency in `platformio.ini`
(`board_build.f_cpu`; `/status` reports it as `cpuMHz`), and the screen page shows them
as they happen, like the clock's panel. `panel.cpp` charges, per TFT_eSPI call:

- **SPI:** every byte at `SPI_FREQUENCY` (40 MHz). Exact, and independent of the CPU.
- **CPU:** the firmware's time on the PC since the previous call, times `CPU_FACTOR`
  (6.4 at 160 MHz, twice that at 80).
- **Path loops** (`strokePoly`/`fillPoly` in `faces.cpp`: the Big tiles' line font,
  icons, custom paths): float maths, software on the ESP8266. Timing them on the PC
  varies 2× from run to run, so they count their work instead (`EMU_WORK`, estimated
  ESP8266 cycles per pixel and per segment; compiled out on the clock), charged at
  `F_CPU` × `WORK_SCALE` (1.0).

Then it sleeps the time off, pushing screen updates meanwhile. Time between draws is not
charged. `GET :8080/timing` lists the last draws (PC time, counted cycles, SPI time,
modelled clock time); `EMU_TIMING=0` makes draws instant.

Measured against the clock at 160 MHz (firmware 865c672): Classic 52 vs 54 ms, Weather
65 vs 68, Simple 40 vs 47, Forecast 96 vs 98, Flip 91 vs 87, Agents 59 vs 54, Big time
719 vs 722, a path-heavy custom face 312 vs 315. Big date and Big weather came out 20%
and 50% low, with different content on the two screens (another day name, another
weather icon). Expect about ±20%; the PC part varies a little from run to run.

To calibrate again (after changing the path loops, or with new numbers from the clock):
put the clock's `/status` drawMs in `CLOCK_MS` in `calibrate.py`, run
`python emulator/calibrate.py` (twice), and copy the printed constants into
`panel.cpp`. When changing a path loop in `faces.cpp`, keep its `EMU_WORK` counts in
step with the work it does.

| On the clock | In the emulator |
|---|---|
| WiFi | Always connected, through the container's network. The setup AP is not emulated |
| Time | The host's clock; the time zone from settings is applied |
| Weather, MQTT | Real network requests |
| Storage (LittleFS, 3 MB) | `/data/smartclock-littlefs3048KB` in the `clock-data` volume, formatted on first start (`EMU_NO_FORMAT=1` keeps a blank volume, to test the "storage not mounted" recovery) |
| EEPROM (password, boot counter) | `/data/eeprom` |
| RTC memory (crash marker) | `/data/rtc.bin`, cleared by **Power cycle** |
| Reboot (`ESP.restart()`) | The process exits and `run.sh` starts it again |
| Firmware update | The upload and all its checks run, plain `.bin` or `.bin.gz`, and the image is staged; the final commit fails ("Firmware commit failed": the host emulation has no flash outside the storage area), so the emulator keeps its build |
| Heap, stack | The chip's sizes and failures (below) |

The flash budget is not checked here: `pio run` still decides whether a change fits.

## Memory

The emulator is a 32-bit program (`build.sh`, `EMU_BITS=64` for a native one), so
pointers, `String` and JSON slots have the chip's sizes. While the firmware runs
(`setup()` and each `loop()`), its allocations come from a fixed arena run the way the
chip's allocator (umm_malloc) runs its heap: 8-byte blocks, 4 bytes of header, best fit,
so fragmentation adds up as it does there. A single block of 64 KB or more is the host
mock's own (a UDP socket takes 128 KB) and stays on the host heap.

The firmware's own frames are held to the chip's 4 KB stack: it is built with
`-finstrument-functions`, and each function entry measures its depth. The host mock and
glibc under it (network lookups, `printf`) are much deeper than their chip counterparts
and are not counted.

| Variable | Default | |
|---|---|---|
| `EMU_HEAP` | 25600 | Arena bytes: the clock had about 15.7 KB free at rest with built-in faces (1.8.0), the emulator's firmware uses about 9.8 KB there |
| `EMU_STACK` | 4096 | Stack bytes for the firmware's frames. The host's x86 frames are not the chip's Xtensa ones: compare `stackFree` in `/status` on the clock and here, and adjust |

Out of memory in `new` and a stack overflow end as on the chip: a panic, and the next
boot reports "Software/System restart". The log shows the cause and the call chain
(`addr2line`), and `GET /stats` and the screen page's **Device** panel show it as the
last crash. `malloc()` returning null is not a panic, as on the chip.

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
