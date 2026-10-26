# AI Agent Monitor

Firmware for a small ESP8266 desk clock (sold as the "Smart Weather Clock" **SD PRO**, a
GeekMagic SmallTV look-alike) that shows time, weather, photos and, above all,
**messages and screens pushed by AI agents**. A fork of
[michaeldale/smartclock-oss](https://github.com/michaeldale/smartclock-oss) (GPL-3.0),
at github.com/SimonLuckenuik/ai-agent-monitor. Development happens on `faces-v2`;
`main` is still upstream 1.3.2. Upstream is the `upstream` remote: no pull requests to
it for now.

The owner's clock is at **192.168.66.55** (MAC `8C:8C:29:AD:EC:8A`, so hostname
`agentmonitor-adec8a`), on an isolated IoT VLAN. It runs SmartClock-OSS 1.3.2; this
branch has **not run on hardware yet**.

## Hard constraints (read before changing firmware)

- **Flash budget.** An OTA image must be ≤ 0x7E000 = **516,096 bytes**
  (`scripts/build_options.py` fails the build above it). Currently ~512 KB: about
  **4 KB left**. Measure every change with `pio run`; find what grew with a link map
  (see Debugging). Things that cost a lot for little: `strtof`/`atof`/`strtod`
  (~9 KB of libc: use `parseNum()` in faces.cpp), `atan2f`/`fmodf`, any `%f`/`%g`
  in printf (also forbidden by the safety tests: float printf is not linked), inlined
  ArduinoJson lookups (go through the `noinline` helpers `js()`, `num()`, `colorOf()`).
  mDNS is out of the default build for space (`env:esp12e_mdns` adds it back, ~20 KB).
- **RAM.** ~24 KB free heap at run time. No frame buffer (240×240×2 = 115 KB): faces
  redraw only what changed, text needs a `bg`/`pad` to overwrite itself, and anything
  that moves redraws the face (flicker). Paths allocate 3.2 KB while drawing
  (`new (std::nothrow)`, checked).
- **Fonts are ASCII.** TFT_eSPI fonts 2 (16 px) and 4 (26 px) have letters; 6 (48 px)
  and 8 (75 px) are digits only. No accents: incoming text goes through `asciiFold()`
  (src/ascii.cpp). French is written without accents. Larger type uses the monoline
  stroke font in faces.cpp (`monoText()`, glyphs A–Z, 0–9, `-`, space, `*` = degree),
  traced from Roboto Mono's centre lines: monospaced, 16.9-unit cells, plain zero.
- **Flash layout** must stay `eagle.flash.4m3m.ld` (LittleFS at 0x100000–0x3FA000), the
  stock layout, or an OTA over stock lands on the wrong filesystem. LittleFS never
  auto-formats on the device.
- **Face ids are an API.** 0–6 are the stock theme numbers (geekmagic-hacs and
  `/api/set?key=theme` use them): 0 Classic, 1 Weather, 2 Photo, 3 Big date (was Dial),
  4 Simple, 5 Forecast, 6 Flip; then 7 Big time, 8 Big weather, 9 Agents board;
  `FACE_CUSTOM` 10, `FACE_MESSAGE` 11 (never saved). Renumbering needs a migration in
  `Config::load()` (`facesv`, now 3).
- **Line endings.** Upstream files are stored **CRLF** in git; keep them CRLF when
  editing (a whole-file LF rewrite shows every line changed: check
  `git ls-files --eol <file>`). Files under `emulator/` that run in Linux (`*.sh`,
  Dockerfile) are LF via `emulator/.gitattributes`.
- **Never sign in to, or flash, the real clock yourself.** Updates need the device
  password; the owner uploads images through the settings page (System → Firmware
  update). Build the image and hand over the path.

## Layout

