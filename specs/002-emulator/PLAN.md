# Plan

1. Build the firmware on the esp8266/Arduino core's host emulation in Docker (`build.sh`).
2. Screen server in `emulator/emulator.cpp`: WebSocket rows, `/fb`, `/fs`, `/password`,
   power and reset.
3. `screen.html`: canvas, zoom, buttons, storage browser.
4. `dev-reload.sh` with the `emu-dev` container.
5. Replace the stand-in display with the real TFT_eSPI on a simulated ST7789
   (`emulator/panel.cpp`), with SPI and CPU timing from `f_cpu`; `emulator/calibrate.py`.
6. Document in `emulator/README.md` and CLAUDE.md.
