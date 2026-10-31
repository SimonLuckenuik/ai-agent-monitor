# Plan

Each step is a commit, checked by the unit, integration and safety tests, with free heap
at rest compared before and after: the clock's own figure from the emulator's calibrated
arena, and `scripts/ram_report.py` for static RAM. Released as 2.0.0 (MQTT and mDNS back,
a large internal change), after which the owner installs it and the clock confirms.

1. **The update buffer on demand.** `FirmwareValidation firmware` (`src/main.cpp`,
   `include/firmware_validation.h`) becomes a pointer allocated when an upload starts and
   freed when it ends or fails. If 4.2 KB is not free, the upload is refused with a
   message ("Not enough memory: try again"); the settings page shows it. The safety
   tests (`tests/run_safety_tests.py`) cover every update case already. Saves 4.1 KB of
   `.data`.
2. **One route dispatcher.** A `RequestHandler` subclass (`src/routes.h`) holds a table
   in flash: path, method, auth level, handler function pointer, and an optional upload
   function. It replaces the `server.on()` calls in `setupRoutes()` and
   `integrationsRoutes()`. The not-found handler and the face-upload hook (spec 024) stay.
   Checked by the integration suite: every route is exercised there, and a new case
   compares `OPTIONS`, 404 and 405 answers with 1.9.1's. Saves about 4 KB of heap.
3. **Strings in flash.**
   - **Messages and fixed text:** `F()` for `server.send` messages, `FPSTR`/`PSTR` for
     tables, `F("key")` for JSON keys in settings and `/status`.
   - **Keyword tables:** the ones a face reads every frame (`GROUPS`, `TIME_F`,
     `DATE_F`, ...) stay in RAM unless measured as harmless.
   - **Files, in order:** `main.cpp`, `config.h`, `integrations.cpp`, `weather.cpp`,
     `faces.cpp`.
   - **Size check:** `scripts/ram_report.py` gains a mode that fails when our strings in
     `.rodata` exceed a set size, run by the safety tests, so they don't creep back.
4. **Measure MQTT and mDNS.**
   - **Reporting:** `mqttHeap` and `mdnsHeap` in `/status` (free heap before the start,
     less the lowest while starting and the free heap after), and `heapLow` per feature
     since boot.
   - **Gates:** set from the emulator's figures plus 3 KB for now (MQTT about 6 KB, mDNS
     about 4 KB).
   - **Emulator:** its build includes mDNS (`gen_headers.py` no longer drops
     `SMARTCLOCK_MDNS`), so the integration suite can check that `<host>.local` answers.
5. **MQTT and mDNS beside the big faces.**
   - **Integration case with a local broker** (eclipse-mosquitto in the test compose
     file). With each big example face on screen, MQTT connects, publishes discovery,
     takes `face/set` and `backlight/set`, and the weather still syncs. The mDNS query
     script gets an answer.
   - **Limits:** the 1,536 B MQTT message limit goes into the integrations reference.
6. **Release and the clock.**
   - **Release:** CHANGELOG with upgrade notes, release 2.0.0.
   - **On the clock, after the owner installs it:** read `mqttHeap`, `mdnsHeap` and the
     free heap from `/status`, and set the gates from those (a 2.0.1 if they differ).
   - **Records:** the footprint report updated, the issue records closed.
