# Research

## Free heap on the clock, 1.9.0 (build 9b10951), 2026-10-31

| Moment | Free heap |
|---|---|
| Uptime 30 s, built-in faces | 14,608 B (low 13,456) |
| Built-in faces, before any custom face | 14,136-14,512 B |
| flip-clock-wide shown or animating | 9,960-10,040 B (low 3,664) |
| After flip-clock-wide, a built-in face shown, the face deleted | 10,040-10,056 B |
| A tiny custom face shown instead | 14,136 B |
| retro-runner shown | 6,800-7,000 B |
| night-lake shown | 7,104-7,304 B |

The 4 KB that stays after flip-clock-wide is `customDoc`. `custom()` (`src/faces.cpp`)
only reloads it when the next custom face is shown. Showing a tiny face frees it, the
workaround used to send the faces on 2026-10-31.

## The emulator (`EMU_HEAP` 25600), same steps

- At rest 12.7 KB; flip-clock-wide shown 7.9 KB; theme 3 afterwards 10.5 KB. Sending
  retro-runner then answers 400 "Expected JSON with an items array": `deserializeJson`
  in `storeFace` ran out of memory.
- retro-runner shown: 3.3 KB free. Sending night-lake closes the connection without an
  answer, as on the clock.
- After a power cycle: 10.3 KB free at 26 s, `forecastDays` 0, no `weatherAge`. Before
  that the running emulator had fetched once at boot and not again in three hours
  (`weatherAge` 11,373 s at uptime 11,385 s).

## The weather fetch

- `fetchOpenMeteo()` streams a plain-HTTP reply of about 1 KB through
  `deserializeJson(doc, http.getStream())`. It uses a `WiFiClient`, an `HTTPClient` and
  a `JsonDocument`, and lwIP's receive buffers.
- `WEATHER_MIN_HEAP = 16000` dates from 0.1.1. The 2026-10-05 decision (Open-Meteo)
  kept "a fetch is postponed by 30 s while free heap is under 16 KB" without measuring.
  When free heap is below it, `serviceWeather()` returns before `fetchWeather()`, so
  `wx.error` stays empty.
- Not measured on the chip yet (step 1 of the plan).

## `{sun.*}` today

`case 4` in the value lookup (`src/faces.cpp`) reads `wx.sunrise` and `wx.sunset` (and
the forecast's next day). Without them `up` falls back to 6:00-18:00, and `until_rise`,
`until_set` and `daylen` return `--`. Night-lake's palette and positions use those three.
On the clock that meant no sun and the reflection at the right edge. In the emulator
under the same conditions the dusk palette was picked.

## Options weighed

- **Free `customDoc` only when an upload arrives.** It doesn't work on its own: the web
  server allocates the body (about 4 KB) before the handler runs, so the memory has to be
  free already. Freeing it whenever a built-in face is shown covers the common case. With
  a big face on screen, the upload still has the face's own drawing memory to contend
  with (goal 5 measures this).
- **Lower the weather threshold blindly (say to 8 KB).** Rejected: a fetch that runs out
  of memory mid-request on the chip could fail in lwIP rather than in our code. Measure
  first.
- **Sun times from a table per city.** Rejected: the clock takes any location by
  geocoding. The NOAA approximation is a few dozen lines of float maths, but flash is at
  97% of the update limit, so its size decides (plan step 5).
