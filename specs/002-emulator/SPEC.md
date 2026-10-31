# 002 Emulator: the firmware on the PC, in Docker

Status: Done in 1.4.0 (written after the fact, 2026-10-30)

## Problem

Flashing the clock for every change is slow and needs the owner. The owner asked for an
emulator to test changes locally, including the firmware packages, with a fake screen.

## Goals

1. Run the real firmware source on the PC in a small Docker image, with its web server
   reachable outside the container.
2. A screen page that shows what the panel shows, live.
3. Behave like the clock: same drawing (TFT_eSPI), similar drawing speed at the CPU
   frequency in `platformio.ini`.

## Requirements

1. `docker compose -f emulator/docker-compose.yml up` serves the firmware on :8000 and the
   screen page on :8080 (WebSocket updates).
2. Screen page: zoom (real, 1×, 1.5×, 2×), Power cycle, Reset, Enter recovery, a Settings
   button that signs in by itself (password through the URL), a storage browser.
3. An API on :8080 for the storage (`/fs`, `/fs/file`), the frame buffer (`/fb`), timing.
4. The real TFT_eSPI on a simulated ST7789; draws cost about the clock's time
   (calibrated ±20%), `EMU_TIMING=0` for instant draws.
5. A fast loop: rebuild in a deps container and swap into the running emulator.
