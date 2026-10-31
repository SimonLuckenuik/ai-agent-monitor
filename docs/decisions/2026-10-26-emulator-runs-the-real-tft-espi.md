# The emulator runs the clock's real TFT_eSPI, at the clock's speed

Date: 2026-10-26

Spec: [002](../../specs/002-emulator/SPEC.md)

## Decision

Replace the emulator's stand-in display with the real TFT_eSPI on a simulated ST7789,
and model draw time from the SPI clock and estimated CPU cycles (`EMU_WORK`) at
`f_cpu`, calibrated against `/status` `drawMs` from the clock (±20%).

## Why

The owner wanted the emulator's experience (80 vs 160 MHz, rendering) to match the
clock, so visual changes can be judged without flashing. `EMU_TIMING=0` keeps tests fast.
