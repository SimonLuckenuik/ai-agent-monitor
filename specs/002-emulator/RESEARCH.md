# Research

- The esp8266 Arduino core has a host build (`tests/host`) that runs sketches on Linux;
  WiFi is always "connected" there, no multicast (so mDNS is left out).
- First version used a stand-in display; drawing differed from the clock. The real
  TFT_eSPI on a fake SPI bus gives identical pixels.
- Timing: float maths is software on the ESP8266 and hardware on the PC, so path loops
  count estimated cycles (`EMU_WORK`) instead of being timed; SPI time comes from the
  bus clock. Calibrated against `/status` `drawMs` from the clock (`emulator/calibrate.py`).
- A peer that stops reading hung the single-threaded server: `build.sh` patches
  `mockWrite()` to give up on a poll timeout; stalled viewers are dropped.
