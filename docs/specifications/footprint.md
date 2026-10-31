# Flash and memory footprint

Measured 2026-10-31 on 1.9.1 (build 5ddec26), from the release build's ELF. Code is
attributed through the DWARF line table, so inlined code counts where it is written (the
built-in faces are inlined into `faceDraw`). Data comes from `nm -l`, and the heap from
the clock's `/status` and the emulator (`EMU_HEAP`, spec 022). Scripts:
`scripts/footprint_files.py` and `scripts/footprint_sections.py`.

## The budgets

| Budget | Size | Used | Left |
|---|---|---|---|
| Update image, compressed (`.bin.gz`; the limit is 1 MB minus image minus gz) | 614,400 B | 425,570 B (97%) | about 19 KB of compressed room, about 2 plain bytes per compressed byte saved |
| Image, plain (`firmware.bin`) | | 597,696 B | the plain limit (516,096 B) has been exceeded since 1.4.0; updates are `.bin.gz` only |
| RAM, static (`.data` 5.9 KB + `.rodata` 13.0 KB + `.bss` 32.9 KB) | 81,920 B | 51,884 B | |
| Heap, free at rest on the clock (built-in faces) | | | 14.1-14.6 KB |
| Stack (cont) | 4,096 B | about 3,050 B at the deepest since boot | 1,040 B |

## Flash by feature (plain bytes, code + data)

| Feature | Flash | Parts |
|---|---|---|
| ESP8266 SDK, WiFi, lwIP, core, newlib | about 200 KB | precompiled SDK blobs about 138 KB; lwIP (tcp, dhcp, dns, etharp, pbuf) about 25 KB; core about 27 KB; `printf` 4.2 KB and `scanf` 3.4 KB; C++ exception unwinding 3.3 KB; `sinf`/`cosf` argument reduction 2.6 KB |
| Custom faces engine | about 32 KB | items and text 11.2 KB, paths 7.4 KB, expressions 4.6 KB, the face loader 3.2 KB, paint 2.0 KB, colours 1.8 KB, `resolve` (placeholders) 3.9 KB, the faces API 1.8 KB |
| ArduinoJson (shared: settings, faces, weather, API) | about 33 KB | each new kind of input (a String, a File, a filter) instantiates the parser again: about 2 KB each |
| Display (TFT_eSPI) | about 35 KB | driver 17.3 KB, sprites 6.3 KB, fonts 2, 4 and 6 7.7 KB |
| Settings page | about 25 KB | `INDEX_HTML_GZ` 22.2 KB (72 KB of HTML), its handlers in `main.cpp` |
| LittleFS | about 25 KB | `lfs.c` 17 KB, the Arduino wrapper 7.5 KB |
| mDNS | about 21 KB | LEAmDNS 19.5 KB, `UdpContext` 1.8 KB. **Never runs:** `serviceMdns()` waits for 28 KB of free heap |
| Web server | about 16 KB | ESP8266WebServer 9.5 KB, `std::function` handlers 4.5 KB, DNS server (setup portal) 1.9 KB |
| Weather | about 19 KB | Open-Meteo fetch and parse 3.7 KB, HTTPClient 3.6 KB, weather icons `WI_DATA` 7.5 KB and their code 1.2 KB, Weather/Forecast/Day tiles faces 3.1 KB |
| Built-in faces | about 10 KB | Big tiles 2.3 KB, Day tiles 1.8 KB, Message face and icons 2.3 KB, Photo 1.2 KB, Agents 1.0 KB, Weather 0.8 KB, Classic 0.6 KB, Forecast 0.6 KB, HH:MM digits 0.6 KB |
| Settings and palettes (`main.cpp`, `config.h`) | about 12 KB | palettes and roles 3.7 KB, config load and save 3.2 KB plus 1.7 KB of JSON, backup and restore 1.5 KB, sign-in 1.2 KB |
| WiFi management (`main.cpp`) | 3.6 KB | |
| Agent messages | 3.4 KB | |
| Photos | about 8.6 KB | TJpg decoder 4.0 KB, upload and storage 3.4 KB, the face 1.2 KB |
| MQTT | about 7 KB | PubSubClient 2.8 KB, our MQTT code 2.2 KB, Home Assistant discovery 1.9 KB. **Never connects on 1.9.x:** it waits for 16 KB of free heap |
| Crash records (spec 021) | 1.8 KB | |
| Firmware update | about 2 KB | validation and the gz check, on top of the core's Updater |

## RAM held all the time

