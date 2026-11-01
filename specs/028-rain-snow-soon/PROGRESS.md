# 028 Progress

- 2026-10-31: proposed, from smalltv-pomodoro's rain warning.
- 2026-11-01: built.
  - `weather.cpp` asks for `minutely_15=rain,snowfall,precipitation_probability,
    weather_code` (4 quarters) in the same request and keeps `wx.rainIn`, `wx.snowIn`,
    `wx.rainMm`, `wx.snowCm`.
  - Placeholders `now.rain_in`, `now.snow_in` (empty when none is due, so `{x|fallback}`
    works), `now.rain_mm`, `now.snow_cm` (inches with mph; no float printf: `decText`).
  - Weather today: the last cell (the chance of precipitation) says when, in the rain
    colour; face preview variants in English and French. `/status` weather gains
    `rainIn` and `snowIn`.
  - Tests: 5 unit checks of the placeholders; integration case `rain_snow_soon` (dry,
    rain in 30, rain likely in 15 by code and chance, snow in 15, rain now), with
    screenshots of the tile; the example face `umbrella` with its sample.
