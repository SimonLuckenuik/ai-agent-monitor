# Lines vanished in off-screen repaints

Status: Fixed in 1.8.0 (worked around)

Date: 2026-10-30

## Symptom

After a minute tick, Bauhaus lost both hands and Binary clock its divider line.

## Cause

TFT_eSPI 2.5.43 `drawWedgeLine` clips its box with the viewport datum (`clipWindow`) but
draws its pixels without it; with a strip's datum at the strip's screen position the
line landed outside the sprite.

## Fix

`wedge()` in `src/faces.cpp` draws lines in a strip in the strip's own coordinates with
no datum. No other TFT_eSPI call used here goes through `clipWindow`.