| Path | What |
|---|---|
| `src/main.cpp` | Boot, recovery, WiFi/AP, web API, settings, OTA, rotation, night tile, backlight |
| `src/faces.cpp` | Every face; the custom-face renderer (expressions, SVG paths, gradients, wrapping); the Message face; the Big tiles and their monoline font; weather icons |
| `src/integrations.cpp` | Custom faces API, agent messages, geekmagic-hacs compatibility, MQTT |
| `src/weather.cpp` | Open-Meteo, WMO words in English and French |
| `src/config.h` | Settings stored in `/config.json` (load/save/migrations) |
| `src/ascii.cpp` | UTF-8 to ASCII folding |
| `web/index.html` | Settings page, gzipped into the firmware (self-contained: no CDN) |
| `emulator/` | Docker emulator (see below) |
| `tests/unit/` | Host unit tests (expressions, paths, colours, icons, folding) |
| `tests/face_preview/` | Renders every face to PNG; checks incremental == full redraw |
| `tests/run_safety_tests.py` | OTA handler, flash layout, size budget, no float printf, guards |
| `plugins/ai-agent-monitor/` | Claude Code plugin + skill; `.claude-plugin/marketplace.json` at the root |
| `docs/specifications/integrations-api.md` | The API reference: source of truth for messages and the face format |
| `.github/workflows/` | `build.yml` (every push), `release.yml` (tags `v*`) |
| `release/` | Local flash images (gitignored) |

## Build and test

Requires PlatformIO (`python -m pip install --user platformio`) and Zig for the host
tests (`python -m pip install ziglang==0.13.0 --target .pio-tools`).

```bash
python -m platformio run                        # firmware + size guard
python tests/unit/run_unit_tests.py             # 173 checks
python tests/run_safety_tests.py                # needs the firmware.bin from pio run
python tests/face_preview/run_face_preview.py   # .pio/face-preview/*.png, sheet.png
claude plugin validate .                        # plugin and marketplace manifests
```

Run all four before committing firmware changes. The face preview's PNGs are worth
looking at after any drawing change.

Flash image for the owner: `cp .pio/build/esp12e/firmware.bin release/SDPro_AIAgentMonitor_v<FW_VERSION>.bin`
(the name must start with `SDP` for the stock update page). Releases: bump
`FW_VERSION` in src/main.cpp (no `-dev`), add its CHANGELOG section, tag `v<version>`:
`release.yml` builds without WiFi details and publishes the image.

## Emulator

The firmware source on the esp8266/Arduino core's host emulation, in Docker. Screen,
settings page and API all work; WiFi is always "connected", time is the host's. It runs
the real TFT_eSPI on a simulated ST7789 (`emulator/panel.cpp`), so drawing is the
clock's, and draws take about the clock's time at `board_build.f_cpu` (calibrated,
±20%; `emulator/README.md` "Drawing speed"). `EMU_TIMING=0` makes them instant.

```bash
docker compose -f emulator/docker-compose.yml up -d --build
```

- http://127.0.0.1:8080: the screen (WebSocket), zoom, Power cycle / Reset / Enter
  recovery, storage browser, a Settings button that signs in by itself.
- http://127.0.0.1:8000: the firmware's own web server and API.
- From scripts use `127.0.0.1`, not `localhost` (Windows tries IPv6 first: +200 ms).

Fast loop (seconds instead of an image rebuild):

```bash
docker build -f emulator/Dockerfile --target deps -t smartclock-emu-deps .
docker run -d --name emu-dev -v "$PWD:/repo" smartclock-emu-deps sleep infinity
sh emulator/dev-reload.sh     # rebuild in emu-dev, swap into the running emulator, reset
```

Talking to it: `plugins/ai-agent-monitor/skills/ai-agent-monitor/scripts/clock.py`
(`message`, `push`, `show`, `vars`, `faces`, `screenshot OUT.png`). Against the
emulator it signs in by itself (password from `http://127.0.0.1:8080/password`).
Signed-in settings: `POST /api/auth/login` (form `password=`), then
`GET /api/set?key=…&value=…` with the cookie. Look at screenshots after every visual
change.

## Debugging

- **Emulator stops answering** (both ports time out, process at 0% CPU in `poll`): a
  peer that stopped reading. `build.sh` patches the core's `mockWrite()` to give up on
  a poll timeout, and stalled screen viewers are dropped. If it still happens:
  `docker restart emulator-smartclock-1`. Note that `dev-reload.sh` cannot reset a
  hung process (its reset goes over HTTP): restart the container after the swap.
