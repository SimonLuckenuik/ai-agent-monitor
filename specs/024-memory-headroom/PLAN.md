# Plan

1. **Measure the fetch.** A debug build that logs free heap and the largest block before,
   during (low-water mark) and after `fetchOpenMeteo()`, in the emulator and through
   `/status` on a build the owner installs. Or, if the owner prefers one install, the
   release itself reports `weatherHeapLow` (the low-water mark of the last fetch) in
   `/status`. Files: `src/weather.cpp`, `src/main.cpp`. Result in RESEARCH.
2. **Threshold and report.** `WEATHER_MIN_HEAP` set to the measured need plus about 2 KB;
   a postponed fetch sets `wx.error` to "Waiting for memory" until the next success.
   Checked by an integration case: the emulator at rest loads the weather and refreshes
   it (the mocked clock moved 15 minutes).
3. **Free the custom face when it is not shown.** `customDoc` cleared when the shown face
   is not custom (in `faceDraw` on a face change) and when a face is stored or deleted; a
   face upload (`handleFaces` POST) clears it before parsing. `storeFace` checks the body
   with a filtered parse (`items` only, counted, not kept) and maps `NoMemory` to 507.
   Files: `src/faces.cpp`, `src/integrations.cpp`. Checked by an integration case: every
   example face sent while each big one (retro-runner, flip-clock-wide, night-lake) is
   shown, under the emulator's clock memory.
4. **Sun times without the weather.** Sunrise and sunset from latitude, longitude, date
   and the clock's UTC offset (the NOAA approximation, float), used by `{sun.*}` and by
   the built-in faces' sun facts only when the weather has none. Unit checks against
   Open-Meteo's times for a few places and dates (Saint-Hubert today, a polar day and
   night, the equator). Files: `src/faces.cpp` (or a small `src/sun.cpp`),
   `tests/unit/unit_tests.cpp`. Night-lake screenshot in the emulator without weather.
5. **Size check.** `pio run`, compressed size against the limit; if over budget, drop
   step 4's fallback to "sun.up from 6 to 18 and until_* computed from that" (no
   trigonometry) and say so in PROGRESS.
6. **Docs and release.** CHANGELOG (1.9.1), integrations-api.md (`weatherError`, 507,
   `{sun.*}` without weather), the issues closed, STATUS. Release 1.9.1; the owner
   installs it and the three faces plus the weather are checked on the clock.
