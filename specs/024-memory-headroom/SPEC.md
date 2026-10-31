# 024 Weather, sun times and face uploads within the clock's heap

Status: Proposed (2026-10-31, the owner asked for it after the weather stopped loading)

## Problem

On the clock with 1.9.0, three things fail for lack of free heap (about 14.3 KB at rest,
7 KB with a big custom face shown):

- **Weather never loads**
  ([issue](../../docs/issues/2026-10-31-weather-never-fetched-under-16-kb.md)): the fetch
  waits for 16 KB free, a threshold set for OpenWeatherMap's 16 KB forecast. Open-Meteo's
  reply is about 1 KB, streamed into the parser. Nothing reports the wait.
- **`{sun.*}` comes from the weather**: without it `until_rise`, `until_set` and `daylen`
  are `--`, and night-lake draws without its sun, with its reflection at the right edge.
- **Face uploads fail after a big face**
  ([issue](../../docs/issues/2026-10-31-face-upload-needs-the-loaded-face-memory.md)):
  the face last drawn stays parsed in `customDoc` (about 4 KB for flip-clock-wide) after
  the clock moves on and after the face is deleted. Uploading another face then closes the
  connection, or answers "Expected JSON" when the parser runs out of memory.

## Goals

1. The weather loads and refreshes every 15 minutes on the clock at rest, and while any
   example face is shown. A postponed fetch shows in `/status` as `weatherError`
   ("Waiting for memory").
2. The fetch threshold is what a fetch takes on the chip, measured, plus a margin.
3. `{sun.up}`, `rise`, `set`, `until_rise`, `until_set` and `daylen` have values from
   the location's latitude and longitude when the weather has none. Within a few minutes
   of Open-Meteo's times.
4. The custom face is parsed only while a custom face is on screen. Showing a built-in
   face, or deleting the face, frees it.
5. Every example face can be sent while any other is shown, on the clock and in the
   emulator. Running out of memory answers 507 "Not enough memory", not "Expected JSON".
6. The firmware stays within the update limit (now 97%: each KB counts) and RAM use at rest
   does not grow.

## Non-goals

- Shrinking the faces' own memory use while shown (retro-runner leaves about 7 KB free).
  It only goes ahead if goal 5 cannot be met without it.
- Moonrise and moonset.
- Any change to the face format.