- **Logs:** `docker logs emulator-smartclock-1`; state: `GET /status`; storage: the
  screen page's Storage panel or the screen server's API, no sign-in:
  `GET :8080/fs?path=/dir` (one directory: `{"path","total","used","entries":[{"name",
  "dir","size"}]}`) and `GET :8080/fs/file?path=/config.json`. It shows the emulator's
  own storage (volume `emulator_clock-data`), which starts empty: none of the stock
  firmware's files. The firmware itself has no storage listing (flash is full); on the
  real clock only `/api/photos` lists a folder (`/photo`).
- **Drawing speed:** `GET /status` has `cpuMHz` and `drawMs`, `{"<face id>": [last,
  slowest]}` in ms (draws over 1 ms only; the slowest is usually the full redraw when
  the face comes up). The emulator's numbers are modelled (`GET :8080/timing` has the
  breakdown); the clock's are the truth. New clock numbers: `emulator/calibrate.py`.
- **Isolate:** run the binary inside `emu-dev` (`/tmp/smartclock -P /tmp/x -L 3048 -S 0`)
  where no browser can reach it.
- **What uses flash:**
  `PLATFORMIO_BUILD_FLAGS="-Wl,-Map,.pio/build/esp12e/firmware.map" python -m platformio run`,
  then sum the map by object, or
  `~/.platformio/packages/toolchain-xtensa/bin/xtensa-lx106-elf-nm -S --size-sort -C .pio/build/esp12e/firmware.elf`.
- **Git Bash:** prefix `docker exec` paths with `MSYS_NO_PATHCONV=1`. Heredocs with
  backslashes or `'''` get mangled: write scripts to a file and run them.
- **Line endings** again: if a diff shows every line changed, restore CRLF.

## Things to keep in step

- A face or API change: `CHANGELOG.md` (Unreleased), `docs/specifications/integrations-api.md`,
  then regenerate the skill's `reference/face-format.md` from it (the "Face JSON"
  section to "Home Assistant dashboards"), and the skill's `SKILL.md` / `reference/api.md`.
- Plugin changes: bump the version in both `plugin.json` and `marketplace.json`.
- New source files: add them to `emulator/build.sh`; headers used by faces.cpp to the
  copy lists in `tests/face_preview/run_face_preview.py` and `tests/unit/run_unit_tests.py`.
- Face preview mocks (`tests/face_preview/mock/`) are shared with the unit tests (the
  emulator uses the real TFT_eSPI): new TFT_eSPI or String calls need a mock.
- Path loops in `faces.cpp` (`strokePoly`, `fillPoly`): keep their `EMU_WORK` cycle
  counts in step with the work; after a big change, `emulator/calibrate.py`.
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Status and next priorities

Done on `faces-v2` (see CHANGELOG Unreleased): emulator, custom faces v2 (expressions,
`show`, `fps`, SVG paths, gradients, wrapping, `{name|fallback}`), agent messages
(`/api/message`, 8 icons, rotation turns, backlight boost), French, Big tiles (Big date,
Big time, Big weather) and the Agents board, night tile option, hostname and setup
network `agentmonitor-XXXXXX` / `AI Agent Monitor XXXXXX`, unit tests, CI, plugin 0.2.0.

Next:
1. **First run on the real clock**: the owner flashes `release/…bin`; check heap
   (`/status` heap, heapLow), path drawing time, full-screen gradient speed, the Big
   tiles' drawing time (stroke rendering is float-heavy on a CPU without an FPU).
2. Merge `faces-v2` into `main` (the README's plugin install line needs it).
3. Claude Code hooks in the plugin (Notification → waiting, Stop → done,
   UserPromptSubmit → clear), using `/api/message`.
4. Flash headroom: ~4 KB left. Candidates: drop unused fonts or faces, trim
   integrations (MQTT discovery), shorten strings.
5. The settings page stays English and self-contained (no CDN).
