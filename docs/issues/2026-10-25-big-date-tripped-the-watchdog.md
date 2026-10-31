# Big date reset the clock (software watchdog)

Status: Fixed in 1.4.0

Date: 2026-10-25

## Cause

A large line-font glyph took over a second of float maths without a yield; the software
watchdog resets after about 3 s.

## Fix

Stroked paths yield every row and test only pixels near each segment; CPU at 160 MHz.
