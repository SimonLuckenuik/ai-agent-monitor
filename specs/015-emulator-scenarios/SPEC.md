# 015 Emulator scenarios: mock time, weather and pushed values

Status: Done in 1.6.0 (written after the fact, 2026-10-30)

## Problem

To test screens in every condition, the owner wants to mock the date and time, the
weather, and values pushed as if over MQTT, set one by one or from built-in presets, and
play with them from the emulator's page.

## Requirements

1. A mocked clock with a pace (stopped to 3600×): `GET/POST /time` (`epoch`, `local`,
   `add`, `speed`).
2. Weather form (current and four days) and presets; `GET /weather`.
3. `POST /mqtt` hands any topic to the firmware's own MQTT handler; `GET /vars`;
   `POST /facesapi`.
4. `emulator/presets.json` (times, weathers, scenarios) and every example face with its
   samples (`GET /presets`, `GET /examples/…`), shipped in the image.
5. A Scenarios panel on the screen page (Time, Weather, Values and MQTT).
6. Nothing in the device firmware (`#if SMARTCLOCK_EMULATOR` only).
