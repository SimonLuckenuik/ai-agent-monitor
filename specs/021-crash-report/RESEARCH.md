# Research

- ESP8266 Arduino core: `custom_crash_callback` (weak, `core_esp8266_postmortem.cpp`) is
  called for exceptions, software and hardware watchdog resets, `panic()` and `abort()`,
  before the restart. A failed `new` without `std::nothrow` and a detected stack overflow
  (`cont_check`) both end in `abort()`/`panic()`, which the next boot reports as
  "Software/System restart".
- RTC user memory: 512 B; the boot marker uses slot 32 (`markBoot` in `src/main.cpp`).
- On the clock (2026-10-31, 1.8.0): free heap about 15.7 KB with face 7 shown; a repaint
  strip takes 5 KB (`BAND_PX` 2560 px), a path draw 3.2 KB more (`MAX_PTS` 400 x 2 floats),
  so about 7 KB is left while a path draws inside a strip, before fragmentation.
- The clock's USB port is power only (CLAUDE.md); serial needs an adapter on the board's pads.
