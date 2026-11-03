# Testing

Four suites check the firmware before it reaches the clock. Three run on this computer in
seconds; the fourth runs the real firmware in the emulator (Docker) and drives it over
its own HTTP API. Nothing here touches the clock: what only the chip can confirm is
listed at the end.

| Suite | Runs | Needs | Time | Command |
|---|---|---|---|---|
| Unit | `src/faces.cpp` and its helpers, compiled for the host | Zig (`.pio-tools`), one `pio run` for the libraries | under a minute | `python tests/unit/run_unit_tests.py` |
| Safety | the update handler from `src/main.cpp` against a mock updater, and checks on the source, the image and the ELF | Zig, the firmware built (`pio run`) | about a minute | `python tests/run_safety_tests.py` |
| Face preview | every built-in face drawn on the host, to PNG | Zig, one `pio run` | about a minute | `python tests/face_preview/run_face_preview.py` |
| Settings page | the page's own code (time zone rules, backups) from `web/index.html`, against the browser's time zone database and a mock clock; the minified page the clock embeds keeps every handler | Node | seconds | `node tests/web/tz_rules.test.js`, `node tests/web/backup.test.js`, `node tests/web/minified_page.test.js` |
| Local time | `src/tz_rule.h` against glibc: every rule the page gives a time zone, and hand-written ones, every 15 minutes of two years (spec 036) | Linux, g++, Node (CI; locally in the emulator's container) | seconds | `node tests/tz/zone_rules.js > rules.txt`, then `g++ -O2 -std=c++17 -I src tests/tz/tz_rule_tests.cpp -o tz && ./tz rules.txt` |
| Integration | the whole firmware in the emulator, through its HTTP API, MQTT and mDNS | Docker | 20-25 minutes (building the image included) | `python tests/integration/run_integration_tests.py` |

Setup, once:

```bash
python -m pip install --user platformio zopfli==0.4.3
python -m pip install ziglang==0.13.0 --target .pio-tools
python -m platformio run    # also needs Node: npx fetches terser for the settings page
```

Before committing a firmware change, run the build and the first three suites. Before a
release, run the integration suite too. The release workflow (`.github/workflows/release.yml`)
builds and tests again before it publishes.

## Which suite catches what

| A change to | Run |
|---|---|
| Expressions, placeholders, colours, paths, text folding | Unit, face preview, then the integration cases `example_faces` and `face_variables` |
| A built-in face's drawing | Face preview (look at `.pio/face-preview/sheet.png`), integration `builtin_faces` and the screens it saves |
| Custom faces (format, rotation, palettes, sprites) | Unit, integration (`example_faces`, `example_faces_memory`, `rotation_*`, `palettes_and_roles`) |
| The web API, settings, sign-in, routes | Safety (the guards), integration (`settings_round_trip`, `route_methods`, `face_validation`, `messages_api`) |
| Firmware updates, recovery, the flash layout | Safety |
| Memory: anything allocated, any new string | Safety (the strings check), integration `example_faces_memory`, `faces_beside_big_faces`, `mqtt_and_mdns`; the emulator's Device panel and heap map |
| MQTT, mDNS, the weather fetch | Unit (`mdns_tests`), integration `mqtt_and_mdns`, `faces_beside_big_faces`, `status_and_fixed_time` |
| Crash handling | Integration `crash_loop_protection` |

## Unit tests

`tests/unit/unit_tests.cpp` includes `src/faces.cpp` itself, so its internal functions are
reachable, and builds against the mocks in `tests/face_preview/mock/`. 405 checks:

| Group | Checks |
|---|---|
| `testNumbers` | number parsing: signs, decimals, junk after the number, empty input |
| `testExpressions` | `+ - * / %`, comparisons, `&& \|\| !`, `?:`, functions, strings, malformed input, deep nesting (the parser has fixed stacks of 12) |
| `testFields` | `show` conditions and field values |
| `testColours` | `#rgb`, `#rrggbb`, named colours, `rgb()`, mixing |
| `testPaths` | SVG path data: every command, relative and absolute, arcs, scaling |
| `testMessageFace` | the message face's template and its icons |
| `testMonoFont` | the big font: symbols, accents, lower case |
| `testAsciiFold` | text folded to the fonts' character set |
| `testFaceOrder` | the rotation list |
| `testPlaceholders` | `{time.*}`, `{date.*}`, `{now.*}`, `{sun.*}`, `{moon.*}`, `{msg.*}`, `{age.*}`, fallbacks |
| `testColors` | palettes and roles, the night palette, `@name` in faces |

A new placeholder, function or item field gets a check here.

`tests/unit/mdns_tests.cpp` checks the mDNS packets (`src/mdns_packet.cpp`, spec 027): the
queries answered and ignored, name compression, pointer loops, truncated packets,
200,000 random and mangled packets, and every answer read back record by record.

## Safety tests

`tests/run_safety_tests.py` extracts the update handler from `src/main.cpp` (from
`FirmwareValidation* firmware` to the crash handlers) into `tests/ota_harness.cpp`. It
then runs the real images (`.pio/build/esp12e/firmware.bin` and `.bin.gz`) through the
mock updater:

- **Plain and compressed updates:** complete, chunked, renamed, truncated, corrupt,
  oversized, a bad header or trailer, a filesystem image, several files, empty, aborted
  and retried, too big, updater failures. The update buffer is freed after every case.
- **The power-cycle counter:** recovery on the third short boot, reset, no needless
  flash erase.

Then it checks the source, the image and the ELF:

- the 4m3m flash layout, as stock, and the filesystem where stock puts it;
- the image within the update budget;
- no float `printf` or `scanf`;
- every reboot through `safeRestart()`;
- the guards:
  - every handler that changes the clock checks the sign-in;
  - each integration endpoint checks its own switch;
  - `/api/set` without sign-in allows only the face and brightness;
- the mount that never formats, the early recovery server, the station retry backoff,
  the persistent setup network;
- our string literals in RAM: at most 4,864 B (`scripts/ram_report.py --max-ours`). New
  strings belong in flash: `F()`, `PSTR()`, `FPSTR()` (spec 025).

## Face preview

`tests/face_preview/run_face_preview.py` builds `src/faces.cpp` against the mocks with the
real TFT_eSPI fonts. It draws every built-in face to `.pio/face-preview/*.png` and a
`sheet.png`. It fails when an incremental redraw leaves pixels a full redraw would not:
the check behind "only redraw what changed". Look at the sheet after any drawing change.

## Integration tests

`tests/integration/run_integration_tests.py` builds the emulator image and starts its own
copy (`tests/integration/docker-compose.yml`, project `aam-itest`):

- the clock on ports 18000 (the firmware's web server) and 18080 (the emulator's);
- blank storage on every run;
- an MQTT broker (eclipse-mosquitto) reachable as `broker`.

The clock runs with the chip's memory: a 25,600 B heap arena and a 4 KB stack
(`emulator/README.md`, "Memory"). A face that runs out there would run out on the clock.

The clock is fixed at Monday 5 October 2026, 10:09 (`POST :18080/time`), and the weather
at a set Open-Meteo reply (`POST :18080/weather`), so every run draws the same screens.
Screens go to `.pio/integration/`, with a `sheet.png`, and a run lists the screens that
changed since the last one.

```bash
python tests/integration/run_integration_tests.py              # build, start, test, stop
python tests/integration/run_integration_tests.py --no-build   # reuse the last image
python tests/integration/run_integration_tests.py -k mqtt      # cases whose name contains "mqtt" (one word, not a pattern)
python tests/integration/run_integration_tests.py --keep       # leave the emulator running after
python tests/integration/run_integration_tests.py --reuse      # use an emulator already running
```

| Case | Checks |
|---|---|
| `status_and_fixed_time` | `/status`: the version and build, the fixed time, the fixed weather, four forecast days |
| `settings_round_trip` | settings saved and read back; an unknown key refused |
| `builtin_faces` | every built-in face shown and drawn (a screenshot each) |
| `french` | the French wording on the faces that have text |
| `imperial_12h` | °F, mph and 12-hour time |
| `extreme_cold` | the widest numbers: -NN everywhere, 100 km/h, 100% humidity, 4-digit sun times |
| `sun_phases` | Weather today's two next sun events before sunrise, by day, after sunset |
| `night_mode` | the night hours: the night face, dimmed backlight, the tint |
| `messages_api` | agent messages: limits (40/160 characters), unknown icons and colours, clamped percent and lifetime, the same id replacing |
| `message_layouts` | every message icon and layout (screenshots) |
| `agents_board` | the Agents board: empty, then four agents in different states |
| `custom_faces_off` | the faces API answers 404 while turned off |
| `example_faces` | every face in `examples/faces` with its sample values |
| `example_faces_memory` | every example face for 8 s through a minute change, with the clock's heap and stack: no crash |
| `faces_beside_big_faces` | with each big face on screen: the weather syncs and every other example face uploads (spec 024) |
| `route_methods` | each route's method: its own, any, or a 404, as the routes did before the routes table (spec 025) |
| `mqtt_and_mdns` | with each big face on screen: MQTT connects to the broker, publishes discovery, takes `face/set`; mDNS answers; the weather syncs; `mqttHeap` and `mdnsHeap` reported (spec 025) |
| `crash_loop_protection` | 3 crashes in a row turn custom faces off, 2 more give safe mode; Resume and Clear; the crash log file (spec 021) |
| `face_variables` | variable options: names, null, `$add`, `$ttl`, `$history`, `{age.*}`, `$persist` across a restart |
| `variables_table_full` | 40 variables at most: a new one refused with a 507, expired ones making room |
| `rotation_when` | a face with `when` is in the rotation only while its condition holds |
| `rotation_list` | one rotation list: custom faces anywhere, turned off, with their own duration |
| `night_custom_face` | a custom face all night, its `when` aside |
| `palettes_and_roles` | named colours, roles bound to them, the night palette, `@role` in faces |
| `orientation` | the screen turned by the display controller |
| `display_features` | a face's `invert`, the gamma curve, the panel off at night |
| `scenarios` | the emulator's Scenarios panel: a running mocked clock, MQTT-style pushes, presets |
| `agent_faces` | the Claude Code skill's example faces with the variables an agent pushes |
| `face_validation` | face limits and errors: 32 items, 4 KB, bad JSON, bad names, unknown faces, a placeholder's fallback |
| `rain_snow_soon` | rain or snow within the hour from the 15-minute forecast: `/status`, the placeholders, Weather today's last cell (spec 028) |
| `request_host` | on the home network only requests addressed to the clock are answered (DNS rebinding, spec 029) |
| `city_place` | the place picked on the settings page: kept for its city, refused when malformed, carried by a backup, dropped by a city typed by hand (spec 035) |
| `restart_keeps_settings` | after a restart: settings, faces and the fixed time kept; messages gone; no recovery |

### Writing a case

A case is a function marked `@case` that takes `emu` (`tests/integration/emulator.py`). It
calls `check(condition, message)`, and `shot(emu, name)` for a screenshot. The helpers:

- **Settings and faces:** `sign_in`, `set(key, value)`, `settings(**values)`, `show(face_id)`,
  `push_face(name, face, show)`, `vars(**values)`, `message(...)`.
- **The emulated world:** `fix_time` and `set_time` (the clock), `fix_weather` (the
  weather), `restart`.
- **Reading back:** `status`, `config`, `stats` (heap, stack, last crash), `framebuffer`,
  `screenshot`.

Add a case for each new feature or edge case. Run it alone with `-k`, then the whole
suite.

## The emulator for manual checks

`docker compose -f emulator/docker-compose.yml up -d --build`, then http://127.0.0.1:8080:

- **Screen and controls:** the screen, power cycle, reset, recovery, and a **Crash**
  button.
- **Device panel:** heap, stack and load live, the last crash with its call chain, the
  firmware's crash log, and a **Heap map** (what holds the heap, by function).
- **Scenarios panel:** the mocked time and weather, agent values, the example faces.

`emulator/README.md` lists the endpoints:

- `/time`, `/weather`, `/power`, `/reset`, `/panic`;
- `/stats` (`?resetlow=1`), `/heapmap` (`?raw=1`), `/fs`.

## What only the clock confirms

The emulator runs the firmware's code with the chip's heap size, stack size and drawing
speed, but on the host. These need the clock itself, and its `/status`:

- **Network buffers:** the real network stack (lwIP) and its buffers. `weatherHeap`,
  `mqttHeap` and `mdnsHeap` report what each feature took on the chip.
- **Stack use:** the stack the chip's own frames use (`stackFree`; measured per function
  with `-fstack-usage`, see CLAUDE.md).
- **The panel:** what the screen actually shows (colours, gamma, flicker), and the
  drawing time on the chip (`drawMs`).
- **Updates and recovery:** installing on real flash, and recovery after a real power
  cycle.
- **Crash records on the chip:** the exception codes and addresses
  (`scripts/decode_crash.py`).