| What | Bytes | Note |
|---|---|---|
| String literals in `.rodata` | about 7,900 | 478 strings outside `F()`/`PSTR`: error messages, URLs, the default palettes (386 B), the Open-Meteo query (347 B), the face names (190 B). Copied into RAM at boot |
| `firmware` (the update check buffer, `firmware_validation.h`) | 4,148 | In `.data`; only an update uses it |
| SDK and lwIP (`g_ic`, `pmc`, ARP and DNS tables, ...) | about 5,000 | fixed |
| Face variables (`vars`) | 1,440 | the variable table |
| `sinf`/`cosf` reduction table (`ef_rem_pio2`) | 920 | in RAM because newlib keeps it in `.data` |
| Value histories (sparklines) | 592 | |
| Big tiles' shown state (`bigShown`) | 384 | |
| Config (`cfg`), web server, `Update` | 360, 272, 228 | plus the Strings they point to, on the heap |
| Message slots, repaint sprite object, item boxes, changed-region cover | 288, 288, 256, 240 | |

## Heap in use (transient or while shown)

| What | Bytes | Source |
|---|---|---|
| A custom face's parsed document | 2-5 KB (flip-clock-wide about 4 KB) | clock: 14.1 KB free with a tiny face, 10.0 KB after flip-clock-wide |
| A big face while shown (document and drawing) | retro-runner and night-lake about 7 KB, flip-clock-wide about 4.5 KB with dips to 3.7 KB free while animating | clock `/status` |
| A weather fetch | 6.1 KB | emulator `weatherHeap`; the clock reports its own with 1.9.1 |
| A face upload | the body twice for a moment (up to 8 KB), then once, plus a filtered check (about 1 KB) | ESP8266WebServer copies the body into the `plain` argument |
| mDNS once started | not measured | it needs 28 KB free to start |
| MQTT connection | not measured | it needs 16 KB free to connect |

## Where the RAM went since 0.1.x

On 0.1.1 the clock had about **32 KB** free at rest (weather and forecast running; STATUS,
2026-10-05). mDNS's 28 KB gate and MQTT's 16 KB gate were set against that. On 1.8.0 it
had 15.3-16.4 KB free, and on 1.9.x it has 14.1-14.6 KB.

Static RAM per release (`.data` + `.rodata` + `.bss`, each tag built again):

| Release | `.rodata` | `.bss` | Static in all | Heap left before the SDK and runtime take theirs |
|---|---|---|---|---|
| 0.1.2 | 7,312 | 27,528 | 40,692 | 41,228 |
| 1.3.2 | 8,624 | 28,576 | 43,060 | 38,860 |
| 1.4.0 | 10,928 | 29,424 | 46,232 | 35,688 |
| 1.6.0 | 12,800 | 31,304 | 50,000 | 31,920 |
| 1.8.0 | 12,800 | 32,168 | 50,880 | 31,040 |
| 1.9.0 | 13,024 | 32,912 | 51,872 | 30,048 |
| 1.9.1 | 13,036 | 32,912 | 51,884 | 30,036 |

That's 11.2 KB more static RAM since 0.1.2:

- **Strings in `.rodata`: about +5.7 KB.** Each feature's messages, keys and URLs. Of
  the 8.7 KB of strings in RAM today, 7.9 KB are ours: `main.cpp` 2.7 KB, `faces.cpp`
  2.0 KB, `integrations.cpp` 1.4 KB, `weather.cpp` 0.8 KB, `config.h` 0.5 KB.
- **Tables for the features: about +5.5 KB.** Face variables 1,440 B, value histories
  592 B, the big font's table 456 B, the Big tiles' state 384, message slots 288, the
  repaint sprite object 288, item boxes and hashes about 900 B, the night panel 184,
  crash record 144, the config's new fields 220, the role and name tables.

The remaining drop (about 7 KB, 32 KB minus 11 KB minus 14 KB) is heap held at runtime.
The emulator's heap map (`EMU_HEAPMAP=1`, `GET :8080/heapmap`) shows 10.4 KB in use at
rest. The largest holders:

- **The settings' Strings (palettes, roles, order, names): about 2.4 KB.**
- **The web server's routes: 53 handlers of about 56 B each, 3.0 KB.**
- **The photo upload handler: 1 KB.**
- **The current weather and forecast: about 0.6 KB.**

