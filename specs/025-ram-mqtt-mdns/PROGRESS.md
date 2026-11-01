# Progress

- 2026-10-31: proposed after the footprint review. MQTT and mDNS have been held off by
  unmeasured gates (16 KB and 28 KB) since about 1.4.0. mDNS measured in the emulator
  (420 B), MQTT from the code (about 2.5 KB). About 12 KB of RAM can come back (the update
  buffer, our strings, the routes). Waiting for approval.
- 2026-10-31: approved. Step 1: the update check (`FirmwareValidation`, 4,148 B) is
  allocated when an upload starts and freed when it ends or fails; out of memory answers
  "Not enough memory for an update: try again". Static RAM 51,884 -> 47,796 B. Safety
  tests pass, and the harness now asserts the buffer is freed after every case.
- 2026-10-31: steps 2-5.
  - **Routes:** two flash tables (`src/routes.h`), 36 B each in flash. Heap in use at rest
    in the emulator went from 10.3 to 5.5 KB. New case `route_methods`: the same 20
    method and path answers as 1.9.1's routes.
  - **Strings:** `server.send` messages in `PSTR`, `failOta`/`sendHeader` in `F()`, JSON
    keys and `key ==` comparisons in `F()`, the weather URLs and the default palettes in
    flash. Our strings in RAM went from 8,199 to 4,600 B. The `faces.cpp` keywords and
    messages stay: they are drawn or compared per frame through `const char*`.
  - **Size check:** `ram_report.py --max-ours 4864`, run by the safety tests.
  - **Static RAM:** 51,884 -> 44,240 B.
  - **MQTT and mDNS:** `mqttHeap`, `mdnsHeap`, `mdnsState` in `/status`; gates of
    6,144 B (MQTT) and 4,096 B (mDNS); the emulator now builds mDNS.
  - **Case `mqtt_and_mdns`:** against a mosquitto service in the test compose file, with
    each big face shown: MQTT connects, publishes discovery and takes `face/set`, mDNS
    answers, and the weather syncs. retro-runner: 10,856 B free with both on;
    `mqttHeap` 1.4 KB (plus the 1.5 KB buffer the emulator cannot see), `mdnsHeap` 560 B.
  - **Image:** 98% of the update limit (426,379 B compressed).
- 2026-10-31: the owner installed 2.0.0 on the clock. `/status` at 113 s up: heap 25,328 B
  free at rest (14.1-14.6 KB on 1.9.1), `heapLow` 18,280 B, `stackFree` 960 B, weather
  loaded (`weatherHeap` 3,080 B: the chip's fetch takes half what the emulator showed),
  `mdnsState` on. MQTT is off on this clock (no broker), so `mqttHeap` waits for one.
  All 39 example faces installed through the API, heap unchanged. The crash log holds
  one panic from 1.9.1 (82 min up, 4,000 B free), most likely the update upload under
  low memory: on 2.0.0 the update buffer is allocated or refused.
- 2026-10-31: the owner showed every custom face on the clock by hand, the big ones first
  (night-lake, retro-runner, block-world); `/status` read every 4 s throughout. No restart and
  no new crash (uptime 12,489 s, crashes still 1). Least free heap 16,256 B (`heapLow`), with
  retro-runner on screen 16,736 B and night-lake 17,072 B; the built-in faces 23.8-25.1 KB.
  `stackFree` 736 B all along (960 B at boot). The rotation holds only the built-in faces: the
  installed faces are shown on demand.
