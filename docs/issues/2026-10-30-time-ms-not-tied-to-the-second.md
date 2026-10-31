# `{time.ms}` was not tied to the clock's second

Status: Fixed in 1.8.0

Date: 2026-10-30

## Symptom

The flip-clock example's leaves left grey stripes, and `{time.sec}==0&&{time.ms}<400`
did not mean the first 0.4 s of a minute.

## Cause

`{time.ms}` was `millis() % 1000`, unrelated to when the second changed, and read again
by each strip of one repaint.

## Fix

`faceDraw()` notes when the second changes; `{time.ms}` is the time since then, fixed
at the start of each frame.