Objects built before `setup()` (the MQTT client's buffer, for one) are not counted there.

## MQTT and mDNS against the free heap

| | Needs (held) | Needs (at start) | Gate today | Fits at rest (14 KB) |
|---|---|---|---|---|
| MQTT | about 2-2.5 KB: the 1,536 B message buffer, the TCP connection (lwIP pcb, receive buffers) | about 2-3 KB more while the Home Assistant discovery messages are built (one String of 400-900 B each, the face list growing with each custom face) | 16 KB | **Yes**: about 11.5 KB would be left; with a big face shown, about 4.5 KB (the weather and uploads free the face first, spec 024) |
| mDNS (LEAmDNS) | not measured: the responder, its host and service records, a multicast UDP socket | probing and announcing at start, a reply buffer per query | 28 KB | **Unknown**: likely 3-5 KB, to measure on the chip |

Measured in the emulator against a local broker (2026-10-31): MQTT connects and
publishes discovery; the arena saw only 40 B more because the client's buffer is
allocated from the host heap before `setup()`. The figure above comes from the code.
The emulator does not run mDNS (the build leaves it out), and neither the emulator's
lwIP nor its sockets are the chip's, so the TCP and UDP parts must be measured on the
clock: a `/status` field per feature (as `weatherHeap` does for the weather).

**So MQTT never lacked room for itself: the 16 KB gate blocks it.** mDNS's 28 KB gate was
reachable only up to about 1.3.x. To bring both back:

1. Have `/status` report the heap each one takes when it starts (`mqttHeap`,
   `mdnsHeap`), and gate on that plus a margin (as spec 024 did for the weather).
2. Win back room. The update buffer only during an update is 4.1 KB, and the strings in
   flash are about 6-7 KB.
3. MQTT then fits beside a big face. mDNS probably does too. Otherwise it could start on
   demand, or be dropped for 21 KB of flash.

## Keeping the strings in flash

| | Today | With `F()` / `PSTR` / `FPSTR` |
|---|---|---|
| RAM | 7.9 KB of our strings copied into RAM at boot | 6-7 KB of it back. The keyword tables used per frame (`GROUPS`, `TIME_F`, ...) may stay in RAM (about 1 KB) |
| Flash | each string is already in flash (the copy source) | about the same. `F()` strings are 4-byte aligned, and calls that take a flash string add a little code: a few hundred bytes, about 0.5 KB compressed (97% → 97-98%) |
| Speed | RAM reads | flash reads through the cache: no visible change for messages and URLs, slower for keyword tables read many times a frame (left in RAM) |
| Work | | Mechanical but wide: about 470 literals across `main.cpp`, `faces.cpp`, `integrations.cpp`, `weather.cpp` and `config.h`. JSON keys (`doc["key"]`) need `F("key")` or a `const char` table in flash. Checked by the unit, integration and safety tests and a `.rodata` size check |
| Risk | | A flash string passed where a RAM `const char*` is expected crashes on the chip (unaligned flash read). The compiler does not catch every case, so the emulator cannot either: the safety test can grep for the patterns, and the clock checks it |

## Candidates for savings

| Change | Saves | Cost or risk |
|---|---|---|
| Allocate the update buffer only during an update | 4.1 KB RAM | An update needs 4.1 KB of heap free when it starts (the settings page could say so) |
| Move the remaining string literals to flash (`F()`, `PSTR`, `FPSTR`) | up to about 7 KB RAM | Mechanical, spread over many files; slightly slower to read |
| Drop mDNS (it never starts) or start it on demand | 21 KB flash (about 10 KB compressed) | The `.local` name goes away. The DHCP hostname already names the clock in the router |
| Lower MQTT's 16 KB gate to a measured need | makes MQTT work again | Measure first, as for the weather (spec 024) |
| One ArduinoJson input type (always `const char*`, or always a Stream) | about 2 KB flash per instantiation dropped | Small changes at each call site |
| Drop `scanf` | 3.4 KB flash | Not called from `src/`: a core or library function pulls it in (find which with a link map) |
| `-fno-exceptions` if the core allows it | about 3 KB flash | The core and libraries may rely on it; check |
| `sinf`/`cosf` on reduced angles only (or a small table) | 920 B RAM, 2.6 KB flash | The expressions' `sin()`/`cos()` take degrees; reduce to ±180 first |

## Measuring again

```bash
python -m platformio run
python scripts/footprint_files.py .pio/build/esp12e/firmware.elf       # by source file
python scripts/footprint_sections.py .pio/build/esp12e/firmware.elf .  # by "// ----" section
```

Stack frames come from `-fstack-usage` (see
CLAUDE.md), and live heap from `/status` (`heap`, `heapLow`, `weatherHeap`) and the
emulator's `GET :8080/stats`.
