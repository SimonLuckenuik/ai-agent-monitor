# 007 Big tiles and the line font

Status: Done in 1.4.0 (written after the fact, 2026-10-30)

## Problem

The owner wants three tiles in one modern style, each showing exactly one thing as
large as possible, to rotate time, date and weather; and one tile all night.

## Requirements

1. Big time (7): hours on the first line, minutes on the second.
2. Big date (3): the day's name full width, the day as a big number, the month full width
   (no year).
3. Big weather (8): the icon and the temperature, big.
4. One monospaced line font traced from Roboto Mono's centre lines (A-Z, 0-9, a few
   symbols), digits aligned; also used by Classic and Simple.
5. "At night show": one face all night (spec 008).
6. Measure draw times on the clock (`/status` `drawMs`, `cpuMHz`).
