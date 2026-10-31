# Weather never loads on 1.9.0, and night-lake draws without its sun

Status: Fixed in 1.9.1 (spec 024), to confirm on the clock

Date: 2026-10-31

## Summary

On the clock with 1.9.0 (build 9b10951) the weather has not loaded since boot: `/status`
has no `weather`, no `weatherAge`, no `weatherError`, and `forecastDays` is 0. Faces show
`-- C`. The owner's photo of night-lake at 13:19 shows no sun and the sun's reflection at
the far right of the lake.

## Cause

`serviceWeather()` (`src/main.cpp`) postpones a fetch by 30 s while free heap is under
`WEATHER_MIN_HEAP` (16,000 B, set in 0.1.1 for OpenWeatherMap's 16 KB forecast). Open-Meteo's
reply is about 1 KB over plain HTTP. 1.9.0 idles at 14.1-14.6 KB free, so the fetch
is postponed forever and no error is recorded. The emulator does the same after a power
cycle (10.3 KB free at 26 s, no weather); before, it had fetched once at boot and never
again (`weatherAge` 11,373 s).

The `{sun.*}` values come from the weather's sunrise and sunset. Without them `sun.up`
falls back to 6:00-18:00 and `until_rise`, `until_set` and `daylen` are `--`, so
night-lake's sun position and palette expressions go wrong: no sun on the clock, the
reflection at the right edge. In the emulator under the same conditions the face picks
the dusk palette.

## Fix (to propose)

- Measure what an Open-Meteo fetch needs on the chip and lower the threshold to that,
  with a `weatherError` ("waiting for memory") when it postpones.
- `{sun.*}` from the location's latitude and longitude when the weather has none.
- With the face-upload issue ([record](2026-10-31-face-upload-needs-the-loaded-face-memory.md)):
  free the loaded custom face when it is not needed.

## Fix

[Spec 024](../../specs/024-memory-headroom/SPEC.md), 1.9.1: the threshold is 8 KB (a fetch
took 6,096 B in the emulator; `/status` reports `weatherHeap` on the clock). A custom face
on screen gives its document back when a fetch is short of memory, and a wait shows as
`weatherError` "Waiting for memory". Integration case `faces_beside_big_faces`: the
weather syncs with each big face on screen. Sun times from the location were written and
dropped at the owner's request: with the weather loading, `{sun.*}` has its values.
