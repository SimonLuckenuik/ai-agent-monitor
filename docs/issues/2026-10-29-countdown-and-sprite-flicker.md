# Countdown face flickered every second; animated sprites blinked

Status: Fixed in 1.5.0

Date: 2026-10-29

## Causes

- A `show` condition such as `{at}-{time.epoch}>0` was hashed as its text, which changes
  every second, so the whole face redrew.
- An opaque sprite cleared its box before drawing each frame (claude-mascot blinked).

## Fixes

`show` counts by its outcome; opaque sprites paint each pixel once.
