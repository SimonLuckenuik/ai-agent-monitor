# 014 The display's own features

Status: Done in 1.6.0 (written after the fact, 2026-10-30)

## Problem

The owner asked what the ST7789 offers out of the box, and chose rotation (by hardware
first), colour inversion, gamma curves and turning the display off at night.

## Requirements

1. Screen orientation 0/90/180/270 by the controller (MADCTL), for a clock on its side
   or upside down.
2. A custom face's `invert` (INVON/INVOFF, no redraw) for flashing.
3. Gamma curve (GAMSET 2.2, 1.8, 2.5, 1.0) by day and at night.
4. "Screen off" at night: the panel sleeps (SLPIN), warnings and errors wake it.
5. The emulator models all four.
6. After a restart, the address screen shows the IP, the name and the firmware version.
